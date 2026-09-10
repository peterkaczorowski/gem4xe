/* m28_acc.c -- the Phase 36 gate accessory.
 *
 * The smallest thing that is genuinely a desk accessory, in the shape
 * the donor's own skeleton has: register a name in the Desk menu, then
 * an indefinite message loop that never exits.  It is to the accessories
 * what src/m11_app.c is to the applications -- not a demonstration but a
 * thing whose every observable is a number the gate can read out of the
 * pool by symbol.
 *
 * WHAT IT PROVES BY EXISTING.  An accessory is co-resident: it is loaded
 * before the desktop, it keeps its memory while the desktop runs above
 * it, and it survives every program the desktop starts and every return
 * to the desktop.  So it counts the turns it has been given and the
 * messages it has been sent, and the gate reads those counters at points
 * where the desktop has been up, gone away and come back.
 *
 * THE TITLE IS A STATIC, and that is the contract rather than a
 * convenience: menu_register keeps the POINTER, not a copy (the AES puts
 * it straight into an object's ob_spec), so a title in a local or in
 * memory the accessory might free is a dangling pointer in the menu.
 * The two leading spaces are the donor's own convention -- they line the
 * name up with the desktop's "About" item above it.
 */
#include "gem.h"

/* Everything the gate reads, by symbol out of build/m28_acc.sym. */
WORD acc_id;                    /* what appl_init answered */
WORD acc_menu;                  /* what menu_register answered */
WORD acc_msgs;                  /* messages taken from the queue */
WORD acc_opens;                 /* AC_OPEN */
WORD acc_closes;                /* AC_CLOSE */
WORD acc_last[8];               /* the last message, word for word */
WORD acc_ticks;                 /* timer waits completed: it is alive */

static char acc_title[] = "  Gate accessory";

int main(void)
{
    WORD msg[8];
    WORD mx, my, mb, ks, kr, br;
    WORD i;

    acc_id = appl_init();
    acc_menu = menu_register(acc_id, acc_title);

    /* The loop a desk accessory never leaves.  A timer goes with the
     * message so that the accessory is a process with something to wake
     * up FOR: a message alone would make it invisible to the scheduler
     * between AC_OPENs, and the point of the count is to show it running
     * while the desktop runs. */
    for (;;) {
        WORD what = evnt_multi((UWORD)(MU_MESAG | MU_TIMER), 1, 1, 1,
                               0, 0, msg, 500, 0,
                               &mx, &my, &mb, &ks, &kr, &br);
        if (what & MU_TIMER)
            acc_ticks++;
        if (!(what & MU_MESAG))
            continue;
        acc_msgs++;
        for (i = 0; i < 8; i++)
            acc_last[i] = msg[i];
        if (msg[0] == AC_OPEN)
            acc_opens++;
        else if (msg[0] == AC_CLOSE)
            acc_closes++;
    }
}
