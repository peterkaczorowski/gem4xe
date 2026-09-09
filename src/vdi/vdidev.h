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

/* Whatever the device has queued, done and on the screen.  A blit list
 * started and waited for on VBXE; nothing at all on ANTIC, where the
 * write WAS the drawing. */
void dev_flush(void);

#endif /* GEM4XE_VDIDEV_H */
