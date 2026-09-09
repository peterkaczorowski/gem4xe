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

/* A rectangle in the current pattern and writing mode.  Where the VBXE
 * device expands the pattern into VRAM once and lets the blitter walk
 * it, this one asks the VDI for a row per scanline and writes it -- the
 * same pattern, arriving a different way, which is exactly what the seam
 * is for.  pat_bits() takes the screen row and applies the pattern's own
 * row mask, so the phase is the VDI's and not this file's. */
void dev_patt_rect(WORD x1, WORD y1, WORD x2, WORD y2, WORD pen)
{
    WORD mode = (WORD)(vwk.wrt_mode + 1);
    WORD y;

    for (y = y1; y <= y2; y++)
        antic_patt_span((int16_t)x1, (int16_t)x2, (int16_t)y,
                        pat_bits(y), (int16_t)mode, (uint8_t)(pen ? 1 : 0));
}

/* A styled horizontal or vertical line.  The mask arrives anchored to
 * the screen's 16-pixel grid, so a horizontal line IS a patterned span
 * of one row and a vertical one is the same mask indexed by y. */
void dev_style_line(WORD x1, WORD y1, WORD x2, WORD y2, UWORD mask)
{
    WORD mode = (WORD)(vwk.wrt_mode + 1);
    uint8_t pen = (uint8_t)(vwk.line_color ? 1 : 0);
    UWORD bits;
    WORD a, b;

    if (y1 == y2) {
        bits = style_anchor(mask, x1, (x2 >= x1) ? 1 : -1);
        a = x1;  b = x2;  order(&a, &b);
        if (!clip_rect(&a, &y1, &b, &y2))
            return;
        antic_patt_span((int16_t)a, (int16_t)b, (int16_t)y1, bits,
                        (int16_t)mode, pen);
        return;
    }
    bits = style_anchor(mask, y1, (y2 >= y1) ? 1 : -1);
    a = y1;  b = y2;  order(&a, &b);
    if (!clip_rect(&x1, &a, &x2, &b))
        return;
    antic_vline((int16_t)x1, (int16_t)a, (int16_t)b, bits,
                (int16_t)mode, pen);
}

/* Nothing is precomputed here, so there is nothing to throw away. */
void dev_invalidate(void)
{
}
