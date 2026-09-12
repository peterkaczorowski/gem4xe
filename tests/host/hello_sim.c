/* hello_sim.c -- the kit's example, run against a recorder.
 *
 * The example is the file an author copies first, so the order of what
 * it does matters more than most code: announce yourself, ask the AES
 * for the character cell, open a workstation, draw, close, leave.  A
 * program that draws before v_opnvwk or forgets appl_exit is exactly
 * the mistake an example teaches.
 *
 * So this replaces the three call gates with a recorder that answers
 * plausibly -- a workstation handle, a screen size -- links it with
 * example/hello.c and the bindings, and tests/host/test_sdk.py reads
 * the order back out of the simulator.  It proves the shape, not the
 * pixels; the pixels are test-m11's business, and the kit rebuilds
 * that program byte for byte.
 */
#include "gem.h"

#define NREC 64

WORD hello_n;
WORD hello_op[NREC];            /* VDI opcodes as they are, AES over 1000 */
WORD hello_ret;

SIMPLE_CALL void vdi_call(VDIPB FAR *pb)
{
    (void)pb;
    if (hello_n < NREC)
        hello_op[hello_n++] = contrl[0];
    if (contrl[0] == 100 || contrl[0] == 1) {   /* a workstation opens */
        contrl[6] = 1;
        intout[0] = 639;                        /* the last pixel across */
        intout[1] = 239;                        /* ...and down */
    }
}

SIMPLE_CALL void aes_call(AESPB FAR *pb)
{
    (void)pb;
    if (hello_n < NREC)
        hello_op[hello_n++] = (WORD)(1000 + control[0]);
    int_out[0] = 1;
}

SIMPLE_CALL void dos_call(GDPB FAR *pb)
{
    if (hello_n < NREC)
        hello_op[hello_n++] = (WORD)(2000 + pb->fn);
    pb->ret = 0;
}
