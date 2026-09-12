/* antic.c -- the ANTIC mode F surface.  See antic.h for the map and for
 * why the framebuffer is where it is.
 *
 * Everything here is CPU work.  There is no blitter on this side of the
 * seam, which is the whole difference between the two drivers: the VBXE
 * one compiles blit lists because its VRAM is on a 1.79 MHz bus however
 * fast the CPU runs, and this one writes bytes because its framebuffer
 * is plain motherboard RAM the accelerator reaches at full speed.
 */
#include "portab.h"
#include "antic.h"

/* Character N's row r of a face.  The strip is one byte per character
 * per row, 256 of them, the glyph LEFT-ALIGNED in its byte -- which is
 * the layout every face gem4xe links is repacked into, whatever the
 * donor's packing was.  WHICH face is the caller's business: this file
 * used to reach for font8x8 itself, and then quietly went on drawing it
 * after the VDI had been told to use the condensed one. */
#define AN_FONT_STRIDE 256

/* The ADDRESS is a uint32_t and the arithmetic is done on it BEFORE the
 * cast, which is not a style choice: a far pointer indexed by a computed
 * subscript does not survive cc65816 5.18, and the same idiom is what
 * vdi_font_expand and draw_glyph_cpu use on the other device. */
static uint8_t an_font_row(uint32_t face, uint16_t ch, uint16_t row)
{
    const uint8_t FAR *sr =
        (const uint8_t FAR *)(face + (uint32_t)row * AN_FONT_STRIDE);
    return sr[ch & 0xFF];
}

#define REG8(a)  (*(volatile uint8_t *)(a))
#define SCREEN   ((volatile uint8_t *)AN_SCREEN)

/* ANTIC's instruction bytes. */
#define AN_MODE_F   0x0F
#define AN_LMS      0x40
#define AN_JVB      0x41
#define AN_BLANK8   0x70                /* eight blank scan lines */

/* The masks a run's first and last byte are painted through: bit 7 is
 * the LEFTMOST pixel of a byte, which is the one thing about a 1bpp
 * Atari screen that catches everyone once. */
static const uint8_t an_left[8] = {
    0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01
};
static const uint8_t an_right[8] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF
};

void antic_init(uint8_t fg, uint8_t bg)
{
    volatile uint8_t *d = (volatile uint8_t *)AN_DLIST;
    uint16_t i;

    /* The screen off while the list is built: ANTIC reads the list as it
     * goes, and a half-written one is a machine that fetches instructions
     * from whatever was there. */
    REG8(AN_DMACTL) = 0;
    REG8(AN_SDMCTL) = 0;

    /* Three blank rows are the OS's own convention for the top of a
     * display list, and they are what puts the first drawn line at scan
     * line 8. */
    *d++ = AN_BLANK8;
    *d++ = AN_BLANK8;
    *d++ = AN_BLANK8;

    *d++ = AN_MODE_F | AN_LMS;              /* line 0, from the base */
    *d++ = (uint8_t)AN_SCREEN;
    *d++ = (uint8_t)(AN_SCREEN >> 8);
    for (i = 1; i < AN_SPLIT; i++)
        *d++ = AN_MODE_F;
    /* ...and again at the line the 4 KB crossing falls in front of, at
     * exactly the address the linear formula gives (antic.h). */
    *d++ = AN_MODE_F | AN_LMS;
    *d++ = (uint8_t)(AN_SCREEN + AN_SPLIT * AN_STRIDE);
    *d++ = (uint8_t)((AN_SCREEN + AN_SPLIT * AN_STRIDE) >> 8);
    for (i = AN_SPLIT + 1; i < AN_H; i++)
        *d++ = AN_MODE_F;

    *d++ = AN_JVB;
    *d++ = (uint8_t)AN_DLIST;
    *d++ = (uint8_t)(AN_DLIST >> 8);

    REG8(AN_COLPF1) = fg;
    REG8(AN_COLPF2) = bg;
    REG8(AN_COLBK)  = bg;
    REG8(AN_COLOR1) = fg;
    REG8(AN_COLOR2) = bg;
    REG8(AN_COLOR4) = bg;

    REG8(AN_DLISTL)     = (uint8_t)AN_DLIST;
    REG8(AN_DLISTL + 1) = (uint8_t)(AN_DLIST >> 8);
    REG8(AN_SDLSTL)     = (uint8_t)AN_DLIST;
    REG8(AN_SDLSTL + 1) = (uint8_t)(AN_DLIST >> 8);

    antic_clear(0);
    REG8(AN_DMACTL) = 0x22;                 /* list DMA + normal playfield */
    REG8(AN_SDMCTL) = 0x22;
}

void antic_recolour(int16_t pen, uint8_t value)
{
    if (pen) {
        REG8(AN_COLPF1) = value;
        REG8(AN_COLOR1) = value;
    } else {
        REG8(AN_COLPF2) = value;
        REG8(AN_COLBK)  = value;
        REG8(AN_COLOR2) = value;
        REG8(AN_COLOR4) = value;
    }
}

void antic_off(void)
{
    REG8(AN_DMACTL) = 0;
    REG8(AN_SDMCTL) = 0;
}

/* The shadow is what is kept, not the register: DOS runs with the OS VBI
 * on, and the VBI writes the shadow to DMACTL every frame, so the shadow
 * is what DOS will see again -- and the register is written too, for
 * the frames until then.  Measured in docs/bench.md: the September 2
 * figures were taken with the OS's display list under the runner's
 * buffers, which is this switch thrown by accident; with the list intact
 * every VRAM-bound row ran a quarter slower, and this is the remedy. */
static uint8_t an_saved_dmactl;

void antic_suspend(void)
{
    an_saved_dmactl = REG8(AN_SDMCTL);
    REG8(AN_DMACTL) = 0;
    REG8(AN_SDMCTL) = 0;
}

void antic_resume(void)
{
    REG8(AN_SDMCTL) = an_saved_dmactl;
    REG8(AN_DMACTL) = an_saved_dmactl;
}

void antic_clear(uint8_t value)
{
    volatile uint8_t *p = SCREEN;
    uint16_t n = AN_BYTES;

    while (n--)
        *p++ = value;
}

/* The byte a pixel is in.  The shift is UNSIGNED for the reason
 * tools/ccbug rule 12 gives -- cc65816 5.18 mangles a signed 16-bit
 * right shift -- and the caller has already established that x is not
 * negative.  With the signed shift the diagonal in test-m24 was drawn
 * correctly to x=31 and then eight bytes short of where it belonged for
 * every pixel after it, which is what a sign that appears at 32 looks
 * like from the outside. */
static volatile uint8_t *an_at(int16_t x, int16_t y)
{
    return SCREEN + (uint16_t)y * AN_STRIDE + ((uint16_t)x >> 3);
}

void antic_plot(int16_t x, int16_t y, uint8_t set)
{
    volatile uint8_t *p;
    uint8_t bit, v;

    if (x < 0 || y < 0 || x >= AN_W || y >= AN_H)
        return;
    p = an_at(x, y);
    bit = (uint8_t)(0x80 >> (x & 7));
    v = *p;                                 /* ccbug rule 3: the byte comes
                                             * out, is decided, and goes back;
                                             * never *p = c ? a : b */
    v = set ? (uint8_t)(v | bit) : (uint8_t)(v & (uint8_t)~bit);
    *p = v;
}

/* One run.  The whole bytes in the middle are written outright and only
 * the two ends are read-modify-write, which is the difference between a
 * span and a string of plots. */
void antic_hline(int16_t x1, int16_t x2, int16_t y, uint8_t set)
{
    volatile uint8_t *p;
    uint8_t lm, rm, fill, v;
    int16_t b1, b2, b;

    if (y < 0 || y >= AN_H)
        return;
    if (x1 > x2) {
        int16_t t = x1; x1 = x2; x2 = t;
    }
    if (x2 < 0 || x1 >= AN_W)
        return;
    if (x1 < 0)
        x1 = 0;
    if (x2 >= AN_W)
        x2 = AN_W - 1;

    /* ccbug rule 12: never right-shift a signed 16-bit value.  Both are
     * non-negative by now, so an unsigned copy is the shift to make. */
    b1 = (int16_t)((uint16_t)x1 >> 3);
    b2 = (int16_t)((uint16_t)x2 >> 3);
    lm = an_left[x1 & 7];
    rm = an_right[x2 & 7];
    fill = set ? 0xFF : 0x00;               /* decided once, not per byte */
    p = SCREEN + (uint16_t)y * AN_STRIDE + (uint16_t)b1;

    if (b1 == b2) {                         /* one byte, both ends in it */
        uint8_t m = (uint8_t)(lm & rm);
        v = *p;
        v = set ? (uint8_t)(v | m) : (uint8_t)(v & (uint8_t)~m);
        *p = v;
        return;
    }
    v = *p;
    v = set ? (uint8_t)(v | lm) : (uint8_t)(v & (uint8_t)~lm);
    *p = v;
    /* THE MIDDLE, and the shape it is written in is not a preference.
     * `*p++ = set ? 0xFF : 0x00;` -- the obvious line -- breaks two of
     * the standing rules at once (tools/ccbug: never store a conditional
     * through a pointer, never increment a pointer in the expression
     * that uses it), and cc65816 5.18 miscompiles it in a way no amount
     * of staring at one iteration reveals: the loop runs ONCE for any
     * count above two and then leaves the function, so the run's right
     * edge is never drawn either.  Two middle bytes work and eight do
     * not, which is exactly the kind of thing that would have shipped.
     * Hoisting the constant out and separating the increment from the
     * store fixes it and is better code besides. */
    for (b = (int16_t)(b1 + 1); b < b2; b++) {
        p++;
        *p = fill;
    }
    p++;
    v = *p;
    v = set ? (uint8_t)(v | rm) : (uint8_t)(v & (uint8_t)~rm);
    *p = v;
}

void antic_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t set)
{
    int16_t y;

    if (y1 > y2) {
        y = y1; y1 = y2; y2 = y;
    }
    if (y1 < 0)
        y1 = 0;
    if (y2 >= AN_H)
        y2 = AN_H - 1;
    for (y = y1; y <= y2; y++)
        antic_hline(x1, x2, y, set);
}

/* ---- the writing modes ------------------------------------------------ */

/* One destination byte: the source applied through mask `m` in `mode`,
 * everything outside the mask left alone.  The VDI's modes are numbered
 * from 1 (src/vdi/vdi.h), which is what the caller passes. */
#define AN_MD_REPLACE 1
#define AN_MD_TRANS   2
#define AN_MD_XOR     3
#define AN_MD_ERASE   4

static uint8_t an_apply(uint8_t dst, uint8_t src, uint8_t m,
                        int16_t mode, uint8_t pen)
{
    uint8_t ink = pen ? 0xFF : 0x00;
    uint8_t out;

    switch (mode) {
    case AN_MD_TRANS:
        out = (uint8_t)((dst & (uint8_t)~src) | (ink & src));
        break;
    case AN_MD_XOR:
        out = (uint8_t)(dst ^ src);
        break;
    case AN_MD_ERASE:
        out = (uint8_t)((dst & src) | (ink & (uint8_t)~src));
        break;
    default:                                /* AN_MD_REPLACE */
        out = pen ? src : (uint8_t)~src;
        break;
    }
    return (uint8_t)((dst & (uint8_t)~m) | (out & m));
}

/* A solid run: the source is all ones, so REPLACE and TRANS write the
 * pen, XOR inverts and ERASE does nothing. */
void antic_span(int16_t x1, int16_t x2, int16_t y, int16_t mode, uint8_t pen)
{
    volatile uint8_t *p;
    uint8_t lm, rm, mid, v;
    int16_t b1, b2, b;

    if (y < 0 || y >= AN_H)
        return;
    if (x1 > x2) {
        int16_t t = x1; x1 = x2; x2 = t;
    }
    if (x2 < 0 || x1 >= AN_W)
        return;
    if (x1 < 0)
        x1 = 0;
    if (x2 >= AN_W)
        x2 = AN_W - 1;

    b1 = (int16_t)((uint16_t)x1 >> 3);
    b2 = (int16_t)((uint16_t)x2 >> 3);
    lm = an_left[x1 & 7];
    rm = an_right[x2 & 7];
    mid = an_apply(0, 0xFF, 0xFF, mode, pen);   /* a whole byte of it */
    p = SCREEN + (uint16_t)y * AN_STRIDE + (uint16_t)b1;

    if (b1 == b2) {
        v = *p;
        v = an_apply(v, 0xFF, (uint8_t)(lm & rm), mode, pen);
        *p = v;
        return;
    }
    v = *p;
    v = an_apply(v, 0xFF, lm, mode, pen);
    *p = v;
    for (b = (int16_t)(b1 + 1); b < b2; b++) {
        p++;
        if (mode == AN_MD_XOR) {            /* XOR still reads the byte */
            v = *p;
            v = (uint8_t)(v ^ 0xFF);
            *p = v;
        } else if (mode != AN_MD_ERASE) {
            *p = mid;
        }
    }
    p++;
    v = *p;
    v = an_apply(v, 0xFF, rm, mode, pen);
    *p = v;
}

void antic_rect_mode(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                     int16_t mode, uint8_t pen)
{
    int16_t y;

    if (y1 > y2) {
        y = y1; y1 = y2; y2 = y;
    }
    if (y1 < 0)
        y1 = 0;
    if (y2 >= AN_H)
        y2 = AN_H - 1;
    for (y = y1; y <= y2; y++)
        antic_span(x1, x2, y, mode, pen);
}

/* ---- text -------------------------------------------------------------
 * The font is a 1bpp strip already (src/vdi/vdi.h): character N's row r
 * is font8x8[r * FONT_STRIDE + N], bit 7 leftmost.  So a glyph goes into
 * a 1bpp framebuffer as itself, shifted into place across at most two
 * bytes -- no expansion, no second pre-shifted copy, none of what the
 * VBXE driver keeps in VRAM to blit the same glyph at an odd x.
 *
 * A cell that runs off an edge is dropped whole rather than clipped:
 * the VDI clips text by the cell, and a partial glyph is not something
 * GEM asks for. */
void antic_glyph(uint32_t face, uint16_t ch, int16_t x, int16_t y,
                 int16_t mode, uint8_t pen, int16_t w, int16_t h)
{
    volatile uint8_t *p;
    uint16_t row;
    uint8_t shift, g, v, cell;

    if (w < 1 || w > 8)
        return;
    cell = (uint8_t)(0xFF << (8 - w));  /* the columns the face uses */
    if (x < 0 || y < 0 || x + w > AN_W || y + h > AN_H)
        return;
    shift = (uint8_t)(x & 7);
    p = SCREEN + (uint16_t)y * AN_STRIDE + ((uint16_t)x >> 3);

    for (row = 0; row < (uint16_t)h; row++) {
        g = (uint8_t)(an_font_row(face, ch, row) & cell);
        if (shift == 0) {
            v = *p;
            v = an_apply(v, g, cell, mode, pen);
            *p = v;
        } else {
            uint8_t hi = (uint8_t)(g >> shift);
            uint8_t lo = (uint8_t)(g << (8 - shift));
            uint8_t mh = (uint8_t)(cell >> shift);
            uint8_t ml = (uint8_t)(cell << (8 - shift));
            v = *p;
            v = an_apply(v, hi, mh, mode, pen);
            *p = v;
            v = p[1];
            v = an_apply(v, lo, ml, mode, pen);
            p[1] = v;
        }
        p += AN_STRIDE;
    }
}

uint8_t antic_get_pixel(int16_t x, int16_t y)
{
    volatile uint8_t *p;

    if (x < 0 || y < 0 || x >= AN_W || y >= AN_H)
        return 0;
    p = an_at(x, y);
    return (uint8_t)((*p >> (7 - (x & 7))) & 1);
}

/* ---- patterns, and the styled lines that are patterns ------------------ */

/* The byte of `patrow` that lands on screen byte `bx`: the pattern is
 * aligned to the screen's 16-pixel word, so an even byte takes the high
 * half and an odd byte the low one. */
static uint8_t an_patt_byte(uint16_t patrow, int16_t bx)
{
    return (uint8_t)((bx & 1) ? (patrow & 0xFF) : (patrow >> 8));
}

void antic_patt_span(int16_t x1, int16_t x2, int16_t y, uint16_t patrow,
                     int16_t mode, uint8_t pen)
{
    volatile uint8_t *p;
    uint8_t lm, rm, src, v;
    int16_t b1, b2, b;

    if (y < 0 || y >= AN_H)
        return;
    if (x1 > x2) {
        int16_t t = x1; x1 = x2; x2 = t;
    }
    if (x2 < 0 || x1 >= AN_W)
        return;
    if (x1 < 0)
        x1 = 0;
    if (x2 >= AN_W)
        x2 = AN_W - 1;

    b1 = (int16_t)((uint16_t)x1 >> 3);
    b2 = (int16_t)((uint16_t)x2 >> 3);
    lm = an_left[x1 & 7];
    rm = an_right[x2 & 7];
    p = SCREEN + (uint16_t)y * AN_STRIDE + (uint16_t)b1;

    if (b1 == b2) {
        src = an_patt_byte(patrow, b1);
        v = *p;
        v = an_apply(v, src, (uint8_t)(lm & rm), mode, pen);
        *p = v;
        return;
    }
    src = an_patt_byte(patrow, b1);
    v = *p;
    v = an_apply(v, src, lm, mode, pen);
    *p = v;
    for (b = (int16_t)(b1 + 1); b < b2; b++) {
        p++;
        src = an_patt_byte(patrow, b);
        v = *p;
        v = an_apply(v, src, 0xFF, mode, pen);
        *p = v;
    }
    p++;
    src = an_patt_byte(patrow, b2);
    v = *p;
    v = an_apply(v, src, rm, mode, pen);
    *p = v;
}

void antic_vline(int16_t x, int16_t y1, int16_t y2, uint16_t mask,
                 int16_t mode, uint8_t pen)
{
    int16_t y;
    uint8_t bit;

    if (x < 0 || x >= AN_W)
        return;
    if (y1 > y2) {
        y = y1; y1 = y2; y2 = y;
    }
    if (y1 < 0)
        y1 = 0;
    if (y2 >= AN_H)
        y2 = AN_H - 1;
    for (y = y1; y <= y2; y++) {
        /* the style is anchored to the screen's grid, as a horizontal
         * one is: pixel y takes bit 15 - (y & 15) */
        bit = (uint8_t)((mask >> (15 - (y & 15))) & 1);
        antic_patt_span(x, x, y, bit ? 0xFFFF : 0x0000, mode, pen);
    }
}

/* ---- vro_cpyfm, screen to screen -------------------------------------- */

void antic_copy(int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                int16_t w, int16_t h)
{
    int16_t y, i, n;
    int16_t back_y, back_x;

    if (w <= 0 || h <= 0)
        return;
    back_y = (int16_t)(dy > sy);            /* rows bottom-up */
    back_x = (int16_t)(dx > sx);            /* and right to left */

    if (((sx ^ dx) & 7) == 0 && (sx & 7) == 0 && (w & 7) == 0) {
        /* byte aligned at both ends and a whole number of bytes wide */
        n = (int16_t)((uint16_t)w >> 3);
        for (y = 0; y < h; y++) {
            int16_t syy = back_y ? (int16_t)(sy + h - 1 - y) : (int16_t)(sy + y);
            int16_t dyy = back_y ? (int16_t)(dy + h - 1 - y) : (int16_t)(dy + y);
            volatile uint8_t *s, *d;
            uint8_t v;

            if (syy < 0 || syy >= AN_H || dyy < 0 || dyy >= AN_H)
                continue;
            s = SCREEN + (uint16_t)syy * AN_STRIDE + ((uint16_t)sx >> 3);
            d = SCREEN + (uint16_t)dyy * AN_STRIDE + ((uint16_t)dx >> 3);
            if (back_x) {
                s += n - 1;
                d += n - 1;
                for (i = 0; i < n; i++) {
                    v = *s;
                    *d = v;
                    s--;
                    d--;
                }
            } else {
                for (i = 0; i < n; i++) {
                    v = *s;
                    *d = v;
                    s++;
                    d++;
                }
            }
        }
        return;
    }
    /* Unaligned, or a width that is not a whole number of bytes: pixel by
     * pixel, in the direction that keeps an overlapping move safe.  The
     * VBXE driver falls back the same way when the blitter's alignment
     * rules are not met, so the two agree about what a move does. */
    for (y = 0; y < h; y++) {
        int16_t syy = back_y ? (int16_t)(sy + h - 1 - y) : (int16_t)(sy + y);
        int16_t dyy = back_y ? (int16_t)(dy + h - 1 - y) : (int16_t)(dy + y);

        for (i = 0; i < w; i++) {
            int16_t sxx = back_x ? (int16_t)(sx + w - 1 - i) : (int16_t)(sx + i);
            int16_t dxx = back_x ? (int16_t)(dx + w - 1 - i) : (int16_t)(dx + i);
            uint8_t v = antic_get_pixel(sxx, syy);
            antic_plot(dxx, dyy, v);
        }
    }
}

/* ---- the mouse cursor -------------------------------------------------- */

#define AN_CUR_W  16
#define AN_CUR_NB 3                     /* 16 pixels at any shift: 3 bytes */

static uint8_t an_cur_buf[AN_CUR_NB * AN_CUR_W];
static int16_t an_cur_bx, an_cur_y, an_cur_nb, an_cur_nr;
static uint8_t an_cur_valid;

void antic_cursor_save(int16_t x, int16_t y)
{
    int16_t bx0 = (int16_t)((uint16_t)(x < 0 ? 0 : x) >> 3);
    int16_t bx1 = (int16_t)((uint16_t)(x + AN_CUR_W - 1) >> 3);
    int16_t y0 = y, y1 = (int16_t)(y + AN_CUR_W - 1);
    int16_t r, c;

    an_cur_valid = 0;
    if (x >= AN_W || y >= AN_H || x + AN_CUR_W <= 0 || y + AN_CUR_W <= 0)
        return;
    if (bx1 > AN_STRIDE - 1)
        bx1 = AN_STRIDE - 1;
    if (y0 < 0)
        y0 = 0;
    if (y1 > AN_H - 1)
        y1 = AN_H - 1;
    if (bx1 < bx0 || y1 < y0)
        return;
    an_cur_bx = bx0;
    an_cur_y = y0;
    an_cur_nb = (int16_t)(bx1 - bx0 + 1);
    an_cur_nr = (int16_t)(y1 - y0 + 1);
    for (r = 0; r < an_cur_nr; r++) {
        volatile uint8_t *p = SCREEN + (uint16_t)(y0 + r) * AN_STRIDE
                              + (uint16_t)bx0;
        for (c = 0; c < an_cur_nb; c++) {
            uint8_t v = *p;
            an_cur_buf[r * AN_CUR_NB + c] = v;
            p++;
        }
    }
    an_cur_valid = 1;
}

void antic_cursor_restore(void)
{
    int16_t r, c;

    if (!an_cur_valid)
        return;
    for (r = 0; r < an_cur_nr; r++) {
        volatile uint8_t *p = SCREEN + (uint16_t)(an_cur_y + r) * AN_STRIDE
                              + (uint16_t)an_cur_bx;
        for (c = 0; c < an_cur_nb; c++) {
            uint8_t v = an_cur_buf[r * AN_CUR_NB + c];
            *p = v;
            p++;
        }
    }
    an_cur_valid = 0;
}

void antic_cursor_discard(void)
{
    an_cur_valid = 0;
}

void antic_cursor_paint(int16_t x, int16_t y, const uint16_t *mask,
                        const uint16_t *data, uint8_t bg, uint8_t fg)
{
    int16_t r, c;

    for (r = 0; r < AN_CUR_W; r++) {
        uint16_t m = mask[r], d = data[r];
        for (c = 0; c < AN_CUR_W; c++) {
            uint16_t bit = (uint16_t)(0x8000u >> c);
            if (d & bit)
                antic_plot((int16_t)(x + c), (int16_t)(y + r), fg);
            else if (m & bit)
                antic_plot((int16_t)(x + c), (int16_t)(y + r), bg);
        }
    }
}
