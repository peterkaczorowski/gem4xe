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
