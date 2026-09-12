/* farmem_sim.c -- far_alloc, asked for the blocks that used to break it.
 *
 * A block may not cross a bank boundary: Calypsi's `FAR` pointer
 * arithmetic is 16 bits WITHIN a bank, so a buffer that straddles one
 * wraps to the bottom of its own bank the moment it is indexed past the
 * edge -- and the bottom of a far bank is the far code image.  That is
 * what corrupted the file selector for a whole afternoon
 * (docs/phase24.md), and this is the shape of the case that did it: a
 * heap whose cursor is near the top of a bank, and a block bigger than
 * what is left.
 *
 * The loader's symbol is stubbed: nothing here probes hardware, and
 * far_alloc only reads the cursor and the last bank.
 */
#include "sys/farmem.h"

const uint8_t _fl_top[3] = { 0, 0, 1 };     /* the image ends in bank $01 */

#define CASES 8

/* what each case asked for and got: base, and the bank each end is in */
uint32_t fm_base[CASES];
uint16_t fm_bank_lo[CASES];
uint16_t fm_bank_hi[CASES];
uint16_t fm_straddled;          /* blocks whose two ends differ: must be 0 */
uint16_t fm_refused;            /* a block bigger than a bank */

static void take(int i, uint32_t brk, uint32_t bytes)
{
    uint32_t a;

    farmem.brk = brk;
    a = far_alloc(bytes);
    fm_base[i] = a;
    if (!a) {
        fm_bank_lo[i] = fm_bank_hi[i] = 0xFFFF;
        return;
    }
    fm_bank_lo[i] = (uint16_t)(a >> 16);
    fm_bank_hi[i] = (uint16_t)((a + bytes - 1) >> 16);
    if (fm_bank_lo[i] != fm_bank_hi[i])
        fm_straddled++;
}

int main(void)
{
    farmem.kind = FARMEM_RAPIDUS;
    farmem.first_bank = 2;
    farmem.last_bank = 0xEF;
    farmem.banks = 0xEE;

    /* 900 bytes -- the file selector's name list -- from cursors that
     * leave less than that at the top of a bank */
    take(0, 0x02FF00UL, 900);
    take(1, 0x02FFFCUL, 900);
    take(2, 0x02FC00UL, 900);       /* fits: 1024 left */
    take(3, 0x020000UL, 900);       /* a whole bank left */
    /* the awkward sizes: exactly the rest of a bank, and one more */
    take(4, 0x03FF00UL, 0x100);
    take(5, 0x03FF00UL, 0x101);
    /* a block bigger than a bank can never be indexed: refused */
    take(6, 0x040000UL, 0x10004UL);
    fm_refused = (uint16_t)(fm_base[6] == 0);
    /* and the last bank's edge: no room above it, so nothing comes back */
    take(7, 0xEFFF00UL, 900);
    return 0;
}
