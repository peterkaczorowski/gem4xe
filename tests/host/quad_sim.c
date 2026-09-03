/* quad_sim.c -- Altirra's three relative pointing devices, walked through
 * the target's pointer layer in the compiler's simulator, for
 * tests/host/test_pointer.py.
 *
 * The emulator's bridge can set the four joystick lines but not run a
 * device model, so the only quadrature the emulated machine ever sees is
 * what a JOY verb can make of the trak-ball's direction-and-pulse lines
 * (tests/emu/m10_irq.py: +x and +y).  This closes the rest: the ST and
 * Amiga Gray codes, and the trak-ball with its direction lines low, in all
 * four directions, on each device.
 *
 * The models are Altirra's, from inputcontroller.cpp: ATMouseController's
 * kSTTabX/Y and kAMTabX/Y index the accumulated position's low two bits,
 * ATTrackballController keeps a direction bit per axis (clear for +) and
 * puts the accumulator's low bit on the pulse line, and the PIA reads the
 * complement.  Which is to say they are a reading of the emulator, and the
 * signs asserted here are the emulator's; a real mouse has not been near
 * any of this.
 *
 * Two things are checked on each walk.  ptr_poll() on its polled path
 * (irq.how == IRQ_OFF) decodes one sample a call through the tables
 * ptr_init() filled -- the target's own C.  And the same samples go through
 * a C copy of the timer handler's arithmetic (src/sys/irq.s, irq_pokey:
 * pair = plo[nibble]; d = qtab[prev | pair]; prev = pair << 2), which is
 * what runs on the target and cannot run here; the copy pins the tables to
 * that indexing.  Both are asked for the same answer. */
#include <stdint.h>
#include "vdi/pointer.h"
#include "sys/irq.h"

#define PORTA  (*(volatile uint8_t *)0xD300)
#define TRIG0  (*(volatile uint8_t *)0xD010)
#define TRIG1  (*(volatile uint8_t *)0xD011)

#define STEPS  7            /* odd: a walk that ends mid-cycle */

/* Results, one per device: for each of +x, -x, +y, -y in turn, the change
 * in ptr_state.x and ptr_state.y over STEPS steps, packed as signed bytes:
 * [0]=dx(+x) [1]=dy(+x) [2]=dx(-x) [3]=dy(-x) [4]=dx(+y) ... [7]=dy(-y). */
int8_t r_st[8], r_amiga[8], r_tb[8];
/* 1 if the handler-arithmetic copy agreed with the polled path on every
 * step of every walk; otherwise the walk number it first disagreed on,
 * negated. */
int16_t r_handler_agrees;

static const uint8_t st_x[4] = { 0x00, 0x02, 0x03, 0x01 };
static const uint8_t st_y[4] = { 0x00, 0x08, 0x0c, 0x04 };
static const uint8_t am_x[4] = { 0x00, 0x02, 0x0A, 0x08 };
static const uint8_t am_y[4] = { 0x00, 0x01, 0x05, 0x04 };

static uint16_t ax, ay;     /* the model's accumulators */
static uint8_t  tb_dir;     /* trak-ball direction bits, as the model keeps them */

static uint8_t model_bits(WORD kind)
{
    switch (kind) {
    case PTR_ST_MOUSE:    return (uint8_t)(st_x[ax & 3] + st_y[ay & 3]);
    case PTR_AMIGA_MOUSE: return (uint8_t)(am_x[ax & 3] + am_y[ay & 3]);
    default:              return (uint8_t)(tb_dir + ((ax << 1) & 2) + ((ay << 3) & 8));
    }
}

/* One model step along an axis: dir = +1 or -1.  Altirra moves the
 * accumulator one count toward the target per update and, for the
 * trak-ball, sets the axis's direction bit for - and clears it for +. */
static void model_step(WORD axis, int dir)
{
    if (axis == 0) {
        if (dir > 0) { ax++; tb_dir &= (uint8_t)~1; } else { ax--; tb_dir |= 1; }
    } else {
        if (dir > 0) { ay++; tb_dir &= (uint8_t)~4; } else { ay--; tb_dir |= 4; }
    }
    PORTA = (uint8_t)(~model_bits(ptr_state.kind) & 0x0F);
}

static int walk_no;

static void walk(WORD kind, WORD axis, int dir, int8_t *out)
{
    WORD x0, y0;
    int16_t hx = 0, hy = 0;
    int i;

    ax = 0; ay = 0; tb_dir = 0;
    ptr_state.kind = kind;
    PORTA = (uint8_t)(~model_bits(kind) & 0x0F);
    TRIG0 = 1; TRIG1 = 1;
    ptr_init((ptr_kind)kind, 320, 120);     /* seeds from PORTA as set */
    x0 = ptr_state.x; y0 = ptr_state.y;
    walk_no++;

    for (i = 0; i < STEPS; i++) {
        uint8_t n, p;
        model_step(axis, dir);
        ptr_poll();
        /* the handler's arithmetic, over the same sample */
        n = (uint8_t)(PORTA & 0x0F);
        p = irq_plo[n];
        hx = (int16_t)(hx + irq_qtab[irq_prev_lo | p]);
        irq_prev_lo = (uint8_t)(p << 2);
        p = irq_phi[n];
        hy = (int16_t)(hy + irq_qtab[irq_prev_hi | p]);
        irq_prev_hi = (uint8_t)(p << 2);
    }
    out[0] = (int8_t)(ptr_state.x - x0);
    out[1] = (int8_t)(ptr_state.y - y0);
    if (r_handler_agrees == 1 &&
        (hx != (int16_t)(ptr_state.x - x0) || hy != (int16_t)(ptr_state.y - y0)))
        r_handler_agrees = (int16_t)-walk_no;
}

static void device(WORD kind, int8_t *out)
{
    walk(kind, 0, +1, out + 0);
    walk(kind, 0, -1, out + 2);
    walk(kind, 1, +1, out + 4);
    walk(kind, 1, -1, out + 6);
}

int main(void)
{
    irq.how = IRQ_OFF;
    r_handler_agrees = 1;
    device(PTR_ST_MOUSE, r_st);
    device(PTR_AMIGA_MOUSE, r_amiga);
    device(PTR_TRAKBALL, r_tb);
    return 0;
}
