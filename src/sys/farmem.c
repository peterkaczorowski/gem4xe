/* farmem.c -- discover and allocate linear RAM above bank $00. */
#include "farmem.h"

/* Calypsi wants the address-space qualifier AFTER the base type in a
 * declarator: `uint8_t __far *p`, not `__far uint8_t *p`.  The latter parses
 * at file scope but is rejected as a local, which is a confusing way to find
 * out. */

FARMEM farmem;

/* One past the highest far address the loader wrote, recorded by
 * src/farload.s as it copied the image up.  Probing and allocating both
 * start in the bank above it -- which is the first bank the program does not
 * occupy, taken from what actually arrived rather than restated as a
 * constant or predicted by the linker (the far code is spread over one
 * memory per bank from $01 up, and the linker has no operator for the end of
 * a section that lives in several memories).
 *
 * This is not caution, it is a repair.  The first version of this file
 * probed and allocated from bank $01, where the far code lives -- so
 * farmem_probe() wrote a bank number into $010100 and far_alloc() returned
 * $010000, and the program corrupted three bytes of its own text.  The
 * symptom was three unrelated VDI conformance failures that MOVED with the
 * optimisation level, because a different function was sitting on those
 * addresses each time. */
extern const uint8_t _fl_top[3];

static uint16_t far_first_free_bank(void)
{
    uint32_t top = (uint32_t)_fl_top[0] | ((uint32_t)_fl_top[1] << 8) |
                   ((uint32_t)_fl_top[2] << 16);
    uint16_t first = (uint16_t)((top + 0xFFFFUL) >> 16);
    /* Bank $00 is the Atari's own; nothing far may ever start there, even
     * in a build whose image somehow recorded no far bytes at all. */
    return first ? first : 1;
}

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

/* Far to far, ascending: the AES's shell buffer and an application's
 * copy of it both live above bank $00 (shel_get, shel_put). */
void far_copy(uint32_t dst, uint32_t src, uint16_t len)
{
    uint8_t __far *d = (uint8_t __far *)dst;
    const uint8_t __far *s = (const uint8_t __far *)src;
    while (len--)
        *d++ = *s++;
}

/* Far fill (the shell buffer's clearing). */
void far_fill(uint32_t dst, uint8_t v, uint16_t len)
{
    uint8_t __far *d = (uint8_t __far *)dst;
    while (len--)
        *d++ = v;
}

/* Bounded strcpy across the bank boundary, both ways: a string an
 * application hands the AES lives in far memory and the AES's own
 * buffers are sized, so neither copy may run past `max` with its NUL. */
void far_strget(char *dst, uint32_t src, uint16_t max)
{
    const char __far *p = (const char __far *)src;
    uint16_t k;
    for (k = 0; k + 1 < max && p[k]; k++)
        dst[k] = p[k];
    dst[k] = 0;
}

void far_strput(uint32_t dst, const char *src, uint16_t max)
{
    char __far *p = (char __far *)dst;
    uint16_t k;
    for (k = 0; k + 1 < max && src[k]; k++)
        p[k] = src[k];
    p[k] = 0;
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
    uint16_t first = far_first_free_bank();
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
/* ⚠ A BLOCK MAY NOT CROSS A BANK BOUNDARY.  Calypsi's `__far` pointer
 * arithmetic is 16-bit WITHIN a bank -- carrying into the bank byte is
 * what `__huge` is for -- so a buffer that straddles one wraps round to
 * the bottom of its own bank the moment it is indexed past the edge,
 * and the bottom of a far bank is the far code image.  That is the same
 * corruption the comment at the top of this file describes, reached by
 * another road: a fault that moves with the layout, because whether a
 * block straddles depends on where the image ended.
 *
 * So a request that will not fit in what is left of the current bank
 * starts the next one.  The gap is lost, which costs at most 64 KB of
 * the fifteen megabytes this machine has -- and buys an allocator whose
 * blocks can be indexed. */
/* The far heap's floor, and it is there for the reason the pool's is
 * (src/sys/app.c): app_free winds the heap back to where app_load found
 * it, which is right for the program and wrong for anything permanent
 * taken since.  The shell's buffers, the file selector's names, GEMDOS's
 * state and an accessory's whole far image are all below it. */
static uint32_t far_low;
uint16_t far_refused;

void far_keep_mark(void)
{
    far_low = farmem.brk;
}

void far_release(uint32_t mark)
{
    if (mark < far_low) {
        far_refused++;
        return;
    }
    farmem.brk = mark;
}

uint32_t far_alloc(uint32_t bytes)
{
    uint32_t base = farmem.brk;
    uint32_t end = (uint32_t)(farmem.last_bank + 1) << 16;
    uint32_t bank_end;

    if (!farmem.banks || bytes == 0)
        return 0;
    bytes = (bytes + 3) & ~3UL;                 /* keep it 4-byte aligned */
    if (bytes > 0x10000UL)                      /* no block can be indexed
                                                 * past its own bank */
        return 0;
    bank_end = (base | 0xFFFFUL) + 1;
    if (base + bytes > bank_end)
        base = bank_end;                        /* start the next bank */
    if (base + bytes > end || base + bytes < base)
        return 0;
    farmem.brk = base + bytes;
    return base;
}

/* Whole banks, for what must not straddle one: an application's code,
 * which the 65816 executes bank by bank (src/gem4xe.scm on why).  The
 * cursor moves up to the next bank boundary first, so the banks come
 * back aligned; what that skips is lost to the bump allocator, as
 * everything it hands out is.  Returns the first bank's number. */
uint16_t far_alloc_banks(uint16_t n)
{
    uint32_t base = (farmem.brk + 0xFFFFUL) & ~0xFFFFUL;
    uint32_t end = (uint32_t)(farmem.last_bank + 1) << 16;
    uint32_t bytes = (uint32_t)n << 16;
    if (!farmem.banks || n == 0)
        return 0;
    if (base + bytes > end || base + bytes < base)
        return 0;
    farmem.brk = base + bytes;
    return (uint16_t)(base >> 16);
}
