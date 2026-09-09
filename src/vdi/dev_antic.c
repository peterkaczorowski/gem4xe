/* dev_antic.c -- the ANTIC side of the seam (vdidev.h).
 *
 * 320x168 at 1bpp in plain motherboard RAM, which the accelerator writes
 * at full speed.  So every primitive here writes BYTES, there is no list
 * to compile and nothing to flush -- the write WAS the drawing.  That is
 * the whole difference from src/vdi/dev_vbxe.c, and the reason the seam
 * is drawn where it is: above this line the VDI decides WHAT to draw,
 * below it the device decides how, and neither has to know the other's
 * bus.
 *
 * THE PEN.  This device has two colours, so a VDI pen is 0 or 1 and
 * there is no palette to map through.  A pen the VDI thinks is one of
 * sixteen arrives here as whatever it is; anything but 0 is ink, which
 * is the only sensible reading of "colour 7" on a device that has two.
 */
#include "vdi.h"
#include "vdidev.h"
#include "../antic/antic.h"

void dev_fill_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
{
    antic_rect_mode((int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2,
                    1 /* MD_REPLACE */, (uint8_t)(pen ? 1 : 0));
}

void dev_xor_rect(WORD x1, WORD y1, WORD x2, WORD y2)
{
    antic_rect_mode((int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2,
                    3 /* MD_XOR */, 1);
}

void dev_flush(void)
{
    /* nothing: the bytes are already on the screen */
}
