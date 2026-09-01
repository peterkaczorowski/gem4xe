/* farmem.c -- discover and allocate linear RAM above bank $00. */
#include "farmem.h"

/* Calypsi wants the address-space qualifier AFTER the base type in a
 * declarator: `uint8_t __far *p`, not `__far uint8_t *p`.  The latter parses
 * at file scope but is rejected as a local, which is a confusing way to find
 * out. */

FARMEM farmem;

/* The first bank the program does not occupy, exported by src/farload.s from
 * the end of the FarCode memory in src/gem4xe.scm.  Probing and allocating
 * both start here.
 *
 * This is not caution, it is a repair.  The far code lives in bank $01, and
 * the first version of this file probed and allocated from bank $01 -- so
 * farmem_probe() wrote a bank number into $010100 and far_alloc() returned
 * $010000, and the program corrupted three bytes of its own text.  The
 * symptom was three unrelated VDI conformance failures that MOVED with the
 * optimisation level, because a different function was sitting on those
 * addresses each time. */
extern const uint8_t _fl_heap_bank;

/* The offset within each bank used for probing.  $0100 rather than $0000
 * because if a bank turns out to MIRROR bank $00, a write at offset 0 would
 * land in the OS zero page; $0100 is the 6502 stack page, which the 65816 is
 * not using in native mode with its own stack elsewhere. */
#define PROBE_OFF 0x0100

void far_write8(uint32_t addr, uint8_t v)
{
    uint8_t __far *p = (uint8_t __far *)addr;
    *p = v;
}

uint8_t far_read8(uint32_t addr)
{
    uint8_t __far *p = (uint8_t __far *)addr;
    return *p;
}

void far_put(uint32_t dst, const uint8_t *src, uint16_t len)
{
    uint8_t __far *p = (uint8_t __far *)dst;
    while (len--)
        *p++ = *src++;
}

void far_get(uint8_t *dst, uint32_t src, uint16_t len)
{
    uint8_t __far *p = (uint8_t __far *)src;
    while (len--)
        *dst++ = *p++;
}

/* Write every bank's own number into it, then read them all back.
 *
 * This is the classic RAM-sizing trick and it is alias-proof by construction:
 * a bank that mirrors another has been overwritten by the later write and
 * reads back the wrong number.  A bank with no RAM reads back floating bus or
 * ROM.  Either way it fails the same test, so no board-specific knowledge is
 * needed to size the memory -- only to NAME the board.
 *
 * Bank $FF is skipped: on Rapidus it holds the accelerator's registers, and
 * writing the bank number over them would be a poor way to start.
 */
void farmem_probe(void)
{
    uint16_t b;
    uint16_t first = _fl_heap_bank;
    uint8_t run_first = 0, run_len = 0, best_first = 0, best_len = 0;

    farmem.kind = FARMEM_NONE;
    farmem.first_bank = farmem.last_bank = farmem.banks = 0;
    farmem.bytes = 0;
    farmem.brk = 0;

    /* Name the board if we recognise it.  This does not affect sizing. */
    if (far_read8(0xFF0000UL) == '6' && far_read8(0xFF0001UL) == 'S')
        farmem.kind = FARMEM_RAPIDUS;

    for (b = first; b <= 0xFE; b++)
        far_write8(((uint32_t)b << 16) | PROBE_OFF, (uint8_t)b);

    for (b = first; b <= 0xFF; b++) {
        uint8_t ok = (b <= 0xFE) &&
                     (far_read8(((uint32_t)b << 16) | PROBE_OFF) == (uint8_t)b);
        if (ok) {
            if (!run_len)
                run_first = (uint8_t)b;
            run_len++;
        } else {
            if (run_len > best_len) {
                best_len = run_len;
                best_first = run_first;
            }
            run_len = 0;
        }
    }

    if (!best_len)
        return;
    if (farmem.kind == FARMEM_NONE)
        farmem.kind = FARMEM_UNKNOWN;
    farmem.first_bank = best_first;
    farmem.banks = best_len;
    farmem.last_bank = (uint8_t)(best_first + best_len - 1);
    farmem.bytes = (uint32_t)best_len << 16;
    /* The bump starts at the foot of the run, which is already past the far
     * code; the probe byte at PROBE_OFF is inside the first allocation and is
     * dead by then. */
    farmem.brk = (uint32_t)best_first << 16;
}

/* A bump allocator.  gem4xe has no need to free far memory -- the AES's
 * lifetime is the program's -- and a bump pointer over megabytes is both
 * correct and impossible to fragment. */
uint32_t far_alloc(uint32_t bytes)
{
    uint32_t base = farmem.brk;
    uint32_t end = (uint32_t)(farmem.last_bank + 1) << 16;
    if (!farmem.banks || bytes == 0)
        return 0;
    bytes = (bytes + 3) & ~3UL;                 /* keep it 4-byte aligned */
    if (base + bytes > end || base + bytes < base)
        return 0;
    farmem.brk = base + bytes;
    return base;
}
