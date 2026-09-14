/* farmem.h -- linear memory above bank $00, discovered rather than assumed.
 *
 * gem4xe requires a 65C816 with linear RAM.  Two boards provide it and they do
 * not agree on the map:
 *
 *   Rapidus   14.5 MB SDRAM at $080000-$EFFFFF, plus SRAM in banks $01-$07,
 *             registers in bank $FF.  Signature "6S9038E " at $FF0000.
 *   Antonia   65816 with 4 MB or 8 MB.  NOT emulated by Altirra, so nothing
 *             here about it has been tested -- see farmem_probe().
 *
 * Hardcoding either map would make the other fail silently, so this PROBES:
 * it writes each bank's own number into it and reads them all back, which
 * finds real RAM, sizes it, and rejects mirrors in one pass.  Any board that
 * exposes linear RAM works, tested or not.
 *
 * That is the same reasoning as the pointer seam: the thing above the seam
 * asks for memory, not for a particular accelerator.
 *
 * WHAT IT WILL NOT HAND OUT: the banks the program itself is running from.
 * gem4xe's code lives in the banks from $01 up (src/gem4xe.scm), copied up
 * at load time by src/farload.s, so probing and allocation both begin at the
 * first bank above the highest address the loader wrote -- taken from the
 * loader's own record of it, not written down twice.
 */
#ifndef GEM4XE_FARMEM_H
#define GEM4XE_FARMEM_H

#include <stdint.h>

#define FARMEM_NONE     0   /* no linear RAM -- gem4xe cannot run */
#define FARMEM_RAPIDUS  1
#define FARMEM_UNKNOWN  2   /* linear RAM found, board not identified */

typedef struct {
    uint8_t  kind;
    uint8_t  first_bank;    /* lowest usable bank */
    uint8_t  last_bank;     /* highest usable bank, inclusive */
    uint8_t  banks;         /* count of usable banks */
    uint32_t bytes;         /* banks * 64K */
    uint32_t brk;           /* bump allocator cursor */
} FARMEM;

extern FARMEM farmem;

void     farmem_probe(void);
uint32_t far_alloc(uint32_t bytes);     /* 0 on failure; never crosses a
                                         * bank -- it skips to the next */
/* ...and one that MAY cross, for a file read in whole (far_read_file).
 * A FAR POINTER'S ARITHMETIC IS 16 BITS on this compiler, so what lives
 * in a span must be reached by recomputing the address -- far_read8,
 * far_write8, far_put_span, far_copy_span -- and never by walking a
 * pointer over the boundary.  The reasoning is in farmem.c. */
uint32_t far_alloc_span(uint32_t bytes);
/* Everything taken so far is permanent: no later far_release() may go
 * below it.  far_refused counts the releases turned away. */
void     far_keep_mark(void);
void     far_release(uint32_t mark);
extern uint16_t far_refused;

uint16_t far_alloc_banks(uint16_t n);   /* n whole banks, aligned: the
                                         * first bank's number, 0 on failure */

void     far_write8(uint32_t addr, uint8_t v);
uint8_t  far_read8(uint32_t addr);
void     far_put(uint32_t dst, const uint8_t *src, uint16_t len);
void     far_get(uint8_t *dst, uint32_t src, uint16_t len);
void     far_copy(uint32_t dst, uint32_t src, uint16_t len);
/* The same two for a far_alloc_span block, which may cross a bank. */
void     far_put_span(uint32_t dst, const uint8_t *src, uint16_t len);
void     far_copy_span(uint32_t dst, uint32_t src, uint32_t len);
void     far_fill(uint32_t dst, uint8_t v, uint16_t len);
void     far_strget(char *dst, uint32_t src, uint16_t max);   /* bounded strcpy, */
void     far_strput(uint32_t dst, const char *src, uint16_t max); /* NUL always */

#endif /* GEM4XE_FARMEM_H */
