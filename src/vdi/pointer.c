/* pointer.c -- pointing-device back ends behind the seam in pointer.h. */
#include "pointer.h"
#include "../vbxe/vbxe.h"

PTR_STATE ptr_state;
PTR_STATE ptr_seen;

#define PORTA  (*(volatile uint8_t *)0xD300)   /* PIA: joystick 0 in low nibble */
#define TRIG0  (*(volatile uint8_t *)0xD010)   /* GTIA: 0 = pressed             */
#define TRIG1  (*(volatile uint8_t *)0xD011)
#define POT0   (*(volatile uint8_t *)0xD200)
#define POT1   (*(volatile uint8_t *)0xD201)
#define POT(n) (*(volatile uint8_t *)(0xD200 + (n)))
#define TRIG(n) (*(volatile uint8_t *)(0xD010 + (n)))
#define ALLPOT (*(volatile uint8_t *)0xD208)
#define POTGO  (*(volatile uint8_t *)0xD20B)

#define POT_MAX 228     /* POKEY's pot counter tops out here on a real Atari */

static uint8_t last_x, last_y;      /* previous quadrature phase per axis */
static WORD    pot_pending;

static uint8_t xem_port;            /* XEM1: joystick port it answered on */
static uint8_t xem_found;
static uint8_t xem_rx, xem_ry;      /* the readings the movement is from */
static uint8_t xem_wheel;           /* previous wheel phase */

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

/* Copy the record, and copy it again if it moved while being copied.  A
 * writer that lands between two of the reads leaves at least one field
 * changed by the time it is re-read, unless it changed nothing that was
 * read before it -- in which case the copy is the new state anyway. */
void ptr_sample(void)
{
    do {
        ptr_seen.x = ptr_state.x;
        ptr_seen.y = ptr_state.y;
        ptr_seen.buttons = ptr_state.buttons;
        ptr_seen.wheel = ptr_state.wheel;
    } while (ptr_seen.x != ptr_state.x || ptr_seen.y != ptr_state.y
             || ptr_seen.buttons != ptr_state.buttons
             || ptr_seen.wheel != ptr_state.wheel);
    ptr_seen.kind = ptr_state.kind;
}

void ptr_warp(WORD x, WORD y)
{
    ptr_state.x = x;
    ptr_state.y = y;
    clamp();
    ptr_sample();
}

void ptr_init(ptr_kind kind, WORD x, WORD y)
{
    ptr_state.kind = (WORD)kind;
    ptr_state.buttons = 0;
    ptr_state.wheel = 0;
    ptr_warp(x, y);
    last_x = last_y = 0;
    pot_pending = 0;
    xem_found = 0;
    xem_wheel = 0;
    if (kind == PTR_TABLET || kind == PTR_XEM1) {
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

/* mouSTer in XEM1 mode.  The adapter keeps a 7-bit position counter per
 * axis and presents it on the port's two POT lines, sent as 64..191 so that
 * exactly one of bits 6 and 7 is set: an empty port (the counter runs to
 * 228) or a paddle at either end fails that, which is how the device is
 * found -- both ports are tried until a pair validates.  The left button
 * is the trigger, right and middle are the port's upper two direction
 * lines, and its lower two carry the wheel as plain quadrature.
 *
 * Per the sample driver: a reading is used only if both axes validate; the
 * difference from the last USED reading is taken modulo 128 and halved
 * (the low bit is noise), and a reading that halves to nothing is not
 * taken as the new reference, so a slow drag of one count a frame is not
 * lost but arrives every other frame. */
WORD ptr_xem1_valid(uint8_t pot)
{
    return (WORD)((((pot >> 1) ^ pot) & 0x40) != 0);
}

WORD ptr_decode_xem1(uint8_t ref, uint8_t now)
{
    WORD d = (WORD)((now - ref) & 0x7F);
    if (d & 0x40)
        d -= 128;
    return (WORD)((d < 0) ? -((-d) >> 1) : (d >> 1));
}

static void poll_xem1(void)
{
    uint8_t px, py, pa, q;
    WORD    d;

    if (pot_pending && (ALLPOT & 0x0F) != 0)
        return;                     /* scan still running; keep the old value */
    if (!xem_found) {
        for (q = 0; q < 2; q++) {
            px = POT(q * 2);
            py = POT(q * 2 + 1);
            if (ptr_xem1_valid(px) && ptr_xem1_valid(py)) {
                xem_port = q;
                xem_rx = px;
                xem_ry = py;
                xem_found = 1;
                break;
            }
        }
    } else {
        px = POT(xem_port * 2);
        py = POT(xem_port * 2 + 1);
        if (ptr_xem1_valid(px) && ptr_xem1_valid(py)) {
            d = ptr_decode_xem1(xem_rx, px);
            if (d) {
                ptr_state.x = (WORD)(ptr_state.x + d);
                xem_rx = px;
            }
            d = ptr_decode_xem1(xem_ry, py);
            if (d) {
                ptr_state.y = (WORD)(ptr_state.y + d);
                xem_ry = py;
            }
            clamp();
        }
    }
    if (xem_found) {
        pa = (uint8_t)(PORTA >> (xem_port * 4));
        ptr_state.buttons = (WORD)(((TRIG(xem_port) & 1) ? 0 : 1)
                                   | ((pa & 4) ? 0 : 2)
                                   | ((pa & 8) ? 0 : 4));
        q = (uint8_t)(pa & 3);
        ptr_state.wheel = (WORD)(ptr_state.wheel + ptr_decode_quad(xem_wheel, q));
        xem_wheel = q;
    }
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
    case PTR_XEM1:
        poll_xem1();
        break;
    default:
        break;
    }
}
