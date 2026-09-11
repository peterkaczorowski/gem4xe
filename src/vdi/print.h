/* print.h -- the printer's page.
 *
 * 640 x 800 dots at one bit each, in far memory, rasterised by
 * src/vdi/dev_print.c and emitted by whatever GEM4XE.CFG's PRINTER= names
 * (docs/printing.md).
 *
 * THE SIZE IS THE BANK.  80 bytes a row by 800 rows is 64,000 against a
 * far bank's 65,536, so the page never straddles one -- which matters
 * because far_alloc refuses a block that would (it cannot be indexed
 * past its own bank) and because __far pointer arithmetic is sixteen
 * bits WITHIN a bank, the trap docs/phase24.md is about.  Inside one bank
 * the whole page is reachable with plain 16-bit offsets, so this device's
 * loops read like src/antic/antic.c's rather than like a class of
 * pointer bug.
 */
#ifndef GEM4XE_PRINT_H
#define GEM4XE_PRINT_H

#include <stdint.h>

#define PR_W        640                     /* dots across: 80 columns of 8 */
#define PR_H        800                     /* ...and down: 100 rows        */
#define PR_STRIDE   (PR_W / 8)              /* 80 bytes a row               */
#define PR_BYTES    ((uint16_t)PR_STRIDE * PR_H)    /* 64,000: one bank     */
#define PR_DPI      100                     /* what the emitter declares    */

/* The page itself, 0 until pr_page_open().  One at a time: a second
 * printer workstation shares it, as it would share a screen. */
extern uint8_t __far *pr_page;

/* What v_updwk writes, and where.  src/gem.c sets both from GEM4XE.CFG;
 * the numbers are CFG_PRINT_* in src/sys/config.h and the two lists must
 * agree -- tests/host/test_print.py says so. */
#define PR_NONE 0
#define PR_PCL  1
#define PR_PS   2

extern WORD        pr_kind;
extern const char *pr_dest;


/* Take the page from the far heap and clear it to white; FALSE if the
 * heap has none.  v_opnwk's, and released by v_clswk. */
int16_t pr_page_open(void);
void    pr_page_close(void);

/* The page, off the machine, in whatever GEM4XE.CFG's PRINTER= names and
 * to wherever its PRINTTO= names (src/vdi/emit.c).  What v_updwk does.
 * FALSE when there is no page, no printer configured, or the destination
 * would not open -- none of which is a failure worth stopping for. */
WORD pr_emit(void);

#endif /* GEM4XE_PRINT_H */
