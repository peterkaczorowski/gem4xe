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
#include "font.h"
#include "sys/zwin.h"
#include "../vbxe/vbxe.h"
#include "../sys/irq.h"
#include "../sys/zwin.h"
#include "../sys/farmem.h"

WORD contrl[CONTRL_SIZE];
WORD intin[INTIN_SIZE];
WORD ptsin[PTSIN_SIZE];
WORD intout[INTOUT_SIZE];
WORD ptsout[PTSOUT_SIZE];
Vwk  vwk;

/* Standard VDI line styles 1..6; 7 is the workstation's own (vwk.ud_ls). */
static const UWORD line_styles[7] = {
    0xFFFF, 0xFFFF, 0xFFF0, 0xE0E0, 0xFF18, 0xFF00, 0xF191
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

/* And back: the VDI pen that maps to a hardware index.  A search, not a
 * second table -- it is wanted once per v_get_pixel and nowhere hot. */
static WORD rev_col(WORD hw)
{
    WORD i;
    for (i = 0; i < 16; i++)
        if (map_col[i] == (uint8_t)hw)
            return i;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* helpers                                                                */
/* ---------------------------------------------------------------------- */

/* An arithmetic right shift, by hand.  cc65816 5.18 compiles a signed
 * 16-bit >> to a LOGICAL shift and, by more than one bit, to a logical
 * shift plus a sign extension from the wrong bit -- (900 >> 3) is 0 and
 * (900 >> 4) is -8 (tools/ccbug B12, proved in the simulator).  Shifting
 * an unsigned copy is right, so the sign is handled here: floor division
 * by a power of two, which is what a shift is supposed to be. */
static WORD asr(WORD v, WORD n)
{
    if (v >= 0)
        return (WORD)((UWORD)v >> n);
    return (WORD)(-(WORD)(((UWORD)(-v) + (UWORD)((1u << n) - 1u)) >> n));
}

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
 * untouched, and why the AES can draw an opaque dialog with hollow boxes.
 * Neither carries a table: patmsk is 0, so every row is the constant
 * pat_bits() returns for it.
 *
 * st_fl_ptr: resolve the interior and index to WHICH pattern rows are the
 * current ones -- the donor's name, and the donor's table split: dithers
 * then OEM patterns, coarse then fine hatches. */
static void st_fl_ptr(void)
{
    WORD fi = vwk.fill_index;

    vwk.patidx = 0;
    switch (vwk.fill_style) {
    case FIS_SOLID:
        vwk.patsrc = PAT_SOLID;                  vwk.patmsk = 0;   break;
    case FIS_PATTERN:
        if (fi < 8) { vwk.patsrc = PAT_DITHER; vwk.patidx = (WORD)(fi * 4);
                      vwk.patmsk = 3; }
        else        { vwk.patsrc = PAT_OEM;    vwk.patidx = (WORD)((fi - 8) * 8);
                      vwk.patmsk = 7; }
        break;
    case FIS_HATCH:
        if (fi < 6) { vwk.patsrc = PAT_HATCH0; vwk.patidx = (WORD)(fi * 8);
                      vwk.patmsk = 7; }
        else        { vwk.patsrc = PAT_HATCH1; vwk.patidx = (WORD)((fi - 6) * 16);
                      vwk.patmsk = 15; }
        break;
    case FIS_USER:                              /* the caller's own, near */
        vwk.patsrc = PAT_USER;                   vwk.patmsk = 15;  break;
    default:
        vwk.patsrc = PAT_HOLLOW;                 vwk.patmsk = 0;   break;
    }
}

/* One row of the current fill pattern.  `r` is a y coordinate; patmsk
 * turns it into a row within the pattern, and patidx says where that
 * pattern begins in its table.  The standard tables are far and the
 * user's is in the workstation, which is the whole reason the source is
 * a number rather than a pointer. */
static UWORD pat_bits(WORD r)
{
    WORD i = (WORD)(r & vwk.patmsk);

    switch (vwk.patsrc) {
    case PAT_SOLID:  return 0xFFFF;
    case PAT_DITHER: return fill_dither[vwk.patidx + i];
    case PAT_OEM:    return fill_oem[vwk.patidx + i];
    case PAT_HATCH0: return fill_hatch0[vwk.patidx + i];
    case PAT_HATCH1: return fill_hatch1[vwk.patidx + i];
    case PAT_USER:   return vwk.ud_patrn[i];
    default:         return 0x0000;             /* hollow */
    }
}

/* The pattern the blitter reads lives in VRAM at VR_PATT: 16 rows, each one
 * 16-pixel repeat of the pattern expanded to nibbles -- `set` where the bit
 * is one, `clr` where it is zero -- and written twice over, so that a blit
 * may begin at any byte of the repeat and read eight bytes before the
 * pattern counter sends it back to that byte.  Expanding costs 256 writes
 * through the MEMAC window, so what is there is remembered and reused until
 * the pattern or the nibbles change. */
static WORD    pe_src, pe_idx, pe_msk;
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

    if (pe_valid && pe_src == vwk.patsrc && pe_idx == vwk.patidx &&
        pe_msk == vwk.patmsk && pe_set == set && pe_clr == clr)
        return;
    w = vram_win(VR_PATT);
    for (r = 0; r < VR_PATT_ROWS; r++) {
        patt_row(w, pat_bits(r), set, clr);
        w += VR_PATT_STRIDE;
    }
    pe_src = vwk.patsrc;  pe_idx = vwk.patidx;  pe_msk = vwk.patmsk;
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
    if (vwk.patsrc == PAT_SOLID) {
        paint_rect(x1, y1, x2, y2, pen);
        return;
    }
    if (vwk.patsrc == PAT_HOLLOW) {
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
            const uint8_t __far *sr =
                (const uint8_t __far *)(vdi_font + (uint32_t)row * FONT_STRIDE);
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
            const uint8_t __far *sr =
                (const uint8_t __far *)(vdi_font + (uint32_t)row * FONT_STRIDE);
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

/* ---------------------------------------------------------------------- */
/* 1bpp rasters: icons and cut glyphs                                     */
/* ---------------------------------------------------------------------- */

/* Expand a rectangle of a ONE-PLANE form into the screen in writing mode
 * `mode` (MD_*), with HARDWARE pens `fg` and `bg`.  This is vrt_cpyfm, and
 * v_gtext's fallback for a glyph the clip cuts or a mode the glyph blits do
 * not do.
 *
 * The source lives in RAM, not VRAM, so the blitter cannot read it; but it
 * can do the writing.  The CPU expands the clipped destination rectangle
 * into one 4bpp strip at VR_STRIP through one MEMAC mapping, and one blit
 * applies it; the strip holds most of a page, so a wide form goes in bands.
 *
 * The rules per mode, pixel by pixel, are the VDI's and tools/vdiref.py's:
 *   replace      fg where set, bg where clear
 *   transparent  fg where set
 *   XOR          complement where set
 *   erase        bg where CLEAR
 * and a pixel outside the clip rectangle or the screen is left alone.
 * Clipping is to the pixel.
 *
 * Which blit applies the strip depends on the pens.  Blitter mode 6, the
 * nibble stencil, writes each non-zero nibble of its source and leaves the
 * pixel under a zero one alone: so a strip holding the pen wherever a pixel
 * is written and 0 elsewhere -- clipped pixels included -- is the whole
 * raster in one blit, as long as no pen written is hardware 0, white, the
 * one nibble mode 6 cannot write.  A raster that writes only white is one
 * AND strip, 0 where written and $F elsewhere; XOR is one XOR strip, $F
 * where set.  A replace with white in it is a plain copy of the strip when
 * every strip byte lies wholly inside the clip; when the first or the last
 * does not, the strip is ORed in under an AND blit whose source is ONE row
 * -- $00 inside, $F over the pixel outside, the same on every row -- that
 * the blitter reads at a source step of zero.  The second strip of earlier
 * versions, a band of AND bytes for every raster, is gone: half the
 * slow-bus stores, and one control block instead of two.
 *
 * Eight source pixels -- one byte -- at a time: each nibble indexes a
 * 16-entry table of strip WORDS, the two strip bytes for four pixels, so a
 * source byte becomes four strip bytes in two indexed loads and stores.
 * Only a strip byte with a pixel outside the clip -- the first and the
 * last of a row -- and up to three bytes of remainder are built pixel by
 * pixel.  The tables are rebuilt only when the mode or a pen changes.  The
 * expander's state is in the direct page: the 65816 reaches it in two-byte
 * instructions and indexes through it, where a stack local costs a
 * three-byte one and cannot be indexed.  The first version did every pixel
 * on its own, ~120 instructions each; the second went four pixels at a time
 * through a shift register, ~80 per four; the third built two strips
 * (docs/phase8c.md).
 *
 * Source bits are MSB-first within each byte, rows `stride` bytes apart --
 * the VDI's own layout, kept exactly (see the MFDB note in vdi.h).  The form
 * is in bank $00: `bits` is a near pointer. */
#define R1_ROW   ((uint16_t)(VR_STRIP_LEN - 512))   /* the AND row's place */
                                        /* in the page; the strip is below  */
#define R1_TWO   0xFF                   /* r1_kind: the AND row and an OR   */

static uint8_t r1_pv[4];                /* two pixels (even<<1|odd) -> byte */
static UWORD   r1_v16[16];              /* four pixels -> two strip bytes,  */
                                        /* the first in the low byte        */
static uint8_t r1_out;                  /* the nibble of a pixel left alone */
static uint8_t r1_kind;                 /* BLT_MODE_* of the blit, or R1_TWO */
static UWORD   r1_sig = 0xFFFF;         /* what the tables were built for   */
static const uint8_t bit_of[8] = {0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1};

/* the row expander's state, in the direct page */
static const uint8_t    * __attribute__((tiny)) r1_s;   /* next source byte */
static volatile UWORD   * __attribute__((tiny)) r1_w;   /* next strip word  */
static __attribute__((tiny)) UWORD r1_n;                /* bytes to go      */
static __attribute__((tiny)) UWORD r1_b;                /* the source byte  */
static __attribute__((tiny)) UWORD r1_rsh;              /* 8 - shift        */
static __attribute__((tiny)) UWORD r1_hi, r1_lo;        /* the two nibbles' */
                                                        /* table offsets    */
/* A strip word from the table, by byte offset: one indexed load, the offset
 * held in the direct page rather than derived at each use. */
#define T16(t, off)  (*(const UWORD *)((const uint8_t *)(t) + (off)))

/* Build the table for a mode and a pair of pens, unless it stands, and
 * choose the blit.  `edges` says a strip byte has a pixel outside the
 * clip, which only a replace with white in it cares about. */
static void r1_tables(WORD mode, uint8_t fg, uint8_t bg, WORD edges)
{
    UWORD sig = (UWORD)(((UWORD)mode << 9) | ((UWORD)edges << 8) |
                        ((UWORD)fg << 4) | bg);
    uint8_t set_v, clr_v;
    WORD i;

    if (sig == r1_sig)
        return;
    r1_sig = sig;
    /* What a set and a clear source bit put in the strip. */
    switch (mode) {
    case MD_TRANS:
        if (fg) { set_v = fg;  clr_v = 0x0; r1_kind = BLT_MODE_HR;  }
        else    { set_v = 0x0; clr_v = 0xF; r1_kind = BLT_MODE_AND; }
        break;
    case MD_ERASE:
        if (bg) { set_v = 0x0; clr_v = bg;  r1_kind = BLT_MODE_HR;  }
        else    { set_v = 0xF; clr_v = 0x0; r1_kind = BLT_MODE_AND; }
        break;
    case MD_XOR:
        set_v = 0xF;  clr_v = 0x0;  r1_kind = BLT_MODE_XOR;
        break;
    default:                                            /* replace */
        set_v = fg;  clr_v = bg;
        r1_kind = (uint8_t)((fg && bg)   ? BLT_MODE_HR
                          : (!fg && !bg) ? BLT_MODE_AND
                          : edges        ? R1_TWO
                          :                BLT_MODE_COPY);
        break;
    }
    r1_out = (uint8_t)(r1_kind == BLT_MODE_AND ? 0xF : 0x0);
    for (i = 0; i < 4; i++)
        r1_pv[i] = (uint8_t)((((i & 2) ? set_v : clr_v) << 4) |
                             ((i & 1) ? set_v : clr_v));
    for (i = 0; i < 16; i++)
        r1_v16[i] = (UWORD)(r1_pv[i >> 2] | ((UWORD)r1_pv[i & 3] << 8));
}

/* r1_n source bytes that lie on the strip's byte boundaries: four strip
 * bytes each. */
static void expand_aligned(void)
{
    do {
        r1_b = *r1_s++;
        r1_hi = (UWORD)((r1_b >> 4) << 1);
        r1_lo = (UWORD)((r1_b & 15) << 1);
        r1_w[0] = T16(r1_v16, r1_hi);
        r1_w[1] = T16(r1_v16, r1_lo);
        r1_w += 2;
    } while (--r1_n);
}

/* The same, with the source r1_rsh bits to the right of the strip's byte
 * boundaries: each output byte straddles two source bytes.  The last one
 * reads one byte past the row it needs, inside the form or just after it:
 * bank $00 RAM, and only the bits above the boundary are used. */
static void expand_shifted(void)
{
    do {
        r1_b = (UWORD)((((UWORD)r1_s[0] << 8) | r1_s[1]) >> r1_rsh) & 0xFF;
        r1_s++;
        r1_hi = (UWORD)((r1_b >> 4) << 1);
        r1_lo = (UWORD)((r1_b & 15) << 1);
        r1_w[0] = T16(r1_v16, r1_hi);
        r1_w[1] = T16(r1_v16, r1_lo);
        r1_w += 2;
    } while (--r1_n);
}

/* One strip byte for a pixel pair, `i` its bits (even<<1|odd), with the
 * pixel outside the clip -- if there is one -- left alone. */
static void edge_byte(WORD i, WORD in_even, WORD in_odd, volatile uint8_t *pv)
{
    uint8_t vb = r1_pv[i];
    if (!in_even) vb = (uint8_t)((vb & 0x0F) | (r1_out << 4));
    if (!in_odd)  vb = (uint8_t)((vb & 0xF0) | r1_out);
    *pv = vb;
}

/* source pixel p of a row, as 0 or 1 */
#define SRC_BIT(row, p) (((row)[(UWORD)(p) >> 3] & bit_of[(p) & 7]) ? 1 : 0)

static void raster_1bpp(const uint8_t *bits, uint16_t stride,
                        WORD sx1, WORD sy1, WORD w, WORD h,
                        WORD dx1, WORD dy1, WORD mode, uint8_t fg, uint8_t bg)
{
    WORD cx0, cy0, cx1, cy1, bx0, nb, band, y;
    WORD first_part, last_part, n8, rem, sxf, sh, px_first, px_rem, px_last;

    if (w <= 0 || h <= 0)
        return;
    /* The destination, clipped to the pixel: the clip rectangle when one is
     * set, then the screen. */
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

    /* A strip byte with a pixel outside the clip is built on its own: the
     * first when the clip starts at odd x, the last when it ends at even x.
     * The bytes between go a source byte -- four strip bytes -- at a time,
     * and up to three remain: a pair of strip bytes from the high nibble of
     * the next source byte, then a single one built from its pixels. */
    first_part = (WORD)(cx0 & 1);
    last_part  = (WORD)(!(cx1 & 1));
    {
        WORD nf = (WORD)(nb - first_part - last_part);
        n8  = (WORD)((UWORD)nf >> 2);   /* nf >= 0; and see asr() */
        rem = (WORD)(nf & 3);
        /* source x under the first whole byte's even pixel, and under the
         * even pixel of each byte built on its own */
        sxf = (WORD)(sx1 + (bx0 * 2 + first_part * 2 - dx1));
        sh  = (WORD)(sxf & 7);
        px_first = (WORD)(sxf - 1);                     /* its odd pixel   */
        px_rem   = (WORD)(sxf + n8 * 8 + ((rem & 2) ? 4 : 0));
        px_last  = (WORD)(sx1 + (cx1 - dx1));
    }
    r1_rsh = (UWORD)(8 - sh);
    r1_tables(mode, fg, bg, (WORD)(first_part | last_part));

    band = (WORD)(R1_ROW / (uint16_t)nb);       /* rows a band holds */
    if (band > 256)
        band = 256;
    if (blit_pending())     /* the last raster may still read the strip */
        blit_run();
    if (r1_kind == R1_TWO) {
        /* the AND row: everything inside cleared, the outside pixel kept */
        volatile uint8_t *pa = vram_win(VR_STRIP) + R1_ROW;
        WORD k;
        for (k = 0; k < nb; k++)
            pa[k] = 0x00;
        if (first_part) pa[0] = 0xF0;
        if (last_part)  pa[nb - 1] |= 0x0F;
    }
    /* the source row under the first clipped destination row */
    bits += (uint16_t)(sy1 + (cy0 - dy1)) * stride;
    for (y = cy0; y <= cy1; y += band) {
        WORD rows = (WORD)(cy1 - y + 1);
        volatile uint8_t *pv;
        uint32_t dst;
        WORD r;

        if (rows > band)
            rows = band;
        if (blit_pending())     /* the last band may still read the strip */
            blit_run();
        pv = vram_win(VR_STRIP);
        for (r = 0; r < rows; r++, bits += stride) {
            if (first_part) {
                edge_byte(SRC_BIT(bits, px_first), 0, 1, pv);
                pv++;
            }
            if (n8) {
                r1_s = bits + ((UWORD)sxf >> 3);
                r1_w = (volatile UWORD *)pv;
                r1_n = (UWORD)n8;
                if (sh)
                    expand_shifted();
                else
                    expand_aligned();
                pv += n8 * 4;
            }
            if (rem & 2) {
                WORD p = (WORD)(sxf + n8 * 8);
                WORD i = (WORD)((SRC_BIT(bits, p) << 3) |
                                (SRC_BIT(bits, p + 1) << 2) |
                                (SRC_BIT(bits, p + 2) << 1) |
                                 SRC_BIT(bits, p + 3));
                *(volatile UWORD *)pv = r1_v16[i];
                pv += 2;
            }
            if (rem & 1) {
                edge_byte((WORD)((SRC_BIT(bits, px_rem) << 1) |
                                  SRC_BIT(bits, px_rem + 1)), 1, 1, pv);
                pv++;
            }
            if (last_part) {
                edge_byte((WORD)(SRC_BIT(bits, px_last) << 1), 1, 0, pv);
                pv++;
            }
        }
        dst = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE + (uint32_t)bx0;
        if (r1_kind == R1_TWO) {
            blit_mask(VR_STRIP + R1_ROW, 0, dst, SCR_STRIDE,
                      (uint16_t)nb, (uint16_t)rows, 0xFF, 0x00,
                      BLT_MODE_AND);
            blit_mask(VR_STRIP, (uint16_t)nb, dst, SCR_STRIDE,
                      (uint16_t)nb, (uint16_t)rows, 0xFF, 0x00,
                      BLT_MODE_OR);
        } else {
            blit_mask(VR_STRIP, (uint16_t)nb, dst, SCR_STRIDE,
                      (uint16_t)nb, (uint16_t)rows, 0xFF, 0x00, r1_kind);
        }
        if (blit_pending())
            blit_run();
    }
}

/* A glyph the clipping rectangle or the screen cuts, or a writing mode the
 * two-blit path does not do (XOR, erase): its eight rows go through the same
 * 1bpp raster path as an icon.  Erase paints the text colour where the glyph
 * is CLEAR, so that is the "background" it is given. */
static void draw_glyph_cpu(WORD ch, WORD cx, WORD cy)
{
    uint8_t g[FONT_H];
    WORD row, mode = (WORD)(vwk.wrt_mode + 1);
    uint8_t ink = (uint8_t)HW(vwk.text_color);
    for (row = 0; row < FONT_H; row++)
        g[row] = *(const uint8_t __far *)
                  (vdi_font + (uint32_t)row * FONT_STRIDE + (ch & 0xFF));
    raster_1bpp(g, 1, 0, 0, FONT_W, FONT_H, cx, cy, mode, ink,
                (uint8_t)(mode == MD_ERASE ? ink : HW(0)));
}

/* v_gtext.  Alignment is left/baseline (vst_alignment is not implemented, so
 * only the default applies): the y given is the BASELINE, and the cell top is
 * y - FONT_TOP.
 *
 * Replace mode paints the cell background first; transparent mode leaves it.
 * GEM has no separate text background colour -- replace mode uses pen 0.
 * XOR and erase modes go pixel by pixel; the AES draws text in replace and
 * transparent mode only, so those two are the ones the blitter serves. */
/* Where the point v_gtext is given puts the cell's top-left corner, given
 * the alignment vst_alignment set.  The horizontal is the string's width,
 * the vertical the font's own metrics: the ST's numbering, and EmuTOS's
 * gsx_tblt does the same arithmetic. */
static WORD align_x(WORD x, WORD w)
{
    switch (vwk.h_align) {
    case TA_CENTRE: return (WORD)(x - w / 2);
    case TA_RIGHT:  return (WORD)(x - w);
    default:        return x;
    }
}

static WORD align_y(WORD y)
{
    switch (vwk.v_align) {
    case TA_HALF:    return (WORD)(y - FONT_HALF);
    case TA_ASCENT:  return (WORD)(y - FONT_ASCENT);
    case TA_BOTTOM:  return (WORD)(y - FONT_H + 1);
    case TA_DESCENT: return (WORD)(y - FONT_TOP - FONT_DESCENT);
    case TA_TOP:     return y;
    default:         return (WORD)(y - FONT_TOP);   /* the baseline */
    }
}

/* One glyph at a cell's top-left corner, in the current writing mode,
 * thickened if vst_effects asked for it.  The whole cell has to be
 * inside the screen AND the clip for the blitter to serve it; anything
 * else goes pixel by pixel through the window.  Shared by v_gtext and by
 * v_justified, which places its cells one at a time. */
static void draw_char(WORD ch, WORD cx, WORD cy)
{
    WORD mode = (WORD)(vwk.wrt_mode + 1);
    WORD blittable = (mode == MD_REPLACE || mode == MD_TRANS);
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
        draw_glyph(ch, cx, cy, HW(vwk.text_color),
                   (WORD)(mode == MD_REPLACE));
        blit_run();
    } else {
        draw_glyph_cpu(ch, cx, cy);
    }
    /* Thickened: the same glyph again, one pixel right, drawn
     * transparently so the two overlap into a heavier letter.  It costs a
     * second blit a glyph and nothing else. */
    if ((vwk.text_effects & TXT_THICKEN) && cx + 1 + FONT_W <= SCR_W) {
        WORD bx = (WORD)(cx + 1);
        fits = (cy >= 0 && cy + FONT_H <= SCR_H);
        if (fits && vwk.clip)
            fits = (bx >= vwk.xmn_clip && cy >= vwk.ymn_clip &&
                    bx + FONT_W - 1 <= vwk.xmx_clip &&
                    cy + FONT_H - 1 <= vwk.ymx_clip);
        if (fits && blittable) {
            draw_glyph(ch, bx, cy, HW(vwk.text_color), 0);
            blit_run();
        } else {
            draw_glyph_cpu(ch, bx, cy);
        }
    }
}

/* The row under a string, in the text colour: solid and clipped, because
 * an underline is text and does not take the fill pattern. */
static void underline(WORD x1, WORD x2, WORD cy)
{
    WORD a = x1, b = (WORD)(cy + FONT_H - 1), c = x2, d = b;

    if (clip_rect(&a, &b, &c, &d))
        paint_rect(a, b, c, d, vwk.text_color);
}

static void vdi_v_gtext(void)
{
    WORD n = contrl[3], i;
    WORD x = align_x(ptsin[0], (WORD)(n * FONT_W));
    WORD cy = align_y(ptsin[1]);

    for (i = 0; i < n && i < INTIN_SIZE; i++)
        draw_char(intin[i], (WORD)(x + i * FONT_W), cy);
    if ((vwk.text_effects & TXT_UNDERLINE) && n > 0) {
        WORD w = (WORD)(n * FONT_W + ((vwk.text_effects & TXT_THICKEN) ? 1 : 0));
        underline(x, (WORD)(x + w - 1), cy);
    }
}

/* ---------------------------------------------------------------------- */
/* opcodes                                                                */
/* ---------------------------------------------------------------------- */

static void v_nop(void) { }

/* work_out: intout[0..44] then ptsout[0..11].  Values describe this device --
 * a 640x240, 16-colour, non-scalable raster screen with no GDPs yet. */
/* What each GDP draws with: bar, pie, circle, ellipse and elliptical pie
 * are fill areas (3), justified text is text (2), the arcs and the
 * rounded outline are polylines (0). */
static const WORD gdp_attr[10] = { 3, 0, 3, 3, 3, 0, 3, 0, 3, 2 };

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
    /* The ten GDPs, and for each of them the attribute it draws with:
     * 3 = fill area, 2 = text, 0 = polyline.  A program reads these
     * rather than assuming, which is the whole point of the table. */
    intout[14] = 10;
    for (i = 0; i < 10; i++) {
        intout[15 + i] = (WORD)(i + 1);
        intout[25 + i] = gdp_attr[i];
    }
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
    ptsout[8]  = DEF_MKWD;  ptsout[9]  = DEF_MKHT;   /* min marker */
    ptsout[10] = MAX_MKWD;  ptsout[11] = MAX_MKHT;   /* max marker */
    contrl[2] = 6;
    contrl[4] = 45;
}

/* The palette in HARDWARE order: entry map_col[pen] gets pen's colour. */
/* What vs_color was ASKED for, 0..1000 a channel, so vq_color can answer
 * the request as well as what the hardware made of it.  In the banked
 * window (src/sys/zwin.h) because bank $00's own data is spoken for, and
 * read-mostly is exactly what it is for. */
ZWIN static WORD pal_req[16][3];

/* 0..1000 to the eight bits the palette takes, and back.  VBXE keeps
 * seven significant bits and copies the top one down into the eighth, so
 * what a caller reads back as the ACTUAL colour is not always what it
 * asked for -- tools/vbxeref.py's dac() is the same arithmetic. */
static uint8_t col_to_hw(WORD v)
{
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    return (uint8_t)(((int32_t)v * 255 + 500) / 1000);
}

static WORD hw_to_col(uint8_t h)
{
    uint8_t dac = (uint8_t)((h & 0xFE) | ((h >> 7) & 1));
    return (WORD)(((int32_t)dac * 1000 + 127) / 255);
}

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
    for (pen = 0; pen < 16; pen++) {
        pal_req[pen][0] = hw_to_col(gem_rgb[pen * 3]);
        pal_req[pen][1] = hw_to_col(gem_rgb[pen * 3 + 1]);
        pal_req[pen][2] = hw_to_col(gem_rgb[pen * 3 + 2]);
    }
}

/* vs_color: one entry of the palette, in the VDI's thousandths.  An index
 * the device does not have is ignored, which is what the donor does. */
static void vdi_vs_color(void)
{
    uint8_t rgb[3];
    WORD i = intin[0], k;

    if (i < 0 || i > 15)
        return;
    for (k = 0; k < 3; k++) {
        WORD v = intin[k + 1];
        pal_req[i][k] = (WORD)(v < 0 ? 0 : v > 1000 ? 1000 : v);
        rgb[k] = col_to_hw(v);
    }
    vbxe_palette(1, (uint8_t)HW(i), rgb, 1);
}

/* vq_color: what was asked for (flag 0) or what the hardware made of it
 * (flag 1).  They differ because the DAC keeps seven bits. */
static void vdi_vq_color(void)
{
    WORD i = intin[0], k;

    intout[0] = i;
    if (i < 0 || i > 15) {
        intout[0] = -1;
        intout[1] = intout[2] = intout[3] = 0;
    } else if (intin[1]) {
        for (k = 0; k < 3; k++)
            intout[k + 1] = hw_to_col(col_to_hw(pal_req[i][k]));
    } else {
        for (k = 0; k < 3; k++)
            intout[k + 1] = pal_req[i][k];
    }
    contrl[4] = 4;
}

/* ---- workstations ----------------------------------------------------- */

/* The donor gives every v_opnvwk a Vwk of its own and looks the caller's
 * handle up before each call, so that an application's attributes and
 * clip and the AES's never disturb each other: the AES draws on the
 * physical workstation, an application on the virtual one it opened on
 * it.  Here the drawing code addresses ONE workstation, `vwk`, absolutely
 * (what the small data model makes cheap), so the open ones are kept in
 * a table and the one a call names is copied in when it is not the one
 * already there -- a switch costs two copies of a Vwk and happens where
 * the drawing changes hands, not per call.  Handle 1 is the physical
 * workstation, at [0]; a slot whose handle is 0 is closed.  The current
 * slot's copy in the table is stale until the next switch writes it. */
#define NUM_VWK 4
ZWIN static Vwk vwk_tab[NUM_VWK];
static WORD     vwk_cur;            /* the slot vwk holds */

/* Make handle h's workstation the current one; 0 if it is not open. */
static WORD vwk_select(WORD h)
{
    WORD i = (WORD)(h - 1);
    if (h < 1 || h > NUM_VWK || vwk_tab[i].handle == 0)
        return 0;
    if (i != vwk_cur) {
        vwk_tab[vwk_cur] = vwk;
        vwk = vwk_tab[i];
        vwk_cur = i;
        /* A user pattern lives in the Vwk, so PAT_USER names a
         * different sixteen rows in every workstation that has one, and
         * the cache cannot tell them apart: what it holds may be the
         * other's. */
        if (vwk.patsrc == PAT_USER)
            pe_valid = 0;
    }
    return 1;
}

/* init_wk: a workstation's attributes as work_in asks for them, validated
 * the way the setters validate them, clip off, the rest at their defaults.
 * (The donor stores work_in[8] as the fill index WITHOUT the minus one
 * that vsf_style applies -- so does the original DRI driver -- which
 * selects the pattern after the one asked for and reads past the table at
 * index 24.  Not reproduced.) */
static void init_wk(WORD handle)
{
    uint8_t *z = (uint8_t *)&vwk;
    WORD l, top;

    for (l = 0; l < (WORD)sizeof vwk; l++)
        z[l] = 0;                   /* the user pattern too */
    vwk.handle     = handle;
    vwk.xmx_clip   = SCR_W - 1;
    vwk.ymx_clip   = SCR_H - 1;
    vwk.wrt_mode   = MD_REPLACE - 1;
    vwk.line_width = 1;
    vwk.fill_per   = 1;
    vwk.ud_ls      = 0xFFFF;
    l = intin[1];  vwk.line_index = (l < 1 || l > 7)  ? 1 : l;
    l = intin[2];  vwk.line_color = (l < 0 || l > 15) ? 1 : l;
    l = intin[6];  vwk.text_color = (l < 0 || l > 15) ? 1 : l;
    l = intin[7];  vwk.fill_style = (l < FIS_HOLLOW || l > FIS_USER) ? FIS_HOLLOW : l;
    top = (vwk.fill_style == FIS_PATTERN) ? MAX_FILL_PATTERN : MAX_FILL_HATCH;
    l = intin[8];  vwk.fill_index = (WORD)(((l < 1 || l > top) ? 1 : l) - 1);
    l = intin[9];  vwk.fill_color = (l < 0 || l > 15) ? 1 : l;
    /* work_in[3] and [4] are the marker: the type, and its colour. */
    l = intin[3];
    vwk.mark_index = (WORD)(((l < MIN_MARK_STYLE || l > MAX_MARK_STYLE)
                             ? DEF_MARK_STYLE : l) - 1);
    l = intin[4];  vwk.mark_color = (l < 0 || l > 15) ? 1 : l;
    vwk.mark_height = DEF_MKHT;
    vwk.mark_scale  = 1;
    st_fl_ptr();
}

static void vdi_v_opnwk(void)
{
    WORD i;
    /* The physical workstation, and every virtual one closed with it. */
    for (i = 0; i < NUM_VWK; i++)
        vwk_tab[i].handle = 0;
    vwk_cur = 0;
    init_wk(VDI_PHYS_HANDLE);
    vwk_tab[0] = vwk;
    pe_valid = 0;
    lh_valid = lv_valid = 0;
    /* Opening the device resets the driver, cursor included: the saved
     * block under the pointer belongs to a screen that no longer applies. */
    cur_hide  = 1;
    cur_drawn = 0;
    sv_nb     = 0;
    cursor_expand();                /* strips for the form in force  */
    load_palette();
    fill_workout();
    contrl[6] = vwk.handle;
}

/* v_opnvwk: the first free handle above the physical one, or 0 in
 * contrl[6] when there is none -- the donor's answer.  The device is not
 * touched: the palette, the pointer and the screen are the physical
 * workstation's. */
static void vdi_v_opnvwk(void)
{
    WORD i;
    for (i = 1; i < NUM_VWK; i++)
        if (vwk_tab[i].handle == 0)
            break;
    if (i == NUM_VWK) {
        contrl[6] = 0;
        return;
    }
    vwk_tab[vwk_cur] = vwk;
    vwk_cur = i;
    init_wk((WORD)(i + 1));
    vwk_tab[i] = vwk;
    fill_workout();
    contrl[6] = vwk.handle;
}

/* The physical workstation current again, from a virtual one that is
 * closed: its copy in the table is what the last switch away saved. */
static void vwk_to_phys(void)
{
    vwk_cur = 0;
    vwk = vwk_tab[0];
    if (vwk.patsrc == PAT_USER)
        pe_valid = 0;
}

/* v_clsvwk: the workstation the call names -- the dispatcher made it
 * current -- unless it is the physical one, which only v_clswk closes.
 * The physical workstation is current afterwards, as in the donor. */
static void vdi_v_clsvwk(void)
{
    if (vwk_cur == 0)
        return;
    vwk_tab[vwk_cur].handle = 0;
    vwk_to_phys();
}

/* What a program left open when it ended (src/sys/app.c app_free): the
 * AES draws on the physical workstation, so every virtual one is some
 * program's, and with one program at a time they are all its. */
void vdi_close_virtuals(void)
{
    WORD i;
    for (i = 1; i < NUM_VWK; i++)
        vwk_tab[i].handle = 0;
    if (vwk_cur != 0)
        vwk_to_phys();
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
    intout[2] = TXT_DONE;           /* the text effects really applied */
    intout[3] = 0;                  /* scaling: raster, not scalable */
    intout[4] = 4;                  /* PLANES -- the AES reads this one */
    intout[5] = 1;                  /* lookup table present */
    intout[6] = 1;                  /* performance: rough */
    intout[9]  = 4;                 /* writing modes */
    intout[10] = 2;                 /* highest input mode: request */
    intout[11] = 1;                 /* text alignment: yes */
    intout[14] = PTSIN_SIZE / 2 - 1;/* vertices a fill area may have */
    intout[15] = INTIN_SIZE;        /* words of intin */
    intout[16] = 2;                 /* buttons on the pointing device */
    intout[19] = vwk.clip;          /* clipping, right now */
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

/* A diagonal: Bresenham, one pixel at a time through the MEMAC window, the
 * one primitive the blitter does not accelerate.  The pixel rules are
 * tools/vdiref.py's _paint_pixel: replace writes the pen where the style bit
 * is set and pen 0 where it is clear, transparent the pen where set, XOR the
 * complement where set, erase the pen where clear.  The style's bit 15 is
 * the first pixel and it rotates once per pixel, on and off the screen.
 *
 * Every mode is one read-modify-write, (byte & am) ^ xv, with am and xv
 * chosen by the style bit and the pixel's parity -- four entries -- and a
 * pixel a mode leaves alone is skipped before the window is touched.  The
 * stepper's state is in the direct page (see the raster section).  The
 * first version called paint_pixel(), plot(), plot_visible() and two VRAM
 * accessors per step, ~200 instructions; the second kept everything in
 * stack locals, ~150 (docs/phase8c.md). */
#define WIN ((volatile uint8_t *)MEMAC_WIN_ADDR)   /* the window, fixed  */

static __attribute__((tiny)) WORD  ld_x, ld_y, ld_i;
static __attribute__((tiny)) WORD  ld_dx, ld_dy, ld_ndy, ld_sx, ld_sy, ld_err;
static __attribute__((tiny)) UWORD ld_n;                /* pixels to go        */
static __attribute__((tiny)) UWORD ld_m;                /* the style, rotating */
static __attribute__((tiny)) UWORD ld_cx0, ld_cy0, ld_cw, ld_ch;
static __attribute__((tiny)) UWORD ld_page;             /* the 4K page mapped  */
static __attribute__((tiny)) UWORD ld_rpage;            /* the row's page ...  */
static __attribute__((tiny)) WORD  ld_roff, ld_rstep;   /* ... and offset in it */
static __attribute__((tiny)) UWORD ld_off, ld_pg;
static __attribute__((tiny)) UWORD ld_inside;           /* no pixel needs the */
                                                        /* clip test          */
static __attribute__((tiny)) uint8_t ld_a, ld_v;
static uint8_t ld_am[4], ld_xv[4], ld_skip[4];  /* [style bit << 1 | x & 1] */
                                /* not tiny: cc65816 5.18 dies on an indexed  */
                                /* direct-page array (docs/phase8c.md)        */

static void line_diag(WORD x1, WORD y1, WORD x2, WORD y2, UWORD mask)
{
    WORD cx0 = 0, cy0 = 0, cx1 = SCR_W - 1, cy1 = SCR_H - 1;
    WORD lo, hi, i;
    uint8_t pen = (uint8_t)HW(vwk.line_color), pen0 = (uint8_t)HW(0);

    if (vwk.clip) {
        if (cx0 < vwk.xmn_clip) cx0 = vwk.xmn_clip;
        if (cy0 < vwk.ymn_clip) cy0 = vwk.ymn_clip;
        if (cx1 > vwk.xmx_clip) cx1 = vwk.xmx_clip;
        if (cy1 > vwk.ymx_clip) cy1 = vwk.ymx_clip;
    }
    if (cx1 < cx0 || cy1 < cy0)
        return;
    /* a line whose box misses the clip has no pixel to plot */
    lo = x1; hi = x2; order(&lo, &hi);
    if (hi < cx0 || lo > cx1)
        return;
    lo = y1; hi = y2; order(&lo, &hi);
    if (hi < cy0 || lo > cy1)
        return;
    ld_cx0 = (UWORD)cx0;  ld_cw = (UWORD)(cx1 - cx0);
    ld_cy0 = (UWORD)cy0;  ld_ch = (UWORD)(cy1 - cy0);
    /* both ends inside the clip: so is every pixel between them */
    ld_inside = (UWORD)(x1 >= cx0 && x1 <= cx1 && x2 >= cx0 && x2 <= cx1 &&
                        y1 >= cy0 && y1 <= cy1 && y2 >= cy0 && y2 <= cy1);

    /* the four (style bit, parity) cases: what the byte keeps, what flips */
    for (i = 0; i < 4; i++) {
        WORD set = (WORD)(i & 2), odd = (WORD)(i & 1);
        uint8_t keep = (uint8_t)(odd ? 0xF0 : 0x0F);   /* the other pixel */
        uint8_t am = 0xFF, xv = 0;
        switch (vwk.wrt_mode + 1) {
        case MD_TRANS:
            if (set) { am = keep; xv = pen; }
            break;
        case MD_XOR:
            if (set) xv = 0xF;
            break;
        case MD_ERASE:
            if (!set) { am = keep; xv = pen; }
            break;
        default:
            am = keep; xv = set ? pen : pen0;
            break;
        }
        ld_am[i] = am;
        ld_xv[i] = (uint8_t)(odd ? xv : (xv << 4));
        ld_skip[i] = (uint8_t)(am == 0xFF && xv == 0);
    }

    ld_x = x1;  ld_y = y1;
    ld_dx = (WORD)(x2 - x1); if (ld_dx < 0) ld_dx = (WORD)-ld_dx;
    ld_dy = (WORD)(y2 - y1); if (ld_dy < 0) ld_dy = (WORD)-ld_dy;
    ld_ndy = (WORD)-ld_dy;
    ld_sx = (WORD)(x1 < x2 ? 1 : -1);
    ld_sy = (WORD)(y1 < y2 ? 1 : -1);
    ld_err = (WORD)(ld_dx - ld_dy);
    /* Bresenham steps the major axis every time, so the pixel count is
     * known: no endpoint compare in the loop */
    ld_n = (UWORD)((ld_dx > ld_dy ? ld_dx : ld_dy) + 1);
    ld_m = mask;
    ld_page = 0xFFFF;
    /* The row address as a 4K page and an offset in it, so that a step is
     * 16-bit arithmetic with a carry test.  y1 may be negative here: the
     * page is then negative too, and counts back up onto the screen. */
    {
        int32_t row = (int32_t)VR_SCREEN0 + (int32_t)y1 * (int32_t)SCR_STRIDE;
        ld_rpage = (UWORD)(row >> 12);
        ld_roff  = (WORD)(row & 0x0FFF);
    }
    ld_rstep = (WORD)(ld_sy > 0 ? SCR_STRIDE : -SCR_STRIDE);
    for (;;) {
        WORD e2;
        if (ld_m != 0xFFFF)     /* a solid style rotates into itself */
            ld_m = (UWORD)((ld_m << 1) | (ld_m >> 15));
        ld_i = (WORD)(((ld_m & 1) << 1) | (ld_x & 1));
        if (!ld_skip[ld_i] &&
            (ld_inside || ((UWORD)(ld_x - ld_cx0) <= ld_cw &&
                           (UWORD)(ld_y - ld_cy0) <= ld_ch))) {
            ld_off = (UWORD)(ld_roff + ((UWORD)ld_x >> 1));
            ld_pg  = ld_rpage;
            if (ld_off >= 0x1000) {
                ld_off -= 0x1000;
                ld_pg++;
            }
            if (ld_pg != ld_page) {
                ld_page = ld_pg;
                vram_map_page((uint8_t)ld_pg);
            }
            ld_a = ld_am[ld_i];
            ld_v = ld_xv[ld_i];
            WIN[ld_off] = (uint8_t)((WIN[ld_off] & ld_a) ^ ld_v);
        }
        if (--ld_n == 0)
            break;
        e2 = (WORD)(ld_err << 1);
        if (e2 > ld_ndy) { ld_err = (WORD)(ld_err - ld_dy); ld_x = (WORD)(ld_x + ld_sx); }
        if (e2 <  ld_dx) {
            ld_err = (WORD)(ld_err + ld_dx);
            ld_y = (WORD)(ld_y + ld_sy);
            ld_roff = (WORD)(ld_roff + ld_rstep);
            if ((UWORD)ld_roff >= 0x1000) {      /* crossed a page, either way */
                if (ld_roff < 0) { ld_roff += 0x1000; ld_rpage--; }
                else             { ld_roff -= 0x1000; ld_rpage++; }
            }
        }
    }
}

/* v_pline.  Horizontal and vertical runs go through the blitter as 1-row and
 * 1-column rectangles; that covers essentially every line the AES draws, since
 * it builds boxes out of axis-aligned polylines and never calls the GDPs.
 * Diagonals fall back to Bresenham through the MEMAC window and are slow. */
static void draw_line(WORD x1, WORD y1, WORD x2, WORD y2)
{
    UWORD mask = (vwk.line_index == 7) ? vwk.ud_ls
               : line_styles[(vwk.line_index >= 1 && vwk.line_index <= 6)
                             ? vwk.line_index : 1];

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

    line_diag(x1, y1, x2, y2, mask);
}

static void line_ends(WORD *pt, WORD n);    /* the arrowheads, below */

static void vdi_v_pline(void)
{
    WORD n = contrl[1], i;
    for (i = 0; i + 1 < n; i++)
        draw_line(ptsin[i * 2], ptsin[i * 2 + 1],
                  ptsin[i * 2 + 2], ptsin[i * 2 + 3]);
    if (vwk.line_beg == LE_ARROWED || vwk.line_end == LE_ARROWED)
        line_ends(ptsin, n);
}

/* ---------------------------------------------------------------------- */
/* filled areas                                                           */
/* ---------------------------------------------------------------------- */

/* A polyline over n points held as x,y pairs -- v_pline's loop, but over an
 * array the caller names rather than over ptsin. */
static void polyline_pts(const WORD *pt, WORD n)
{
    WORD i;
    for (i = 0; i + 1 < n; i++)
        draw_line(pt[i * 2], pt[i * 2 + 1], pt[i * 2 + 2], pt[i * 2 + 3]);
}

/* The perimeter of a filled area is SOLID and in the FILL colour, whatever
 * the line attributes say: the donor forces LN_MASK to $FFFF and passes
 * fill_color, which here is a save and a restore. */
static void fill_perimeter(const WORD *pt, WORD n)
{
    WORD sv_color = vwk.line_color, sv_index = vwk.line_index;
    vwk.line_color = vwk.fill_color;
    vwk.line_index = 1;                     /* solid */
    polyline_pts(pt, n);
    vwk.line_color = sv_color;
    vwk.line_index = sv_index;
}

/* clc_flit's edge buffer: the x where each edge crosses one scan line.
 * ZWIN because bank $00's data is spoken for and this is touched only from
 * the driver's own calls. */
#define MAX_INTERSECT 32
ZWIN static WORD fill_buf[MAX_INTERSECT];

/* The donor's bubble sort.  There are almost always exactly two
 * intersections, which is why a bubble sort is the right one. */
static void bub_sort(WORD *buf, WORD count)
{
    WORD i, j;

    for (i = (WORD)(count - 1); i > 0; i--) {
        WORD *p = buf;
        for (j = 0; j < i; j++) {
            WORD v = *p++;
            if (v > *p) {
                *(p - 1) = *p;
                *p = v;
            }
        }
    }
}

/* One scan line of a polygon's interior, clipped the way the donor clips
 * it -- the clip rectangle first, then the screen, which on the donor its
 * device layer applies for it. */
static void fill_span(WORD x1, WORD x2, WORD y)
{
    if (vwk.clip) {
        if (x1 < vwk.xmn_clip) {
            if (x2 < vwk.xmn_clip)
                return;                     /* entirely left of the clip */
            x1 = vwk.xmn_clip;
        }
        if (x2 > vwk.xmx_clip) {
            if (x1 > vwk.xmx_clip)
                return;                     /* entirely right of it */
            x2 = vwk.xmx_clip;
        }
    }
    if (x2 < 0 || x1 > SCR_W - 1)
        return;
    if (x1 < 0) x1 = 0;
    if (x2 > SCR_W - 1) x2 = SCR_W - 1;
    fill_rect(x1, y, x2, y, vwk.fill_color);
}

/* clc_flit -- for each scan line, where every edge crosses it, sorted, and
 * filled in pairs (Sutherland and Hodgman).  The arithmetic is the donor's
 * down to the rounding: the +1 before the >>1 is what puts the boundary
 * pixel INSIDE the fill, which is what TOS does and what a perimeter drawn
 * afterwards then lands on top of.
 *
 * `start` is the bottom scan line and `end` is one ABOVE the top one: the
 * loop stops before it, so a polygon does not paint its own topmost row.
 * That is the donor's rule, not a rounding accident, and a caller that
 * abuts two polygons depends on it. */
static void clc_flit(const WORD *pt, WORD vectors, WORD start, WORD end)
{
    WORD y, i;

    for (y = start; y > end; y--) {
        WORD n = 0;

        for (i = 0; i < vectors; i++) {
            WORD y1 = pt[i * 2 + 1], y2 = pt[i * 2 + 3];
            WORD dy = (WORD)(y2 - y1), dy1, dy2;

            if (!dy)                        /* horizontal: ignored */
                continue;
            dy1 = (WORD)(y - y1);
            dy2 = (WORD)(y - y2);
            /* same sign at both ends means the scan line misses the edge */
            if ((dy1 ^ dy2) >= 0)
                continue;
            if (n >= MAX_INTERSECT)
                break;
            {
                WORD x1 = pt[i * 2], x2 = pt[i * 2 + 2], m;
                int32_t dx = (int32_t)(x2 - x1) * 2;   /* doubled, to round */

                if (dx < 0) {
                    m = (WORD)((int32_t)dy2 * dx / dy);
                    fill_buf[n++] = (WORD)(asr((WORD)(m + 1), 1) + x2);
                } else {
                    m = (WORD)((int32_t)dy1 * dx / dy);
                    fill_buf[n++] = (WORD)(asr((WORD)(m + 1), 1) + x1);
                }
            }
        }
        if (n < 2)
            continue;
        bub_sort(fill_buf, n);
        for (i = 0; i + 1 < n; i += 2)
            fill_span(fill_buf[i], fill_buf[i + 1], y);
    }
}

/* polygon -- the interior, then the perimeter if vsf_perimeter asked for
 * one.  pt must have room for ONE MORE point: the polygon is closed in
 * place, as the donor closes it. */
static void polygon(WORD *pt, WORD n)
{
    WORD i, miny, maxy;

    if (n < 2)
        return;
    miny = maxy = pt[1];
    for (i = 1; i < n; i++) {
        WORD k = pt[i * 2 + 1];
        if (k < miny)
            miny = k;
        else if (k > maxy)
            maxy = k;
    }
    if (vwk.clip) {
        if (maxy < vwk.ymn_clip || miny > vwk.ymx_clip)
            return;                         /* wholly outside the clip */
        if (miny < vwk.ymn_clip)
            miny = (WORD)(vwk.ymn_clip - 1);
        if (maxy > vwk.ymx_clip)
            maxy = vwk.ymx_clip;
    }
    /* and the screen, which the donor leaves to its device layer */
    if (maxy < 0 || miny > SCR_H - 1)
        return;
    if (miny < -1) miny = -1;
    if (maxy > SCR_H - 1) maxy = SCR_H - 1;

    pt[n * 2]     = pt[0];                  /* close it */
    pt[n * 2 + 1] = pt[1];
    clc_flit(pt, n, maxy, miny);
    if (vwk.fill_per)
        fill_perimeter(pt, (WORD)(n + 1));
}

static void vdi_v_fillarea(void)
{
    WORD n = contrl[1];

    if (n > PTSIN_SIZE / 2 - 1)
        n = PTSIN_SIZE / 2 - 1;             /* room to close it in place */
    polygon(ptsin, n);
}

/* ---------------------------------------------------------------------- */
/* markers                                                                */
/* ---------------------------------------------------------------------- */

/* The six GEM markers, in the donor's own encoding: a count of polylines,
 * then for each of them a count of points and that many x,y offsets from
 * the marker's centre, in units the marker scale multiplies. */
static const WORD m_dot[]    = { 1, 2, 0, 0, 0, 0 };
static const WORD m_plus[]   = { 2, 2, 0, -3, 0, 3, 2, -4, 0, 4, 0 };
static const WORD m_star[]   = { 3, 2, 0, -3, 0, 3, 2, 3, 2, -3, -2,
                                 2, 3, -2, -3, 2 };
static const WORD m_square[] = { 1, 5, -4, -3, 4, -3, 4, 3, -4, 3, -4, -3 };
static const WORD m_cross[]  = { 2, 2, -4, -3, 4, 3, 2, -4, 3, 4, -3 };
static const WORD m_dmnd[]   = { 1, 5, -4, 0, 0, -3, 4, 0, 0, 3, -4, 0 };

static const WORD * const markers[6] = {
    m_dot, m_plus, m_star, m_square, m_cross, m_dmnd
};

/* v_pmarker: each point gets the current marker, drawn as the polylines
 * that define it -- solid, in the marker colour, at the marker scale.
 *
 * One departure from the donor: it also sets vwk->clip = 1 and never puts
 * it back, so a program that draws one marker finds clipping switched on
 * for good.  That is a bug in the ROM, not a contract, and gem4xe leaves
 * the workstation's clip state alone. */
static void vdi_v_pmarker(void)
{
    WORD sv_index = vwk.line_index, sv_color = vwk.line_color;
    WORD npts = contrl[1], i, j, k;
    WORD seg[10];

    vwk.line_index = 1;                     /* solid */
    vwk.line_color = vwk.mark_color;

    for (i = 0; i < npts; i++) {
        WORD cx = ptsin[i * 2], cy = ptsin[i * 2 + 1];
        const WORD *m = markers[vwk.mark_index];
        WORD lines = *m++;

        for (j = 0; j < lines; j++) {
            WORD n = *m++;
            for (k = 0; k < n && k < 5; k++) {
                seg[k * 2]     = (WORD)(cx + vwk.mark_scale * *m++);
                seg[k * 2 + 1] = (WORD)(cy + vwk.mark_scale * *m++);
            }
            polyline_pts(seg, n);
        }
    }
    vwk.line_index = sv_index;
    vwk.line_color = sv_color;
}

/* ---------------------------------------------------------------------- */
/* v_contourfill -- the paint bucket                                      */
/* ---------------------------------------------------------------------- */
/* The donor keeps a queue of segments and lets the pixels it has already
 * painted stop the search, which only works for a SOLID fill: a patterned
 * one leaves interior-coloured pixels behind and the search walks back
 * into them.  Here the region is discovered first, into a bitmap in far
 * memory, and painted afterwards -- one bit a pixel, 19,200 bytes of a
 * fourteen-megabyte heap, which is the kind of thing this machine's
 * memory makes free.  It also makes the answer independent of the order
 * the search takes, so the model and the driver need only agree about the
 * REGION and not about the walk.
 *
 * Everything happens a ROW at a time.  The first version asked the
 * hardware for one pixel and the bitmap for one bit at a time, and a
 * screen-sized bucket took 44 seconds: a page mapped and a call made per
 * pixel.  A row of the screen read through one mapping, and a row of the
 * bitmap fetched and put back in one go, is the same algorithm without
 * any of that. */
#define CF_STRIDE (SCR_W / 8)           /* bytes a row of the bitmap */
#define CF_BYTES  (CF_STRIDE * SCR_H)
/* Seeds waiting to be examined, one per RUN rather than one per pixel.
 * 8192 of them is 32 KB of far memory and more than a 640 x 240 region
 * can need in any shape that has been drawn on this screen; a fill that
 * did overflow it would come out incomplete, which is the one way the
 * driver and the model could disagree. */
#define CF_STACK  8192

ZWIN static uint32_t cf_map;            /* the bitmap, far */
ZWIN static uint32_t cf_stack;          /* CF_STACK seeds of two WORDs */
ZWIN static WORD     cf_sp;
ZWIN static WORD     cf_search;         /* the hardware pen the search knows */
ZWIN static WORD     cf_type;           /* 1 = fill while it IS that pen */
ZWIN static WORD     cf_x0, cf_y0, cf_x1, cf_y1;    /* where it may go */
ZWIN static WORD     cf_py, cf_sy;      /* the rows the two buffers hold */
ZWIN static WORD     cf_dirty;          /* the bitmap row has been marked */

/* One row of the screen, through a single mapping of each 4K page it
 * crosses -- the whole point of doing this a row at a time. */
static void cf_read_px(WORD y, uint8_t *px)
{
    uint32_t base = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE;
    WORD i = 0;

    while (i < SCR_STRIDE) {
        volatile uint8_t *w = vram_win(base + (uint32_t)i);
        WORD room = (WORD)(0x1000 - (WORD)((base + (uint32_t)i) & 0x0FFF));
        WORD k = (WORD)(SCR_STRIDE - i), j;

        if (k > room)
            k = room;
        for (j = 0; j < k; j++)
            px[i + j] = w[j];
        i = (WORD)(i + k);
    }
}

static void cf_load_px(WORD y, uint8_t *px)
{
    if (cf_py == y)
        return;
    cf_read_px(y, px);
    cf_py = y;
}

/* And one row of the bitmap, written back when the search moves on. */
static void cf_flush_sn(uint8_t *sn)
{
    if (cf_dirty && cf_sy >= 0)
        far_put(cf_map + (uint32_t)cf_sy * CF_STRIDE, sn, CF_STRIDE);
    cf_dirty = 0;
}

static void cf_load_sn(WORD y, uint8_t *sn)
{
    if (cf_sy == y)
        return;
    cf_flush_sn(sn);
    far_get(sn, cf_map + (uint32_t)y * CF_STRIDE, CF_STRIDE);
    cf_sy = y;
}

/* A pixel is interior if it is inside the area the fill may reach and its
 * colour answers the search the way v_contourfill was asked to.  px holds
 * the row already. */
static WORD cf_inside(WORD x, const uint8_t *px)
{
    uint8_t b;
    WORD hw;

    if (x < cf_x0 || x > cf_x1)
        return 0;
    b = px[(UWORD)x >> 1];              /* unsigned: see asr() */
    hw = (WORD)((x & 1) ? (b & 0x0F) : (b >> 4));
    return (WORD)(cf_type ? (hw == cf_search) : (hw != cf_search));
}

static WORD cf_seen(WORD x, const uint8_t *sn)
{
    uint8_t b = sn[(UWORD)x >> 3];      /* unsigned: see asr() */
    return (WORD)((b >> (7 - (x & 7))) & 1);
}

static void cf_push(WORD x, WORD y)
{
    WORD e[2];

    if (cf_sp >= CF_STACK)
        return;
    e[0] = x;  e[1] = y;
    far_put(cf_stack + (uint32_t)cf_sp * 4, (const uint8_t *)e, 4);
    cf_sp++;
}

/* The rows of one neighbour: every run of it that is interior and not yet
 * taken gets ONE seed, which is what the stack is sized for. */
static void cf_seed_row(WORD y, WORD run0, WORD run1, uint8_t *px, uint8_t *sn)
{
    WORD i;

    if (y < cf_y0 || y > cf_y1)
        return;
    cf_load_px(y, px);
    cf_load_sn(y, sn);
    for (i = run0; i <= run1; i++) {
        if (!cf_inside(i, px) || cf_seen(i, sn))
            continue;
        cf_push(i, y);
        while (i <= run1 && cf_inside(i, px))
            i++;
    }
}

static void vdi_v_contourfill(void)
{
    uint8_t px[SCR_STRIDE];             /* one row of the screen */
    uint8_t sn[CF_STRIDE];              /* one row of the bitmap */
    WORD x = ptsin[0], y = ptsin[1], index = intin[0];
    WORD i, run0, run1;

    cf_x0 = 0;  cf_y0 = 0;  cf_x1 = SCR_W - 1;  cf_y1 = SCR_H - 1;
    if (vwk.clip) {
        if (vwk.xmn_clip > cf_x0) cf_x0 = vwk.xmn_clip;
        if (vwk.ymn_clip > cf_y0) cf_y0 = vwk.ymn_clip;
        if (vwk.xmx_clip < cf_x1) cf_x1 = vwk.xmx_clip;
        if (vwk.ymx_clip < cf_y1) cf_y1 = vwk.ymx_clip;
    }
    if (x < cf_x0 || x > cf_x1 || y < cf_y0 || y > cf_y1)
        return;
    if (!cf_map) {
        cf_map = far_alloc(CF_BYTES);
        cf_stack = far_alloc((uint32_t)CF_STACK * 4);
    }
    if (!cf_map || !cf_stack)
        return;

    cf_py = cf_sy = -1;
    cf_dirty = 0;
    cf_load_px(y, px);
    /* A colour index says "stop where that colour starts"; no index says
     * "spread over the colour the seed is on". */
    if (index >= 0) {
        cf_search = HW(index);
        cf_type = 0;
    } else {
        uint8_t b = px[(UWORD)x >> 1];
        cf_search = (WORD)((x & 1) ? (b & 0x0F) : (b >> 4));
        cf_type = 1;
    }
    {   /* the bitmap starts empty */
        uint32_t a;
        for (i = 0; i < CF_STRIDE; i++)
            sn[i] = 0;
        for (a = 0; a < CF_BYTES; a += CF_STRIDE)
            far_put(cf_map + a, sn, CF_STRIDE);
    }

    cf_sp = 0;
    cf_push(x, y);
    while (cf_sp > 0) {
        WORD e[2], sx, sy;

        cf_sp--;
        far_get((uint8_t *)e, cf_stack + (uint32_t)cf_sp * 4, 4);
        sx = e[0];  sy = e[1];
        cf_load_px(sy, px);
        cf_load_sn(sy, sn);
        if (!cf_inside(sx, px) || cf_seen(sx, sn))
            continue;
        for (run0 = sx; run0 > cf_x0 && cf_inside((WORD)(run0 - 1), px); run0--)
            ;
        for (run1 = sx; run1 < cf_x1 && cf_inside((WORD)(run1 + 1), px); run1++)
            ;
        for (i = run0; i <= run1; i++) {
            UWORD k = (UWORD)i >> 3;    /* unsigned: see asr() */
            uint8_t b = sn[k];
            sn[k] = (uint8_t)(b | (1u << (7 - (i & 7))));
        }
        cf_dirty = 1;
        cf_seed_row((WORD)(sy - 1), run0, run1, px, sn);
        cf_seed_row((WORD)(sy + 1), run0, run1, px, sn);
    }
    cf_flush_sn(sn);

    /* and now the paint: every run of the region, row by row, through the
     * fill pattern and the writing mode like any other filled area */
    cf_sy = -1;
    for (y = cf_y0; y <= cf_y1; y++) {
        cf_load_sn(y, sn);
        x = cf_x0;
        while (x <= cf_x1) {
            if (!cf_seen(x, sn)) {
                x++;
                continue;
            }
            run0 = x;
            while (x <= cf_x1 && cf_seen(x, sn))
                x++;
            fill_rect(run0, y, (WORD)(x - 1), y, vwk.fill_color);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* vsl_ends, and the arrowheads it asks for                               */
/* ---------------------------------------------------------------------- */

/* An integer square root, bit by bit: the arrowhead needs the length of
 * the vector it points along and nothing else in the driver does. */
static UWORD isqrt32(uint32_t v)
{
    uint32_t rem = 0, root = 0;
    WORD i;

    for (i = 0; i < 16; i++) {
        root <<= 1;
        rem = (rem << 2) | (v >> 30);
        v <<= 2;
        if (rem > root) {
            rem -= root + 1;
            root += 2;
        }
    }
    return (UWORD)(root >> 1);
}

/* (m1 * m2 + d/2) / d -- the donor's mul_div_round, which the arrowhead
 * geometry works in thousandths with. */
static WORD mul_div_round(WORD m1, WORD m2, WORD d)
{
    int32_t v = (int32_t)m1 * m2;

    v = (v < 0) ? (v - d / 2) : (v + d / 2);
    return (WORD)(v / d);
}

/* One arrowhead, at the point pt[tip] of a polyline walked in steps of
 * `inc` points.  The head is a filled triangle in the LINE colour, and
 * the line's end point is pulled back to its base so the shaft does not
 * stick through the tip -- which is why this takes the caller's array and
 * edits it, exactly as the donor's draw_arrow does.
 *
 * The tip is an INDEX rather than a pointer into the middle of the array,
 * so that every index here is non-negative.  The donor walks backwards
 * from the last point with a pointer; compiled here, pt[-2] came back as
 * neither point and the arrow at that end pointed off into the distance. */
static void draw_arrow(WORD *pt, WORD count, WORD tip, WORD inc)
{
    WORD len = 8, wid = 4;              /* line_width is 1 on this device */
    WORD dx = 0, dy = 0, i, k, nskip = 0;
    WORD line_len, dxf, dyf, htx, hty, bx, by;
    WORD tx = pt[tip], ty = pt[tip + 1];
    WORD tri[8];
    uint32_t len2 = 0;
    WORD sv_style, sv_color, sv_index, sv_per;

    for (i = 1; i < count; i++) {       /* the first point far enough away */
        /* Through scalars: `pt[tip] - pt[j]` in one expression reads the
         * second element as ZERO (tools/ccbug B13, B1's family), and the
         * arrowhead then points at the origin. */
        WORD j = (WORD)(tip + i * inc * 2), qx = pt[j], qy = pt[j + 1];

        nskip = i;
        dx = (WORD)(tx - qx);
        dy = (WORD)(ty - qy);
        len2 = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);
        if (len2 >= (uint32_t)(len * len))
            break;
    }
    line_len = (WORD)isqrt32(len2);
    if (line_len < len)                 /* too short to carry a head */
        return;

    dxf = mul_div_round(dx, 1000, line_len);
    dyf = mul_div_round(dy, 1000, line_len);
    htx = mul_div_round(len, dxf, 1000);
    hty = mul_div_round(len, dyf, 1000);
    bx  = mul_div_round(wid, (WORD)-dyf, 1000);
    by  = mul_div_round(wid, dxf, 1000);

    tri[0] = (WORD)(tx + bx - htx);  tri[1] = (WORD)(ty + by - hty);
    tri[2] = (WORD)(tx - bx - htx);  tri[3] = (WORD)(ty - by - hty);
    tri[4] = tx;                     tri[5] = ty;

    sv_style = vwk.fill_style;  sv_index = vwk.fill_index;
    sv_color = vwk.fill_color;  sv_per   = vwk.fill_per;
    vwk.fill_style = FIS_SOLID;         /* a head is solid, in the line's
                                         * colour: the donor's s_fa_attr */
    vwk.fill_color = vwk.line_color;
    vwk.fill_per = 0;
    st_fl_ptr();
    polygon(tri, 3);
    vwk.fill_style = sv_style;  vwk.fill_index = sv_index;
    vwk.fill_color = sv_color;  vwk.fill_per = sv_per;
    st_fl_ptr();

    /* Pull the tip back to the head's base, and drag the points the head
     * swallowed along with it -- the ones BETWEEN the tip and the point
     * the direction was taken from, which for a two-point line is none of
     * them.  (The donor walks a pointer from that point towards the tip
     * and stops when it arrives, which is the same thing said in
     * pointers; taking its far point with it would leave the line with
     * nowhere to go.) */
    tx = (WORD)(tx - htx);
    ty = (WORD)(ty - hty);
    pt[tip]     = tx;
    pt[tip + 1] = ty;
    for (k = (WORD)(nskip - 1); k >= 1; k--) {
        WORD j = (WORD)(tip + k * inc * 2);

        pt[j]     = tx;
        pt[j + 1] = ty;
    }
}

/* The ends of a polyline: an arrowed end gets a head, and the head is
 * drawn OVER the line the way the donor draws it -- the shaft is already
 * there, and pulling the end point back only matters to what the caller
 * does with its own array afterwards. */
static void line_ends(WORD *pt, WORD n)
{
    if (n < 2)
        return;
    if (vwk.line_beg == LE_ARROWED)
        draw_arrow(pt, n, 0, 1);
    if (vwk.line_end == LE_ARROWED)
        draw_arrow(pt, n, (WORD)((n - 1) * 2), -1);
}

static void vdi_vsl_ends(void)
{
    WORD b = intin[0], e = intin[1];

    vwk.line_beg = (b < 0 || b > 2) ? 0 : b;
    vwk.line_end = (e < 0 || e > 2) ? 0 : e;
    intout[0] = vwk.line_beg;
    intout[1] = vwk.line_end;
    contrl[4] = 2;
}

/* ---------------------------------------------------------------------- */
/* the graphics device primitives (v_gdp)                                 */
/* ---------------------------------------------------------------------- */
/* Angles are in TENTHS of a degree throughout, anticlockwise from east,
 * which is how the VDI takes them. */
#define HALFPI  900
#define GDP_PI 1800
#define TWOPI  3600

/* Isin/Icos: 0..32767 for an angle 0..900, interpolated between the
 * table's 0.8 degree steps. */
static UWORD Isin(WORD angle)
{
    UWORD i = (UWORD)angle >> 3, rem = (UWORD)(angle & 7);
    UWORD s = vdi_sin_tbl[i];

    if (rem)
        s = (UWORD)(s + (UWORD)(((uint32_t)(UWORD)(vdi_sin_tbl[i + 1] - s)
                                 * rem) >> 3));
    return s;
}

static UWORD Icos(WORD angle)
{
    return Isin((WORD)(HALFPI - angle));
}

/* (a * b + 32768) / 65536, the donor's umul_shift */
static UWORD umul_shift(UWORD a, UWORD b)
{
    return (UWORD)((((uint32_t)a * b) + 32768UL) >> 16);
}

/* Precalculated for the rounded box's five points a corner, scaled to
 * 32767 rather than 65536. */
#define Isin225 12539
#define Isin450 23170
#define Isin675 30273
#define Icos225 Isin675
#define Icos450 Isin450
#define Icos675 Isin225

/* (m1 * m2) / d, truncated toward zero -- the donor's mul_div, which is
 * a 68000 muls/divs pair. */
static WORD mul_div(WORD m1, WORD m2, WORD d)
{
    return (WORD)((int32_t)m1 * m2 / d);
}

/* clc_pts: where an angle lands on the ellipse, in raster coordinates.
 * y grows downward, so the first quadrant's y offset is negative -- which
 * is what the Y_NEGATIVE default says. */
#define X_NEGATIVE 0x02
#define Y_NEGATIVE 0x01
static void clc_pts(WORD *pt, WORD angle, WORD xc, WORD yc,
                    WORD xrad, WORD yrad)
{
    WORD xdiff, ydiff, negative = Y_NEGATIVE;

    while (angle >= TWOPI)
        angle = (WORD)(angle - TWOPI);
    if (angle > 3 * HALFPI) {               /* fourth quadrant */
        angle = (WORD)(TWOPI - angle);
        negative = 0;
    } else if (angle > GDP_PI) {            /* third */
        angle = (WORD)(angle - GDP_PI);
        negative = X_NEGATIVE;
    } else if (angle > HALFPI) {            /* second */
        angle = (WORD)(GDP_PI - angle);
        negative = X_NEGATIVE | Y_NEGATIVE;
    }
    /* the two the table cannot answer: it stops at 89.6 degrees */
    if (angle > VDI_SIN_ANGLE_MAX) {
        xdiff = 0;
        ydiff = yrad;
    } else if (angle < HALFPI - VDI_SIN_ANGLE_MAX) {
        xdiff = xrad;
        ydiff = 0;
    } else {
        xdiff = (WORD)umul_shift(Icos(angle), (UWORD)xrad);
        ydiff = (WORD)umul_shift(Isin(angle), (UWORD)yrad);
    }
    if (negative & X_NEGATIVE) xdiff = (WORD)-xdiff;
    if (negative & Y_NEGATIVE) ydiff = (WORD)-ydiff;
    pt[0] = (WORD)(xc + xdiff);
    pt[1] = (WORD)(yc + ydiff);
}

/* How many segments a curve is drawn in: the larger radius over four,
 * clamped.  ptsin is where the points go, so the ceiling is ours. */
static WORD clc_nsteps(WORD xrad, WORD yrad)
{
    WORD steps = (WORD)((UWORD)(xrad > yrad ? xrad : yrad) >> 2);

    if (steps < MIN_ARC_CT) steps = MIN_ARC_CT;
    else if (steps > MAX_ARC_CT) steps = MAX_ARC_CT;
    return steps;
}

/* clc_arc: the points of a circular or elliptical arc, into ptsin, then
 * drawn -- an open arc as a polyline, everything else as a polygon
 * (which closes itself, so a circle needs no repeated first point).  A
 * pie slice gets the centre as its last point. */
static void clc_arc(WORD sub, WORD steps, WORD xc, WORD yc,
                    WORD xrad, WORD yrad, WORD beg_ang, WORD del_ang,
                    WORD end_ang)
{
    WORD *pt = ptsin, n = 1, i;

    clc_pts(pt, beg_ang, xc, yc, xrad, yrad);
    for (i = 1; i < steps; i++) {
        WORD angle = (WORD)(mul_div(del_ang, i, steps) + beg_ang);
        clc_pts(&pt[n * 2], angle, xc, yc, xrad, yrad);
        if (pt[n * 2] != pt[(n - 1) * 2] ||     /* duplicates ignored */
            pt[n * 2 + 1] != pt[(n - 1) * 2 + 1])
            n++;
    }
    clc_pts(&pt[n * 2], end_ang, xc, yc, xrad, yrad);
    n++;
    if (sub == GDP_PIE || sub == GDP_ELLPIE) {
        pt[n * 2]     = xc;
        pt[n * 2 + 1] = yc;
        n++;
    }
    if (sub == GDP_ARC || sub == GDP_ELLARC)
        polyline_pts(pt, n);
    else
        polygon(pt, n);
}

/* The six curve GDPs.  A circle's y radius is its x radius: this device
 * says its pixels are square (work_out's 372 x 372 microns), and the
 * donor scales by exactly that ratio. */
static void gdp_curve(WORD sub)
{
    WORD xc = ptsin[0], yc = ptsin[1], xrad, yrad, beg, end, del;

    if (sub <= GDP_CIRCLE)
        xrad = yrad = (sub == GDP_CIRCLE) ? ptsin[4] : ptsin[6];
    else {
        xrad = ptsin[2];
        yrad = ptsin[3];
    }
    if (xrad < 0) xrad = (WORD)-xrad;       /* TOS takes either sign */
    if (yrad < 0) yrad = (WORD)-yrad;

    if (vwk.clip &&
        (xc + xrad < vwk.xmn_clip || xc - xrad > vwk.xmx_clip ||
         yc + yrad < vwk.ymn_clip || yc - yrad > vwk.ymx_clip))
        return;

    if (sub == GDP_CIRCLE || sub == GDP_ELLIPSE) {
        beg = 0;
        end = TWOPI;
    } else {
        beg = intin[0];
        end = intin[1];
    }
    del = (WORD)(end - beg);
    if (del < 0)
        del = (WORD)(del + TWOPI);
    clc_arc(sub, clc_nsteps(xrad, yrad), xc, yc, xrad, yrad, beg, del, end);
}

/* v_rbox / v_rfbox: four quadrants of five points each, from the corner
 * radius -- a sixty-fourth of the screen width, clamped to half the
 * shorter side. */
#define CORNER_POINTS 5
static void gdp_rbox(WORD sub)
{
    WORD xoff[CORNER_POINTS], yoff[CORNER_POINTS];
    WORD x1 = ptsin[0], y1 = ptsin[1], x2 = ptsin[2], y2 = ptsin[3];
    WORD xr, yr, xcentre, ycentre, i, *p = ptsin;

    if (x1 > x2) { WORD t = x1; x1 = x2; x2 = t; }
    if (y1 < y2) { WORD t = y1; y1 = y2; y2 = t; }   /* (x1,y1) lower left */

    xr = (WORD)(SCR_W >> 6);
    if (xr > (x2 - x1) / 2) xr = (WORD)((x2 - x1) / 2);
    yr = xr;                                /* square pixels */
    if (yr > (y1 - y2) / 2) yr = (WORD)((y1 - y2) / 2);

    xoff[0] = 0;
    xoff[1] = mul_div(Icos675, xr, 32767);
    xoff[2] = mul_div(Icos450, xr, 32767);
    xoff[3] = mul_div(Icos225, xr, 32767);
    xoff[4] = xr;
    yoff[0] = yr;
    yoff[1] = mul_div(Isin675, yr, 32767);
    yoff[2] = mul_div(Isin450, yr, 32767);
    yoff[3] = mul_div(Isin225, yr, 32767);
    yoff[4] = 0;

    xcentre = (WORD)(x2 - xr);              /* upper right */
    ycentre = (WORD)(y2 + yr);
    for (i = 0; i < CORNER_POINTS; i++) {
        *p++ = (WORD)(xcentre + xoff[i]);
        *p++ = (WORD)(ycentre - yoff[i]);
    }
    ycentre = (WORD)(y1 - yr);              /* lower right, reversed */
    for (i = CORNER_POINTS - 1; i >= 0; i--) {
        *p++ = (WORD)(xcentre + xoff[i]);
        *p++ = (WORD)(ycentre + yoff[i]);
    }
    xcentre = (WORD)(x1 + xr);              /* lower left */
    for (i = 0; i < CORNER_POINTS; i++) {
        *p++ = (WORD)(xcentre - xoff[i]);
        *p++ = (WORD)(ycentre + yoff[i]);
    }
    ycentre = (WORD)(y2 + yr);              /* upper left, reversed */
    for (i = CORNER_POINTS - 1; i >= 0; i--) {
        *p++ = (WORD)(xcentre - xoff[i]);
        *p++ = (WORD)(ycentre - yoff[i]);
    }

    if (sub == GDP_RBOX) {                  /* an outline: close it */
        *p++ = ptsin[0];
        *p   = ptsin[1];
        polyline_pts(ptsin, 4 * CORNER_POINTS + 1);
    } else {
        polygon(ptsin, 4 * CORNER_POINTS);
    }
}

/* v_justified: the string spread to a given width.  intin[0] asks for
 * the spaces between words to carry it, intin[1] for the gaps between
 * every character; the remainder after the division is spread one pixel
 * at a time over the first few of them, which is what stops a justified
 * line from ending a pixel short.  The font is monospaced, so the width
 * of the string is simply its length in cells. */
static void gdp_justified(void)
{
    WORD cnt = (WORD)(contrl[3] - 2);
    WORD interword = intin[0], interchar = intin[1];
    WORD *str = &intin[2];
    WORD max_x = ptsin[2];
    WORD spaces = 0, width, i;
    WORD wordx = 0, rmword = 0, rmwordx = 0;
    WORD charx = 0, rmchar = 0, rmcharx = 0;
    WORD x, cy, x0, last = 0;

    if (cnt < 0)
        return;
    if (interword)
        for (i = 0; i < cnt; i++)
            if (str[i] == ' ')
                spaces++;

    width = (WORD)(cnt * FONT_W);

    if (interword && spaces) {
        WORD delword = (WORD)((max_x - width) / spaces);
        rmword = (WORD)((max_x - width) % spaces);
        if (rmword < 0) {
            rmwordx = -1;
            rmword = (WORD)-rmword;
        } else
            rmwordx = 1;
        if (interchar) {                    /* both: a word may only give
                                             * half a cell to the gaps */
            WORD expand = FONT_W / 2;
            if (delword > expand) { delword = expand; rmword = 0; }
            if (delword < -expand) { delword = (WORD)-expand; rmword = 0; }
            width = (WORD)(width + delword * spaces + rmword * rmwordx);
        }
        wordx = delword;
    }
    if (interchar && cnt > 1) {
        charx = (WORD)((max_x - width) / (cnt - 1));
        rmchar = (WORD)((max_x - width) % (cnt - 1));
        if (rmchar < 0) {
            rmcharx = -1;
            rmchar = (WORD)-rmchar;
        } else
            rmcharx = 1;
    }

    /* The point is placed by the JUSTIFIED width, not the string's own:
     * a centred justified line is centred on where it will end up. */
    x = align_x(ptsin[0], max_x);
    cy = align_y(ptsin[1]);
    x0 = x;
    for (i = 0; i < cnt; i++) {
        draw_char(str[i], x, cy);
        last = x;
        x = (WORD)(x + FONT_W + charx);
        if (rmchar) {
            x = (WORD)(x + rmcharx);
            rmchar--;
        }
        if (str[i] == ' ') {
            x = (WORD)(x + wordx);
            if (rmword) {
                x = (WORD)(x + rmwordx);
                rmword--;
            }
        }
    }
    if ((vwk.text_effects & TXT_UNDERLINE) && cnt > 0)
        underline(x0, (WORD)(last + FONT_W - 1
                             + ((vwk.text_effects & TXT_THICKEN) ? 1 : 0)), cy);
}

static void vdi_v_gdp(void)
{
    switch (contrl[5]) {
    case GDP_BAR:
        vdi_vr_recfl();
        if (vwk.fill_per) {
            WORD *xy = ptsin;
            xy[5] = xy[7] = xy[3];
            xy[3] = xy[9] = xy[1];
            xy[4] = xy[2];
            xy[6] = xy[8] = xy[0];
            fill_perimeter(xy, 5);
        }
        break;
    case GDP_ARC:
    case GDP_PIE:
    case GDP_CIRCLE:
    case GDP_ELLIPSE:
    case GDP_ELLARC:
    case GDP_ELLPIE:
        gdp_curve(contrl[5]);
        break;
    case GDP_RBOX:
    case GDP_RFBOX:
        gdp_rbox(contrl[5]);
        break;
    case GDP_JUSTIFIED:
        gdp_justified();
        break;
    }
}

static void vdi_vsm_type(void)
{
    WORD v = intin[0];

    if (v < MIN_MARK_STYLE || v > MAX_MARK_STYLE)
        v = DEF_MARK_STYLE;
    vwk.mark_index = (WORD)(v - 1);
    intout[0] = v;
    contrl[4] = 1;
}

/* vsm_height takes a height in ptsin and answers with the cell the marker
 * will really occupy: the scale is a whole multiple of the nominal height,
 * so what comes back is rarely what was asked for. */
static void vdi_vsm_height(void)
{
    WORD h = ptsin[1];

    if (h < DEF_MKHT)
        h = DEF_MKHT;
    else if (h > MAX_MKHT)
        h = MAX_MKHT;
    vwk.mark_height = h;
    vwk.mark_scale = (WORD)((h + DEF_MKHT / 2) / DEF_MKHT);
    ptsout[0] = (WORD)(vwk.mark_scale * DEF_MKWD);
    ptsout[1] = (WORD)(vwk.mark_scale * DEF_MKHT);
    contrl[2] = 1;
}

static void vdi_vsm_color(void)
{
    WORD v = intin[0];

    if (v < 0 || v > 15) v = 1;
    vwk.mark_color = v;
    intout[0] = v;
    contrl[4] = 1;
}

/* ---------------------------------------------------------------------- */
/* the inquiries an application makes about what it set                   */
/* ---------------------------------------------------------------------- */

static void vdi_vql_attributes(void)
{
    intout[0] = vwk.line_index;
    intout[1] = vwk.line_color;
    intout[2] = (WORD)(vwk.wrt_mode + 1);
    ptsout[0] = vwk.line_width;
    ptsout[1] = 0;
    contrl[2] = 1;
    contrl[4] = 3;
}

/* The donor answers this one with mark_index, which is the type MINUS ONE
 * -- an off-by-one in the ROM.  The manual says the marker type, and a
 * caller that feeds the answer back to vsm_type has to get the same marker
 * back, so this reports the type. */
static void vdi_vqm_attributes(void)
{
    intout[0] = (WORD)(vwk.mark_index + 1);
    intout[1] = vwk.mark_color;
    intout[2] = (WORD)(vwk.wrt_mode + 1);
    ptsout[0] = 0;
    ptsout[1] = vwk.mark_height;
    contrl[2] = 1;
    contrl[4] = 3;
}

static void vdi_vqf_attributes(void)
{
    intout[0] = vwk.fill_style;
    intout[1] = vwk.fill_color;
    intout[2] = (WORD)(vwk.fill_index + 1);
    intout[3] = (WORD)(vwk.wrt_mode + 1);
    intout[4] = vwk.fill_per;
    contrl[4] = 5;
}

static void vdi_vsf_perimeter(void)
{
    vwk.fill_per = (WORD)(intin[0] != 0);
    intout[0] = vwk.fill_per;
    contrl[4] = 1;
}

/* vst_rotation: this driver has one direction.  Answering with what was
 * applied -- zero -- is how a caller finds that out. */
static void vdi_vst_rotation(void)
{
    intout[0] = 0;
    contrl[4] = 1;
}

/* v_get_pixel: the one primitive that reads the screen back a pixel at a
 * time.  intout[0] is the hardware index the plane holds, intout[1] the
 * VDI pen that maps to it -- which is what a program comparing against
 * vsf_color's answer wants.  A point off the screen reads as 0.
 *
 * The mouse cursor is drawn INTO the screen here (it is a blit, not a
 * sprite), so a pixel under a visible cursor reads the cursor.  That is
 * true of the donor on an ST as well. */
static void vdi_v_get_pixel(void)
{
    WORD x = ptsin[0], y = ptsin[1], hw = 0;

    if (x >= 0 && y >= 0 && x < SCR_W && y < SCR_H) {
        uint8_t b = vram_read8(VR_SCREEN0 + (uint32_t)y * SCR_STRIDE
                               + (uint32_t)((UWORD)x >> 1));
        hw = (WORD)((x & 1) ? (b & 0x0F) : (b >> 4));
    }
    intout[0] = hw;
    intout[1] = rev_col(hw);
    contrl[4] = 2;
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
 * device's colours.  This is how the AES draws icons and glyph masks.  The
 * work is raster_1bpp()'s, above; the field goes through a scalar on purpose.
 * `src->fd_wdwidth * 2u` here -- src spilled to the stack by the order()
 * calls and dead after this line -- is miscompiled by Calypsi 5.18 into an
 * in-place shift of src's own stack slot: stride became src << 1 and the
 * field was never read, which is what made every row after the first read
 * unrelated memory in Phase 2b (B5 in tools/ccbug/, `make check-cc`). */
static void vdi_vrt_cpyfm(void)
{
    MFDB *src = (MFDB *)(uint16_t)contrl[7];   /* forms live in bank $00 */
    WORD sx1 = ptsin[0], sy1 = ptsin[1], sx2 = ptsin[2], sy2 = ptsin[3];
    WORD wdwidth;

    if (!src || !src->fd_addr)
        return;
    order(&sx1, &sx2); order(&sy1, &sy2);
    wdwidth = src->fd_wdwidth;                              /* WORDS */
    raster_1bpp((const uint8_t *)(uint16_t)src->fd_addr,
                (uint16_t)((uint16_t)wdwidth * 2u),
                sx1, sy1, (WORD)(sx2 - sx1 + 1), (WORD)(sy2 - sy1 + 1),
                ptsin[4], ptsin[5], intin[0],
                (uint8_t)HW(intin[1]), (uint8_t)HW(intin[2]));
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
 * handlers live above bank $00), so both words carry address: [7] low, [8] high
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
 * The OS's CH ($02FC) is filled by its keyboard IRQ handler, and the OS's
 * handler is not running: gem4xe's own vectors are in (src/sys/irq.h), or
 * -- if they could not be installed -- the CPU's I flag is set.  Either way
 * the raw code comes from POKEY.  With the interrupt regime up, the IRQ
 * handler in src/sys/irq.s has already taken KBCODE into an 8-deep ring and
 * this drains it.  Without it, POKEY's own latch is enough: with IRQEN bit
 * 6 set it holds a key press in IRQST bit 6 (0 = a key arrived) and the raw
 * code in KBCODE until the next one, so a key pressed and released between
 * two polls is still there to be read; the latch is cleared by writing the
 * bit low then high in IRQEN.  (The latch also matters to the test rig:
 * AltirraSDL's KEY verb queues a key until the keyboard IRQ is enabled and
 * acknowledged.)
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
    POKMSK |= 0x40;                 /* keyboard IRQ, alongside whatever the */
    IRQEN   = POKMSK;               /* interrupt regime already enabled     */
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

static void kb_queue(uint8_t code)
{
    WORD k = kb_translate(code);
    uint8_t next = (uint8_t)((kb_tail + 1) & (KB_QLEN - 1));
    if (k && next != kb_head) {
        kb_q[kb_tail] = k;
        kb_tail = next;
    }
}

void vdi_key_poll(void)
{
    if (irq.how != IRQ_OFF) {
        /* The handler writes the tail; only this side moves the head. */
        while (irq_kb_head != irq_kb_tail) {
            uint8_t code = irq_kb[irq_kb_head];
            irq_kb_head = (uint8_t)((irq_kb_head + 1) & 7);
            kb_queue(code);
        }
        return;
    }
    if (IRQST & 0x40)               /* bit 6 high: nothing since the last ack */
        return;
    {
        uint8_t code = KBCODE;
        IRQEN = (uint8_t)(POKMSK & ~0x40); /* acknowledge: bit low ...     */
        IRQEN = POKMSK;                    /* ... then high re-arms the latch */
        kb_queue(code);
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
    vwk.ud_ls = (UWORD)intin[0];
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

/* vst_font: choose a face.  There are at most two -- the one linked in and
 * the one a .FNT loaded (src/vdi/font.c) -- so the answer is one of two ids,
 * and as everywhere in the VDI the caller must use what comes back rather
 * than what it asked for. */
static void vdi_vst_font(void)
{
    intout[0] = vdi_font_select(intin[0]);
    contrl[4] = 1;
}

/* vst_load_fonts: GDOS's call, doing here what GDOS does -- read what the
 * system knows about and say how many faces that added.  There is one place
 * to look, SYSTEM.FNT beside the program, so the answer is 0 or 1.  The
 * system loads it at start-up as well; this is how an application asks for
 * it after putting one there, and how it finds out whether there is one. */
static void vdi_vst_load_fonts(void)
{
    intout[0] = (WORD)(vdi_font_load() ? 1 : 0);
    contrl[4] = 1;
}

/* vst_unload_fonts: back to the face that is linked in.  The far memory the
 * loaded strip took stays taken -- far_alloc gives none back -- so a load
 * after this reuses it. */
static void vdi_vst_unload_fonts(void)
{
    vdi_font_default();
}

/* vqt_name: a face's id and its name, one character to a word, which is how
 * the VDI has always answered it.  Face 1 is the system's, 2 the loaded one;
 * an index this driver does not have answers with the system's. */
static void vdi_vqt_name(void)
{
    const char *nm = vdi_font_name(intin[0]);
    WORD i, end = 0;

    intout[0] = vdi_font_id(intin[0]);
    for (i = 0; i < 32; i++) {
        if (!end && !nm[i])
            end = 1;
        intout[i + 1] = (WORD)(end ? 0 : (uint8_t)nm[i]);
    }
    contrl[4] = 33;
}

/* vst_alignment.  The pair chosen is what comes back, and an out-of-range
 * request becomes the default -- the donor's rule, and the reason a caller
 * must use the answer rather than the request. */
static void vdi_vst_alignment(void)
{
    vwk.h_align = (WORD)(intin[0] >= TA_LEFT && intin[0] <= TA_RIGHT
                         ? intin[0] : TA_LEFT);
    vwk.v_align = (WORD)(intin[1] >= TA_BASE && intin[1] <= TA_TOP
                         ? intin[1] : TA_BASE);
    intout[0] = vwk.h_align;
    intout[1] = vwk.v_align;
    contrl[4] = 2;
}

/* vst_effects.  Two of the six are real here -- thickened and underlined,
 * which cost a second blit and a rectangle -- and the answer is what was
 * APPLIED, which is how a GEM application learns what a device can do. */
static void vdi_vst_effects(void)
{
    vwk.text_effects = (WORD)(intin[0] & TXT_DONE);
    intout[0] = vwk.text_effects;
    contrl[4] = 1;
}

/* vst_point.  One face, so the point size is the one it has; as with
 * vst_height, what comes back is what the caller must use. */
static void vdi_vst_point(void)
{
    intout[0] = FONT_POINT;
    ptsout[0] = FONT_W;
    ptsout[1] = FONT_TOP;
    ptsout[2] = FONT_W;
    ptsout[3] = FONT_H;
    contrl[2] = 2;
    contrl[4] = 1;
}

/* vqt_extent: the box the string would cover, as four corners going
 * anticlockwise from the origin -- which for unrotated text is the
 * rectangle n cells wide and one cell high.  A word processor cannot
 * break a line without this. */
static void vdi_vqt_extent(void)
{
    WORD w = (WORD)(contrl[3] * FONT_W
                    + ((vwk.text_effects & TXT_THICKEN) ? 1 : 0));

    ptsout[0] = 0;      ptsout[1] = 0;
    ptsout[2] = w;      ptsout[3] = 0;
    ptsout[4] = w;      ptsout[5] = FONT_H;
    ptsout[6] = 0;      ptsout[7] = FONT_H;
    contrl[2] = 4;
}

/* vqt_width: one character's cell and the two deltas that would carry a
 * proportional face's overhang.  This one is monospaced, so the cell is
 * the width and both deltas are zero; a character the font does not have
 * would answer -1, and this font has all 256. */
static void vdi_vqt_width(void)
{
    intout[0] = intin[0];
    /* Three POINTS, and the VDI reads the two deltas out of the words
     * a point apart: [0] the cell's width, [2] the left delta, [4] the
     * right one (EmuTOS vdi_text.c writes exactly those three).  All
     * six are set, because a caller reads what contrl[2] declares and
     * the three odd words would otherwise be the last call's. */
    ptsout[0] = FONT_W;
    ptsout[1] = 0;
    ptsout[2] = 0;                  /* left delta: a fixed cell has none */
    ptsout[3] = 0;
    ptsout[4] = 0;                  /* right delta */
    ptsout[5] = 0;
    contrl[2] = 3;
    contrl[4] = 1;
}

/* vqt_attributes -- the AES asks for the current text settings before drawing
 * a string, rather than tracking them itself. */
static void vdi_vqt_attributes(void)
{
    intout[0] = 1;                  /* font id: the system font */
    intout[1] = vwk.text_color;
    intout[2] = 0;                  /* rotation: none supported */
    intout[3] = vwk.h_align;        /* what vst_alignment set */
    intout[4] = vwk.v_align;
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

/* One pass of the input machinery, from the caller's loop.  The timer
 * vector fires once per FRAME: with the interrupt regime up, once for every
 * vertical blank irq_frames has counted since the last pass -- so a pass
 * that took three frames delivers three ticks, late but not lost -- and
 * without it, when ANTIC's line counter is seen to wrap (VCOUNT counts
 * 0..155 on PAL), which loses a tick whenever a pass outlasts a frame.
 * Either way a tick means 20 ms whether the loop runs once a frame or a
 * hundred times. */
void vdi_input_poll(void)
{
    static uint8_t  last_vcount;
    static uint16_t last_frames;
    uint8_t  vc = VCOUNT;
    uint16_t ticks;

    if (irq.how != IRQ_OFF) {
        uint16_t f = irq_frames;
        ticks = (uint16_t)(f - last_frames);
        last_frames = f;
    } else {
        ticks = (vc < last_vcount) ? 1 : 0;
    }
    last_vcount = vc;

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
    if (vec_timv)
        while (ticks--)
            vec_timv();
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
    vdi_v_pmarker,   /*  7 */                   vdi_v_gtext,     /*  8 */
    vdi_v_fillarea,  /*  9 */                   v_nop,           /* 10 cellarray (nop) */
    vdi_v_gdp,       /* 11 */                   vdi_vst_height,  /* 12 */
    vdi_vst_rotation,/* 13 */                   vdi_vs_color,    /* 14 */
    vdi_vsl_type,    /* 15 */                   vdi_vsl_width,   /* 16 */
    vdi_vsl_color,   /* 17 */                   vdi_vsm_type,    /* 18 */
    vdi_vsm_height,  /* 19 */                   vdi_vsm_color,   /* 20 */
    vdi_vst_font,    /* 21 */                   vdi_vst_color,   /* 22 */
    vdi_vsf_interior,/* 23 */                   vdi_vsf_style,   /* 24 */
    vdi_vsf_color,   /* 25 */                   vdi_vq_color,    /* 26 */
    v_nop,           /* 27 vq_cellarray (nop)*/ vdi_v_locator,   /* 28 */
    v_nop,           /* 29 valuator (nop) */    vdi_v_choice,    /* 30 */
    vdi_v_string,    /* 31 */                   vdi_vswr_mode,   /* 32 */
    vdi_vsin_mode,   /* 33 */                   v_nop,           /* 34 (does not exist) */
    vdi_vql_attributes, /* 35 */                vdi_vqm_attributes, /* 36 */
    vdi_vqf_attributes, /* 37 */                vdi_vqt_attributes, /* 38 */
    vdi_vst_alignment /* 39 */
};

static const VDI_OP jmptb2[] = {
    vdi_v_opnvwk,    /* 100 */
    vdi_v_clsvwk,    /* 101 */
    vdi_vq_extnd,    /* 102 */
    vdi_v_contourfill, /* 103 */
    vdi_vsf_perimeter, /* 104 */
    vdi_v_get_pixel, /* 105 */
    vdi_vst_effects, /* 106 */
    vdi_vst_point,   /* 107 */
    vdi_vsl_ends,    /* 108 */
    vdi_vro_cpyfm,   /* 109 */
    vdi_vr_trnfm,    /* 110 */
    vdi_vsc_form,    /* 111 */
    vdi_vsf_udpat,   /* 112 */
    vdi_vsl_udsty,   /* 113 */
    vdi_vr_recfl,    /* 114 */
    vdi_vqin_mode,   /* 115 */
    vdi_vqt_extent,  /* 116 */
    vdi_vqt_width,   /* 117 */
    vdi_vex_timv,    /* 118 */
    vdi_vst_load_fonts,  /* 119 */
    vdi_vst_unload_fonts,/* 120 */
    vdi_vrt_cpyfm,   /* 121 */
    vdi_v_show_c,    /* 122 */
    vdi_v_hide_c,    /* 123 */
    vdi_vq_mouse,    /* 124 */
    vdi_vex_butv,    /* 125 */
    vdi_vex_motv,    /* 126 */
    vdi_vex_curv,    /* 127 */
    vdi_vq_key_s,    /* 128 */
    vdi_vs_clip,     /* 129 */
    vdi_vqt_name     /* 130 */
};

#define N1 ((WORD)(sizeof jmptb1 / sizeof jmptb1[0]))
#define N2 ((WORD)(sizeof jmptb2 / sizeof jmptb2[0]))

void vdi(void)
{
    WORD op = contrl[0];
    contrl[2] = 0;                  /* no points out unless a handler says so */
    contrl[4] = 0;                  /* no ints out   ditto */
    /* The workstation the call names, unless the call opens one.  A
     * handle that is not open gets nothing done and nothing back, as
     * in the donor's screen(). */
    if (op != V_OPNWK && op != V_OPNVWK && !vwk_select(contrl[6]))
        return;
    if (op >= 1 && op < 1 + N1)
        jmptb1[op - 1]();
    else if (op >= 100 && op < 100 + N2)
        jmptb2[op - 100]();
}

void vdi_init(void)
{
    WORD i;
    kb_init();
    /* The contour fill's far buffers are taken the first time one is
     * asked for; nothing here clears bss, so say so out loud. */
    cf_map = cf_stack = 0;
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
