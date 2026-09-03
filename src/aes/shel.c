/* shel.c -- the shell library: shel_read, shel_write, shel_get, shel_put,
 * shel_find, shel_envrn.  EmuTOS aes/gemshlib.c, without the shell loop.
 *
 * What the donor's shell library keeps -- the command and tail the next
 * application starts with, the environment, and the 4 KB the desktop
 * parks its state in between programs -- is nothing the AES reads itself;
 * it is memory it lends the applications, across their lifetimes.  So it
 * lives in far memory, taken from the far allocator ONCE, at AES start-up
 * and before any application is loaded: far_alloc is a bump allocator that
 * app_free winds back to where app_load found it, and anything taken
 * after an application would go with it.  The environment is the one
 * exception, a constant in bank $00, because shel_envrn hands out a
 * pointer INTO it and an application's pointers are 16 bits.
 *
 * shel_write records a request, as the donor's does: which program to
 * run next and how.  Acting on it is the shell loop's job, which gem4xe
 * does not have until there is a desktop to return to; the request sits
 * in sh_doexec for whoever asks (src/m3_vdi.c reads it back in the gate).
 *
 * NAMES.  A GEM application names a file the TOS way, X:\DIR\NAME.EXT,
 * and the Atari OS the CIO way, Dn:NAME.EXT.  sh_cioname maps the first
 * onto the second -- drive A..H is D1..D8 and the directory part is
 * dropped, because DOS 2 has no directories (a SpartaDOS path is a debt,
 * docs/phase11.md) -- and leaves anything else alone for CIO to judge,
 * which is where a name with no device at all gets its D: (src/sys/cio.c).
 * Every file the AES opens on an application's behalf goes through it.
 */
#include <string.h>
#include "aes/aes.h"
#include "sys/app.h"
#include "sys/cio.h"
#include "sys/farmem.h"

#define SH_CMDLEN   128         /* MAXPATHLEN: the command, NUL-terminated */
#define SH_TAILLEN  128         /* CMDTAILSIZE: architectural              */
#define SH_BUFLEN   4192        /* SIZE_SHELBUF: TOS 1.04's, as EmuTOS      */

/* The environment, the TOS way: NAME=<NUL>value<NUL> ... <NUL>.  TOS puts
 * the NUL right after PATH= and the search list after it, and every
 * application that reads PATH= skips that NUL (the donor's sh_path does);
 * so does the one path gem4xe offers, the default drive. */
static const char sh_env[] = "PATH=\0D:\0";

WORD sh_doexec;                 /* the pending request: SHW_*, or -1  */
WORD sh_isgem;
static uint32_t sh_cmd_far, sh_tail_far, sh_buf_far;   /* 0 until sh_init */

void sh_init(void)
{
    if (sh_cmd_far)             /* the AES starts up once per script */
        return;
    sh_cmd_far  = far_alloc(SH_CMDLEN);
    sh_tail_far = far_alloc(SH_TAILLEN);
    sh_buf_far  = far_alloc(SH_BUFLEN);
    if (sh_cmd_far && sh_tail_far && sh_buf_far) {
        far_write8(sh_cmd_far, 0);
        far_write8(sh_tail_far, 0);
    }
    sh_doexec = -1;
    sh_isgem = 0;
}

void sh_read(char *pcmd, char *ptail)
{
    if (!sh_cmd_far)
        return;
    far_strget(pcmd, sh_cmd_far, SH_CMDLEN);
    far_get((uint8_t *)ptail, sh_tail_far, SH_TAILLEN);
}

WORD sh_write(WORD doex, WORD isgem, WORD isover, const char *pcmd,
              const char *ptail)
{
    (void)isover;
    if (!sh_cmd_far)
        return 0;
    switch (doex) {
    case 0:                                 /* SHW_NOEXEC: the desktop */
        far_write8(sh_cmd_far, 0);
        sh_doexec = doex;
        sh_isgem = 1;
        break;
    case 1:                                 /* SHW_EXEC */
        far_strput(sh_cmd_far, pcmd, SH_CMDLEN);
        far_put(sh_tail_far, (const uint8_t *)ptail, SH_TAILLEN);
        sh_doexec = doex;
        sh_isgem = (isgem != 0);
        break;
    case 4:                                 /* SHW_SHUTDOWN */
        sh_doexec = doex;
        sh_isgem = 0;
        break;
    case 5:                                 /* SHW_RESCHNG: one resolution */
        far_write8(sh_cmd_far, 0);
        far_write8(sh_tail_far, 0);
        break;
    default:
        break;
    }
    return 1;
}

void sh_get(void *pbuffer, WORD len)
{
    if (!sh_buf_far || len <= 0)
        return;
    if (len > SH_BUFLEN)
        len = SH_BUFLEN;
    far_get(pbuffer, sh_buf_far, (uint16_t)len);
}

void sh_put(const void *pdata, WORD len)
{
    if (!sh_buf_far || len <= 0)
        return;
    if (len > SH_BUFLEN)
        len = SH_BUFLEN;
    far_put(sh_buf_far, pdata, (uint16_t)len);
}

/* The donor's, on the constant: the value after the name, or NULL. */
void sh_envrn(const char **ppath, const char *psrch)
{
    const char *p;
    WORD len = (WORD)strlen(psrch);

    *ppath = 0;
    for (p = sh_env; *p; ) {
        if (strncmp(p, psrch, len) == 0) {
            *ppath = p + len;
            break;
        }
        while (*p++)
            ;
    }
}

/* X:\DIR\NAME.EXT -> Dn:NAME.EXT, uppercased; anything else copied as it
 * is (CIO adds D: to a bare name).  `cio` holds CIO_NAME_MAX + 1. */
void sh_cioname(const char *gem, char *cio)
{
    const char *name = gem;
    WORD k = 0;

    if (gem[0] && gem[1] == ':' && gem[2] == '\\') {
        char d = (char)(gem[0] | 0x20);
        const char *p;
        if (d >= 'a' && d <= 'h') {
            cio[k++] = 'D';
            cio[k++] = (char)('1' + (d - 'a'));
            cio[k++] = ':';
        }
        for (p = gem + 3; *p; p++)          /* the last component */
            if (*p == '\\')
                name = p + 1;
        if (name == gem)
            name = gem + 3;
    }
    for (; *name && k < CIO_NAME_MAX; name++, k++) {
        WORD c = (uint8_t)*name;    /* a WORD, not a char: B8, tools/ccbug */
        if (c >= 'a' && c <= 'z')
            c -= 0x20;
        cio[k] = (char)c;
    }
    cio[k] = 0;
}

/* Does the file exist where the name says, or on the default drive (the
 * one entry in PATH=, and where CIO looks for a bare name anyway)?  The
 * donor rewrites pspec to where it found the file; here the name it was
 * given is already the name that opens it, so pspec is left alone. */
WORD sh_find(char *pspec)
{
    char name[CIO_NAME_MAX + 1];
    int16_t fd;

    sh_cioname(pspec, name);
    fd = cio_open(name, CIO_A_READ, 0);
    if (fd < 0)
        return 0;
    cio_close(fd);
    return 1;
}
