/* pointer.h -- the pointing-device seam.
 *
 * Everything above this produces and consumes ONE thing: an absolute screen
 * position plus button state.  Below it sit devices that disagree
 * fundamentally about how position works:
 *
 *   ST mouse, Amiga mouse, CX80 trak-ball   RELATIVE -- quadrature on the
 *                                           joystick direction lines, read
 *                                           through PORTA, deltas integrated
 *   Touch tablet (CX77), KoalaPad           ABSOLUTE -- POT0/POT1, one
 *                                           position per POKEY pot scan
 *   mouSTer (USB adapter) in XEM1 mode      RELATIVE, but COUNTED BY THE
 *                                           DEVICE -- 7-bit position
 *                                           counters on the POT lines, so
 *                                           one pot scan per frame is enough
 *
 * GEM's own design already assumes this seam: `vex_motv` hands the AES an
 * absolute position and `v_locator` is a device-independent locator, so
 * nothing above here ever learns which device is fitted.
 *
 * ⚠ A relative device needs frequent polling or it loses quadrature counts --
 * the classic Atari 8-bit mouse complaint -- which means an interrupt.
 * gem4xe currently runs with NMI and IRQ switched off (see src/crt_atari.s:
 * the 65816's native-mode vectors are not the ones the Atari OS ROM fills), so
 * the MOUSE back ends are not usable until native-mode vector stubs exist.
 * The absolute back ends have no such problem: one POT read per frame from a
 * polling loop is enough, and they work today.  So does XEM1, where the
 * adapter does the counting: the host reads a position, not a phase, and a
 * frame's movement is a difference of two readings.  Its one demand is a
 * POTGO at a steady cadence (the sample driver says within ~100 cycles,
 * every frame), which is what a VBI gives and a polling loop only
 * approximates -- so it, too, is better off once the vectors exist.
 *
 * XEM1 is decoded from the mouSTer firmware's own sample driver
 * (Mad-Pascal samples/a8/mouSTer/vbl.asm), not from its prose, which calls
 * the readings "relative coordinate changes": the code subtracts the
 * previous reading, so they are counters.  Altirra does not emulate the
 * adapter; the decode is checked against tools/vdiref.py in the compiler's
 * simulator (tests/host/test_pointer.py) and has NOT met the hardware.
 */
#ifndef GEM4XE_POINTER_H
#define GEM4XE_POINTER_H

#include <stdint.h>
#include "vdi.h"

typedef enum {
    PTR_NONE = 0,
    PTR_ST_MOUSE,       /* Atari ST mouse via a joystick-port adapter */
    PTR_AMIGA_MOUSE,    /* same idea, different pin order */
    PTR_TRAKBALL,       /* Atari CX80 trak-ball, in trak-ball mode */
    PTR_TABLET,         /* Atari CX77 touch tablet / KoalaPad, absolute */
    PTR_XEM1            /* mouSTer, XEM1 mode: probes both ports for it   */
} ptr_kind;

typedef struct {
    WORD x, y;          /* absolute, always clamped to the screen */
    WORD buttons;       /* bit 0 = left, bit 1 = right, bit 2 = middle */
    WORD kind;
    WORD wheel;         /* notches, accumulated; XEM1 only, sign unverified */
} PTR_STATE;

/* ptr_state is the device's record and ptr_seen the copy everything above
 * the seam reads.  They differ because the record can change under a
 * reader: today the test harness rewrites it between frames, later a VBI
 * will, and either can land between a reader's fetch of x and its fetch of
 * y.  The AES then sees a position that never existed -- old x, new y --
 * and answers a rectangle it was never in.  ptr_sample() takes the copy in
 * one piece, so a pass of the input loop sees one instant. */
extern PTR_STATE ptr_state;
extern PTR_STATE ptr_seen;

void ptr_init(ptr_kind kind, WORD x, WORD y);
void ptr_poll(void);            /* refresh ptr_state from the hardware */
void ptr_sample(void);          /* ptr_seen = ptr_state, torn reads retried */
void ptr_warp(WORD x, WORD y);  /* force a position (vq_mouse / tests)  */

/* Exposed for the device-layer tests, which drive synthetic quadrature rather
 * than relying on the emulator's input system. */
WORD ptr_decode_quad(uint8_t prev, uint8_t now);

/* XEM1: a reading is a 7-bit counter sent as 64..191; the movement between
 * two readings is their difference modulo 128, halved (the low bit is
 * noise).  Both exposed for the same reason. */
WORD ptr_xem1_valid(uint8_t pot);
WORD ptr_decode_xem1(uint8_t ref, uint8_t now);

#endif /* GEM4XE_POINTER_H */
