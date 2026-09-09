/* dev_vbxe.c -- the VBXE side of the seam (vdidev.h).
 *
 * 640x240 at 4bpp, in VRAM that sits on the 1.79 MHz chip bus however
 * fast the 65816 runs.  So every primitive here compiles a BLIT LIST and
 * the CPU touches a pixel only where the blitter genuinely cannot help;
 * dev_flush() is what starts the list and waits for it.  The other
 * device (src/vdi/dev_antic.c) writes bytes and has nothing to flush,
 * and that difference is the whole reason the seam exists.
 *
 * Two pixels share a byte, high nibble the LEFT one, so every rectangle
 * has up to two partial ends.  Rectangle edges are exactly where 4bpp
 * VDI drivers historically went wrong, which is why tests/emu/m3_vdi.py
 * exercises odd x1, odd x2, and rectangles one pixel wide inside a
 * single byte.
 */
#include "vdi.h"
#include "vdidev.h"
#include "../vbxe/vbxe.h"

/* The VDI's pen order into this device's hardware indices.  The table is
 * the VDI's (src/vdi/vdi.c) because the palette is loaded through it;
 * what it MEANS is this device's, which is why the seam passes a VDI pen
 * and the mapping happens here. */
#define HW(pen) ((WORD)map_col[(pen) & 0x0F])

void dev_fill_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
{
    WORD hwpen = HW(pen);
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
void dev_xor_rect(WORD x1, WORD y1, WORD x2, WORD y2)
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


/* The list started and waited for.  This is the only place the VDI's
 * device-independent code learns that a device might be asynchronous. */
void dev_flush(void)
{
    blit_run();
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
 * an OR of the pattern into it, the pair dev_fill_rect uses. */
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
void dev_patt_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
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

void dev_style_line(WORD x1, WORD y1, WORD x2, WORD y2, UWORD mask)
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


/* The expansions above are caches; this is the VDI saying they are stale. */
void dev_invalidate(void)
{
    pe_valid = 0;
    lh_valid = lv_valid = 0;
}
