/* antic.h -- the secondary display: stock ANTIC and GTIA, no VBXE.
 *
 * ANTIC mode F (GRAPHICS 8): 320 pixels a line, ONE bit each, 40 bytes a
 * line, scanline-sequential with no character-cell indirection.  It is
 * the surface the VDI's second driver rasterises into, and the thing
 * that stops the VBXE's assumptions leaking into portable code.
 *
 * WHERE IT LIVES, AND WHY IT IS 168 LINES.  ANTIC fetches over the chip
 * bus with 16-bit addresses, so the framebuffer has to be in bank $00
 * and in motherboard RAM -- not the accelerator's SRAM, which ANTIC
 * cannot see, and not the banked window $4000-$7FFF, which a DOS
 * switches out from under itself while it services a call and which
 * ANTIC would then happily display.  What is left is the region
 * src/gem4xe.scm reserves for the VBXE's MEMAC window, $8000-$9BFF,
 * which on a machine with no VBXE is simply free: 7,168 bytes, bounded
 * above by SpartaDOS X's screen at $9C00.  Take the display list off the
 * front and 168 lines of 40 bytes is what fits.  The ceiling is memory,
 * not ANTIC.
 *
 *     $8000-$80AE   the display list, 175 bytes
 *     $8100-$9B3F   the framebuffer, 168 x 40 = 6,720 bytes
 *
 * THE FRAMEBUFFER IS LINEAR, which it has no right to be: ANTIC's memory
 * counter wraps at a 4 KB boundary, so a screen that crosses one usually
 * needs its lines re-based and the rasteriser needs to know where.  Here
 * the base is chosen so that the crossing falls exactly BETWEEN two
 * lines -- $8100 + 96*40 is $9000 to the byte -- so one extra LMS at
 * line 96 re-points ANTIC at the address the linear formula already
 * gives, and every line is base + L*40 after all.
 *
 * COLOUR.  Mode F is a hires mode: a set bit takes COLPF1's LUMINANCE on
 * COLPF2's hue, so there are two colours and one of them owns the hue.
 * The VDI's pen 0 is the background and pen 1 the foreground; anything
 * else is device-dependent nonsense here and says so.
 */
#ifndef GEM4XE_ANTIC_H
#define GEM4XE_ANTIC_H

#include <stdint.h>

#define AN_DLIST    0x8000U             /* the display list             */
#define AN_SCREEN   0x8100U             /* the framebuffer              */
#define AN_W        320                 /* pixels across                */
#define AN_H        168                 /* ...and down                  */
#define AN_STRIDE   (AN_W / 8)          /* 40 bytes a line              */
#define AN_BYTES    ((uint16_t)AN_STRIDE * AN_H)
#define AN_SPLIT    96                  /* the line the 4 KB crossing
                                         * falls in front of            */

/* ANTIC and GTIA, the registers this file drives. */
#define AN_DMACTL   0xD400U
#define AN_DLISTL   0xD402U
#define AN_COLPF1   0xD017U
#define AN_COLPF2   0xD018U
#define AN_COLBK    0xD01AU
/* ...and the OS shadows, written too, so that an OS VBI that is running
 * puts back what this file set rather than what the OS last wanted. */
#define AN_SDMCTL   0x022FU
#define AN_SDLSTL   0x0230U
#define AN_COLOR1   0x02C5U
#define AN_COLOR2   0x02C6U
#define AN_COLOR4   0x02C8U

/* Build the display list and turn the screen on.  `fg` and `bg` are
 * Atari colour bytes: fg's luminance and bg's hue are what show. */
void antic_init(uint8_t fg, uint8_t bg);

/* The screen off again, and the OS's own display list back. */
void antic_off(void);

/* Every byte of the framebuffer to `value`. */
void antic_clear(uint8_t value);

/* One pixel, and a horizontal run from x1 to x2 inclusive; `set` paints
 * the foreground, 0 the background.  Both clip to the screen. */
void antic_plot(int16_t x, int16_t y, uint8_t set);
void antic_hline(int16_t x1, int16_t x2, int16_t y, uint8_t set);

/* A filled rectangle, corners inclusive. */
void antic_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t set);

#endif /* GEM4XE_ANTIC_H */
