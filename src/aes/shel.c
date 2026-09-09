/* shel.c -- the shell library: shel_read, shel_write, shel_get, shel_put,
 * shel_find, shel_envrn, and the shell loop that runs the desktop and
 * the programs it asks for.  EmuTOS aes/gemshlib.c.
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
 * run next and how.  Acting on it is sh_main's, the shell loop: the
 * desktop, then whatever it asked for, then the desktop again, until
 * something asks to shut down (SHW_SHUTDOWN).  The request sits in
 * sh_doexec for whoever asks (src/m3_vdi.c reads it back in the gate).
 *
 * NAMES.  A GEM application names a file the TOS way, X:\DIR\NAME.EXT,
 * and the Atari OS the CIO way: Dn:NAME.EXT on DOS 2, Dn:>DIR>NAME.EXT
 * on a SpartaDOS.  sh_cioname maps the first onto whichever the machine
 * booted -- the rule is the DOS seam's, src/sys/dos.h -- and leaves
 * anything else alone for CIO to judge, which is where a name with no
 * device at all gets its D: (src/sys/cio.c).  Every file the AES opens
 * on an application's behalf goes through it.
 */
#include <string.h>
#include "aes/aes.h"
#include "sys/app.h"
#include "sys/cio.h"
#include "sys/dos.h"
#include "sys/gemdos.h"
#include "sys/farmem.h"
#include "lang_rsc.h"

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
static WORD sh_next;            /* what runs next: SH_DESKTOP, SH_PROGRAM */
static uint32_t sh_cmd_far, sh_tail_far, sh_buf_far;   /* 0 until sh_init */

#define SH_DESKTOP  0
#define SH_PROGRAM  1

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
        far_fill(sh_buf_far, 0, SH_BUFLEN);   /* the donor's is zeroed BSS:
                                               * the desktop tests its first
                                               * byte for '#' */
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
        sh_next = SH_DESKTOP;
        break;
    case 1:                                 /* SHW_EXEC */
        far_strput(sh_cmd_far, pcmd, SH_CMDLEN);
        far_put(sh_tail_far, (const uint8_t *)ptail, SH_TAILLEN);
        sh_doexec = doex;
        sh_isgem = (isgem != 0);
        sh_next = SH_PROGRAM;
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

/* The buffer the application names is a full 24-bit address, as the ST's
 * is a 32-bit one: the desktop keeps its copy in far memory. */
void sh_get(uint32_t pbuffer, WORD len)
{
    if (!sh_buf_far || len <= 0)
        return;
    if (len > SH_BUFLEN)
        len = SH_BUFLEN;
    far_copy(pbuffer, sh_buf_far, (uint16_t)len);
}

void sh_put(uint32_t pdata, WORD len)
{
    if (!sh_buf_far || len <= 0)
        return;
    if (len > SH_BUFLEN)
        len = SH_BUFLEN;
    far_copy(sh_buf_far, pdata, (uint16_t)len);
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

/* A GEM path into the name CIO opens: the DOS seam's rule (src/sys/dos.h).
 * `cio` holds CIO_NAME_MAX + 1. */
void sh_cioname(const char *gem, char *cio)
{
    /* Through GEMDOS, not straight to the DOS: a resource named without
     * a path belongs to the directory Dsetpath last named, which is the
     * application's own -- the desktop changes into it before it runs
     * one (src/desk/deskwin.c do_aopen).  Before this, an application in
     * a folder could not find its own resource: it opened in whatever
     * directory the boot batch had left the DOS in. */
    gd_cioname(gem, cio);
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

/* ---- the shell loop ------------------------------------------------- */

/* The desktop's file, read once and kept.  The far heap above the
 * shell's own buffers belongs to whichever program is running and is
 * wound back when it exits (app_free), so the desktop's bytes are taken
 * before the first program and stay for every return to it: a loaded
 * desktop costs a copy, not a disk read. */
#define SH_DESKNAME "DESKTOP.G4A"
static uint32_t sh_desk_blob, sh_desk_len;

WORD sh_runs;                   /* programs started, the desktop included */
WORD sh_lastret;                /* what the last one's main() returned */
WORD sh_lastrc;                 /* the last load's status (APP_*) */

/* The desktop or the requested program: loaded, run, freed.  The load
 * status, which sh_main reports to the user before the next iteration;
 * and after a program the desktop is what runs next unless the program
 * asked otherwise -- the donor's rule, set before the program runs so
 * that its own shel_write wins. */
static WORD sh_ldapp(void)
{
    APP app;
    char cmd[SH_CMDLEN];
    WORD st, was = sh_next;

    if (was == SH_DESKTOP) {
        st = app_load((const uint8_t __far *)sh_desk_blob, sh_desk_len, &app);
    } else {
        far_strget(cmd, sh_cmd_far, SH_CMDLEN);
        sh_next = SH_DESKTOP;
        sh_isgem = 1;
        st = app_load_file(cmd, &app);
    }
    sh_lastrc = st;
    if (st != APP_OK) {
        if (was == SH_DESKTOP)      /* nothing to return to: the loop ends */
            sh_doexec = 4;
        return st;
    }
    sh_runs++;
    sh_doexec = -1;                 /* what the program asks for */
    sh_lastret = app_exec(&app);
    app_free(&app);
    /* A desktop that returns without asking for anything has nothing
     * left to do: that is a shutdown, not the desktop again forever. */
    if (was == SH_DESKTOP && sh_doexec == -1)
        sh_doexec = 4;
    return 0;
}

/* The donor's sh_main: until a shutdown, reset the windows and the menu,
 * clear the screen, report the last failure, run the next thing.  The
 * one departure is what a load failure of the desktop itself means:
 * the donor has a ROM desktop that cannot fail to load, and here it is
 * a file, so the loop ends and the caller hears which way (a negative
 * APP_* status); otherwise the count of programs run. */
WORD sh_main(void)
{
    WORD rc = 0;

    if (!sh_cmd_far)
        return APP_E_POOL;
    if (!sh_desk_blob) {
        char cio[CIO_NAME_MAX + 1];
        sh_cioname(SH_DESKNAME, cio);
        sh_desk_blob = far_read_file(cio, &sh_desk_len);
        if (!sh_desk_blob)
            return APP_E_FILE;
    }
    sh_runs = 0;
    sh_lastret = 0;
    sh_lastrc = 0;
    sh_next = SH_DESKTOP;
    sh_isgem = 1;
    sh_doexec = 0;
    do {
        wm_init();
        mn_init();
        ratinit();                          /* the pointer on, as the donor */
        /* ...and the ARROW, which the donor does not do here because it
         * does not have to: a form belongs to a PROCESS there, and
         * set_mown gives the mouse's new owner its own form back
         * (geminput.c).  gem4xe runs one process, so the form is one
         * global and the shell is the only thing between two programs
         * that can put it right.  Without this the desktop's hourglass
         * -- desk_busy(TRUE), set just before shel_write and deliberately
         * never cleared, because the donor's desktop is about to stop
         * owning the mouse -- stays over the program it started, for as
         * long as that program runs. */
        gr_mouse(ARROW, 0);
        gsx_sclip(&gl_rscreen);
        ob_draw(gl_wtree, ROOT, 0);         /* the desk, edge to edge */
        if (rc)
            fm_alert(1, rc == APP_E_FILE
                        ? lang_str(LS_APPNOTFOUND)
                        : lang_str(LS_APPNOTLOAD));
        rc = sh_ldapp();
    } while (sh_doexec != 4);
    return rc ? rc : sh_runs;
}
