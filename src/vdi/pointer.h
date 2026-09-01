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
 * polling loop is enough, and they work today.
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
    PTR_TABLET          /* Atari CX77 touch tablet / KoalaPad, absolute */
} ptr_kind;

typedef struct {
    WORD x, y;          /* absolute, always clamped to the screen */
    WORD buttons;       /* bit 0 = left, bit 1 = right */
    WORD kind;
} PTR_STATE;

extern PTR_STATE ptr_state;

void ptr_init(ptr_kind kind, WORD x, WORD y);
void ptr_poll(void);            /* refresh ptr_state from the hardware */
void ptr_warp(WORD x, WORD y);  /* force a position (vq_mouse / tests)  */

/* Exposed for the device-layer tests, which drive synthetic quadrature rather
 * than relying on the emulator's input system. */
WORD ptr_decode_quad(uint8_t prev, uint8_t now);

#endif /* GEM4XE_POINTER_H */
