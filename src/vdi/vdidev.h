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
