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
