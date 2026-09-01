/* pointer.c -- pointing-device back ends behind the seam in pointer.h. */
#include "pointer.h"
#include "../vbxe/vbxe.h"

PTR_STATE ptr_state;

#define PORTA  (*(volatile uint8_t *)0xD300)   /* PIA: joystick 0 in low nibble */
#define TRIG0  (*(volatile uint8_t *)0xD010)   /* GTIA: 0 = pressed             */
#define TRIG1  (*(volatile uint8_t *)0xD011)
#define POT0   (*(volatile uint8_t *)0xD200)
#define POT1   (*(volatile uint8_t *)0xD201)
#define ALLPOT (*(volatile uint8_t *)0xD208)
#define POTGO  (*(volatile uint8_t *)0xD20B)

#define POT_MAX 228     /* POKEY's pot counter tops out here on a real Atari */

static uint8_t last_x, last_y;      /* previous quadrature phase per axis */
static WORD    pot_pending;

/* Classic 2-bit Gray-code quadrature table, indexed by (prev << 2) | now.
 * The four "impossible" entries (a two-step jump, i.e. a missed sample) are 0
 * rather than a guess: on a missed count it is better to drop motion than to
 * invent it in the wrong direction. */
static const signed char qdec[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0
};

WORD ptr_decode_quad(uint8_t prev, uint8_t now)
{
    return qdec[((prev & 3) << 2) | (now & 3)];
}

static void clamp(void)
{
    if (ptr_state.x < 0) ptr_state.x = 0;
    if (ptr_state.y < 0) ptr_state.y = 0;
    if (ptr_state.x > SCR_W - 1) ptr_state.x = SCR_W - 1;
    if (ptr_state.y > SCR_H - 1) ptr_state.y = SCR_H - 1;
}

void ptr_warp(WORD x, WORD y)
{
    ptr_state.x = x;
    ptr_state.y = y;
    clamp();
}

void ptr_init(ptr_kind kind, WORD x, WORD y)
{
    ptr_state.kind = (WORD)kind;
    ptr_state.buttons = 0;
    ptr_warp(x, y);
    last_x = last_y = 0;
    pot_pending = 0;
    if (kind == PTR_TABLET) {
        POTGO = 0;                  /* start the first scan */
        pot_pending = 1;
    }
}

/* Relative devices.  PORTA's low nibble carries the four quadrature lines; the
 * pin order is what differs between an ST mouse and an Amiga one.
 *
 *   ST     b0 = XB, b1 = XA, b2 = YA, b3 = YB
 *   Amiga  b0 = YB, b1 = YA, b2 = XB, b3 = XA   (H and V swapped)
 *
 * A CX80 trak-ball in trak-ball mode presents direction-and-pulse rather than
 * true quadrature, but the same table works: the pulse line toggles and the
 * direction line selects which way the phase walks. */
static void poll_relative(void)
{
    uint8_t p = (uint8_t)(PORTA & 0x0F);
    uint8_t qx, qy;

    if (ptr_state.kind == PTR_AMIGA_MOUSE) {
        qy = (uint8_t)(p & 3);
        qx = (uint8_t)((p >> 2) & 3);
    } else {
        qx = (uint8_t)(p & 3);
        qy = (uint8_t)((p >> 2) & 3);
    }
    ptr_state.x = (WORD)(ptr_state.x + ptr_decode_quad(last_x, qx));
    ptr_state.y = (WORD)(ptr_state.y + ptr_decode_quad(last_y, qy));
    last_x = qx;
    last_y = qy;
    clamp();
    ptr_state.buttons = (WORD)(((TRIG0 & 1) ? 0 : 1) | ((TRIG1 & 1) ? 0 : 2));
}

/* Absolute devices.  POKEY's pot scan takes a full frame in normal mode, so
 * this reads the previous scan's result and immediately starts the next --
 * one position per frame, which is all a GUI pointer needs.
 *
 * The counter tops out around 228, against a 640x240 screen: roughly 2.8
 * screen pixels per step horizontally and about 1:1 vertically.  Good enough
 * to hit a menu item, coarse for drawing.  Callers wanting smooth motion
 * should filter, not scale harder. */
static void poll_absolute(void)
{
    uint16_t px, py;
    if (pot_pending && (ALLPOT & 3) != 0)
        return;                     /* scan still running; keep the old value */
    px = POT0;
    py = POT1;
    if (px > POT_MAX) px = POT_MAX;
    if (py > POT_MAX) py = POT_MAX;
    ptr_state.x = (WORD)(((uint32_t)px * (SCR_W - 1)) / POT_MAX);
    ptr_state.y = (WORD)(((uint32_t)py * (SCR_H - 1)) / POT_MAX);
    clamp();
    ptr_state.buttons = (WORD)(((TRIG0 & 1) ? 0 : 1) | ((TRIG1 & 1) ? 0 : 2));
    POTGO = 0;                      /* start the next scan */
    pot_pending = 1;
}

void ptr_poll(void)
{
    switch (ptr_state.kind) {
    case PTR_ST_MOUSE:
    case PTR_AMIGA_MOUSE:
    case PTR_TRAKBALL:
        poll_relative();
        break;
    case PTR_TABLET:
        poll_absolute();
        break;
    default:
        break;
    }
}
