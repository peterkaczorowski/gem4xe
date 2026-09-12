/* dev_stub.c -- a VDI device, for the simulator builds in tests/host.
 *
 * src/vdi/pointer.c clamps to the screen, and since the device seam became
 * a runtime one (docs/phase34.md) the screen's size is a field of the table
 * `vdev` points at rather than a constant.  On the target that table is
 * src/vdi/dev_vbxe.c's or dev_antic.c's; here it is this, because linking
 * either one would drag in a blitter or a display list to test a quadrature
 * decoder.
 *
 * THE NUMBERS ARE THE VBXE'S, and they have to be: tools/vdiref.py -- the
 * reference these tests compare against -- is written to that surface, so a
 * stub that said anything else would make the two disagree about where a
 * tablet's rightmost reading lands.  The struct comes from vdidev.h, so a
 * field that moves fails to compile here rather than lying.
 *
 * Every call is null.  Nothing in pointer.c reaches through the seam; if
 * something ever does, it will say so by taking the machine to address
 * zero, which is a better answer than a stub that quietly does nothing.
 */
#include "portab.h"
#include "vdi/vdidev.h"
#include "vbxe/vbxe.h"

static const VDIDEV host_dev = {
    VB_W, VB_H, VB_STRIDE,
    8, 8,                       /* the 8x8 system font's cell... */
    6, 6, 4, 1, 1, 9,           /* ...top, ascent, half, descent, bottom, pt */
    0,                          /* no face: nothing here draws a glyph */
};

const VDIDEV FAR *vdev = (const VDIDEV FAR *)&host_dev;
