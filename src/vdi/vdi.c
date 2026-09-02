/* vdi.c -- GEM VDI dispatcher, workstation state and output primitives.
 *
 * The rendering strategy is the one the hardware forces: VBXE sits on the
 * 1.79 MHz chip bus no matter how fast the 65816 runs, so a primitive's job is
 * to EMIT BLITTER CONTROL BLOCKS, not to write pixels.  Measured, a
 * full-screen fill costs 0.51 of a frame through the blitter; the same work
 * through the MEMAC window would be roughly twenty times slower.
 */
#include "vdi.h"
#include "pointer.h"
#include "../vbxe/vbxe.h"

WORD contrl[CONTRL_SIZE];
WORD intin[INTIN_SIZE];
WORD ptsin[PTSIN_SIZE];
WORD intout[INTOUT_SIZE];
WORD ptsout[PTSOUT_SIZE];
Vwk  vwk;

/* Standard VDI line styles 1..7 (7 is user-defined; we start it solid). */
UWORD line_styles[8] = {
    0xFFFF, 0xFFFF, 0xFFF0, 0xE0E0, 0xFF18, 0xFF00, 0xF191, 0xFFFF
};

/* VDI pen -> hardware pen.
 *
 * GEM numbers its pens white=0, black=1, red=2 ... but XOR mode complements
 * the pixel's BITS, and the AES relies on that complement turning black into
 * white and back (a selected button is drawn by XORing its rectangle).  That
 * only holds if black and white are bitwise complements in the hardware, so
 * the VDI keeps GEM's pen numbers at its interface and stores every pixel
 * through this map -- the same permutation the ST's VDI applies -- with the
 * palette loaded in hardware order to match.  black -> 15, white -> 0. */
static const uint8_t map_col[16] = {
    0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13
};

/* GEM's standard 16 colours, in VDI pen order; v_opnwk permutes them into
 * hardware order through map_col[] before loading the palette. */
static const uint8_t gem_rgb[16 * 3] = {
    0xFF, 0xFF, 0xFF,   /*  0 white        */
    0x00, 0x00, 0x00,   /*  1 black        */
    0xFF, 0x00, 0x00,   /*  2 red          */
    0x00, 0xFF, 0x00,   /*  3 green        */
    0x00, 0x00, 0xFF,   /*  4 blue         */
    0x00, 0xFF, 0xFF,   /*  5 cyan         */
    0xFF, 0xFF, 0x00,   /*  6 yellow       */
    0xFF, 0x00, 0xFF,   /*  7 magenta      */
    0xBB, 0xBB, 0xBB,   /*  8 light grey   */
    0x77, 0x77, 0x77,   /*  9 dark grey    */
    0xBB, 0x00, 0x00,   /* 10 dark red     */
    0x00, 0xBB, 0x00,   /* 11 dark green   */
    0x00, 0x00, 0xBB,   /* 12 dark blue    */
    0x00, 0xBB, 0xBB,   /* 13 dark cyan    */
    0xBB, 0xBB, 0x00,   /* 14 dark yellow  */
    0xBB, 0x00, 0xBB    /* 15 dark magenta */
};

#define HW(pen) ((WORD)map_col[(pen) & 0x0F])

/* ---------------------------------------------------------------------- */
/* helpers                                                                */
/* ---------------------------------------------------------------------- */

static void order(WORD *a, WORD *b)
{
    if (*a > *b) { WORD t = *a; *a = *b; *b = t; }
}

/* Clip a rectangle to the workstation's clipping rectangle.
 * Returns 0 if nothing is left. */
static WORD clip_rect(WORD *x1, WORD *y1, WORD *x2, WORD *y2)
{
    if (vwk.clip) {
        if (*x1 < vwk.xmn_clip) *x1 = vwk.xmn_clip;
        if (*y1 < vwk.ymn_clip) *y1 = vwk.ymn_clip;
        if (*x2 > vwk.xmx_clip) *x2 = vwk.xmx_clip;
        if (*y2 > vwk.ymx_clip) *y2 = vwk.ymx_clip;
    }
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 > SCR_W - 1) *x2 = SCR_W - 1;
    if (*y2 > SCR_H - 1) *y2 = SCR_H - 1;
    return (*x1 <= *x2 && *y1 <= *y2);
}

/* Fill a device rectangle in 4bpp chunky, honouring odd-pixel edges.
 *
 * A pixel x lives in byte x>>1: high nibble when x is even, low nibble when
 * odd.  The blitter is byte-granular, so an edge byte that is only half inside
 * the rectangle has to keep its other nibble.  That is done with a pair of
 * constant-source read-modify-write blits -- AND away the nibble being
 * replaced, then OR the colour in -- which works for every colour including 0.
 * (Mode 6's nibble stencil would be one blit instead of two, but it cannot
 * write colour 0: a zero nibble means "transparent".)
 *
 * Rectangle edges are exactly where 4bpp VDI drivers historically went wrong,
 * so tests/emu/m3_vdi.py deliberately exercises odd x1, odd x2, and rectangles
 * one pixel wide inside a single byte.
 */
static void fill_rect_dev(WORD x1, WORD y1, WORD x2, WORD y2, WORD hwpen)
{
    uint32_t base = VR_SCREEN0 + (uint32_t)y1 * SCR_STRIDE;
    uint16_t rows = (uint16_t)(y2 - y1 + 1);
    uint8_t  c    = (uint8_t)(((hwpen & 0x0F) << 4) | (hwpen & 0x0F));
    WORD bl = (WORD)(x1 >> 1), br = (WORD)(x2 >> 1);

    if (bl == br) {
        if ((x1 & 1) == 0 && (x2 & 1) == 1) {           /* whole byte */
            blit_fill(base + bl, SCR_STRIDE, 1, rows, c);
        } else if (x1 & 1) {                            /* low nibble only */
            blit_and(base + bl, SCR_STRIDE, 1, rows, 0xF0);
            blit_or(base + bl, SCR_STRIDE, 1, rows, (uint8_t)(c & 0x0F));
        } else {                                        /* high nibble only */
            blit_and(base + bl, SCR_STRIDE, 1, rows, 0x0F);
            blit_or(base + bl, SCR_STRIDE, 1, rows, (uint8_t)(c & 0xF0));
        }
        return;
    }
    if (x1 & 1) {                                       /* partial left */
        blit_and(base + bl, SCR_STRIDE, 1, rows, 0xF0);
        blit_or(base + bl, SCR_STRIDE, 1, rows, (uint8_t)(c & 0x0F));
        bl++;
    }
    if ((x2 & 1) == 0) {                                /* partial right */
        blit_and(base + br, SCR_STRIDE, 1, rows, 0x0F);
        blit_or(base + br, SCR_STRIDE, 1, rows, (uint8_t)(c & 0xF0));
        br--;
    }
    if (br >= bl)
        blit_fill(base + bl, SCR_STRIDE, (uint16_t)(br - bl + 1), rows, c);
}

/* XOR a device rectangle: complement every pixel, which is what XOR mode
 * means in the VDI -- the pen is not consulted.  Same edge handling as the
 * fill, with the blitter's XOR mode doing the read-modify-write. */
static void xor_rect_dev(WORD x1, WORD y1, WORD x2, WORD y2)
{
    uint32_t base = VR_SCREEN0 + (uint32_t)y1 * SCR_STRIDE;
    uint16_t rows = (uint16_t)(y2 - y1 + 1);
    WORD bl = (WORD)(x1 >> 1), br = (WORD)(x2 >> 1);

    if (bl == br) {
        uint8_t m = 0xFF;
        if (x1 & 1)            m = 0x0F;        /* low nibble only  */
        else if ((x2 & 1) == 0) m = 0xF0;       /* high nibble only */
        blit_xor(base + bl, SCR_STRIDE, 1, rows, m);
        return;
    }
    if (x1 & 1) {
        blit_xor(base + bl, SCR_STRIDE, 1, rows, 0x0F);
        bl++;
    }
    if ((x2 & 1) == 0) {
        blit_xor(base + br, SCR_STRIDE, 1, rows, 0xF0);
        br--;
    }
    if (br >= bl)
        blit_xor(base + bl, SCR_STRIDE, (uint16_t)(br - bl + 1), rows, 0xFF);
}

/* Single pixel, read-modify-write through the MEMAC window.  The blitter
 * cannot help with one pixel, and a Bresenham line is the one primitive it
 * does not accelerate at all.  `hwpen` is a HARDWARE pen: callers map. */
static WORD plot_visible(WORD x, WORD y)
{
    if (vwk.clip && (x < vwk.xmn_clip || x > vwk.xmx_clip ||
                     y < vwk.ymn_clip || y > vwk.ymx_clip))
        return 0;
    return (x >= 0 && y >= 0 && x < SCR_W && y < SCR_H);
}

static void plot(WORD x, WORD y, WORD hwpen)
{
    uint32_t a;
    uint8_t b;
    if (!plot_visible(x, y))
        return;
    a = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE + (uint32_t)(x >> 1);
    b = vram_read8(a);
    if (x & 1)
        b = (uint8_t)((b & 0xF0) | (hwpen & 0x0F));
    else
        b = (uint8_t)((b & 0x0F) | ((hwpen & 0x0F) << 4));
    vram_write8(a, b);
}

static void plot_xor(WORD x, WORD y)
{
    uint32_t a;
    if (!plot_visible(x, y))
        return;
    a = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE + (uint32_t)(x >> 1);
    vram_write8(a, (uint8_t)(vram_read8(a) ^ ((x & 1) ? 0x0F : 0xF0)));
}

/* Paint one pixel of a primitive in the current writing mode, given whether
 * the primitive's pattern bit is SET there.  These are the VDI's rules:
 *   replace      pen where set, pen 0 where clear
 *   transparent  pen where set
 *   XOR          complement where set
 *   erase        pen where CLEAR ("reverse transparent")
 */
static void paint_pixel(WORD x, WORD y, WORD pen, WORD set)
{
    switch (vwk.wrt_mode + 1) {
    case MD_TRANS:   if (set) plot(x, y, HW(pen));           break;
    case MD_XOR:     if (set) plot_xor(x, y);                break;
    case MD_ERASE:   if (!set) plot(x, y, HW(pen));          break;
    default:         plot(x, y, set ? HW(pen) : HW(0));      break;
    }
}

/* The same for a solid rectangle -- every pattern bit set -- which lets the
 * blitter do it.  Clipping is the caller's job. */
static void paint_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
{
    switch (vwk.wrt_mode + 1) {
    case MD_XOR:    xor_rect_dev(x1, y1, x2, y2);              break;
    case MD_ERASE:  return;                 /* nothing is clear: no-op */
    default:        fill_rect_dev(x1, y1, x2, y2, HW(pen));    break;
    }
    blit_run();
}

/* ---------------------------------------------------------------------- */
/* fill patterns                                                          */
/* ---------------------------------------------------------------------- */

/* A hollow fill is a pattern with no bits and a solid one all bits; the
 * writing mode then decides what a clear bit means.  Replace writes pen 0
 * there -- which is why a hollow box in replace mode comes out WHITE, not
 * untouched, and why the AES can draw an opaque dialog with hollow boxes. */
static const UWORD pat_hollow = 0x0000;
static const UWORD pat_solid  = 0xFFFF;

/* st_fl_ptr: resolve the interior and index to the pattern's rows and the
 * mask that turns y into a row number.  The donor's name and the donor's
 * table split: dithers then OEM patterns, coarse then fine hatches. */
static void st_fl_ptr(void)
{
    WORD fi = vwk.fill_index;
    switch (vwk.fill_style) {
    case FIS_SOLID:
        vwk.patptr = &pat_solid;                 vwk.patmsk = 0;   break;
    case FIS_PATTERN:
        if (fi < 8) { vwk.patptr = &fill_dither[fi * 4];        vwk.patmsk = 3; }
        else        { vwk.patptr = &fill_oem[(fi - 8) * 8];     vwk.patmsk = 7; }
        break;
    case FIS_HATCH:
        if (fi < 6) { vwk.patptr = &fill_hatch0[fi * 8];        vwk.patmsk = 7; }
        else        { vwk.patptr = &fill_hatch1[(fi - 6) * 16]; vwk.patmsk = 15; }
        break;
    case FIS_USER:
        vwk.patptr = vwk.ud_patrn;               vwk.patmsk = 15;  break;
    default:
        vwk.patptr = &pat_hollow;                vwk.patmsk = 0;   break;
    }
}

/* The pattern the blitter reads lives in VRAM at VR_PATT: 16 rows, each one
 * 16-pixel repeat of the pattern expanded to nibbles -- `set` where the bit
 * is one, `clr` where it is zero -- and written twice over, so that a blit
 * may begin at any byte of the repeat and read eight bytes before the
 * pattern counter sends it back to that byte.  Expanding costs 256 writes
 * through the MEMAC window, so what is there is remembered and reused until
 * the pattern or the nibbles change. */
static const UWORD *pe_ptr;
static WORD    pe_msk;
static uint8_t pe_set, pe_clr, pe_valid;

/* One 16-pixel repeat as eight nibble-pair bytes, written twice over. */
static void patt_row(volatile uint8_t *w, UWORD bits, uint8_t set, uint8_t clr)
{
    WORD k;
    for (k = 0; k < 8; k++) {
        uint8_t b = (uint8_t)(((bits & 0x8000) ? (set & 0xF0) : (clr & 0xF0)) |
                              ((bits & 0x4000) ? (set & 0x0F) : (clr & 0x0F)));
        w[k] = b;
        w[k + 8] = b;
        bits <<= 2;
    }
}

static void patt_expand(uint8_t set, uint8_t clr)
{
    volatile uint8_t *w;
    WORD r;

    if (pe_valid && pe_ptr == vwk.patptr && pe_msk == vwk.patmsk &&
        pe_set == set && pe_clr == clr)
        return;
    w = vram_win(VR_PATT);
    for (r = 0; r < VR_PATT_ROWS; r++) {
        patt_row(w, vwk.patptr[r & vwk.patmsk], set, clr);
        w += VR_PATT_STRIDE;
    }
    pe_ptr = vwk.patptr;  pe_msk = vwk.patmsk;
    pe_set = set;         pe_clr = clr;
    pe_valid = 1;
}

/* How a span of pattern is combined with the screen, per writing mode.  The
 * expansion holds what the mode needs (see patt_rect_dev), and `nib` says
 * which nibbles of each byte are inside the rectangle: 0xFF for the run of
 * whole bytes, 0x0F or 0xF0 for a partial byte at an odd edge.
 *
 * Mode 6 (the nibble stencil) cannot write nibble 0, and pen 0 -- white --
 * IS nibble 0; so a transparent or erase fill in pen 0 goes through AND
 * instead, F where the pixel survives and 0 where it is cleared.  AND is a
 * read-modify-write mode and the blitter skips any byte whose source is 0,
 * which is precisely the byte whose two pixels should both be cleared, so
 * it is applied a nibble at a time with the other nibble's mask forced to
 * F through the XOR mask.  Replace at an edge must leave the other pixel
 * alone, which a copy cannot: there it is an AND to clear the nibble and
 * an OR of the pattern into it, the pair fill_rect_dev uses. */
enum { PT_COPY, PT_HR, PT_AND, PT_XOR };

static void patt_span(WORD how, uint32_t src, uint16_t sstride, uint32_t dst,
                      uint16_t bytes, uint16_t rows, uint8_t nib)
{
    switch (how) {
    case PT_COPY:
        if (nib == 0xFF) {
            blit_pattern(src, sstride, dst, SCR_STRIDE, bytes, rows,
                         0xFF, 0x00, BLT_MODE_COPY, 8);
        } else {
            blit_and(dst, SCR_STRIDE, bytes, rows, (uint8_t)~nib);
            blit_pattern(src, sstride, dst, SCR_STRIDE, bytes, rows,
                         nib, 0x00, BLT_MODE_OR, 8);
        }
        break;
    case PT_HR:
        blit_pattern(src, sstride, dst, SCR_STRIDE, bytes, rows,
                     nib, 0x00, BLT_MODE_HR, 8);
        break;
    case PT_AND:
        if (nib & 0x0F)
            blit_pattern(src, sstride, dst, SCR_STRIDE, bytes, rows,
                         0x0F, 0xF0, BLT_MODE_AND, 8);
        if (nib & 0xF0)
            blit_pattern(src, sstride, dst, SCR_STRIDE, bytes, rows,
                         0xF0, 0x0F, BLT_MODE_AND, 8);
        break;
    default:
        blit_pattern(src, sstride, dst, SCR_STRIDE, bytes, rows,
                     nib, 0x00, BLT_MODE_XOR, 8);
        break;
    }
}

/* What the expansion holds and how it is combined, per writing mode:
 *
 *   replace       copy of (pen where set, 0 where clear); 0 is pen 0
 *   transparent   stencil (pen where set) -- or AND (0 where set) for pen 0
 *   XOR           XOR with F where set
 *   erase         transparent with set and clear exchanged
 */
static WORD patt_mode(WORD pen, uint8_t *set, uint8_t *clr)
{
    uint8_t nn = (uint8_t)(HW(pen) * 0x11);

    switch (vwk.wrt_mode + 1) {
    case MD_TRANS:
        if (nn) { *set = nn;   *clr = 0x00; return PT_HR; }
        *set = 0x00; *clr = 0xFF; return PT_AND;
    case MD_ERASE:
        if (nn) { *set = 0x00; *clr = nn;   return PT_HR; }
        *set = 0xFF; *clr = 0x00; return PT_AND;
    case MD_XOR:
        *set = 0xFF; *clr = 0x00; return PT_XOR;
    default:
        *set = nn;   *clr = 0x00; return PT_COPY;
    }
}

/* A device rectangle in the current pattern and writing mode: the general
 * case of vr_recfl, solid and hollow having been peeled off by fill_rect().
 *
 * The pattern is anchored to the SCREEN, not to the rectangle -- row y takes
 * pattern row (y AND mask), bit 15 is pixel 0 of every 16-aligned word -- so
 * adjoining fills tile seamlessly, which the GEM desktop's background relies
 * on.  A screen byte column c therefore wants pattern byte (c AND 7), which
 * is where each blit's source read starts, and the blitter's pattern counter
 * repeats the eight bytes from there.  The expansion has 16 rows, so a tall
 * rectangle goes in bands of up to 16 rows, each band its own short blit
 * list: edges first, then the run of whole bytes.  See patt_mode for what
 * each writing mode puts in the expansion. */
static void patt_rect_dev(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
{
    uint8_t set, clr;
    WORD how = patt_mode(pen, &set, &clr);
    WORD y;
    WORD bl = (WORD)(x1 >> 1), br = (WORD)(x2 >> 1);

    if (blit_pending())         /* nothing may still be reading VR_PATT */
        blit_run();
    patt_expand(set, clr);

    y = y1;
    while (y <= y2) {
        WORD     pr   = (WORD)(y & (VR_PATT_ROWS - 1));
        uint16_t rows = (uint16_t)(VR_PATT_ROWS - pr);
        uint32_t src  = VR_PATT + (uint32_t)pr * VR_PATT_STRIDE;
        uint32_t dst  = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE;
        WORD l = bl, r = br;

        if (rows > (uint16_t)(y2 - y + 1))
            rows = (uint16_t)(y2 - y + 1);
        if (x1 & 1) {                                   /* partial left */
            patt_span(how, src + (l & 7), VR_PATT_STRIDE, dst + l, 1, rows, 0x0F);
            l++;
        }
        if ((x2 & 1) == 0) {                            /* partial right */
            patt_span(how, src + (r & 7), VR_PATT_STRIDE, dst + r, 1, rows, 0xF0);
            r--;
        }
        if (r >= l)
            patt_span(how, src + (l & 7), VR_PATT_STRIDE, dst + l,
                      (uint16_t)(r - l + 1), rows, 0xFF);
        blit_run();
        y += (WORD)rows;
    }
}

/* A styled horizontal or vertical line as a pattern blit.  The AES draws its
 * rubber boxes and drag outlines in vsl_udsty dots, and plotting a 300x150
 * box pixel by pixel through the MEMAC window -- a read and a write on the
 * 1.79 MHz bus for each of 900 dots -- took milliseconds to show and again
 * to erase, enough to push a WM_MOVED past the frame it was due in.
 *
 * The style is anchored to the line's FIRST point: bit 15 there, the next
 * bit at each step towards the second point, in either direction, which is
 * what the Bresenham path below does and what tools/vdiref.py specifies.
 * As a word anchored to the screen (bit 15 at pixel 0 of every 16-aligned
 * word, the way a fill pattern is) that is the style rotated by the start
 * position, and clipping the line afterwards moves nothing.  A horizontal
 * line is one row of that word, expanded like a pattern row; a vertical line
 * is a column of bytes, one per row, all-set or all-clear, written once and
 * replicated by the blitter to VR_LINE_V_ROWS so that any screen height is
 * one control block.  Each strip is remembered, like the fill expansion: a
 * box's four sides alternate two phases, and its erase repeats them. */
static UWORD   lh_bits, lv_bits;
static uint8_t lh_set, lh_clr, lh_valid;
static uint8_t lv_set, lv_clr, lv_valid;

static UWORD style_anchor(UWORD mask, WORD from, WORD dir)
{
    UWORD p = 0;
    WORD j;

    for (j = 0; j < 16; j++) {
        UWORD i = (UWORD)((dir > 0) ? (j - from) : (from - j)) & 15;
        if (mask & (UWORD)(1u << (15 - i)))
            p |= (UWORD)(1u << (15 - j));
    }
    return p;
}

static void style_line(WORD x1, WORD y1, WORD x2, WORD y2, UWORD mask)
{
    uint8_t set, clr;
    WORD how = patt_mode(vwk.line_color, &set, &clr);
    WORD a, b;
    UWORD bits;
    uint32_t dst;

    if (blit_pending())         /* nothing may still be reading the strips */
        blit_run();
    if (y1 == y2) {
        WORD l, r;

        bits = style_anchor(mask, x1, (x2 >= x1) ? 1 : -1);
        a = x1;  b = x2;  order(&a, &b);
        if (!clip_rect(&a, &y1, &b, &y2))
            return;
        if (!(lh_valid && lh_bits == bits && lh_set == set && lh_clr == clr)) {
            patt_row(vram_win(VR_LINE_H), bits, set, clr);
            lh_bits = bits;  lh_set = set;  lh_clr = clr;  lh_valid = 1;
        }
        dst = VR_SCREEN0 + (uint32_t)y1 * SCR_STRIDE;
        l = (WORD)(a >> 1);  r = (WORD)(b >> 1);
        if (a & 1) {                                    /* partial left */
            patt_span(how, VR_LINE_H + (l & 7), 0, dst + l, 1, 1, 0x0F);
            l++;
        }
        if ((b & 1) == 0) {                             /* partial right */
            patt_span(how, VR_LINE_H + (r & 7), 0, dst + r, 1, 1, 0xF0);
            r--;
        }
        if (r >= l)
            patt_span(how, VR_LINE_H + (l & 7), 0, dst + l,
                      (uint16_t)(r - l + 1), 1, 0xFF);
    } else {
        bits = style_anchor(mask, y1, (y2 >= y1) ? 1 : -1);
        a = y1;  b = y2;  order(&a, &b);
        if (!clip_rect(&x1, &a, &x2, &b))
            return;
        if (!(lv_valid && lv_bits == bits && lv_set == set && lv_clr == clr)) {
            volatile uint8_t *w = vram_win(VR_LINE_V);
            UWORD bb = bits;
            WORD k;
            for (k = 0; k < 16; k++) {
                w[k] = (bb & 0x8000) ? set : clr;
                bb <<= 1;
            }
            /* the column, 16 rows of it, copied over the rest of the strip:
             * a source Y step of 0 re-reads the same 16 bytes each row */
            blit_copy(VR_LINE_V, 0, VR_LINE_V + 16, 16, 16,
                      (uint16_t)(VR_LINE_V_ROWS / 16 - 1));
            blit_run();
            lv_bits = bits;  lv_set = set;  lv_clr = clr;  lv_valid = 1;
        }
        dst = VR_SCREEN0 + (uint32_t)a * SCR_STRIDE + (uint32_t)(x1 >> 1);
        patt_span(how, VR_LINE_V + (a & 15), 1, dst, 1, (uint16_t)(b - a + 1),
                  (x1 & 1) ? 0x0F : 0xF0);
    }
    blit_run();
}

/* vr_recfl's rectangle: the current pattern in the current mode.  The two
 * patterns with nothing to decide per pixel take the constant-source fills:
 * solid is paint_rect; hollow in replace mode is a fill in pen 0, in erase
 * mode a fill in the pen ("pen where clear", and every bit is), and in the
 * other two modes nothing at all. */
static void fill_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
{
    if (vwk.patptr == &pat_solid) {
        paint_rect(x1, y1, x2, y2, pen);
        return;
    }
    if (vwk.patptr == &pat_hollow) {
        switch (vwk.wrt_mode + 1) {
        case MD_REPLACE: fill_rect_dev(x1, y1, x2, y2, HW(0));    break;
        case MD_ERASE:   fill_rect_dev(x1, y1, x2, y2, HW(pen));  break;
        default:         return;
        }
        blit_run();
        return;
    }
    patt_rect_dev(x1, y1, x2, y2, pen);
}

/* ---------------------------------------------------------------------- */
/* mouse cursor                                                           */
/* ---------------------------------------------------------------------- */

/* GEM's MFORM, as vsc_form delivers it: 37 words of intin.  Rendering rule is
 * the standard one -- the MASK is painted in mf_bg first, then the DATA over
 * it in mf_fg, so a mask bit with no data bit gives the outline colour and a
 * clear mask bit leaves the screen alone. */
static WORD  cur_xhot, cur_yhot, cur_bg, cur_fg;
static UWORD cur_mask[16], cur_data[16];
static WORD  cur_hide = 1;          /* visible only at 0; starts hidden */
static WORD  cur_drawn;             /* is the saved block valid?        */
static WORD  sv_bx, sv_y, sv_nb, sv_nr;   /* what cursor_save() captured */

/* The pointer is drawn by the blitter.  vsc_form expands the form to 4bpp
 * strips, one pair per parity of x: an AND strip ($0 under the form, $F
 * elsewhere) and an OR strip (the data colour under the data, the mask colour
 * under the rest of the mask, $0 elsewhere).  A show is then the save copy
 * and two blits, one chain, wherever the pointer is; a hide is one copy.
 *
 * The first version plotted the 256 pixels through the MEMAC window.  At
 * ~60 us a pixel that was 12 ms a show -- most of a frame -- paid on every
 * move and twice around every primitive the AES hides the pointer for, and
 * it is what pushed the window sizer's release past the frame it was due in
 * (docs/phase8b.md).  The four strips are a kilobyte written once per form.
 *
 * Like real GEM the pointer ignores the clipping rectangle: it is drawn
 * wherever it is on the screen (the plotted version clipped it, and so did
 * the model -- both were wrong the same way). */
#define CUR_W          16
#define CUR_ROWS       VR_CURSOR_ROWS
#define CUR_STRIDE     VR_CURSOR_STRIDE
#define CUR_BYTES      (CUR_W / 2 + 1)         /* the odd-x span; even pads */
#define CUR_STRIP_LEN  ((uint32_t)CUR_STRIDE * CUR_ROWS)
#define VR_CUR_AND(par) (VR_CURSOR + (uint32_t)(par) * 2 * CUR_STRIP_LEN)
#define VR_CUR_OR(par)  (VR_CUR_AND(par) + CUR_STRIP_LEN)
typedef char cursor_strips_fit_one_page
    [((VR_CURSOR & 0xFFFUL) + VR_CURSOR_LEN <= 0x1000UL) ? 1 : -1];
typedef char cursor_stride_holds_the_odd_span[(CUR_STRIDE >= CUR_BYTES) ? 1 : -1];

static void cursor_expand(void)
{
    volatile uint8_t *w;
    WORD par, row, p;
    uint8_t fg = (uint8_t)HW(cur_fg), bg = (uint8_t)HW(cur_bg);

    if (blit_pending())         /* a show may still be reading the strips */
        blit_run();
    w = vram_win(VR_CURSOR);
    for (par = 0; par < 2; par++) {
        volatile uint8_t *pa = w + (uint16_t)(par * 2 * CUR_STRIP_LEN);
        volatile uint8_t *po = pa + (uint16_t)CUR_STRIP_LEN;
        for (row = 0; row < CUR_ROWS; row++) {
            UWORD m = cur_mask[row], d = cur_data[row];
            uint8_t a = 0, o = 0;
            /* strip pixel p holds form column p - par: at odd x the form
             * starts in the low nibble of its first byte */
            for (p = 0; p < 2 * CUR_BYTES; p++) {
                WORD col = (WORD)(p - par);
                uint8_t an = 0x0F, on = 0x00;
                if (col >= 0 && col < CUR_W) {
                    UWORD bit = (UWORD)(0x8000u >> col);
                    if (d & bit)      { an = 0; on = fg; }
                    else if (m & bit) { an = 0; on = bg; }
                }
                if (p & 1) {
                    pa[p >> 1] = (uint8_t)(a | an);
                    po[p >> 1] = (uint8_t)(o | on);
                } else {
                    a = (uint8_t)(an << 4);
                    o = (uint8_t)(on << 4);
                }
            }
            pa += CUR_STRIDE;
            po += CUR_STRIDE;
        }
    }
}

/* Queue the copy of what is under the form.  The caller runs the chain. */
static void cursor_save(WORD cx, WORD cy)
{
    WORD bx0, bx1, y0, y1;
    bx0 = (WORD)(cx >> 1);
    bx1 = (WORD)((cx + 15) >> 1);
    y0 = cy;
    y1 = (WORD)(cy + 15);
    if (bx0 < 0) bx0 = 0;
    if (y0 < 0) y0 = 0;
    if (bx1 > SCR_STRIDE - 1) bx1 = SCR_STRIDE - 1;
    if (y1 > SCR_H - 1) y1 = SCR_H - 1;
    if (bx1 < bx0 || y1 < y0) { sv_nb = 0; return; }
    sv_bx = bx0;
    sv_y  = y0;
    sv_nb = (WORD)(bx1 - bx0 + 1);
    sv_nr = (WORD)(y1 - y0 + 1);
    blit_copy(VR_SCREEN0 + (uint32_t)sv_y * SCR_STRIDE + (uint32_t)sv_bx,
              SCR_STRIDE, VR_CURSAVE, VR_CURSAVE_STRIDE,
              (uint16_t)sv_nb, (uint16_t)sv_nr);
}

static void cursor_restore(void)
{
    if (!sv_nb)
        return;
    blit_copy(VR_CURSAVE, VR_CURSAVE_STRIDE,
              VR_SCREEN0 + (uint32_t)sv_y * SCR_STRIDE + (uint32_t)sv_bx,
              SCR_STRIDE, (uint16_t)sv_nb, (uint16_t)sv_nr);
    blit_run();
    sv_nb = 0;
}

/* Queue the two strip blits over the block cursor_save() described.  The
 * strips are read from the same offset the screen edges clipped away. */
static void cursor_paint(WORD cx, WORD cy)
{
    WORD par = (WORD)(cx & 1);
    uint32_t off, dst;
    if (!sv_nb)
        return;
    off = (uint32_t)(sv_y - cy) * CUR_STRIDE
        + (uint32_t)(sv_bx - (WORD)(cx >> 1));
    dst = VR_SCREEN0 + (uint32_t)sv_y * SCR_STRIDE + (uint32_t)sv_bx;
    blit_mask(VR_CUR_AND(par) + off, CUR_STRIDE, dst, SCR_STRIDE,
              (uint16_t)sv_nb, (uint16_t)sv_nr, 0xFF, 0x00, BLT_MODE_AND);
    blit_mask(VR_CUR_OR(par) + off, CUR_STRIDE, dst, SCR_STRIDE,
              (uint16_t)sv_nb, (uint16_t)sv_nr, 0xFF, 0x00, BLT_MODE_OR);
}

static void cursor_show_now(void)
{
    WORD cx = (WORD)(ptr_seen.x - cur_xhot);
    WORD cy = (WORD)(ptr_seen.y - cur_yhot);
    if (blit_pending())         /* room for the three blocks */
        blit_run();
    cursor_save(cx, cy);
    cursor_paint(cx, cy);
    blit_run();
    cur_drawn = 1;
}

static void cursor_hide_now(void)
{
    if (cur_drawn) {
        cursor_restore();
        cur_drawn = 0;
    }
}

/* Called by the input loop after ptr_poll().  Erase-move-redraw, and only when
 * the pointer actually moved -- redrawing a stationary cursor every frame
 * would cost three blits for nothing. */
void vdi_cursor_move(void)
{
    static WORD lastx = -1, lasty = -1;
    if (cur_hide)
        return;
    if (ptr_seen.x == lastx && ptr_seen.y == lasty && cur_drawn)
        return;
    cursor_hide_now();
    lastx = ptr_seen.x;
    lasty = ptr_seen.y;
    cursor_show_now();
}

static void vdi_vsc_form(void)
{
    WORD i;
    cur_xhot = intin[0];
    cur_yhot = intin[1];
    /* intin[2] is nplanes (always 1 here); [3] bg, [4] fg */
    cur_bg = intin[3];
    cur_fg = intin[4];
    for (i = 0; i < 16; i++) {
        cur_mask[i] = (UWORD)intin[5 + i];
        cur_data[i] = (UWORD)intin[21 + i];
    }
    cursor_expand();
}

/* GEM's nesting rule: v_hide_c increments a counter and the cursor is visible
 * only at zero.  v_show_c with intin[0] == 0 forces it visible regardless of
 * how deeply it was hidden; anything else decrements by one. */
static void vdi_v_show_c(void)
{
    if (contrl[3] > 0 && intin[0] != 0) {
        if (cur_hide > 0)
            cur_hide--;
    } else {
        cur_hide = 0;
    }
    if (cur_hide == 0 && !cur_drawn)
        cursor_show_now();
}

static void vdi_v_hide_c(void)
{
    if (cur_hide == 0)
        cursor_hide_now();
    cur_hide++;
}

/* v_locator.  GEM passes the INITIAL locator position in ptsin, which is the
 * standard way to place the pointer, and returns where it ended up.  In sample
 * mode (vsin_mode 2) it returns immediately without waiting for input, which
 * is what the AES uses via gsx_setmousexy(). */
static void vdi_v_locator(void)
{
    if (contrl[1] > 0)
        ptr_warp(ptsin[0], ptsin[1]);
    ptsout[0] = ptr_seen.x;
    ptsout[1] = ptr_seen.y;
    intout[0] = 0;                  /* terminator: none */
    contrl[2] = 1;
    contrl[4] = 1;
}

static void vdi_vq_mouse(void)
{
    intout[0] = ptr_seen.buttons;
    ptsout[0] = ptr_seen.x;
    ptsout[1] = ptr_seen.y;
    contrl[2] = 1;
    contrl[4] = 1;
}

/* ---------------------------------------------------------------------- */
/* text                                                                   */
/* ---------------------------------------------------------------------- */

/* Expand the 1bpp font strip into 4bpp glyph MASKS in VRAM: $F where ink,
 * $0 where paper.  One mask serves every ink colour -- see draw_glyph().
 *
 * Layout mirrors the source strip so a glyph is a single rectangle:
 *   VR_FONT + row*FONT_VSTRIDE + ch*FONT_BYTES, 4 bytes wide, 8 rows.
 *
 * The blitter has no shifter, so that strip only serves an EVEN x.  A second
 * strip holds every glyph one pixel to the right -- five bytes wide, with a
 * paper nibble at each end -- for odd x:
 *   VR_FONT_ODD + row*FONT_OSTRIDE + ch*FONT_OBYTES, 5 bytes wide, 8 rows.
 * The paper nibbles at the ends make the AND/OR pair leave the neighbouring
 * pixels alone, which is what keeps this correct under clipping.  Before it
 * existed, text at odd x went pixel by pixel through the MEMAC window at
 * about 6 ms per glyph -- a dialog's worth of text took half a second.
 *
 * Expanding on target rather than shipping a 4bpp blob keeps 2 KB linked
 * instead of 18 KB, which matters when the whole program lives in bank $00.
 */
#define FONT_BYTES   (FONT_W / 2)                 /* 4 bytes per glyph row */
#define FONT_VSTRIDE (256 * FONT_BYTES)           /* 1024 bytes per row    */
#define FONT_OBYTES  (FONT_W / 2 + 1)             /* 5 bytes, shifted      */
#define FONT_OSTRIDE (256 * FONT_OBYTES)          /* 1280 bytes per row    */
#define VR_FONT_ODD  (VR_FONT + (uint32_t)FONT_VSTRIDE * FONT_H)

/* A nibble-pair for every possible bit-pair, so expansion is a table lookup
 * rather than four conditionals per byte. */
static const uint8_t nib2[4] = { 0x00, 0x0F, 0xF0, 0xFF };

void vdi_font_expand(void)
{
    /* The expanded font is 8 KB at VR_FONT: exactly two 4 KB MEMAC pages, and
     * within a page the layout is contiguous.  Streaming through a window
     * pointer keeps this to 16-bit pointer arithmetic; doing it with a
     * vram_write() per glyph costs 32-bit address maths 2,048 times and took
     * several emulated SECONDS. */
    WORD page, row, ch;
    for (page = 0; page < 2; page++) {
        volatile uint8_t *p = vram_win(VR_FONT + (uint32_t)page * 0x1000);
        for (row = (WORD)(page * 4); row < (WORD)(page * 4 + 4); row++) {
            const uint8_t __far *sr = &font8x8[row * FONT_STRIDE];
            for (ch = 0; ch < 256; ch++) {
                uint8_t b = sr[ch];
                *p++ = nib2[(b >> 6) & 3];
                *p++ = nib2[(b >> 4) & 3];
                *p++ = nib2[(b >> 2) & 3];
                *p++ = nib2[b & 3];
            }
        }
    }

    /* The odd strip's rows are 1280 bytes, so they straddle MEMAC pages:
     * count the bytes left in the window and re-map when it runs out. */
    {
        uint32_t a = VR_FONT_ODD;
        volatile uint8_t *p = vram_win(a);
        uint16_t left = (uint16_t)(MEMAC_WIN_SIZE - (a & (MEMAC_WIN_SIZE - 1)));
        for (row = 0; row < FONT_H; row++) {
            const uint8_t __far *sr = &font8x8[row * FONT_STRIDE];
            for (ch = 0; ch < 256; ch++) {
                uint8_t b = sr[ch];
                uint8_t out[FONT_OBYTES];
                WORD k;
                out[0] = nib2[(b >> 7) & 1];          /* paper, pixel 0     */
                out[1] = nib2[(b >> 5) & 3];
                out[2] = nib2[(b >> 3) & 3];
                out[3] = nib2[(b >> 1) & 3];
                out[4] = nib2[(b & 1) << 1];          /* pixel 7, paper     */
                for (k = 0; k < FONT_OBYTES; k++) {
                    *p++ = out[k];
                    if (--left == 0) {          /* next page, from its top */
                        a = (a | (MEMAC_WIN_SIZE - 1)) + 1;
                        p = vram_win(a);
                        left = MEMAC_WIN_SIZE;
                    }
                }
            }
        }
    }
}

/* Draw one glyph with its top-left at (cx, cy), cell fully visible, either
 * parity of cx -- the odd strip carries the shift.
 *
 * Two blits, and they work for every ink colour including 0:
 *
 *   AND  src=mask, and=$FF, xor=$FF, mode 4
 *        c is $00 where ink and $FF where paper.  Mode 4 is the one mode that
 *        WRITES 0 when c == 0 instead of skipping, so this clears exactly the
 *        ink pixels and leaves the paper ones untouched.
 *   OR   src=mask, and=ink*$11, xor=$00, mode 3
 *        c is the ink colour where ink and 0 where paper; a zero c is skipped,
 *        so only ink pixels are painted.
 *
 * With ink == 0 the OR writes nothing -- and it does not need to, because the
 * AND already left colour 0 there.  That is why no inverted mask is needed,
 * and why mode 6's nibble stencil (which cannot write colour 0) is not used.
 *
 * A caller that has just cleared the cell (replace mode) passes `cleared`
 * and the AND is skipped: there is nothing left for it to clear.
 */
static void draw_glyph(WORD ch, WORD cx, WORD cy, WORD hwink, WORD cleared)
{
    uint32_t src, dst;
    uint16_t sstride, bytes;
    uint8_t  c = (uint8_t)(((hwink & 0x0F) << 4) | (hwink & 0x0F));

    if (cx & 1) {
        src     = VR_FONT_ODD + (uint32_t)(ch & 0xFF) * FONT_OBYTES;
        sstride = FONT_OSTRIDE;
        bytes   = FONT_OBYTES;
    } else {
        src     = VR_FONT + (uint32_t)(ch & 0xFF) * FONT_BYTES;
        sstride = FONT_VSTRIDE;
        bytes   = FONT_BYTES;
    }
    dst = VR_SCREEN0 + (uint32_t)cy * SCR_STRIDE + (uint32_t)(cx >> 1);

    if (!cleared)
        blit_mask(src, sstride, dst, SCR_STRIDE, bytes, FONT_H,
                  0xFF, 0xFF, BLT_MODE_AND);
    if (c)
        blit_mask(src, sstride, dst, SCR_STRIDE, bytes, FONT_H,
                  c, 0x00, BLT_MODE_OR);
}

/* Pixel-by-pixel fallback: a glyph the clipping rectangle cuts, or a writing
 * mode the two-blit path does not do (XOR, erase).  Correct everywhere, and
 * about fifty times slower than the blitter path. */
static void draw_glyph_cpu(WORD ch, WORD cx, WORD cy)
{
    WORD row, col;
    for (row = 0; row < FONT_H; row++) {
        uint8_t b = font8x8[row * FONT_STRIDE + (ch & 0xFF)];
        for (col = 0; col < FONT_W; col++)
            paint_pixel((WORD)(cx + col), (WORD)(cy + row), vwk.text_color,
                        (WORD)(b & (0x80 >> col)));
    }
}

/* v_gtext.  Alignment is left/baseline (vst_alignment is not implemented, so
 * only the default applies): the y given is the BASELINE, and the cell top is
 * y - FONT_TOP.
 *
 * Replace mode paints the cell background first; transparent mode leaves it.
 * GEM has no separate text background colour -- replace mode uses pen 0.
 * XOR and erase modes go pixel by pixel; the AES draws text in replace and
 * transparent mode only, so those two are the ones the blitter serves. */
static void vdi_v_gtext(void)
{
    WORD x = ptsin[0], y = ptsin[1];
    WORD n = contrl[3], i;
    WORD cy = (WORD)(y - FONT_TOP);
    WORD mode = (WORD)(vwk.wrt_mode + 1);
    WORD blittable = (mode == MD_REPLACE || mode == MD_TRANS);

    for (i = 0; i < n && i < INTIN_SIZE; i++) {
        WORD cx = (WORD)(x + i * FONT_W);
        WORD fits = (cx >= 0 && cy >= 0 &&
                     cx + FONT_W <= SCR_W && cy + FONT_H <= SCR_H);
        if (fits && vwk.clip)
            fits = (cx >= vwk.xmn_clip && cy >= vwk.ymn_clip &&
                    cx + FONT_W - 1 <= vwk.xmx_clip &&
                    cy + FONT_H - 1 <= vwk.ymx_clip);
        if (fits && blittable) {
            if (mode == MD_REPLACE)
                fill_rect_dev(cx, cy, (WORD)(cx + FONT_W - 1),
                              (WORD)(cy + FONT_H - 1), HW(0));
            draw_glyph(intin[i], cx, cy, HW(vwk.text_color),
                       (WORD)(mode == MD_REPLACE));
            blit_run();
        } else {
            draw_glyph_cpu(intin[i], cx, cy);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* opcodes                                                                */
/* ---------------------------------------------------------------------- */

static void v_nop(void) { }

/* work_out: intout[0..44] then ptsout[0..11].  Values describe this device --
 * a 640x240, 16-colour, non-scalable raster screen with no GDPs yet. */
static void fill_workout(void)
{
    WORD i;
    for (i = 0; i < 45; i++)
        intout[i] = 0;
    for (i = 0; i < 12; i++)
        ptsout[i] = 0;
    intout[0]  = SCR_W - 1;         /* xres - 1 */
    intout[1]  = SCR_H - 1;         /* yres - 1 */
    intout[2]  = 0;                 /* device is precisely scalable */
    intout[3]  = 372;               /* pixel width  in microns (approx) */
    intout[4]  = 372;               /* pixel height in microns */
    intout[5]  = 1;                 /* one character height */
    intout[6]  = 7;                 /* line types */
    intout[7]  = 1;                 /* line widths (1 = continuous) */
    intout[8]  = 6;                 /* marker types */
    intout[9]  = 8;                 /* marker sizes */
    intout[10] = 1;                 /* faces */
    intout[11] = 24;                /* patterns */
    intout[12] = 12;                /* hatches */
    intout[13] = 16;                /* colours available at once */
    intout[14] = 0;                 /* GDPs supported -- none yet */
    intout[35] = 1;                 /* can do colour */
    intout[36] = 0;                 /* no text rotation */
    intout[37] = 1;                 /* can do fill area */
    intout[38] = 0;                 /* no cell array */
    intout[39] = 16;                /* palette: >2 means colour */
    intout[40] = 1;                 /* locators */
    intout[41] = 1;                 /* valuators */
    intout[42] = 1;                 /* choice devices */
    intout[43] = 1;                 /* string devices */
    intout[44] = 2;                 /* workstation type: input/output */
    ptsout[0] = 8;  ptsout[1] = 8;  /* min char width / height */
    ptsout[2] = 8;  ptsout[3] = 8;  /* max char width / height */
    ptsout[4] = 1;  ptsout[5] = 0;  /* min line width */
    ptsout[6] = 1;  ptsout[7] = 0;  /* max line width */
    contrl[2] = 6;
    contrl[4] = 45;
}

/* The palette in HARDWARE order: entry map_col[pen] gets pen's colour. */
static void load_palette(void)
{
    uint8_t hw[16 * 3];
    WORD pen;
    for (pen = 0; pen < 16; pen++) {
        WORD h = map_col[pen];
        hw[h * 3]     = gem_rgb[pen * 3];
        hw[h * 3 + 1] = gem_rgb[pen * 3 + 1];
        hw[h * 3 + 2] = gem_rgb[pen * 3 + 2];
    }
    vbxe_palette(1, 0, hw, 16);
}

static void vdi_v_opnwk(void)
{
    vwk.handle     = 1;
    vwk.clip       = 0;
    vwk.xmn_clip   = 0;
    vwk.ymn_clip   = 0;
    vwk.xmx_clip   = SCR_W - 1;
    vwk.ymx_clip   = SCR_H - 1;
    vwk.wrt_mode   = MD_REPLACE - 1;
    vwk.line_width = 1;
    vwk.fill_per   = 1;
    /* init_wk: the attributes work_in asks for, validated the way the
     * setters validate them.  (The donor stores work_in[8] as the fill
     * index WITHOUT the minus one that vsf_style applies -- so does the
     * original DRI driver -- which selects the pattern after the one asked
     * for and reads past the table at index 24.  Not reproduced.) */
    {
        WORD l, top;
        l = intin[1];  vwk.line_index = (l < 1 || l > 7)  ? 1 : l;
        l = intin[2];  vwk.line_color = (l < 0 || l > 15) ? 1 : l;
        l = intin[6];  vwk.text_color = (l < 0 || l > 15) ? 1 : l;
        l = intin[7];  vwk.fill_style = (l < FIS_HOLLOW || l > FIS_USER) ? FIS_HOLLOW : l;
        top = (vwk.fill_style == FIS_PATTERN) ? MAX_FILL_PATTERN : MAX_FILL_HATCH;
        l = intin[8];  vwk.fill_index = (WORD)(((l < 1 || l > top) ? 1 : l) - 1);
        l = intin[9];  vwk.fill_color = (l < 0 || l > 15) ? 1 : l;
    }
    st_fl_ptr();
    pe_valid = 0;
    lh_valid = lv_valid = 0;
    /* Opening a workstation resets the driver, cursor included: the saved
     * block under the pointer belongs to a screen that no longer applies. */
    cur_hide  = 1;
    cur_drawn = 0;
    sv_nb     = 0;
    cursor_expand();                /* strips for the form in force  */
    load_palette();
    fill_workout();
    contrl[6] = vwk.handle;
}

static void vdi_vq_extnd(void)
{
    WORD i;
    if (intin[0] == 0) {
        fill_workout();
        return;
    }
    for (i = 0; i < 45; i++)
        intout[i] = 0;
    for (i = 0; i < 12; i++)
        ptsout[i] = 0;
    intout[0] = 0;                  /* screen type: not a separate buffer */
    intout[1] = 16;                 /* background colours */
    intout[2] = 0;                  /* text effects */
    intout[3] = 0;                  /* scaling: raster, not scalable */
    intout[4] = 4;                  /* PLANES -- the AES reads this one */
    intout[5] = 1;                  /* lookup table present */
    intout[6] = 1;                  /* performance: rough */
    contrl[2] = 6;
    contrl[4] = 45;
}

/* v_clrwk.  If the cursor is up, the block saved underneath it describes a
 * screen that is about to cease to exist, so it must be thrown away rather
 * than restored -- restoring it would stamp stale pixels onto a cleared
 * screen.  The cursor is then repainted so it survives the clear, which is
 * what GEM applications expect. */
static void vdi_v_clrwk(void)
{
    WORD was_drawn = cur_drawn;
    sv_nb = 0;                          /* discard, do not restore */
    cur_drawn = 0;
    blit_fill(VR_SCREEN0, SCR_STRIDE, SCR_STRIDE, SCR_H, 0x00);
    blit_run();
    if (was_drawn && cur_hide == 0)
        cursor_show_now();
}

static void vdi_vs_clip(void)
{
    WORD x1 = ptsin[0], y1 = ptsin[1], x2 = ptsin[2], y2 = ptsin[3];
    vwk.clip = intin[0];
    if (vwk.clip) {
        order(&x1, &x2);
        order(&y1, &y2);
        vwk.xmn_clip = x1;  vwk.ymn_clip = y1;
        vwk.xmx_clip = x2;  vwk.ymx_clip = y2;
    } else {
        vwk.xmn_clip = 0;   vwk.ymn_clip = 0;
        vwk.xmx_clip = SCR_W - 1;
        vwk.ymx_clip = SCR_H - 1;
    }
}

/* vr_recfl -- the AES's workhorse.  It never calls v_fillarea or the bar GDP;
 * every filled box in GEM comes through here. */
static void vdi_vr_recfl(void)
{
    WORD x1 = ptsin[0], y1 = ptsin[1], x2 = ptsin[2], y2 = ptsin[3];
    order(&x1, &x2);
    order(&y1, &y2);
    if (!clip_rect(&x1, &y1, &x2, &y2))
        return;
    fill_rect(x1, y1, x2, y2, vwk.fill_color);
}

/* v_pline.  Horizontal and vertical runs go through the blitter as 1-row and
 * 1-column rectangles; that covers essentially every line the AES draws, since
 * it builds boxes out of axis-aligned polylines and never calls the GDPs.
 * Diagonals fall back to Bresenham through the MEMAC window and are slow. */
static void draw_line(WORD x1, WORD y1, WORD x2, WORD y2)
{
    UWORD mask = line_styles[(vwk.line_index >= 1 && vwk.line_index <= 7)
                             ? vwk.line_index : 1];
    WORD dx, dy, sx, sy, err, e2, bit = 0;

    if ((y1 == y2 || x1 == x2) && mask != 0xFFFF) {
        style_line(x1, y1, x2, y2, mask);
        return;
    }
    if (y1 == y2 && mask == 0xFFFF) {
        WORD a = x1, b = x2;
        order(&a, &b);
        if (clip_rect(&a, &y1, &b, &y2))
            paint_rect(a, y1, b, y2, vwk.line_color);
        return;
    }
    if (x1 == x2 && mask == 0xFFFF) {
        WORD a = y1, b = y2;
        order(&a, &b);
        if (clip_rect(&x1, &a, &x2, &b))
            paint_rect(x1, a, x2, b, vwk.line_color);
        return;
    }

    dx = (WORD)(x2 - x1); if (dx < 0) dx = (WORD)-dx;
    dy = (WORD)(y2 - y1); if (dy < 0) dy = (WORD)-dy;
    sx = (WORD)(x1 < x2 ? 1 : -1);
    sy = (WORD)(y1 < y2 ? 1 : -1);
    err = (WORD)(dx - dy);
    for (;;) {
        paint_pixel(x1, y1, vwk.line_color,
                    (WORD)(mask & (1u << (15 - (bit & 15)))));
        bit++;
        if (x1 == x2 && y1 == y2)
            break;
        e2 = (WORD)(err << 1);
        if (e2 > -dy) { err = (WORD)(err - dy); x1 = (WORD)(x1 + sx); }
        if (e2 <  dx) { err = (WORD)(err + dx); y1 = (WORD)(y1 + sy); }
    }
}

static void vdi_v_pline(void)
{
    WORD n = contrl[1], i;
    for (i = 0; i + 1 < n; i++)
        draw_line(ptsin[i * 2], ptsin[i * 2 + 1],
                  ptsin[i * 2 + 2], ptsin[i * 2 + 3]);
}

/* attribute setters -- each returns the value actually selected */
static void vdi_vsl_type(void)
{
    WORD v = intin[0];
    if (v < 1 || v > 7) v = 1;
    vwk.line_index = v;
    intout[0] = v;  contrl[4] = 1;
}
static void vdi_vsl_width(void)
{
    vwk.line_width = 1;                 /* only 1 is supported (see work_out) */
    ptsout[0] = 1;  ptsout[1] = 0;  contrl[2] = 1;
}
static void vdi_vsl_color(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 15) v = 1;
    vwk.line_color = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vsf_interior(void)
{
    WORD v = intin[0];
    if (v < FIS_HOLLOW || v > FIS_USER) v = FIS_HOLLOW;
    vwk.fill_style = v;  intout[0] = v;  contrl[4] = 1;
    st_fl_ptr();
}
/* The valid range depends on the interior in force -- 1..24 for patterns,
 * 1..12 for hatches -- and an index outside it becomes 1, not the nearest
 * end.  Stored minus one, as the donor keeps it. */
static void vdi_vsf_style(void)
{
    WORD v = intin[0];
    WORD top = (vwk.fill_style == FIS_PATTERN) ? MAX_FILL_PATTERN : MAX_FILL_HATCH;
    if (v < 1 || v > top) v = 1;
    vwk.fill_index = (WORD)(v - 1);  intout[0] = v;  contrl[4] = 1;
    st_fl_ptr();
}
/* vsf_udpat: a 16-word single-plane pattern.  Anything else is refused, as
 * the donor refuses a count that is neither 16 nor 16 * planes; this device
 * has one plane's worth of pattern.  Returns nothing. */
static void vdi_vsf_udpat(void)
{
    WORD i;
    if (contrl[3] != 16)
        return;
    for (i = 0; i < 16; i++)
        vwk.ud_patrn[i] = (UWORD)intin[i];
    pe_valid = 0;                   /* the expansion may hold the old one */
}
static void vdi_vsf_color(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 15) v = 1;
    vwk.fill_color = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vst_color(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 15) v = 1;
    vwk.text_color = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vswr_mode(void)
{
    WORD v = intin[0];
    if (v < MD_REPLACE || v > MD_ERASE) v = MD_REPLACE;
    vwk.wrt_mode = (WORD)(v - 1);
    intout[0] = v;  contrl[4] = 1;
}

/* vro_cpyfm -- opaque raster copy.  The AES uses this for icons and for the
 * menu/alert screen save-restore (bb_save / bb_restore), always in mode 3
 * (S_ONLY, replace), which is the only mode implemented here.
 *
 * FORMS.  Each MFDB names a raster form: fd_addr 0 is the screen (the VDI's
 * own convention), anything else is a VRAM address whose rows are
 * fd_wdwidth words x fd_nplanes apart -- the AES's save buffer at VR_SAVE is
 * the one such form, laid out like the screen so a save is a same-
 * coordinates copy.  A null MFDB pointer is taken as the screen too, so a
 * bare screen-to-screen script (the conformance runner's) does not read a
 * form out of address 0.
 *
 * CLIPPING.  The destination is clipped to the workstation's rectangle and
 * the screen when it is the screen, to the form's own bounds otherwise, and
 * the source loses the same span (EmuTOS vdi_raster.c, do_clip).  The source
 * is then clipped to ITS form's bounds and the destination loses that span
 * -- a deliberate departure from the donor, which never clips a source and
 * so reads whatever lies past the edge: on the ST, the next row; here, the
 * next row for a form and, past the end of a form, someone else's VRAM.  A
 * drop-down that pokes off the screen (the Atari corpus's menu_sr landmine)
 * saves and restores the part that is on it.
 *
 * THE ALIGNMENT PROBLEM.  The VBXE blitter is a byte engine with no shifter,
 * so it can only move 4bpp pixels between positions of the SAME PARITY.  When
 * source and destination x have the same parity the whole copy is one blit;
 * when they differ, every pixel has to move half a byte and the blitter cannot
 * do it at all -- so that case falls back to the CPU through the MEMAC window
 * and is roughly twenty times slower.
 *
 * This is worth knowing before the window manager is designed: a window that
 * only ever moves by an EVEN number of pixels stays on the fast path, and one
 * that moves by an odd number does not.  Snapping window x to even pixels
 * costs nothing visually at 640 wide and keeps every move a pure blit.
 */
typedef struct {
    uint32_t base;
    uint16_t stride;
    WORD     w, h;
    WORD     screen;
} RFORM;

static void rform_of(WORD mfdb_lo, RFORM *f)
{
    const MFDB *m = (const MFDB *)(uint16_t)mfdb_lo;   /* forms live in bank $00 */
    WORD wdwidth, nplanes;

    if (m == 0 || m->fd_addr == 0) {
        f->base = VR_SCREEN0;  f->stride = SCR_STRIDE;
        f->w = SCR_W;  f->h = SCR_H;  f->screen = 1;
        return;
    }
    wdwidth = m->fd_wdwidth;        /* scalars first: never double a load */
    nplanes = m->fd_nplanes;        /* through a pointer in one expression */
    f->base   = m->fd_addr;
    f->stride = (uint16_t)(wdwidth * 2 * nplanes);
    f->w      = m->fd_w;
    f->h      = m->fd_h;
    f->screen = 0;
}

/* Clip a rectangle to a form's bounds.  Returns 0 if nothing is left. */
static WORD clip_to(WORD *x1, WORD *y1, WORD *x2, WORD *y2, WORD w, WORD h)
{
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 > w - 1) *x2 = (WORD)(w - 1);
    if (*y2 > h - 1) *y2 = (WORD)(h - 1);
    return (*x1 <= *x2 && *y1 <= *y2);
}

/* One pixel into a form, already known to be inside it. */
static void rform_plot(const RFORM *f, WORD x, WORD y, WORD hwpen)
{
    uint32_t a = f->base + (uint32_t)y * f->stride + (uint32_t)(x >> 1);
    uint8_t  b = vram_read8(a);
    if (x & 1)
        b = (uint8_t)((b & 0xF0) | (hwpen & 0x0F));
    else
        b = (uint8_t)((b & 0x0F) | ((hwpen & 0x0F) << 4));
    vram_write8(a, b);
}

static void vdi_vro_cpyfm(void)
{
    WORD sx1 = ptsin[0], sy1 = ptsin[1], sx2 = ptsin[2], sy2 = ptsin[3];
    WORD dx1 = ptsin[4], dy1 = ptsin[5], dx2 = ptsin[6], dy2 = ptsin[7];
    RFORM src, dst;
    WORD w, h, y;

    order(&sx1, &sx2); order(&sy1, &sy2);
    order(&dx1, &dx2); order(&dy1, &dy2);
    (void)dx2; (void)dy2;

    w = (WORD)(sx2 - sx1 + 1);
    h = (WORD)(sy2 - sy1 + 1);
    if (w <= 0 || h <= 0)
        return;
    rform_of(contrl[7], &src);
    rform_of(contrl[9], &dst);
    {
        WORD cx1 = dx1, cy1 = dy1;
        WORD cx2 = (WORD)(dx1 + w - 1), cy2 = (WORD)(dy1 + h - 1);

        if (dst.screen ? !clip_rect(&cx1, &cy1, &cx2, &cy2)
                       : !clip_to(&cx1, &cy1, &cx2, &cy2, dst.w, dst.h))
            return;
        sx1 = (WORD)(sx1 + (cx1 - dx1));
        sy1 = (WORD)(sy1 + (cy1 - dy1));
        dx1 = cx1;
        dy1 = cy1;
        w = (WORD)(cx2 - cx1 + 1);
        h = (WORD)(cy2 - cy1 + 1);
        sx2 = (WORD)(sx1 + w - 1);
        sy2 = (WORD)(sy1 + h - 1);
    }
    {
        WORD cx1 = sx1, cy1 = sy1, cx2 = sx2, cy2 = sy2;

        if (!clip_to(&cx1, &cy1, &cx2, &cy2, src.w, src.h))
            return;
        dx1 = (WORD)(dx1 + (cx1 - sx1));
        dy1 = (WORD)(dy1 + (cy1 - sy1));
        sx1 = cx1;
        sy1 = cy1;
        w = (WORD)(cx2 - cx1 + 1);
        h = (WORD)(cy2 - cy1 + 1);
    }

    if (((sx1 ^ dx1) & 1) == 0 && (sx1 & 1) == 0 && (w & 1) == 0) {
        /* byte-aligned both ends: one blit, run backwards if the two
         * overlap that way round */
        blit_move(src.base + (uint32_t)sy1 * src.stride + (uint32_t)(sx1 >> 1),
                  src.stride,
                  dst.base + (uint32_t)dy1 * dst.stride + (uint32_t)(dx1 >> 1),
                  dst.stride,
                  (uint16_t)(w >> 1), (uint16_t)h);
        blit_run();
        return;
    }
    /* Unaligned, or an odd width: pixel by pixel through the window.  Copy in
     * the direction that keeps an overlapping move safe. */
    for (y = 0; y < h; y++) {
        WORD sy = (dy1 > sy1) ? (WORD)(sy1 + h - 1 - y) : (WORD)(sy1 + y);
        WORD dy = (dy1 > sy1) ? (WORD)(dy1 + h - 1 - y) : (WORD)(dy1 + y);
        WORD i;
        for (i = 0; i < w; i++) {
            WORD sx = (dx1 > sx1) ? (WORD)(sx1 + w - 1 - i) : (WORD)(sx1 + i);
            WORD dx = (dx1 > sx1) ? (WORD)(dx1 + w - 1 - i) : (WORD)(dx1 + i);
            uint32_t a = src.base + (uint32_t)sy * src.stride + (uint32_t)(sx >> 1);
            uint8_t  v = vram_read8(a);
            rform_plot(&dst, dx, dy, (WORD)((sx & 1) ? (v & 0x0F) : (v >> 4)));
        }
    }
}

/* vrt_cpyfm -- transparent raster copy: a ONE-PLANE source expanded into the
 * device's colours.  This is how the AES draws icons and glyph masks.
 *
 * The source form lives in RAM, not VRAM, so the blitter cannot read it; but
 * it can do the writing.  The CPU expands the clipped destination rectangle
 * into two 4bpp strips at VR_STRIP -- an AND strip and an OR strip, the same
 * pair the pointer and the text path use -- through one MEMAC mapping, and
 * two blits apply them; XOR mode is one XOR blit of a single strip.  The
 * strips hold half a page each, so a wide form goes in bands.  The first
 * version plotted pixel by pixel: a read and a write on the 1.79 MHz bus per
 * pixel, ~60 us each, three frames for a 32x24 icon (docs/phase8b.md).
 *
 * The rules per mode, pixel by pixel, are the VDI's and tools/vdiref.py's:
 *   replace      fg where set, bg where clear
 *   transparent  fg where set
 *   XOR          complement where set
 *   erase        bg where CLEAR
 * and a pixel outside the clip rectangle or the screen is left alone: AND
 * $F, OR $0 (XOR $0).  Clipping is to the pixel here, as plot() clipped.
 *
 * Source bits are MSB-first within each byte, rows are fd_wdwidth WORDS apart
 * -- the VDI's own layout, kept exactly (see the MFDB note in vdi.h).
 */
#define STRIP_HALF  ((uint16_t)(VR_STRIP_LEN / 2))

static void vdi_vrt_cpyfm(void)
{
    MFDB *src = (MFDB *)(uint16_t)contrl[7];   /* forms live in bank $00 */
    WORD mode = intin[0];
    uint8_t fg = (uint8_t)HW(intin[1]), bg = (uint8_t)HW(intin[2]);
    WORD sx1 = ptsin[0], sy1 = ptsin[1], sx2 = ptsin[2], sy2 = ptsin[3];
    WORD dx1 = ptsin[4], dy1 = ptsin[5];
    const uint8_t *bits;
    WORD w, h, wdwidth;
    uint16_t stride;
    WORD cx0, cy0, cx1, cy1, bx0, nb, band, y;
    uint8_t set_a, set_o, clr_a, clr_o, out_a;

    if (!src || !src->fd_addr)
        return;
    order(&sx1, &sx2); order(&sy1, &sy2);
    w = (WORD)(sx2 - sx1 + 1);
    h = (WORD)(sy2 - sy1 + 1);
    if (w <= 0 || h <= 0)
        return;
    bits    = (const uint8_t *)(uint16_t)src->fd_addr;
    /* The field goes through a scalar on purpose.  `src->fd_wdwidth * 2u`
     * here -- src spilled to the stack by the order() calls and dead after
     * this line -- is miscompiled by Calypsi 5.18 into an in-place shift of
     * src's own stack slot: stride became src << 1 and the field was never
     * read, which is what made every row after the first read unrelated
     * memory in Phase 2b (B5 in tools/ccbug/, `make check-cc`). */
    wdwidth = src->fd_wdwidth;                              /* WORDS */
    stride  = (uint16_t)((uint16_t)wdwidth * 2u);

    /* The destination, clipped as plot() clipped: the clip rectangle when
     * one is set, then the screen. */
    cx0 = dx1;  cy0 = dy1;
    cx1 = (WORD)(dx1 + w - 1);  cy1 = (WORD)(dy1 + h - 1);
    if (vwk.clip) {
        if (cx0 < vwk.xmn_clip) cx0 = vwk.xmn_clip;
        if (cy0 < vwk.ymn_clip) cy0 = vwk.ymn_clip;
        if (cx1 > vwk.xmx_clip) cx1 = vwk.xmx_clip;
        if (cy1 > vwk.ymx_clip) cy1 = vwk.ymx_clip;
    }
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > SCR_W - 1) cx1 = SCR_W - 1;
    if (cy1 > SCR_H - 1) cy1 = SCR_H - 1;
    if (cx1 < cx0 || cy1 < cy0)
        return;
    bx0 = (WORD)(cx0 >> 1);
    nb  = (WORD)((cx1 >> 1) - bx0 + 1);

    /* What a set and a clear source bit contribute, per mode. */
    switch (mode) {
    case MD_TRANS: set_a = 0x0; set_o = fg; clr_a = 0xF; clr_o = 0;  break;
    case MD_ERASE: set_a = 0xF; set_o = 0;  clr_a = 0x0; clr_o = bg; break;
    case MD_XOR:   set_a = 0xF; set_o = 0;  clr_a = 0x0; clr_o = 0;  break;
    default:       set_a = 0x0; set_o = fg; clr_a = 0x0; clr_o = bg; break;
    }
    out_a = (mode == MD_XOR) ? 0x0 : 0xF;   /* the strip that is blitted */

    band = (WORD)(STRIP_HALF / (uint16_t)nb);   /* rows a band holds */
    if (band > 256)
        band = 256;
    /* the source row under the first clipped destination row */
    bits += (uint16_t)(sy1 + (cy0 - dy1)) * stride;
    for (y = cy0; y <= cy1; y += band) {
        WORD rows = (WORD)(cy1 - y + 1);
        volatile uint8_t *pa, *po;
        uint32_t dst;
        WORD r;
        uint8_t and_all = 0xFF, or_any = 0, xor_any = 0;

        if (rows > band)
            rows = band;
        if (blit_pending())     /* the last band may still read the strips */
            blit_run();
        pa = vram_win(VR_STRIP);
        po = pa + STRIP_HALF;
        for (r = 0; r < rows; r++, bits += stride) {
            WORD x = (WORD)(bx0 * 2);
            WORD b;
            for (b = 0; b < nb; b++, x += 2) {
                uint8_t ab, ob, an, on;
                WORD sx;
                /* the even pixel: high nibble */
                if (x < cx0) {
                    an = out_a; on = 0;
                } else {
                    sx = (WORD)(sx1 + (x - dx1));
                    if (bits[(UWORD)sx >> 3] & (uint8_t)(0x80 >> (sx & 7))) {
                        an = set_a; on = set_o;
                    } else {
                        an = clr_a; on = clr_o;
                    }
                }
                ab = (uint8_t)(an << 4);
                ob = (uint8_t)(on << 4);
                /* the odd pixel: low nibble */
                if (x + 1 > cx1) {
                    an = out_a; on = 0;
                } else {
                    sx = (WORD)(sx1 + (x + 1 - dx1));
                    if (bits[(UWORD)sx >> 3] & (uint8_t)(0x80 >> (sx & 7))) {
                        an = set_a; on = set_o;
                    } else {
                        an = clr_a; on = clr_o;
                    }
                }
                ab |= an;
                ob |= on;
                pa[b] = ab;
                and_all &= ab;
                xor_any |= ab;
                if (mode != MD_XOR) {
                    po[b] = ob;
                    or_any |= ob;
                }
            }
            pa += nb;
            po += nb;
        }
        dst = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE + (uint32_t)bx0;
        if (mode == MD_XOR) {
            if (xor_any)
                blit_mask(VR_STRIP, (uint16_t)nb, dst, SCR_STRIDE,
                          (uint16_t)nb, (uint16_t)rows, 0xFF, 0x00,
                          BLT_MODE_XOR);
        } else {
            if (and_all != 0xFF)
                blit_mask(VR_STRIP, (uint16_t)nb, dst, SCR_STRIDE,
                          (uint16_t)nb, (uint16_t)rows, 0xFF, 0x00,
                          BLT_MODE_AND);
            if (or_any)
                blit_mask(VR_STRIP + STRIP_HALF, (uint16_t)nb, dst, SCR_STRIDE,
                          (uint16_t)nb, (uint16_t)rows, 0xFF, 0x00,
                          BLT_MODE_OR);
        }
        if (blit_pending())
            blit_run();
    }
}

/* vr_trnfm converts between VDI-standard and device-specific form.  This
 * device has exactly one form, so there is nothing to convert. */
static void vdi_vr_trnfm(void) { }

/* ---------------------------------------------------------------------- */
/* input: modes, vectors, keyboard                                        */
/* ---------------------------------------------------------------------- */

/* Input modes, one per logical device.  1 = request (block until input),
 * 2 = sample (return whatever is there now).  The AES uses sample mode
 * throughout, because it drives its own event loop. */
static WORD in_mode[5] = { 0, 2, 2, 2, 2 };   /* [1]=locator .. [4]=string */

static VDI_VEC vec_motv, vec_butv, vec_curv, vec_timv;
static WORD    last_buttons;

/* Vector exchange.  The new handler arrives in contrl[7..8] and the old one is
 * returned in contrl[9..10] -- the VDI's own convention.  Data pointers are 16
 * bits here, but FUNCTION pointers are 24 bits under the large code model (the
 * handlers live in bank $01), so both words carry address: [7] low, [8] high
 * -- the LONG at &contrl[7] in this machine's byte order. */
static VDI_VEC vex(VDI_VEC *slot)
{
    VDI_VEC  old = *slot;
    uint32_t a   = (uint32_t)(uint16_t)contrl[7]
                 | ((uint32_t)(uint16_t)contrl[8] << 16);
    uint32_t o   = (uint32_t)old;

    *slot = (VDI_VEC)a;
    contrl[9]  = (WORD)o;
    contrl[10] = (WORD)(o >> 16);
    return old;
}

static void vdi_vex_butv(void) { vex(&vec_butv); }
static void vdi_vex_motv(void) { vex(&vec_motv); }
static void vdi_vex_curv(void) { vex(&vec_curv); }
static void vdi_vex_timv(void)
{
    vex(&vec_timv);
    intout[0] = 20;                 /* tick length in ms: 50 Hz PAL frame */
    contrl[4] = 1;
}

static void vdi_vsin_mode(void)
{
    WORD dev = intin[0], mode = intin[1];
    if (dev >= 1 && dev <= 4 && (mode == 1 || mode == 2))
        in_mode[dev] = mode;
    intout[0] = 1;
    contrl[4] = 1;
}

static void vdi_vqin_mode(void)
{
    WORD dev = intin[0];
    intout[0] = (dev >= 1 && dev <= 4) ? in_mode[dev] : 2;
    contrl[4] = 1;
}

/* The keyboard is read straight from POKEY, not through the OS.
 *
 * The OS's CH ($02FC) is filled by its keyboard IRQ handler, and gem4xe runs
 * with the CPU's I flag set (src/crt_atari.s), so nothing ever fills it.
 * POKEY itself is enough: with IRQEN bit 6 set it latches a key press in
 * IRQST bit 6 (0 = a key arrived) and holds the raw code in KBCODE until the
 * next one.  The latch is what makes polling reliable -- a key that was
 * pressed and released between two polls is still there to be read -- and it
 * is cleared by writing the bit low then high in IRQEN.  The I flag keeps the
 * CPU from taking the interrupt, so nothing vectors through the unfilled
 * native-mode $FFEE.  (It also matters to the test rig: AltirraSDL's KEY verb
 * queues a key until the keyboard IRQ is enabled and acknowledged.)
 *
 * Raw codes are translated through the OS's own key table, found through
 * KEYDEF ($79): 64 entries each for plain, shift (KBCODE bit 6) and control
 * (bit 7) -- measured on the XL OS, not assumed.  The table yields ATASCII;
 * the few codes GEM gives a scan code to are mapped to their PC-keyboard
 * values (RETURN = $1C0D and so on), which is what the AES switches on. */
#define SKSTAT  (*(volatile uint8_t *)0xD20F)
#define KBCODE  (*(volatile uint8_t *)0xD209)
#define IRQEN   (*(volatile uint8_t *)0xD20E)   /* write */
#define IRQST   (*(volatile uint8_t *)0xD20E)   /* read  */
#define POKMSK  (*(volatile uint8_t *)0x0010)   /* OS shadow of IRQEN */
#define KEYDEF  (*(volatile uint16_t *)0x0079)  /* XL OS: -> 192-byte table */
#define VCOUNT  (*(volatile uint8_t *)0xD40B)

#define KB_QLEN 8
static WORD    kb_q[KB_QLEN];
static uint8_t kb_head, kb_tail;
static uint8_t kb_caps;

static void kb_init(void)
{
    kb_head = kb_tail = 0;
    kb_caps = 0;
    IRQEN  = 0x40;                  /* keyboard IRQ: latch presses in IRQST */
    POKMSK = 0x40;
}

/* ATASCII from the OS table -> GEM key code (scan << 8 | ascii).  0 = nothing
 * to deliver (a modifier-only code, a console key, a function key). */
static WORD kb_translate(uint8_t code)
{
    const uint8_t *tab = (const uint8_t *)KEYDEF;
    WORD idx = (WORD)(code & 0x3F);
    uint8_t a;
    if (code & 0x40) idx += 64;                 /* shift row   */
    if (code & 0x80) idx += 128;                /* control row */
    a = tab[idx];
    /* Caps lock is the OS's, and the OS is not running: keep our own, and
     * apply it the way it does -- unmodified letters only. */
    if ((code & 0xC0) == 0 && kb_caps && a >= 'a' && a <= 'z')
        a = tab[idx + 64];
    switch (a) {
    case 0x9B: return 0x1C0D;                   /* RETURN     */
    case 0x7F: return 0x0F09;                   /* TAB        */
    case 0x7E: return 0x0E08;                   /* BACKSPACE  */
    case 0x1B: return 0x011B;                   /* ESC        */
    case 0x1C: return 0x4800;                   /* arrow up   */
    case 0x1D: return 0x5000;                   /* arrow down */
    case 0x1E: return 0x4B00;                   /* arrow left */
    case 0x1F: return 0x4D00;                   /* arrow right*/
    case 0xFE: return 0x537F;                   /* ctrl-backspace: delete */
    case 0x82: kb_caps ^= 1; return 0;          /* CAPS toggles */
    case 0x83: kb_caps = 1;  return 0;          /* shift-CAPS: on */
    default:   return (a >= 0x80) ? 0 : a;      /* plain ASCII, no scan code */
    }
}

void vdi_key_poll(void)
{
    uint8_t code;
    if (IRQST & 0x40)               /* bit 6 high: nothing since the last ack */
        return;
    code = KBCODE;
    IRQEN = 0x00;                   /* acknowledge: bit low ... */
    IRQEN = 0x40;                   /* ... then high re-arms the latch */
    {
        WORD k = kb_translate(code);
        uint8_t next = (uint8_t)((kb_tail + 1) & (KB_QLEN - 1));
        if (k && next != kb_head) {
            kb_q[kb_tail] = k;
            kb_tail = next;
        }
    }
}

/* vq_key_s -- the AES calls this to learn the modifier state without
 * consuming a keystroke.  Bits: 1 right shift, 2 left shift, 4 control,
 * 8 alt.  POKEY reports the shift key directly (SKSTAT bit 3, live, 0 =
 * held); control is only visible as bit 7 of the code of a key that is still
 * held (SKSTAT bit 2 low).  The Atari's one shift key is reported as left. */
static void vdi_vq_key_s(void)
{
    uint8_t sk = SKSTAT;
    WORD st = 0;
    if ((sk & 0x08) == 0)           /* active low, like the rest of SKSTAT */
        st |= 2;
    if ((sk & 0x04) == 0) {
        uint8_t k = KBCODE;
        if (k & 0x40) st |= 2;
        if (k & 0x80) st |= 4;
    }
    intout[0] = st;
    contrl[4] = 1;
}

/* v_string.  Sample mode returns whatever is buffered right now (possibly
 * nothing); request mode would block, which this driver does not do -- the AES
 * uses sample mode throughout and a blocking VDI has nowhere to yield to.
 *
 * gem4xe's string-device convention: ONE key per call, delivered as a GEM key
 * code (scan code in the high byte, ASCII in the low) in intout[0], so the
 * AES gets the value evnt_keybd hands out without a second translation. */
static void vdi_v_string(void)
{
    vdi_key_poll();
    if (kb_head == kb_tail) {
        contrl[4] = 0;
        return;
    }
    intout[0] = kb_q[kb_head];
    kb_head = (uint8_t)((kb_head + 1) & (KB_QLEN - 1));
    contrl[4] = 1;
}

static void vdi_v_choice(void)
{
    intout[0] = 0;
    contrl[4] = 1;
}

/* vsl_udsty -- the user-defined line pattern, which is line type 7.  The AES
 * uses it for rubber-band and drag outlines. */
static void vdi_vsl_udsty(void)
{
    line_styles[7] = (UWORD)intin[0];
}

/* Escape.  Only sub-opcodes 2 and 3 matter to the AES (leave and enter the
 * alpha cursor when switching between text and graphics); this driver is
 * always graphical, so they are genuine no-ops rather than stubs. */
static void vdi_v_escape(void) { }

/* vst_height.  This driver has one fixed 8x8 font, so the requested height is
 * ignored and the actual cell is reported -- which is exactly what the VDI
 * contract asks for: the caller must use what comes back, not what it asked
 * for.  Reported values match ptsout[] in fill_workout(). */
static void vdi_vst_height(void)
{
    /* "Character height" is the font's TOP -- baseline to top of cell --
     * not the cell height: both the ST ROM's dqt_attributes and EmuTOS
     * return fnt->top here, and the AES adds it to a cell's top edge to get
     * the baseline v_gtext wants (gsx_tblt).  Report the cell in [3]. */
    ptsout[0] = FONT_W;             /* character width  */
    ptsout[1] = FONT_TOP;           /* character height (= top) */
    ptsout[2] = FONT_W;             /* cell width       */
    ptsout[3] = FONT_H;             /* cell height      */
    contrl[2] = 2;                  /* two POINTS */
}

/* vqt_attributes -- the AES asks for the current text settings before drawing
 * a string, rather than tracking them itself. */
static void vdi_vqt_attributes(void)
{
    intout[0] = 1;                  /* font id: the system font */
    intout[1] = vwk.text_color;
    intout[2] = 0;                  /* rotation: none supported */
    intout[3] = 0;                  /* horizontal alignment: left */
    intout[4] = 0;                  /* vertical alignment: baseline */
    intout[5] = (WORD)(vwk.wrt_mode + 1);
    ptsout[0] = FONT_W;
    ptsout[1] = FONT_TOP;           /* as vst_height: the font's top */
    ptsout[2] = FONT_W;
    ptsout[3] = FONT_H;
    contrl[2] = 2;
    contrl[4] = 6;
}

void vdi_save_form(MFDB *m)
{
    m->fd_addr = VR_SAVE;
    m->fd_w = SCR_W;
    m->fd_h = SCR_H;
    m->fd_wdwidth = SCR_W / 16;
    m->fd_stand = 0;
    m->fd_nplanes = 4;
    m->fd_r1 = m->fd_r2 = m->fd_r3 = 0;
}

/* One pass of the input machinery: everything a VBI would do, done from the
 * caller's loop instead.  The timer vector fires once per FRAME, detected as
 * ANTIC's line counter wrapping (VCOUNT counts 0..155 on PAL), so it means
 * 20 ms whether the loop runs once a frame or a hundred times.  A pass that
 * takes longer than a frame loses a tick; a VBI will fix that, not this. */
void vdi_input_poll(void)
{
    static uint8_t last_vcount;
    uint8_t vc = VCOUNT;

    ptr_poll();
    ptr_sample();                   /* one instant for the whole pass */
    vdi_key_poll();
    if (vec_curv)
        vec_curv();
    else
        vdi_cursor_move();
    if (vec_motv)
        vec_motv();
    if (ptr_seen.buttons != last_buttons) {
        last_buttons = ptr_seen.buttons;
        if (vec_butv)
            vec_butv();
    }
    if (vc < last_vcount && vec_timv)
        vec_timv();
    last_vcount = vc;
}

/* ---------------------------------------------------------------------- */
/* dispatch                                                               */
/* ---------------------------------------------------------------------- */

typedef void (*VDI_OP)(void);

/* Two flat tables, split exactly as DRI split them: 1..39 and 100..137. */
static const VDI_OP jmptb1[] = {
    vdi_v_opnwk,     /*  1 */  v_nop,           /*  2 v_clswk        */
    vdi_v_clrwk,     /*  3 */  v_nop,           /*  4 v_updwk  (nop) */
    vdi_v_escape,    /*  5 */                   vdi_v_pline,     /*  6 */
    v_nop,           /*  7 v_pmarker */         vdi_v_gtext,     /*  8 */
    v_nop,           /*  9 v_fillarea */        v_nop,           /* 10 cellarray (nop) */
    v_nop,           /* 11 v_gdp */             vdi_vst_height,  /* 12 */
    v_nop,           /* 13 vst_rotation */      v_nop,           /* 14 vs_color */
    vdi_vsl_type,    /* 15 */                   vdi_vsl_width,   /* 16 */
    vdi_vsl_color,   /* 17 */                   v_nop,           /* 18 vsm_type */
    v_nop,           /* 19 vsm_height */        v_nop,           /* 20 vsm_color */
    v_nop,           /* 21 vst_font */          vdi_vst_color,   /* 22 */
    vdi_vsf_interior,/* 23 */                   vdi_vsf_style,   /* 24 */
    vdi_vsf_color,   /* 25 */                   v_nop,           /* 26 vq_color */
    v_nop,           /* 27 vq_cellarray (nop)*/ vdi_v_locator,   /* 28 */
    v_nop,           /* 29 valuator (nop) */    vdi_v_choice,    /* 30 */
    vdi_v_string,    /* 31 */                   vdi_vswr_mode,   /* 32 */
    vdi_vsin_mode,   /* 33 */                   v_nop,           /* 34 (does not exist) */
    v_nop,           /* 35 vql_attributes */    v_nop,           /* 36 vqm_attributes */
    v_nop,           /* 37 vqf_attributes */    vdi_vqt_attributes, /* 38 */
    v_nop            /* 39 vst_alignment */
};

static const VDI_OP jmptb2[] = {
    vdi_v_opnwk,     /* 100 v_opnvwk -- same device, one workstation */
    v_nop,           /* 101 v_clsvwk (nop, as in DRI's own driver) */
    vdi_vq_extnd,    /* 102 */
    v_nop,           /* 103 v_contourfill (nop) */
    v_nop,           /* 104 vsf_perimeter */
    v_nop,           /* 105 v_get_pixel (nop) */
    v_nop,           /* 106 vst_effects */
    v_nop,           /* 107 vst_point */
    v_nop,           /* 108 vsl_ends */
    vdi_vro_cpyfm,   /* 109 */
    vdi_vr_trnfm,    /* 110 */
    vdi_vsc_form,    /* 111 */
    vdi_vsf_udpat,   /* 112 */
    vdi_vsl_udsty,   /* 113 */
    vdi_vr_recfl,    /* 114 */
    vdi_vqin_mode,   /* 115 */
    v_nop,           /* 116 vqt_extent */
    v_nop,           /* 117 vqt_width */
    vdi_vex_timv,    /* 118 */
    v_nop,           /* 119 vst_load_fonts */
    v_nop,           /* 120 vst_unload_fonts */
    vdi_vrt_cpyfm,   /* 121 */
    vdi_v_show_c,    /* 122 */
    vdi_v_hide_c,    /* 123 */
    vdi_vq_mouse,    /* 124 */
    vdi_vex_butv,    /* 125 */
    vdi_vex_motv,    /* 126 */
    vdi_vex_curv,    /* 127 */
    vdi_vq_key_s,    /* 128 */
    vdi_vs_clip      /* 129 */
};

#define N1 ((WORD)(sizeof jmptb1 / sizeof jmptb1[0]))
#define N2 ((WORD)(sizeof jmptb2 / sizeof jmptb2[0]))

void vdi(void)
{
    WORD op = contrl[0];
    contrl[2] = 0;                  /* no points out unless a handler says so */
    contrl[4] = 0;                  /* no ints out   ditto */
    if (op >= 1 && op < 1 + N1)
        jmptb1[op - 1]();
    else if (op >= 100 && op < 100 + N2)
        jmptb2[op - 100]();
}

void vdi_init(void)
{
    WORD i;
    kb_init();
    /* The work_in the AES opens with: everything 1 -- style 1, colour 1
     * (black), solid fill -- and raster coordinates. */
    for (i = 0; i < 10; i++)
        intin[i] = 1;
    intin[10] = 2;
    contrl[0] = V_OPNWK;
    contrl[1] = 0;
    contrl[3] = 11;
    vdi();
}
