/* irq.c -- the OS ROM shadowed into RAM, the native vectors filled, and
 * the interrupt sources switched on.  What and why: irq.h. */
#include "irq.h"
#include "rapidus.h"
#include <calypsi/intrinsics65816.h>

IRQ_INFO irq;

volatile uint16_t irq_frames, irq_timer, irq_qlo, irq_qhi;
volatile uint8_t  irq_kb[8], irq_kb_head, irq_kb_tail, irq_kb_count;
volatile uint8_t  irq_fault;

uint8_t     irq_ptr_on;
uint8_t     irq_plo[16], irq_phi[16];
signed char irq_qtab[16];
uint8_t     irq_prev_lo, irq_prev_hi;
uint8_t     irq_pend, irq_tmp;         /* the handler's scratch */

#define PORTB   (*(volatile uint8_t *)0xD301)
#define NMIEN   (*(volatile uint8_t *)0xD40E)
#define IRQEN   (*(volatile uint8_t *)0xD20E)
#define POKMSK  (*(volatile uint8_t *)0x0010)
#define AUDF1   (*(volatile uint8_t *)0xD200)
#define AUDC1   (*(volatile uint8_t *)0xD201)
#define AUDCTL  (*(volatile uint8_t *)0xD208)
#define STIMER  (*(volatile uint8_t *)0xD209)
#define SKCTL   (*(volatile uint8_t *)0xD20F)

#define PORTB_OSROM  0x01
#define NMIEN_VBI    0x40
#define IRQ_TIMER1   0x01
#define IRQ_KEY      0x40

/* AUDF1 for the pointer sampler.  The 64 kHz base clock over (n + 1):
 * n = 15 is ~3.96 kHz on a PAL machine, ~4.0 kHz on NTSC, a sample every
 * 250 us -- an ST mouse pushed hard makes a transition every few hundred.
 * A design constant, reported in irq.timer_div so a gate can derive the
 * rate it should measure rather than repeat the number. */
#define TIMER_DIV    15

#define VEC_BASE     0xFFE4
#define VEC_LEN      12

/* The OS window: two runs, the hardware page between them left alone. */
static const uint16_t run_lo[2] = { 0xC000, 0xD800 };
static const uint16_t run_hi[2] = { 0xD000, 0x0000 };   /* 0 = wraps: $10000 */

static uint8_t rom_in;                  /* PORTB bit 0 as found */
static uint8_t win3_fast;               /* MCR bit 3 clear as found (Rapidus) */

/* Window 3 between its two configurations: the OS ROM (as found) and the
 * RAM under it, which on a Rapidus is the SRAM window with write-through
 * on.  Reads with the ROM in come from the motherboard whatever the MCR
 * says (irq.h), so the ROM side needs only PORTB. */
static void os_side(void)
{
    PORTB = irq.portb_before;
    if (rapidus.present)
        rapidus_reg_write(RAP_MCR, irq.mcr_before);
}

static void ram_side(void)
{
    PORTB = (uint8_t)(irq.portb_before & ~PORTB_OSROM);
    if (rapidus.present)
        rapidus_reg_write(RAP_MCR, (uint8_t)(irq.mcr_before & ~MCR_SLOW3));
}

/* Copy one run of the window from the OS side to the RAM side, a page at a
 * time through a buffer on the stack, verifying each page as it goes.
 * Returns 0 on a mismatch; the sums accumulate either way. */
static uint8_t copy_run(uint16_t lo, uint16_t hi)
{
    uint8_t buf[256];
    uint16_t page = lo;
    do {
        volatile uint8_t *p = (volatile uint8_t *)page;
        uint16_t i;
        os_side();
        for (i = 0; i < 256; i++) {
            buf[i] = p[i];
            irq.rom_sum += buf[i];
        }
        ram_side();
        for (i = 0; i < 256; i++)
            p[i] = buf[i];
        for (i = 0; i < 256; i++) {
            uint8_t v = p[i];
            irq.ram_sum += v;
            if (v != buf[i])
                return 0;
        }
        page += 256;
    } while (page != hi);
    return 1;
}

/* The RAM side was RAM already: nothing to copy, just sum it, so the report
 * is the same shape either way. */
static void sum_run(uint16_t lo, uint16_t hi)
{
    uint16_t page = lo;
    do {
        volatile uint8_t *p = (volatile uint8_t *)page;
        uint16_t i;
        for (i = 0; i < 256; i++)
            irq.rom_sum += p[i];
        page += 256;
    } while (page != hi);
    irq.ram_sum = irq.rom_sum;
}

static uint8_t write_vectors(void)
{
    volatile uint8_t *v = (volatile uint8_t *)VEC_BASE;
    uint8_t i;
    for (i = 0; i < VEC_LEN; i++)
        v[i] = irq_vectab[i];
    for (i = 0; i < VEC_LEN; i++)
        if (v[i] != irq_vectab[i])
            return 0;
    return 1;
}

static void sources_on(void)
{
    irq_frames = irq_timer = 0;
    irq_qlo = irq_qhi = 0;
    irq_kb_head = irq_kb_tail = irq_kb_count = 0;
    irq_fault = 0;
    irq_prev_lo = irq_prev_hi = 0;

    AUDCTL = 0;                     /* 64 kHz base, no linking */
    AUDF1  = TIMER_DIV;
    AUDC1  = 0;                     /* silent */
    STIMER = 0;                     /* load the divisor */
    SKCTL  = 3;                     /* keyboard scan and debounce, as the OS
                                       leaves it: the keyboard IRQ needs the
                                       scan running */
    POKMSK = IRQ_TIMER1 | IRQ_KEY;
    IRQEN  = 0;                     /* drop anything latched while off ... */
    IRQEN  = POKMSK;                /* ... then arm */
    NMIEN  = NMIEN_VBI;
    __enable_interrupts();
}

uint8_t irq_install(void)
{
    uint8_t ok;

    irq.how  = IRQ_OFF;
    irq.fail = IRQ_FAIL_NONE;
    irq.fast = 0;
    irq.timer_div = TIMER_DIV;
    irq.rom_sum = irq.ram_sum = 0;
    irq.portb_before = PORTB;
    irq.mcr_before = rapidus.present ? rapidus_reg_read(RAP_MCR) : 0;
    rom_in = (uint8_t)(irq.portb_before & PORTB_OSROM);
    win3_fast = (uint8_t)(rapidus.present && !(irq.mcr_before & MCR_SLOW3));

    if (!rom_in && (!rapidus.present || win3_fast)) {
        /* $C000-$FFFF already reads and writes as the RAM side: an OS in
         * RAM, or a caller that did this before us.  Patch in place. */
        sum_run(run_lo[0], run_hi[0]);
        sum_run(run_lo[1], run_hi[1]);
        irq.how = IRQ_RAM_FOUND;
    } else {
        ok = copy_run(run_lo[0], run_hi[0]);
        if (ok)
            ok = copy_run(run_lo[1], run_hi[1]);
        if (!ok) {
            os_side();
            irq.fail = IRQ_FAIL_COPY;
            return irq.how;
        }
        irq.how = IRQ_ROM_COPIED;
    }
    ram_side();
    if (!write_vectors()) {
        os_side();
        irq.how  = IRQ_OFF;
        irq.fail = IRQ_FAIL_VEC;
        return irq.how;
    }
    irq.fast = rapidus.present;
    sources_on();
    return irq.how;
}

void irq_remove(void)
{
    volatile uint16_t spin;

    if (irq.how == IRQ_OFF)
        return;
    NMIEN  = 0;
    IRQEN  = 0;
    POKMSK = 0;
    /* An NMI ANTIC asserted just before NMIEN went low is still coming;
     * let it land on the handlers while they are still what the vectors
     * point at.  Long enough at any clock. */
    for (spin = 0; spin < 512; spin++)
        ;
    __disable_interrupts();
    os_side();
    irq.how  = IRQ_OFF;
    irq.fast = 0;
}
