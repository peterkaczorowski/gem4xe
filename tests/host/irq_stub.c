/* irq_stub.c -- the interrupt regime's state, for the simulator builds in
 * tests/host (quad_sim.c, xem1_sim.c).  src/vdi/pointer.c fills and reads
 * these; on the target src/sys/irq.c defines them and src/sys/irq.s writes
 * them at interrupt time.  Nothing here installs anything: irq.how stays
 * IRQ_OFF, so pointer.c takes its polled path, and the simulator programs
 * play the handler's part themselves where they need it.  The declarations
 * come from src/sys/irq.h, so a type that drifts from the target's fails to
 * compile here rather than lying. */
#include "sys/irq.h"

IRQ_INFO irq;

volatile uint16_t irq_frames, irq_qlo, irq_qhi;
volatile uint32_t irq_timer;
volatile uint8_t  irq_kb[8], irq_kb_head, irq_kb_tail, irq_kb_count;
volatile uint8_t  irq_fault;

uint8_t     irq_ptr_on;
uint8_t     irq_plo[16], irq_phi[16];
signed char irq_qtab[16];
uint8_t     irq_prev_lo, irq_prev_hi;
