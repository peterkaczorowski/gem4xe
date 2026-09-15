/* farmem.c -- discover and allocate linear RAM above bank $00. */
#include "portab.h"
#include "farmem.h"

/* Calypsi wants the address-space qualifier AFTER the base type in a
 * declarator: `uint8_t FAR *p`, not `FAR uint8_t *p`.  The latter parses
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
    uint8_t FAR *p = (uint8_t FAR *)addr;
    *p = v;
}

uint8_t far_read8(uint32_t addr)
{
    uint8_t FAR *p = (uint8_t FAR *)addr;
    return *p;
}

void far_put(uint32_t dst, const uint8_t *src, uint16_t len)
{
    uint8_t FAR *p = (uint8_t FAR *)dst;
    while (len--)
        *p++ = *src++;
}

void far_get(uint8_t *dst, uint32_t src, uint16_t len)
{
    uint8_t FAR *p = (uint8_t FAR *)src;
    while (len--)
        *dst++ = *p++;
}

/* Far to far, ascending: the AES's shell buffer and an application's
 * copy of it both live above bank $00 (shel_get, shel_put). */
void far_copy(uint32_t dst, uint32_t src, uint16_t len)
{
    uint8_t FAR *d = (uint8_t FAR *)dst;
    const uint8_t FAR *s = (const uint8_t FAR *)src;
    while (len--)
        *d++ = *s++;
}

/* Far fill (the shell buffer's clearing). */
void far_fill(uint32_t dst, uint8_t v, uint16_t len)
{
    uint8_t FAR *d = (uint8_t FAR *)dst;
    while (len--)
        *d++ = v;
}

/* Bounded strcpy across the bank boundary, both ways: a string an
 * application hands the AES lives in far memory and the AES's own
 * buffers are sized, so neither copy may run past `max` with its NUL. */
void far_strget(char *dst, uint32_t src, uint16_t max)
{
    const char FAR *p = (const char FAR *)src;
    uint16_t k;
    for (k = 0; k + 1 < max && p[k]; k++)
        dst[k] = p[k];
    dst[k] = 0;
}

void far_strput(uint32_t dst, const char *src, uint16_t max)
{
    char FAR *p = (char FAR *)dst;
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
 *
 * AND EVERY BYTE THE PROBE WRITES IS PUT BACK.  The RAM above bank $00 is not
 * only gem4xe's: Rapidus OS hands it out (kmalloc), and SpartaDOS X's
 * 65816.SYS loads at the top of it -- $EF0000 on a 16 MB Rapidus -- so the
 * probe's $EF at $EF0100 landed in the driver's code.  The SDX file calls it
 * serves then failed, and the desktop said DESKTOP.RSC was not on the boot
 * disk, on a machine where every file was where it should be
 * (docs/phase41.md).  The saved bytes are a local: 255 bytes of stack for a
 * moment at start-up, which bank $00's data could not spare as a static.
 */
void farmem_probe(void)
{
    uint16_t b;
    uint16_t first = far_first_free_bank();
    uint8_t run_first = 0, run_len = 0, best_first = 0, best_len = 0;
    uint8_t save[0xFF];                 /* indexed by bank, $01-$FE */

    farmem.kind = FARMEM_NONE;
    farmem.first_bank = farmem.last_bank = farmem.banks = 0;
    farmem.bytes = 0;
    farmem.brk = 0;

    /* Name the board if we recognise it.  This does not affect sizing. */
    if (far_read8(0xFF0000UL) == '6' && far_read8(0xFF0001UL) == 'S')
        farmem.kind = FARMEM_RAPIDUS;

    for (b = first; b <= 0xFE; b++)
        save[b] = far_read8(((uint32_t)b << 16) | PROBE_OFF);
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

    /* Back as they were, from the top down: of two banks that are one cell,
     * the lower bank's saved byte is written last, and both read that cell
     * before any write. */
    for (b = 0xFE; b >= first; b--)
        far_write8(((uint32_t)b << 16) | PROBE_OFF, save[b]);

    if (!best_len)
        return;
    if (farmem.kind == FARMEM_NONE)
        farmem.kind = FARMEM_UNKNOWN;
    farmem.first_bank = best_first;
    farmem.banks = best_len;
    farmem.last_bank = (uint8_t)(best_first + best_len - 1);
    farmem.bytes = (uint32_t)best_len << 16;
    /* The bump starts at the foot of the run, which is already past the far
     * code. */
    farmem.brk = (uint32_t)best_first << 16;
}

/* A bump allocator.  gem4xe has no need to free far memory -- the AES's
 * lifetime is the program's -- and a bump pointer over megabytes is both
 * correct and impossible to fragment. */
/* ⚠ A BLOCK MAY NOT CROSS A BANK BOUNDARY.  Calypsi's `FAR` pointer
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

/* -- A FAR POINTER'S ARITHMETIC IS SIXTEEN BITS.
 *
 * This is the fact the three functions below exist for, and it is worth
 * stating plainly because it is not what "24-bit pointer" suggests and
 * because getting it wrong is silent.  Calypsi compiles `p + n` on a
 * `__far` pointer as a 16-bit add to the OFFSET with the bank byte
 * loaded unchanged -- the carry is dropped:
 *
 *     clc
 *     lda  _Dp        ; the offset
 *     adc  _Dp+4      ; + n, low 16 bits only
 *     sta  _Dp
 *     lda  _Dp+2      ; the bank, UNTOUCHED
 *
 * So a walk that runs off the top of a bank comes back at its bottom and
 * reads somebody else's memory.  (The manual is consistent with this and
 * says so obliquely: a `far` OBJECT is capped at 64K minus one byte.
 * Crossing banks is what the `huge` attribute is for, and it widens
 * size_t to 32 bits everywhere, which is not a trade this system makes.)
 *
 * far_alloc's rule follows from it: nothing it hands out crosses a bank,
 * so anything allocated there can be walked with an ordinary far pointer.
 * far_put, far_get and far_copy above are correct for exactly that.
 *
 * A block that MAY straddle a bank, for the one thing that has to: a
 * file read into far memory whole (far_read_file).  far_alloc will not
 * give one, and a file bigger than the room left in the current bank
 * therefore could not be read AT ALL -- which went unnoticed for as long
 * as it did because every file gem4xe had loaded was a few KB.  GACS's
 * shell is 139 KB, and the failure it gave was APP_E_FILE, which reads
 * like a missing file.
 *
 * What lives here must be reached by RECOMPUTING the address for each
 * access -- far_read8, far_write8, far_copy_span -- and never by walking
 * a pointer across the boundary. */
uint32_t far_alloc_span(uint32_t bytes)
{
    uint32_t base = farmem.brk;
    uint32_t end = (uint32_t)(farmem.last_bank + 1) << 16;

    if (!farmem.banks || bytes == 0)
        return 0;
    bytes = (bytes + 3) & ~3UL;
    if (base + bytes > end || base + bytes < base)
        return 0;
    farmem.brk = base + bytes;
    return base;
}

/* Bank-safe copies, for what far_alloc_span handed out: the address is
 * rebuilt at every bank boundary instead of being walked over it.  The
 * inner copies are the ordinary far_put/far_copy, which are correct
 * because each call is given a run that stays inside one bank. */
void far_put_span(uint32_t dst, const uint8_t *src, uint16_t len)
{
    while (len) {
        uint32_t room = 0x10000UL - (dst & 0xFFFFUL);
        uint16_t k = (room >= len) ? len : (uint16_t)room;
        far_put(dst, src, k);
        dst += k;
        src += k;
        len = (uint16_t)(len - k);
    }
}

void far_copy_span(uint32_t dst, uint32_t src, uint32_t len)
{
    while (len) {
        uint32_t droom = 0x10000UL - (dst & 0xFFFFUL);
        uint32_t sroom = 0x10000UL - (src & 0xFFFFUL);
        uint32_t k = len;
        if (k > droom) k = droom;
        if (k > sroom) k = sroom;
        if (k > 0x4000UL) k = 0x4000UL;     /* a uint16_t can count it */
        far_copy(dst, src, (uint16_t)k);
        dst += k;
        src += k;
        len -= k;
    }
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
