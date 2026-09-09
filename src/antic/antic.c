/* antic.c -- the ANTIC mode F surface.  See antic.h for the map and for
 * why the framebuffer is where it is.
 *
 * Everything here is CPU work.  There is no blitter on this side of the
 * seam, which is the whole difference between the two drivers: the VBXE
 * one compiles blit lists because its VRAM is on a 1.79 MHz bus however
 * fast the CPU runs, and this one writes bytes because its framebuffer
 * is plain motherboard RAM the accelerator reaches at full speed.
 */
#include "antic.h"
#include "../vdi/vdi.h"                 /* the system font, which this
                                         * driver blits as it stands */

/* Character N's row r of the 8x8 face: the strip is 1bpp already and
 * bit 7 is the leftmost pixel, which is what this device wants too. */
static uint8_t an_font_row(uint16_t ch, uint16_t row)
{
    return font8x8[row * FONT_STRIDE + (ch & 0xFF)];
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

void antic_off(void)
{
    REG8(AN_DMACTL) = 0;
    REG8(AN_SDMCTL) = 0;
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
void antic_glyph(uint16_t ch, int16_t x, int16_t y, int16_t mode, uint8_t pen)
{
    volatile uint8_t *p;
    uint16_t row;
    uint8_t shift, g, v;

    if (x < 0 || y < 0 || x + AN_GLYPH_W > AN_W || y + AN_GLYPH_H > AN_H)
        return;
    shift = (uint8_t)(x & 7);
    p = SCREEN + (uint16_t)y * AN_STRIDE + ((uint16_t)x >> 3);

    for (row = 0; row < AN_GLYPH_H; row++) {
        g = an_font_row(ch, row);
        if (shift == 0) {
            v = *p;
            v = an_apply(v, g, 0xFF, mode, pen);
            *p = v;
        } else {
            uint8_t hi = (uint8_t)(g >> shift);
            uint8_t lo = (uint8_t)(g << (8 - shift));
            uint8_t mh = (uint8_t)(0xFF >> shift);
            uint8_t ml = (uint8_t)(0xFF << (8 - shift));
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
