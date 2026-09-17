/* m31_huge.c -- an application whose far IMAGE is bigger than a bank.
 *
 * Until GACS's GEM shell was linked for this machine, nothing in this tree
 * had a far image over 64 KB, so two limits sat undisturbed and neither
 * would have announced itself:
 *
 *   the .G4A's FAR FIXUP OFFSETS were u16, so an address needing relocation
 *   past $FFFF in the image could not be named at all -- and tools/mkg4a.py
 *   refused a multi-bank image up front, which hid it;
 *
 *   and app_load copied the image with memcpy_far, whose size_t IS SIXTEEN
 *   BITS here (gem4xe is built --data-model=small).  A 115 KB image would
 *   have been copied modulo 65,536, the loader would have reported success,
 *   every fixup would have applied, and the program would have run into
 *   whatever was left of the previous tenant partway through.
 *
 * Format 2 answers the first (three-byte far offsets) and a chunked copy
 * the second.  This program exists so that neither is proved only by GACS,
 * which lives in another repository and is not always present.
 *
 * Getting over a bank takes bytes that are IN the image, so `cfar` is the
 * section to grow: far constants carry their values in the file, where far
 * BSS carries none.  Four blocks rather than one 80 KB block because the
 * linker places a section fragment whole, into one memory, and no memory
 * here is that big (src/app/gemapp.scm).
 *
 * Every block is checked, and the LAST one is the point: it lives in the
 * second code bank, so reading it correctly means the loader copied past
 * 64 KB and relocated a pointer whose fixup offset needed more than two
 * bytes to name.
 */
#include "portab.h"
#include "gem.h"

#define BLK  20000
#define NBLK 3
#define NPTR 4000

/* build/m31_data.c, from tools/m31data.py.
 *
 * m31_blk0..2 are the BULK: 60,000 bytes of far constants, which is what
 * pushes the image past a bank and so is what proves the chunked copy.
 *
 * m31_ptrs is the other half and the subtler one.  Bytes past 64 KB prove
 * a copy; only an ADDRESS past 64 KB proves the fixup OFFSETS, and a byte
 * array holds none.  So it is four thousand far pointers, each into blk0
 * at its own index -- four thousand bank bytes the loader must add to, and
 * the ones in the second code bank sit at image offsets a u16 cannot name.
 * The first version of this program had only the byte blocks, and the test
 * caught it: a perfectly good 105 KB image with not one fixup past 64 KB,
 * which is to say a test that did not test what it exists to test. */
#define FILL(b, i)  ((unsigned char)((i) * 7 + (b) * 131 + 5))

extern const unsigned char FAR *const m31_blocks[NBLK];
extern const unsigned char FAR *const m31_ptrs[NPTR];

NEAR WORD m31_ran;        /* reached the end */
NEAR WORD m31_step;       /* ...or how far it got */
NEAR WORD m31_ok;         /* every block read back as itself */
NEAR WORD m31_badblk;     /* the first block that did not, or -1 */
NEAR WORD m31_ptrok;      /* every pointer still points where it was aimed */
NEAR WORD m31_sum;        /* low word of the sum of all the blocks */

int main(void)
{
    WORD b, ok = 1, bad = -1;
    LONG sum = 0;
    WORD i;

    m31_step = 1;
    appl_init();
    m31_step = 2;

    for (b = 0; b < NBLK; b++) {
        const unsigned char FAR *p = m31_blocks[b];
        for (i = 0; i < BLK; i++) {
            if (p[i] != FILL(b, i)) {
                if (ok) { ok = 0; bad = b; }
                break;
            }
            sum += p[i];
        }
        m31_step = (WORD)(3 + b);
    }

    /* And the pointers, which is what a wrong or missing fixup breaks:
       m31_ptrs[i] was aimed at m31_blk0 + i, so it must read as the byte
       the rule says lives there. */
    {
        WORD pok = 1;
        for (i = 0; i < NPTR; i++) {
            if (*m31_ptrs[i] != FILL(0, i)) { pok = 0; break; }
        }
        m31_ptrok = pok;
        if (!pok) ok = 0;
    }
    m31_step = 8;

    m31_ok = ok;
    m31_badblk = bad;
    m31_sum = (WORD)sum;
    m31_ran = 1;
    m31_step = 9;

    /* ...and WAIT, as src/m29_big.c does and for the same reason: a
     * program that returns has its near region given back and the desktop
     * loaded on top of it (src/sys/app.c, app_free), so results published
     * and then left behind live about two frames.  The gate sampled every
     * ten and read them only when the phase happened to suit -- a coin
     * flip that a 63-byte shift in the system's layout was enough to
     * lose, with every check then reading the zeroes of somebody else's
     * memory.  The gate reads the results and then sends a key. */
    evnt_keybd();

    appl_exit();
    return 0;
}
