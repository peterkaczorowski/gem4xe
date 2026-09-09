/* vdidev.h -- the seam between the VDI and the surface it draws on.
 *
 * Everything above this line is device-INdependent: the dispatcher, the
 * workstation state, clipping, attributes, text metrics, the inquiries.
 * Everything below it knows what a pixel is made of.  Two devices
 * implement it:
 *
 *   src/vdi/dev_vbxe.c   640x240, 4bpp, VRAM on the 1.79 MHz chip bus,
 *                        so every primitive compiles a BLIT LIST and the
 *                        CPU touches pixels only where the blitter
 *                        genuinely cannot help;
 *   src/vdi/dev_antic.c  320x168, 1bpp, plain motherboard RAM the
 *                        accelerator writes at full speed, so every
 *                        primitive writes BYTES and there is nothing to
 *                        flush.
 *
 * The two are chosen at LINK time, not at run time.  The geometry is
 * still compile-time (SCR_W and friends), and making it a variable would
 * touch every clip and every stride in a 3,676-line file that 49
 * conformance cases stand on; one binary that finds no VBXE and falls
 * back is the better product and is the step after this one, not
 * instead of it (docs/phase32.md).
 *
 * THE PEN IS THE VDI'S, not the hardware's.  A caller passes the pen it
 * was given and the device maps it -- through map_col on VBXE, and to
 * nothing at all on a device with two colours.  That is the difference
 * between a seam and a leak: the VDI has no business knowing that one
 * device has a palette and the other has a luminance.
 */
#ifndef GEM4XE_VDIDEV_H
#define GEM4XE_VDIDEV_H

#include "vdi.h"

/* THE GEOMETRY COMES FROM THE DEVICE.  vdi.c used to include vbxe.h for
 * SCR_W, SCR_H and SCR_STRIDE, which is the last place the VBXE leaked
 * into code that is meant to be portable.  Now the device that is being
 * linked says what they are, and the names stay so that nothing else has
 * to change. */
#ifdef GEM4XE_DEV_ANTIC
#  include "../antic/antic.h"
#  define SCR_W       AN_W
#  define SCR_H       AN_H
#  define SCR_STRIDE  AN_STRIDE
   /* Atari's condensed face (bios/fnt_st_6x6.c), which is what the ST
    * uses for icon labels in low resolution -- GEM's own answer to a
    * screen short of pixels.  6 wide is 53 columns here where 8 would be
    * 40, and 6 tall is 28 rows where 8 would be 21: more of both, and a
    * face designed to be read at that size rather than one squeezed. */
#  define FONT_W        6
#  define FONT_H        6
#  define FONT_TOP      4     /* Fonthead.top: baseline to top of cell */
#  define FONT_ASCENT   4
#  define FONT_HALF     3
#  define FONT_DESCENT  1
#  define FONT_BOTTOM   1
#  define FONT_POINT    8
#else
#  include "../vbxe/vbxe.h"
#  define FONT_W        8
#  define FONT_H        8
#  define FONT_TOP      6     /* Fonthead.top: baseline to top of cell */
#  define FONT_ASCENT   6     /* and the rest of the head EmuTOS records */
#  define FONT_HALF     4     /* for this face, which vst_alignment needs */
#  define FONT_DESCENT  1
#  define FONT_BOTTOM   1
#  define FONT_POINT    9
#endif

/* A raster form as the copy sees it: an MFDB resolved, or the screen.
 * `base` is 24-bit because on one device it is a VRAM address and on the
 * other a bank-$00 one, and the VDI does not care which. */
typedef struct {
    uint32_t base;
    uint16_t stride;
    WORD     w, h;
    WORD     screen;
} RFORM;

/* A solid rectangle in the current pen, corners inclusive, already
 * clipped by the caller.  MD_REPLACE's and MD_ERASE's shape; the mode
 * itself is decided above the seam, because that decision is the same
 * whatever the pixels are made of. */
void dev_fill_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen);

/* ...and the same rectangle inverted, which is MD_XOR. */
void dev_xor_rect(WORD x1, WORD y1, WORD x2, WORD y2);

/* A rectangle in the current fill pattern and writing mode, corners
 * inclusive and already clipped.  The pattern's ROWS come from the VDI
 * (pat_bits) because which table they are in is the workstation's
 * business; how they reach the screen is the device's. */
void dev_patt_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen);

/* A horizontal or vertical line in the current pen and mode, styled by
 * `mask` -- already anchored to the screen's grid by style_anchor, so
 * both devices draw a dash in the same place.  The device clips it. */
void dev_style_line(WORD x1, WORD y1, WORD x2, WORD y2, UWORD mask);

/* ---- text -------------------------------------------------------------
 * One glyph of the system font with its top-left at (cx, cy), in the
 * current writing mode and text colour.  `overlay` is 0 for the letter
 * itself and 1 for the second pass that thickens it -- which is not the
 * same as "draw it transparently", because in XOR and erase modes the
 * overlay is drawn in THAT mode too and the device may take a different
 * path for it.  The device clips the cell: a partial glyph is not
 * something GEM asks for, and whether a whole one can be blitted is the
 * device's question, not the VDI's. */
void dev_glyph(WORD ch, WORD cx, WORD cy, WORD overlay);

/* The font strip changed (src/vdi/font.c loaded another face).  A device
 * that keeps the glyphs in some other form re-derives it here: VBXE
 * expands all 256 into 4bpp masks in VRAM at both x parities, 8 KB of
 * them; ANTIC blits the strip as it stands and this is empty. */
void dev_font_changed(void);

/* A ONE-PLANE source expanded into the device's colours -- how the AES
 * draws icons and glyph masks (vrt_cpyfm).  `ink` and `bg` are VDI pens.
 * The source is a near pointer because a form lives in bank $00. */
void dev_raster_1bpp(const uint8_t *bits, uint16_t stride,
                     WORD sx, WORD sy, WORD w, WORD h,
                     WORD dx, WORD dy, WORD mode, WORD ink, WORD bg);

/* ---- the pointer -----------------------------------------------------
 * The VDI owns WHERE it is: the hot spot, the nesting count that
 * v_show_c and v_hide_c keep, and whether it is currently drawn.  The
 * device owns what it is MADE of and what was underneath it.
 *
 * `mask` and `data` are the sixteen rows GEM's MFORM carries, bit 15
 * leftmost; `bg` and `fg` are VDI pens.  A device keeps whatever it
 * needs of them -- VBXE expands the pair into 4bpp strips in VRAM at
 * both x parities, which is a kilobyte written once per form; ANTIC
 * blits the rows as they stand. */
void dev_cursor_form(WORD bg, WORD fg, const UWORD *mask, const UWORD *data);

/* Save what is under (cx, cy) and paint the form over it, done and on
 * the screen by the time this returns. */
void dev_cursor_show(WORD cx, WORD cy);

/* Put back what was under it. */
void dev_cursor_hide(void);

/* Forget what was under it WITHOUT putting it back -- v_clrwk's case,
 * where restoring would stamp stale pixels onto a cleared screen. */
void dev_cursor_discard(void);

/* A DIAGONAL line, styled the same way.  It is separate from
 * dev_style_line because on one device the two could not be less alike:
 * a horizontal run is a single patterned blit and a diagonal is the one
 * primitive the blitter cannot accelerate at all, so it goes pixel by
 * pixel through the MEMAC window.  The VDI decides which it is -- that
 * is geometry, and the same either way -- and the device decides what
 * that costs.
 *
 * The AES never draws one: every box and frame it makes is axis-aligned
 * (it calls neither the GDPs nor v_fillarea), so this is an
 * application's path, and it is slow on both devices for different
 * reasons. */
void dev_line_diag(WORD x1, WORD y1, WORD x2, WORD y2, UWORD mask);

/* ---- rasters ----------------------------------------------------------
 * The screen described as a form: an MFDB with a null address means "the
 * screen", and only the device knows where that is and how wide a row of
 * it is. */
void dev_screen_form(RFORM *f);

/* vro_cpyfm's copy, both rectangles already clipped by the VDI and known
 * to be inside their forms.  Source and destination may be the same form
 * and may overlap, so the device picks its direction.  VBXE takes one
 * blit when the two ends share their alignment and the width is a whole
 * number of bytes, and falls to pixel-by-pixel otherwise, because its
 * blitter has no shifter. */
void dev_copy_form(const RFORM *src, WORD sx, WORD sy,
                   const RFORM *dst, WORD dx, WORD dy, WORD w, WORD h);

/* The off-screen area the AES saves under menus and dialogs into,
 * described as an MFDB.  Where it is and what shape it has are entirely
 * the device's: VRAM on one, and there is no such thing to spare in bank
 * $00 on the other. */
void dev_save_form(MFDB *m);

/* ---- the rest ---------------------------------------------------------
 * The screen, cleared.  The pointer is the VDI's to put back afterwards.
 */
void dev_clear_screen(void);

/* One pixel read back: `value` is what the device stores there and is
 * what v_get_pixel reports as intout[0], `pen` the VDI pen it maps to.
 * Both, because the VDI's contract asks for both and only the device can
 * answer either. */
void dev_get_pixel(WORD x, WORD y, WORD *value, WORD *pen);

/* The value this device stores for a VDI pen -- the units dev_get_pixel
 * and dev_row_pixel answer in. */
WORD dev_pen_value(WORD pen);

/* One screen row into SCR_STRIDE bytes, and a pixel out of it.  This is
 * the paint bucket's, and it is the one primitive that has to look at
 * what is already on the screen a whole row at a time; keeping the row
 * in the DEVICE's packed form and asking for pixels out of it is what
 * stops a 640-pixel row costing 640 bytes of a 2 KB stack. */
void dev_read_row(WORD y, uint8_t *px);
WORD dev_row_pixel(const uint8_t *px, WORD x);

/* How many colours the device can show at once.  v_opnwk reports it, so
 * it is what the AES and every application lay themselves out for -- and
 * it is why this is a device question and not a constant: a two-colour
 * workstation also has to say it cannot do colour at all, the way the
 * ST's monochrome one does. */
WORD dev_colours(void);

/* ...and how many PLANES that is.  The AES reads it out of vq_extnd and
 * sizes its menu save buffer from it, so a device that lies here wastes
 * memory or loses part of a menu. */
WORD dev_planes(void);

/* The palette: sixteen VDI pens' worth of 8-bit RGB, or one of them.
 * The device permutes into whatever order its hardware wants -- and a
 * device with two colours takes what it can of it. */
void dev_palette_all(const uint8_t *rgb);
void dev_palette_one(WORD pen, const uint8_t *rgb);

/* Whatever the device precomputed about the current pattern or pen is
 * stale.  The VDI calls this when a workstation is selected or reset or
 * when the user pattern is replaced: it cannot know WHETHER a device
 * caches anything, only that the ground has moved.  VBXE keeps the
 * pattern expanded to 4bpp in VRAM with a line's strip beside it and
 * throws both away; ANTIC caches nothing and this is empty. */
void dev_invalidate(void);

/* Whatever the device has queued, done and on the screen.  A blit list
 * started and waited for on VBXE; nothing at all on ANTIC, where the
 * write WAS the drawing. */
void dev_flush(void);

#endif /* GEM4XE_VDIDEV_H */
