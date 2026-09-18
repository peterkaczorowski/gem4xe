/* m33_farrsc.c -- a program whose resource does not fit the pool.
 *
 * FARRSC.RSC is 42 KB, 702 objects in 28 trees (tools/farrsc.py); the
 * application pool is 14 KB of bank $00.  Until docs/far-trees.md the
 * AES could not load it at all.  Now it can, for a program that says it
 * can take a far address -- and this is that program, compiled
 * --data-model=large, whose kit sets the word in rsrc_load's int_in that
 * asks for one (src/app/gemlib.c).  The SAME SOURCE is also built
 * --data-model=small as M33S.G4A, whose kit passes no such word: for it
 * rsrc_load must return 0 and nothing else may happen, because a program
 * holding 16-bit pointers handed a far resource would get them silently
 * truncated.  test-m33 runs both.
 *
 * What the large build proves, in order, each step published NEAR so the
 * gate can read it by symbol against the address the loader reported
 * (the m29 pattern):
 *
 *   m33_loaded    rsrc_load returned 1
 *   m33_bank      the bank of tree 0's address from rsrc_gaddr -- NOT ZERO
 *                 is the whole point; a pool resource would answer $00
 *   m33_hdrhi     global[8], the high word of the header's address: the
 *                 AES's own account of where it put the file
 *   m33_ap6       global[6], ap_ptree's high word, likewise
 *   the dialog is CENTRED AND DRAWN from the far tree (the gate looks at
 *                 the screen while this program waits for a key)
 *   m33_findok    objc_find at a point inside the OK button answers FR_OK
 *                 -- a walk through 27 far objects that lands on the
 *                 right one
 *   m33_str0      the first byte of free string 0, read through the far
 *                 address rsrc_gaddr(R_STRING) handed back: 'F'
 *   m33_gfree     rsrc_free returned 1, the far block given back
 */
#include "portab.h"
#include "gem.h"
#include "farrsc.h"

NEAR WORD m33_step;           /* how far it got, if it did not get to the end */
NEAR WORD m33_loaded;
NEAR WORD m33_bank, m33_lo;   /* tree 0's address, high and low */
NEAR WORD m33_hdrlo, m33_hdrhi;
NEAR WORD m33_ap5, m33_ap6;
NEAR WORD m33_cx, m33_cy, m33_cw, m33_ch;   /* where form_center put it */
NEAR WORD m33_find, m33_findok;
NEAR WORD m33_str0;
NEAR WORD m33_gfree;
NEAR WORD m33_model;          /* 4 = large, 2 = small: sizeof a pointer */

/* The name near, so that neither kit has to bounce it: what this program
 * tests is the resource, not the string shim. */
NEAR char m33_name[] = "FARRSC.RSC";

int main(void)
{
    OBJECT *tree = 0;
    void *p;
    WORD x, y;

    m33_model = (WORD)sizeof(void *);
    m33_step = 1;                       /* entered main */
    appl_init();
    m33_step = 2;
    m33_loaded = rsrc_load(m33_name);
    m33_step = 3;
    if (m33_loaded) {
        m33_ap5 = global[5];
        m33_ap6 = global[6];
        m33_hdrlo = global[7];
        m33_hdrhi = global[8];
        if (rsrc_gaddr(R_TREE, FR_DIALOG, &p)) {
            tree = (OBJECT *)p;
            m33_bank = (WORD)((uint32_t)p >> 16);
            m33_lo = (WORD)(uint32_t)p;
        }
        m33_step = 4;
        if (tree) {
            form_center(tree, &m33_cx, &m33_cy, &m33_cw, &m33_ch);
            objc_draw(tree, ROOT, MAX_DEPTH, m33_cx, m33_cy, m33_cw, m33_ch);
            m33_step = 5;
            objc_offset(tree, FR_OK, &x, &y);
            m33_find = objc_find(tree, ROOT, MAX_DEPTH, (WORD)(x + 4), (WORD)(y + 4));
            m33_findok = (WORD)(m33_find == FR_OK);
            m33_step = 6;
        }
        if (rsrc_gaddr(R_STRING, 0, &p))
            m33_str0 = (WORD)(uint8_t)*(const char *)p;
        m33_step = 7;                   /* drawn: the gate looks now */
        evnt_keybd();
        m33_gfree = rsrc_free();
        m33_step = 8;
    }
    /* Both builds stop HERE, with everything published, until the gate
     * has read it: a program's near region is the shell's again the
     * moment it exits, and the first version of this gate read a freed
     * region and got the desktop's variables back. */
    m33_step = 9;
    evnt_keybd();
    appl_exit();
    return m33_loaded;
}
