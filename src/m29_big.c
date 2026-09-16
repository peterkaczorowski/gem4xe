/* m29_big.c -- an application compiled --data-model=large, which is the
 * model the two programs this project exists for are written to.
 *
 * GACS's engine is 24 KB of code, 56 KB of data and 18 KB of constants,
 * and it wants 84 bytes of bank $00 (docs/gacs.md, `make gacs-check`).
 * It gets there by being compiled with 24-bit pointers, so that every
 * global lives in far memory -- and until now gem4xe's application
 * linker map had nowhere to put one: it named `farcode`, `switch`,
 * `cfar`, `libcode` and `code`, but neither of the two sections a
 * large-data program's VARIABLES go in.
 *
 * So this is the smallest program that proves the map: it has static far
 * data of both kinds and it checks both, then writes what it found into
 * NEAR variables, where a gate can read them by symbol against the
 * address the loader reported (app_near).
 *
 *   ZFAR -- big[], which carries no bits and is zeroed by the crt.  The
 *   program requires it to arrive zeroed, fills it with a pattern that
 *   depends on the index, and sums it: a sum that is right is a far
 *   array that was addressed correctly across its whole length, which a
 *   16-bit pointer silently wrapping inside a bank would not give.
 *
 *   FAR -- seed[], which carries bits.  An initialised far array is the
 *   harder case, because its values have to be COPIED there at start-up
 *   by the crt's data_init_table walk, from an initialiser the linker
 *   puts somewhere else.  If that does not happen the array reads as
 *   zeroes and nothing else goes wrong, which is exactly the kind of
 *   quiet failure this file exists to make loud.
 */
#include "portab.h"
#include "gem.h"

#define BIG  3000

/* far, --data-model=large: the compiler decides, and the map must have
 * somewhere to put them. */
static WORD big[BIG];                       /* zfar: no bits */
static WORD seed[8] = { 11, 22, 33, 44, 55, 66, 77, 88 };   /* far: bits */

/* ...and these are NEAR, explicitly.  Under --data-model=large every
 * global goes far unless it is told otherwise, which is the whole point
 * of the model -- so a program that wants something in bank $00 says so.
 * A real shell does the same for the handful of things it hands to the
 * AES, since a tree or a string the AES is given must be in bank $00
 * (src/sys/abi.c, near_of).  Here it is so the gate can read them. */
NEAR WORD m29_zeroed;         /* big[] arrived zeroed */
NEAR WORD m29_seedok;         /* seed[] arrived initialised */
NEAR WORD m29_sum;            /* the pattern's sum, low word */
NEAR WORD m29_first, m29_last;/* big[0] and big[BIG-1] after filling */
NEAR WORD m29_ran;            /* it got to the end */
NEAR WORD m29_step;           /* ...and how far it got, if it did not */
NEAR WORD m29_alert;          /* form_alert with a FAR string: the button, or -1 */
NEAR WORD m29_wfar;           /* wind_set WF_NAME with a FAR title: 0, refused */
NEAR WORD m29_wnear;          /* ...and with a NEAR one: 1, taken */

/* In --data-model=large this literal is FAR (Calypsi cfar); the AES reads
 * a string near, so the shim must bounce it or form_alert draws nothing.
 * LONGER THAN THE 64-BYTE NEAR SCRATCH ON PURPOSE -- 102 bytes: an alert
 * that a screen can hold does not fit there, and the shim takes the AES's
 * pool for it instead (src/sys/abi.c, pool_str). */
static const char s_alert[] =
    "[1][a far alert string that is much"
    "|longer than the sixty-four byte"
    "|near scratch the shim keeps][ OK ]";

/* A window title is the string the shim CANNOT bounce: the AES keeps it
 * and reads it again at every redraw, so a scratch that lasts the call is
 * no use.  Bouncing it where the DRAWING happens would work and was
 * measured -- 116 bytes against the 16 LoRAM has spare -- so until bank
 * $00 is rebalanced a far one is refused and a near one taken
 * (src/sys/abi.c, case 105). */
static const char s_title[] = " far title ";
NEAR char n_title[] = " near title ";

int main(void)
{
    WORD i;
    int32_t sum = 0;

    m29_step = 1;               /* entered main at all */
    appl_init();
    m29_step = 2;               /* ...and came back from appl_init */

    m29_zeroed = 1;
    for (i = 0; i < BIG; i++)
        if (big[i] != 0) {
            m29_zeroed = 0;
            break;
        }

    m29_step = 3;               /* read 3,000 far words */

    m29_seedok = 1;
    for (i = 0; i < 8; i++)
        if (seed[i] != (WORD)(11 * (i + 1)))
            m29_seedok = 0;

    m29_step = 4;               /* read the initialised far array */

    /* A pattern that depends on the index, so a pointer that wrapped
     * would sum differently rather than not at all. */
    for (i = 0; i < BIG; i++)
        big[i] = (WORD)(seed[i & 7] + i);
    for (i = 0; i < BIG; i++)
        sum += big[i];

    m29_step = 5;               /* wrote and summed them */
    m29_sum = (WORD)(sum & 0xFFFF);
    m29_first = big[0];
    m29_last = big[BIG - 1];
    m29_ran = 1;

    {
        WORD h = wind_create(NAME, 0, 16, 320, 100);
        m29_wfar = m29_wnear = -2;          /* no window to ask with */
        if (h >= 0) {
            m29_wfar = wind_set_str(h, WF_NAME, s_title);
            m29_wnear = wind_set_str(h, WF_NAME, n_title);
            wind_delete(h);
        }
    }

    /* A far string to the AES, the peer's reported bug (GACS could not
     * form_alert about its missing .DAT tables): the button it returns,
     * not -1, says near_str bounced the far literal. */
    m29_alert = form_alert(1, s_alert);

    /* ...and WAIT, because a program that returns has its near region
     * given back and the desktop loaded on top of it before a gate can
     * read anything out of it (src/sys/app.c, app_free).  The gate reads
     * the results and then sends a key. */
    evnt_keybd();

    appl_exit();
    return 0;
}
