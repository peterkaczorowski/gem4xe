/* scrap.c -- the scrap manager: scrp_read and scrp_write, which are the
 * AES's record of WHERE the clipboard is.  EmuTOS aes/gemsclib.c.
 *
 * The AES keeps a directory path and nothing else.  The scrap itself is
 * files called SCRAP.* in that directory, written and read by the
 * applications themselves, so what the AES arbitrates is a place rather
 * than a format -- which is why two programs that have never heard of
 * each other can still cut and paste.
 *
 * Neither call validates the path.  TOS stopped doing that and the donor
 * follows it, with the reason in its comment: an application that asks
 * for the scrap directory before anybody has set one wants an answer it
 * can act on, not a refusal.  So sc_read hands back whatever is there,
 * empty string included, and answers TRUE either way.
 *
 * It lives in far memory for the same reason shel.c's buffers do: it is
 * state the AES holds on the applications' behalf, across their
 * lifetimes, and far_alloc is a bump allocator that app_free winds back
 * to where app_load found it.  So it is taken ONCE, at start-up, before
 * any application is loaded -- anything taken after one would go with it.
 */
#include "portab.h"
#include "aes/aes.h"
#include "sys/farmem.h"

/* SH_CMDLEN's number and SH_CMDLEN's reason: a scrap directory is a path
 * like any other, and the donor's sc_clear appends SCRAP.* to this same
 * buffer to search it, so a full path AND a filename have to fit. */
#define SC_PATHLEN  128

static uint32_t sc_path_far;            /* 0 until sc_init */

void sc_init(void)
{
    if (sc_path_far)                    /* the AES starts up once per script */
        return;
    sc_path_far = far_alloc(SC_PATHLEN);
    if (sc_path_far)
        far_write8(sc_path_far, 0);     /* no scrap directory yet */
}

WORD sc_read(char *pscrap)
{
    if (!sc_path_far)
        return 0;
    far_strget(pscrap, sc_path_far, SC_PATHLEN);
    return 1;
}

WORD sc_write(const char *pscrap)
{
    if (!sc_path_far)
        return 0;
    far_strput(sc_path_far, pscrap, SC_PATHLEN);
    return 1;
}
