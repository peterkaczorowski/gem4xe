/* clockapp.c -- the clock as a PROGRAM.
 *
 * src/apps/clock.c is the clock itself, and it is the same object file in
 * two programs: CLOCK.G4A, which is this, and CLOCK.ACC, which is
 * src/apps/clockacc.c.  The difference between an application and a desk
 * accessory is all here and in that file -- when the resource is taken,
 * and what the thing does when its panel is put away.  An application
 * exits; an accessory goes back to waiting for the next AC_OPEN.
 */
#include "gem.h"

WORD clock_start(void);
void clock_panel(void);

int main(void)
{
    appl_init();
    if (!clock_start()) {
        appl_exit();
        return 1;
    }
    clock_panel();
    rsrc_free();
    appl_exit();
    return 0;
}
