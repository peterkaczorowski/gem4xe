/* m16_desk.c -- the stand-in desktop of tests/emu/m16_shell.py.
 *
 * A gem4xe application like any other (src/app/gem.h), built as
 * DESKTOP.G4A on the milestone-3 disk (build/m14-boot.atr) in place of
 * the real desktop (src/desk/), so that the shell loop in
 * src/aes/shel.c can be driven from the keyboard by a gate: it loads
 * this first, runs whatever it asks for with shel_write, and loads it
 * again when that returns, until it asks to shut down.  A line of help,
 * then a key:
 *
 *     R   run M11.G4A, the gate application, and come back
 *     C   run CALC.G4A, and K CLOCK.G4A -- the two programs, which
 *         are gated through this same loop (tests/emu/m22_apps.py)
 *     B   run M29.G4A, which is compiled --data-model=large and keeps
 *         its variables in far memory (tests/emu/m29_big.py)
 *     X   ask for NOPE.G4A, which is not there: the shell's alert, then
 *         the desktop again
 *     Q   shut GEM down and return to DOS
 *
 * The entry, the exit and the two shel_write calls are the contract the
 * real desktop keeps.
 */
#include "gem.h"

int main(void)
{
    WORD work_in[11], work_out[57];
    WORD handle, wchar, hchar, wbox, hbox, k;

    appl_init();
    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (k = 0; k < 10; k++)
        work_in[k] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    vst_color(handle, 1);
    v_gtext(handle, 8, hbox + 16, "gem4xe desktop -- R M11, C CALC, K CLOCK, B M29, X a missing one, Q quits");

    for (;;) {
        k = (WORD)(evnt_keybd() & 0x00FF);
        if (k == 'r' || k == 'R') {
            shel_write(SHW_EXEC, 1, 0, "M11.G4A", "\0");
            break;
        }
        /* Into the folder first, then the program by its full path --
         * which is what the real desktop does before it runs one
         * (src/desk/deskwin.c do_aopen), and what makes this gate cover
         * the thing that was broken: an application in a FOLDER finding
         * its own resource beside it (docs/phase29.md). */
        if (k == 'c' || k == 'C') {
            Dsetpath("A:\\APPS");
            shel_write(SHW_EXEC, 1, 1, "A:\\APPS\\CALC.G4A", "\0");
            break;
        }
        if (k == 'k' || k == 'K') {
            Dsetpath("A:\\APPS");
            shel_write(SHW_EXEC, 1, 1, "A:\\APPS\\CLOCK.G4A", "\0");
            break;
        }
        /* B: the large-data program (src/m29_big.c).  It is in the root
         * rather than in \APPS\ because what test-m29 is about is the
         * MEMORY MODEL, not the path. */
        if (k == 'b' || k == 'B') {
            shel_write(SHW_EXEC, 1, 0, "M29.G4A", "\0");
            break;
        }
        if (k == 'x' || k == 'X') {
            shel_write(SHW_EXEC, 1, 0, "NOPE.G4A", "\0");
            break;
        }
        if (k == 'q' || k == 'Q') {
            shel_write(SHW_SHUTDOWN, 0, 0, "", "\0");
            break;
        }
    }
    v_clsvwk(handle);
    appl_exit();
    return 0;
}
