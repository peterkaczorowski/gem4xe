/* gemdos.c -- GEMDOS on CIO.  See gemdos.h.
 *
 * Every function here has the ST's contract and CIO's means, and the
 * gap between them is written at the function.  Bank $00 has no room
 * for buffers (src/gem4xe.scm), so paths and CIO names live on the
 * stack for the length of a call, a directory read ahead borrows a
 * slice of the application pool (src/sys/app.h) and returns it before
 * the call ends, and everything that outlives a call -- the DTA's
 * search state, the per-drive directories -- is in far memory.
 */
#include "portab.h"
#include <string.h>
#include "gemdos.h"
#include "cio.h"
#include "dos.h"
#include "farmem.h"
#include "clock.h"
#include "app.h"
/* ...and the AES's processes, because a program's open files are its
 * own (gemdos_release).  aes.h brings WORD/UWORD/LONG with it, which
 * this file used to declare for itself. */
#include "../aes/proc.h"

typedef int32_t  LONG;

#define GD_DRIVES   8
#define GD_DIRMAX   64          /* a drive's current directory, NUL in */
#define GD_PATHMAX  128         /* a composed path, NUL in */
#define GD_NAMEMAX  13          /* NAME.EXT, NUL in */
#define GD_KEEPNAME (CIO_NAME_MAX + 1)  /* the CIO name a handle was opened
                                         * with, kept for a seek backwards */
#define GD_SLOTS    7
#define GD_CACHE    1024        /* a search's read-ahead, in bytes */
#define GD_RAWENT   23          /* an SDFS directory entry */
#define GD_DATE0    0x0021      /* 1980-01-01: a DOS without stamps */

/* A search slot in far memory: */
#define SL_OWNER    0           /* LONG  the DTA that owns it; 0 = free */
#define SL_IOCB     4           /* BYTE  the directory's IOCB, 0 closed */
#define SL_RAW      5           /* BYTE  1: SDFS entries; 0: DOS 2 lines */
#define SL_LEN      6           /* UWORD bytes in the cache */
#define SL_CUR      8           /* UWORD the cursor in it */
#define SL_AGE      10          /* BYTE  for the least recently used */
#define SL_ATTR     11          /* BYTE  the search's attribute mask */
#define SL_PAT      12          /* the pattern, GD_NAMEMAX */
#define SL_CACHE    32
#define SL_SIZE     (SL_CACHE + GD_CACHE)

#define DRVBYT      (*(const uint8_t *)0x070A)  /* DOS 2: drives present */

static uint32_t gd_pb;          /* the call block */
static uint32_t gd_dirs;        /* GD_DRIVES x GD_DIRMAX */
/* ...and whether each one has been SET, which is not the same as being
 * the root.  gem4xe keeps its own current directory because the DOS
 * underneath has no GEMDOS to keep one -- but at start-up it does not
 * know where that DOS already is (a boot batch may have changed into a
 * directory before GEM ran), so an UNSET drive means "wherever the DOS
 * is", and a set one means "here, absolutely".  gd_cioname is where the
 * difference is spent. */
static uint8_t gd_dirset[GD_DRIVES];
/* A DEFAULT DTA EACH, not one shared.  A search is found again by the
 * DTA that owns it (SL_OWNER), so two processes both using "the default"
 * would be handed each other's directory position.  gemdos_init takes
 * NUM_PROCS of them, one per process, and gd_dta0 is the first. */
static uint32_t gd_dta0;
#define gd_dta      (rlr->p_gddta)      /* the running process's */
static uint32_t gd_slots;       /* GD_SLOTS x SL_SIZE */
static uint8_t  gd_drive;       /* the current drive, 0 = A */
#define gd_owned    (rlr->p_gdowned)    /* the IOCBs Fopen has out, bit n */
/* Where each open file is, and how to get back to its start.  GEMDOS
 * counts a file in BYTES from the beginning and CIO does not count at
 * all, so the count is kept here, one per IOCB: every Fread and Fwrite
 * adds what it moved, and Fseek is the only thing that reads it.  The
 * name and the mode go with it, because a seek BACKWARDS on a DOS whose
 * POINT is not byte-addressable is a reopen (gd_seek).
 *
 * All of it FAR, in one record per IOCB.  Eight of these are 552 bytes
 * and bank $00 has none to give: putting the position alone in near
 * memory -- 32 bytes -- was enough to make the linker refuse the
 * program (src/gem4xe.scm: WHAT GOES FAR AND WHAT MUST NOT).  The cost
 * is two far accesses per transfer, against a CIO round trip. */
#define FH_AT    0              /* LONG  bytes from the start */
#define FH_MODE  4              /* BYTE  the aux1 it was opened with */
#define FH_NAME  5              /* the GEMDOS path it was opened by:
                                 * a reopen maps it as an open does, and
                                 * Fdatime looks it up in the directory */
#define FH_SIZE  (FH_NAME + GD_KEEPNAME)
static uint32_t gd_files;               /* CIO_IOCBS x FH_SIZE, far */
static uint32_t gd_scratch;             /* a DTA and a path, for Fdatime */

#define gd_ateof    (rlr->p_gdateof)
static uint8_t  gd_ateof_doc;   /* ...and those a read has taken to the end.
                                 * GEMDOS says 0 at the end, every time; DOS
                                 * II+/D 6.4 says it once, and on the read
                                 * after that hands the file's last partial
                                 * sector over again (24 bytes, status $88). */
static uint8_t  gd_age;

uint16_t gemdos_calls, gemdos_bad;

/* ---- far memory, typed ---------------------------------------------- */

static UWORD rd16(uint32_t a) { return *(const UWORD FAR *)a; }
static LONG  rd32(uint32_t a) { return *(const LONG FAR *)a; }
static void  wr16(uint32_t a, UWORD v) { *(UWORD FAR *)a = v; }
static void  wr32(uint32_t a, LONG v) { *(LONG FAR *)a = v; }

static uint32_t gd_file(uint8_t iocb)
{
    return gd_files + (uint32_t)iocb * FH_SIZE;
}

static uint32_t gd_where(uint8_t iocb)
{
    return (uint32_t)rd32(gd_file(iocb) + FH_AT);
}

static void gd_moved(uint8_t iocb, uint32_t by)
{
    wr32(gd_file(iocb) + FH_AT, (LONG)(gd_where(iocb) + by));
}

/* The block's arguments, at the ST's offsets plus four. */
static WORD arg_w(uint8_t off) { return *(const WORD FAR *)(gd_pb + off); }
static LONG arg_l(uint8_t off) { return *(const LONG FAR *)(gd_pb + off); }

/* ---- errors --------------------------------------------------------- */

/* A CIO status into GEMDOS's error; `dir` when the name was a directory's. */
static LONG gd_err(uint8_t st, WORD dir)
{
    switch (st) {
    case CIO_E_NOTFOUND:            /* $AA */
        return dir ? GD_EPTHNF : GD_EFILNF;
    case 0x96:                      /* SpartaDOS: directory not found */
        return GD_EPTHNF;
    case 0x8A: case 0x8B: case 0xA0: /* timeout, NAK, drive number */
        return GD_EDRIVE;
    case CIO_E_INUSE: case 0xA1:    /* IOCB in use, too many open */
        return GD_ENHNDL;
    case CIO_E_NOTOPEN:
        return GD_EIHNDL;
    default:                        /* locked, full, exists, not empty... */
        return GD_EACCDN;
    }
}

/* ---- paths ---------------------------------------------------------- */

/* The application's path -- relative or absolute, with a drive or
 * without -- into its full form: X:\ for a root, X:\DIR\NAME otherwise,
 * uppercased, "." and ".." folded, never a trailing backslash.  `full`
 * holds GD_PATHMAX.  Returns the drive, 0..7, or an error. */
static LONG gd_full(const char *in, char *full)
{
    const char *p = in;
    WORD d = gd_drive, k;

    if (in[0] && in[1] == ':') {
        int c = (uint8_t)in[0];         /* not a byte: B8, tools/ccbug */
        if (c >= 'a' && c <= 'z')
            c -= 0x20;
        if (c < 'A' || c >= 'A' + GD_DRIVES)
            return GD_EDRIVE;
        d = c - 'A';
        p = in + 2;
    }
    full[0] = (char)('A' + d);
    full[1] = ':';
    full[2] = '\\';
    full[3] = 0;
    k = 3;
    if (*p == '\\')
        p++;
    else {                              /* from the drive's directory */
        far_strget(full + 2, gd_dirs + (uint32_t)d * GD_DIRMAX, GD_DIRMAX);
        if (full[2] != '\\') {          /* the root: "" */
            full[2] = '\\';
            full[3] = 0;
        }
        k = (WORD)strlen(full);
    }
    while (*p) {
        const char *e = p;
        WORD n;
        while (*e && *e != '\\')
            e++;
        n = (WORD)(e - p);
        if (n == 1 && p[0] == '.') {
            ;
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (k > 3 && full[k - 1] != '\\')
                k--;
            if (k > 3)
                k--;
        } else if (n) {
            if (k + 1 + n >= GD_PATHMAX)
                return GD_EPTHNF;
            if (k > 3)
                full[k++] = '\\';
            for (; p < e; p++) {
                int c = (uint8_t)*p;
                if (c >= 'a' && c <= 'z')
                    c -= 0x20;
                full[k++] = (char)c;
            }
        }
        p = e;
        if (*p == '\\')
            p++;
    }
    full[k] = 0;
    return d;
}

/* Where the last component of a full path starts: 3 at the root. */
static WORD gd_split(const char *full)
{
    WORD k = (WORD)strlen(full);

    while (k > 3 && full[k - 1] != '\\')
        k--;
    return k;
}

/* A full path into the name CIO opens, or EPTHNF where the DOS has no
 * directories and the path has one.  `cio` holds CIO_NAME_MAX + 1. */
static LONG gd_cio(const char *full, char *cio)
{
    if (!dos.dirsep && gd_split(full) > 3)
        return GD_EPTHNF;
    dos_cioname(full, cio);
    return 0;
}

/* The path work -- two paths and a CIO name, 320 bytes -- is too much
 * for the 1 KB stack a call shares with its caller's own depth (an
 * application's COP is served on gem4xe's stack, under the launcher's
 * frames), so it is taken from the pool for the call and given back. */
typedef struct {
    char in[GD_PATHMAX];
    char full[GD_PATHMAX];
    char cio[CIO_NAME_MAX + 1];
} gd_work_t;
static gd_work_t *gw;

/* The application's path, fetched and composed into gw->full, to the
 * CIO name in gw->cio: the common opening of most functions. */
static LONG gd_name(LONG path)
{
    LONG d;

    far_strget(gw->in, (uint32_t)path, GD_PATHMAX);
    d = gd_full(gw->in, gw->full);
    if (d < 0)
        return d;
    return gd_cio(gw->full, gw->cio);
}

/* ---- the DTA and the search slots ----------------------------------- */

static void gd_dta_fill(const char *name, uint8_t attr, UWORD time,
                        UWORD date, uint32_t size)
{
    far_write8(gd_dta + DTA_ATTRIB, attr);
    wr16(gd_dta + DTA_TIME, time);
    wr16(gd_dta + DTA_DATE, date);
    wr32(gd_dta + DTA_LENGTH, (LONG)size);
    far_strput(gd_dta + DTA_FNAME, name, 14);
}

static uint32_t gd_slot(WORD i)
{
    return gd_slots + (uint32_t)i * SL_SIZE;
}

static void gd_slot_free(uint32_t sl)
{
    uint8_t iocb = far_read8(sl + SL_IOCB);

    if (iocb)
        cio_close(iocb);
    far_write8(sl + SL_IOCB, 0);
    wr32(sl + SL_OWNER, 0);
}

/* The slot the current DTA owns, or 0. */
static uint32_t gd_slot_of_dta(void)
{
    WORD i;

    for (i = 0; i < GD_SLOTS; i++)
        if ((uint32_t)rd32(gd_slot(i) + SL_OWNER) == gd_dta)
            return gd_slot(i);
    return 0;
}

/* A slot for a new search: the DTA's own if it has one, a free one,
 * else the one least recently used -- that search ends. */
static uint32_t gd_slot_new(void)
{
    uint32_t sl = gd_slot_of_dta(), oldest = gd_slot(0);
    WORD i;
    uint8_t age, best = 0;

    if (!sl) {
        for (i = 0; i < GD_SLOTS; i++) {
            uint32_t s = gd_slot(i);
            if (rd32(s + SL_OWNER) == 0) {
                sl = s;
                break;
            }
            age = (uint8_t)(gd_age - far_read8(s + SL_AGE));
            if (age >= best) {
                best = age;
                oldest = s;
            }
        }
        if (!sl)
            sl = oldest;
    }
    gd_slot_free(sl);
    far_write8(sl + SL_AGE, ++gd_age);
    return sl;
}

/* Read ahead into the slot's cache through a slice of the pool: as much
 * of the directory as fits.  The IOCB is closed when the whole of it
 * came; else it stays open for Fsnext to read on, an entry at a time. */
static void gd_slot_fill(uint32_t sl)
{
    uint16_t mark = pool_mark(), room = pool_room(), n, got;
    uint8_t *buf, iocb = far_read8(sl + SL_IOCB);
    WORD st;                            /* not a byte: B8, tools/ccbug */

    n = room > GD_CACHE ? GD_CACHE : (uint16_t)(room & ~1);
    if (n < 64)
        return;
    buf = pool_alloc(n, 2);
    if (!buf)
        return;
    st = cio_read(iocb, buf, n, &got);
    if (got > n)
        got = n;
    far_put(sl + SL_CACHE, buf, got);
    wr16(sl + SL_LEN, got);
    wr16(sl + SL_CUR, 0);
    pool_release(mark);
    if (st == CIO_E_EOF || st == CIO_OK_EOF || got < n
        || (st != CIO_OK)) {
        cio_close(iocb);
        far_write8(sl + SL_IOCB, 0);
    }
}

/* SDFS stamps to the DTA's packed forms.  Years 0..79 are 2000..2079,
 * 80..99 are 1980..1999 (SDX Programming Guide 4.50, the `date`
 * variable) -- which is DOS's own 1980 origin. */
static UWORD gd_date(uint8_t d, uint8_t mo, uint8_t y)
{
    UWORD year = (y < 80) ? 2000 + y : 1900 + y;

    if (mo < 1 || mo > 12 || d < 1 || d > 31)
        return GD_DATE0;
    return (UWORD)(((year - 1980) << 9) | (mo << 5) | d);
}

static UWORD gd_time(uint8_t h, uint8_t m, uint8_t s)
{
    if (h > 23 || m > 59 || s > 59)
        return 0;
    return (UWORD)((h << 11) | (m << 5) | (s >> 1));
}

/* Name and extension fields, space padded, into NAME.EXT. */
static void gd_rawname(const uint8_t *ent, char *name)
{
    WORD n = 8, e = 3, k;

    while (n > 0 && ent[6 + n - 1] == ' ')
        n--;
    while (e > 0 && ent[14 + e - 1] == ' ')
        e--;
    for (k = 0; k < n; k++)
        name[k] = (char)ent[6 + k];
    if (e) {
        name[n++] = '.';
        for (k = 0; k < e; k++)
            name[n++] = (char)ent[14 + k];
    }
    name[n] = 0;
}

/* The next entry of the slot's directory that the search wants, into
 * the DTA: 0, or ENMFIL with the slot freed. */
static LONG gd_next(uint32_t sl)
{
    uint8_t ent[GD_RAWENT + 8], raw = far_read8(sl + SL_RAW), want, attr;
    char name[GD_NAMEMAX], pat[GD_NAMEMAX];
    uint16_t len, cur, got, sectors;
    uint32_t size;
    UWORD date, time;

    far_strget(pat, sl + SL_PAT, GD_NAMEMAX);
    want = far_read8(sl + SL_ATTR);
    for (;;) {
        uint8_t iocb = far_read8(sl + SL_IOCB);
        WORD st;                        /* not a byte: B8 */

        len = rd16(sl + SL_LEN);
        cur = rd16(sl + SL_CUR);
        if (cur < len) {
            if (raw) {
                got = (uint16_t)(len - cur);
                if (got > GD_RAWENT)
                    got = GD_RAWENT;
                far_get(ent, sl + SL_CACHE + cur, got);
                cur += got;
            } else {
                got = 0;
                while (cur < len) {
                    uint8_t c = far_read8(sl + SL_CACHE + cur++);
                    if (c == CIO_EOL)
                        break;
                    if (got < sizeof ent)
                        ent[got++] = c;
                }
            }
            wr16(sl + SL_CUR, cur);
        } else if (iocb) {
            if (raw)
                st = cio_read(iocb, ent, GD_RAWENT, &got);
            else
                st = cio_getrec(iocb, ent, sizeof ent, &got);
            if (st == CIO_E_EOF || st == CIO_OK_EOF
                || (st != CIO_OK && st != CIO_E_TRUNC)) {
                cio_close(iocb);
                far_write8(sl + SL_IOCB, 0);
                if (st != CIO_OK_EOF && st != CIO_E_EOF)
                    break;
            }
            if (got == 0)
                break;
        } else
            break;

        if (raw) {
            if (got < GD_RAWENT || (ent[0] & 0xF8) == 0)
                break;                  /* the end of the directory */
            if (!(ent[0] & 0x08) || (ent[0] & 0x10))
                continue;               /* never used, or deleted */
            gd_rawname(ent, name);
            attr = (uint8_t)(((ent[0] & 0x01) ? FA_RDONLY : 0)
                           | ((ent[0] & 0x02) ? FA_HIDDEN : 0)
                           | ((ent[0] & 0x04) ? FA_ARCHIVE : 0)
                           | ((ent[0] & 0x20) ? FA_SUBDIR : 0));
            size = (uint32_t)ent[3] | ((uint32_t)ent[4] << 8)
                 | ((uint32_t)ent[5] << 16);
            date = gd_date(ent[17], ent[18], ent[19]);
            time = gd_time(ent[20], ent[21], ent[22]);
        } else {
            uint8_t kind = dos_dirline((const char *)ent, got, name, &sectors);
            if (!kind)
                continue;               /* dashes, the FREE trailer */
            attr = (uint8_t)((((kind & DOS_ENT_KIND) == DOS_ENT_DIR) ? FA_SUBDIR : 0)
                           | ((kind & DOS_ENT_LOCKED) ? FA_RDONLY : 0));
            /* DOS 2 counts sectors; 125 data bytes in each of a
             * single-density disk's.  The exact length is in the last
             * sector's byte count, which only a read would show. */
            size = (uint32_t)sectors * 125;
            date = GD_DATE0;
            time = 0;
        }
        if ((attr & FA_SUBDIR) && !(want & FA_SUBDIR))
            continue;
        if ((attr & FA_HIDDEN) && !(want & FA_HIDDEN))
            continue;
        if (!dos_wildcmp(pat, name))
            continue;
        gd_dta_fill(name, attr, time, date, size);
        return 0;
    }
    gd_slot_free(sl);
    return GD_ENMFIL;
}

/* ---- the functions -------------------------------------------------- */

static LONG gd_fsfirst(LONG spec, WORD attr)
{
    char *in = gw->in, *full = gw->full, *cio = gw->cio;
    LONG d;
    WORD k;
    int16_t iocb;
    uint32_t sl;
    uint8_t raw;

    far_strget(in, (uint32_t)spec, GD_PATHMAX);
    d = gd_full(in, full);
    if (d < 0)
        return d;
    k = gd_split(full);
    if (strlen(full + k) >= GD_NAMEMAX)
        return GD_EFILNF;
    strcpy(in, full + k);               /* the pattern */
    if (!in[0])
        return GD_EFILNF;
    strcpy(full + k, "*.*");            /* k is past the separator */
    d = gd_cio(full, cio);
    if (d < 0)
        return d;
    raw = (dos.caps & DOS_CAP_RAWDIR) ? 1 : 0;
    iocb = cio_open(cio, raw ? CIO_A_RAWDIR : CIO_A_DIR, 0);
    if (iocb < 0)
        return gd_err((uint8_t)-iocb, 1);
    sl = gd_slot_new();
    wr32(sl + SL_OWNER, (LONG)gd_dta);
    far_write8(sl + SL_IOCB, (uint8_t)iocb);
    far_write8(sl + SL_RAW, raw);
    wr16(sl + SL_LEN, 0);
    wr16(sl + SL_CUR, 0);
    far_write8(sl + SL_ATTR, (uint8_t)attr);
    far_strput(sl + SL_PAT, in, GD_NAMEMAX);
    gd_slot_fill(sl);
    return gd_next(sl);
}

static LONG gd_fsnext(void)
{
    uint32_t sl = gd_slot_of_dta();

    if (!sl)
        return GD_ENMFIL;
    far_write8(sl + SL_AGE, ++gd_age);
    return gd_next(sl);
}

static LONG gd_open(LONG fname, uint8_t aux1)
{
    LONG d = gd_name(fname);
    int16_t iocb;

    if (d < 0)
        return d;
    iocb = cio_open(gw->cio, aux1, 0);
    if (iocb < 0)
        return gd_err((uint8_t)-iocb, 0);
    gd_owned |= (uint8_t)(1 << iocb);
    gd_ateof &= (uint8_t)~(1 << iocb);
    wr32(gd_file(iocb) + FH_AT, 0);
    far_write8(gd_file(iocb) + FH_MODE, aux1);
    far_strput(gd_file(iocb) + FH_NAME, gw->full, GD_KEEPNAME);
    return iocb + GD_HANDLE_BASE;
}

/* A handle of ours to its IOCB, or 0. */
static uint8_t gd_iocb(WORD h)
{
    WORD i = h - GD_HANDLE_BASE;

    if (i < 1 || i >= CIO_IOCBS || !(gd_owned & (1 << i)))
        return 0;
    return (uint8_t)i;
}

static LONG gd_close(WORD h)
{
    uint8_t iocb = gd_iocb(h);

    if (!iocb)
        return GD_EIHNDL;
    cio_close(iocb);
    gd_owned &= (uint8_t)~(1 << iocb);
    gd_ateof &= (uint8_t)~(1 << iocb);
    return 0;
}

/* Fread and Fwrite.  CIO takes a bank-$00 address, so a buffer there
 * is handed over as it is, in pieces; one anywhere else goes through a
 * slice of the pool -- or, when the pool is spoken for, sixty-four
 * bytes of stack, slowly. */
static LONG gd_xfer(WORD h, LONG count, LONG buf, WORD write)
{
    uint8_t iocb = gd_iocb(h), small[64], *slice = 0;
    WORD st = CIO_OK;                   /* not a byte: B8, tools/ccbug --
                                         a byte st shared m's slot */
    uint32_t a = (uint32_t)buf, done = 0;
    uint16_t mark = 0, n = 0, m, got;

    if (!iocb)
        return GD_EIHNDL;
    if (count <= 0 || (!write && (gd_ateof & (1 << iocb))))
        return 0;
    if ((a >> 16) != 0) {
        uint16_t room = pool_room();
        mark = pool_mark();
        n = room > 2048 ? 2048 : (uint16_t)(room & ~1);
        if (n >= 128)
            slice = pool_alloc(n, 2);
        if (!slice) {
            slice = small;
            n = sizeof small;
        }
    }
    while (count > 0) {
        void *p;
        if (slice) {
            m = count > (LONG)n ? n : (uint16_t)count;
            p = slice;
            if (write)
                far_get(slice, a, m);
        } else {
            m = count > 0x4000L ? 0x4000 : (uint16_t)count;
            p = (void *)(uint16_t)a;
        }
        if (write) {
            st = cio_write(iocb, p, m);
            /* not `got = ok ? m : 0`: cc65816 5.18 -O2 loads got from
             * an unrelated slot for that (B9, tools/ccbug) */
            if (st != CIO_OK && st != CIO_OK_EOF)
                break;
            got = m;
        } else {
            st = cio_read(iocb, p, m, &got);
            if (got > m)
                got = m;
            if (slice && got)
                far_put(a, slice, got);
        }
        done += got;
        a += got;
        count -= got;
        if (got < m || (st != CIO_OK && st != CIO_OK_EOF))
            break;
    }
    if (!write && (got < m || st == CIO_OK_EOF || st == CIO_E_EOF))
        gd_ateof |= (uint8_t)(1 << iocb);
    if (slice && slice != small)
        pool_release(mark);
    gd_moved(iocb, done);               /* what Fseek counts (gd_seek) */
    if (done == 0 && st != CIO_OK && st != CIO_OK_EOF && st != CIO_E_EOF)
        return gd_err((uint8_t)st, 0);
    return (LONG)done;
}

/* ---- Fseek ---------------------------------------------------------------
 * GEMDOS counts a file in bytes from its start.  CIO does not count at
 * all: its POINT takes a sector and an offset within it, which is a
 * position in the FILE only if you know the sector chain -- so this does
 * not use POINT.  It keeps the count itself (gd_at, moved by every
 * transfer) and gets where it is going the way a program with no seek
 * would:
 *
 *   forward     read and throw away, which is what it costs
 *   backward    reopen the file on ITS OWN IOCB (cio_reopen: the handle
 *               is the IOCB, so it must come back on the same one) and
 *               read forward from the start
 *   from the end   read to the end first, which is also how the size is
 *               learned, then treat it as any other target
 *
 * A target past the end is GD_ERANGE with the position left at the end,
 * which is a legal GEMDOS answer and better than pretending: nothing
 * here can make a file longer without writing to it.
 *
 * Cheap where it matters -- Fseek(0, h, 0) rewinds, Fseek(0, h, 2) is
 * "how big is it" -- and honest where it does not. */
/* Read `bytes` away, or as many as there are.  NEVER PAST THE END: a
 * DOS 2 that has already reported EOF answers the next read with the
 * last sector's bytes again -- measured, on a 149-byte file whose last
 * sector holds 24, which came back as 24 more bytes and put the count
 * 24 ahead of the file.  So the end is remembered (gd_ateof, which
 * Fread keeps as well) and a skip that reaches it stops there. */
static LONG gd_skip(uint8_t iocb, uint32_t bytes, uint32_t *moved)
{
    uint8_t small[64], *slice = 0;
    uint16_t mark = pool_mark(), room = pool_room(), n = 0, got = 0, m = 0;
    uint32_t done = 0;
    WORD st = CIO_OK;

    *moved = 0;
    if (gd_ateof & (1 << iocb))         /* nothing left to read */
        return 0;
    n = room > 512 ? 512 : (uint16_t)(room & ~1);
    if (n >= 64)
        slice = pool_alloc(n, 2);
    if (!slice) {
        slice = small;
        n = sizeof small;
    }
    while (done < bytes) {
        m = (bytes - done) > n ? n : (uint16_t)(bytes - done);
        st = cio_read(iocb, slice, m, &got);
        done += got;
        if (got < m || (st != CIO_OK && st != CIO_OK_EOF))
            break;
    }
    if (slice != small)
        pool_release(mark);
    if (got < m || st == CIO_OK_EOF || st == CIO_E_EOF)
        gd_ateof |= (uint8_t)(1 << iocb);
    *moved = done;
    gd_moved(iocb, done);
    if (st != CIO_OK && st != CIO_OK_EOF && st != CIO_E_EOF && !done)
        return gd_err((uint8_t)st, 0);
    return 0;
}

/* ---- Fdatime -------------------------------------------------------------
 * The stamp a file carries, which is in its DIRECTORY ENTRY and not in
 * anything CIO will tell you about an open file -- so this looks the
 * file up by the path it was opened with (kept per handle, FH_NAME) and
 * takes the stamp out of the search's answer.
 *
 * The search would land in the application's DTA and break a
 * Fsfirst/Fsnext walk it might be in the middle of, so it runs against a
 * DTA of gemdos's own and puts the caller's back.
 *
 * SETTING a stamp is EINVFN: neither DOS here has a call for it.  A DOS
 * 2 disk has no stamps at all, and answers with zeros, which is what the
 * ST answers for a file system without them. */
static LONG gd_datime(LONG buf, WORD h, WORD set)
{
    uint8_t iocb = gd_iocb(h);
    uint32_t save = gd_dta, sl;
    LONG r;

    if (!iocb)
        return GD_EIHNDL;
    if (set)
        return GD_EINVFN;
    far_copy(gd_scratch + DTA_SIZE, gd_file(iocb) + FH_NAME, GD_KEEPNAME);
    gd_dta = gd_scratch;
    r = gd_fsfirst((LONG)(gd_scratch + DTA_SIZE), 0);
    if (r == 0) {
        wr16((uint32_t)buf, rd16(gd_dta + DTA_TIME));
        wr16((uint32_t)buf + 2, rd16(gd_dta + DTA_DATE));
    }
    sl = gd_slot_of_dta();              /* the search this took */
    if (sl)
        gd_slot_free(sl);
    gd_dta = save;
    return r;
}

static LONG gd_seek(LONG offset, WORD h, WORD mode)
{
    uint8_t iocb = gd_iocb(h);
    uint32_t at, target, moved = 0;
    LONG here, r;

    if (!iocb)
        return GD_EIHNDL;
    if (mode == 2) {                    /* the end, which is also its size */
        r = gd_skip(iocb, 0x7FFFFFFFUL, &moved);
        if (r < 0)
            return r;
    }
    /* One reading of where the file is, kept in a local: the position
     * lives in far memory and this walks it forwards. */
    at = gd_where(iocb);
    if (mode == 0) {
        if (offset < 0)                 /* before the start of the file */
            return GD_ERANGE;
        target = (uint32_t)offset;
    } else {
        here = (LONG)at + offset;
        if (here < 0)
            return GD_ERANGE;
        target = (uint32_t)here;
    }
    if (target < at) {                  /* back to the start, then forward */
        char name[GD_KEEPNAME];
        far_strget(name, gd_file(iocb) + FH_NAME, sizeof name);
        dos_cioname(name, gw->cio);     /* A:\X -> D1:X, as the open did */
        if (cio_reopen(iocb, gw->cio, far_read8(gd_file(iocb) + FH_MODE), 0) < 0) {
            gd_owned &= (uint8_t)~(1 << iocb);
            return GD_EFILNF;
        }
        wr32(gd_file(iocb) + FH_AT, 0);
        at = 0;
        gd_ateof &= (uint8_t)~(1 << iocb);   /* a fresh open is not at the end */
    }
    if (target > at) {
        r = gd_skip(iocb, target - at, &moved);
        if (r < 0)
            return r;
        at += moved;
        if (at != target)               /* the end came first: gd_skip knows */
            return GD_ERANGE;
    }
    return (LONG)at;
}

/* An XIO on a path: delete, mkdir, rmdir, lock. */
static LONG gd_xio(uint8_t cmd, LONG path, WORD dir)
{
    LONG d = gd_name(path);
    WORD st;                            /* not a byte: B8 */

    if (d < 0)
        return d;
    st = cio_xio(cmd, gw->cio, 0, 0);
    if (st != CIO_OK && st != CIO_OK_EOF)
        return gd_err((uint8_t)st, dir);
    return 0;
}

/* Frename: XIO 32 on "OLD,NEW", the new name a bare NAME.EXT -- the
 * DOS renames within the directory, so a new path is taken for its
 * last component only. */
static LONG gd_rename(LONG oldp, LONG newp)
{
    char *nn = gw->in, *full = gw->full, *cio = gw->cio;
    LONG d = gd_name(oldp);
    WORD k, n;
    WORD st;                            /* not a byte: B8 */

    if (d < 0)
        return d;
    far_strget(nn, (uint32_t)newp, GD_PATHMAX);
    d = gd_full(nn, full);
    if (d < 0)
        return d;
    k = gd_split(full);
    n = (WORD)strlen(cio);
    if (n + 1 + strlen(full + k) > CIO_NAME_MAX)
        return GD_EPTHNF;
    cio[n] = ',';
    strcpy(cio + n + 1, full + k);
    st = cio_xio(CIO_X_RENAME, cio, 0, 0);
    if (st != CIO_OK && st != CIO_OK_EOF)
        return gd_err((uint8_t)st, 0);
    return 0;
}

/* Dsetpath: the drive's directory, kept here -- the DOS's own current
 * directory is never moved.  A SpartaDOS is asked whether the directory
 * exists by opening it; a flat DOS has only the root. */
static LONG gd_setpath(LONG path)
{
    char *in = gw->in, *full = gw->full, *cio = gw->cio;
    LONG d;
    WORD k;
    int16_t iocb;

    far_strget(in, (uint32_t)path, GD_PATHMAX);
    d = gd_full(in, full);
    if (d < 0)
        return d;
    k = (WORD)strlen(full);
    if (k > 3) {
        if (!dos.dirsep)
            return GD_EPTHNF;
        if (k - 2 >= GD_DIRMAX)
            return GD_EPTHNF;
        strcpy(in, full);
        strcat(in, "\\*.*");
        dos_cioname(in, cio);
        iocb = cio_open(cio, CIO_A_RAWDIR, 0);
        if (iocb < 0)
            return gd_err((uint8_t)-iocb, 1);
        cio_close(iocb);
        far_strput(gd_dirs + (uint32_t)d * GD_DIRMAX, full + 2, GD_DIRMAX);
    } else
        far_write8(gd_dirs + (uint32_t)d * GD_DIRMAX, 0);
    gd_dirset[d] = 1;
    return 0;
}

/* A file name the AES was given, as CIO wants it, resolved the way
 * GEMDOS resolves one -- which is what makes Dsetpath mean something to
 * rsrc_load and to the shell (src/aes/shel.c sh_cioname).  A relative
 * name on a drive nobody has set is left to CIO and so to the DOS's own
 * idea of where it is: that is how the system's own files are found at
 * boot, when a start-up batch has changed directory and gem4xe was not
 * told.  docs/phase29.md. */
void gd_cioname(const char *name, char *cio)
{
    uint16_t mark = pool_mark();
    char *full;
    WORD d = gd_drive;

    if (name[0] && name[1] == ':') {
        int c = (uint8_t)name[0];
        if (c >= 'a' && c <= 'z')
            c -= 0x20;
        d = (WORD)(c - 'A');
    }
    /* The scratch is the pool's, not gw's: gw belongs to a GEMDOS call
     * and this is not one -- the AES calls in between, when what gw
     * points at has been released. */
    full = (d >= 0 && d < GD_DRIVES && gd_dirset[d])
           ? (char *)pool_alloc(GD_PATHMAX, 2) : 0;
    if (!full || gd_full(name, full) < 0)
        dos_cioname(name, cio);
    else
        dos_cioname(full, cio);
    pool_release(mark);
}

/* Dgetpath: "" at the root, "\DIR\SUB" below it, as the ST says it. */
static LONG gd_getpath(LONG buf, WORD drv)
{
    char *dir = gw->in;
    WORD d = drv ? drv - 1 : gd_drive;

    if (d < 0 || d >= GD_DRIVES)
        return GD_EDRIVE;
    far_strget(dir, gd_dirs + (uint32_t)d * GD_DIRMAX, GD_DIRMAX);
    far_strput((uint32_t)buf, dir, GD_DIRMAX);
    return 0;
}

/* Dfree: what the directory listing's trailer says -- "nnn FREE
 * SECTORS" on DOS 2.5 and both SpartaDOSes, "nnn Free nn Fil" on DOS
 * II+/D -- in sectors of 128, one to a cluster.  The disk's total is
 * not in anything CIO returns, so b_total is 0: unknown, rather than
 * a guess at the density.
 *
 * The count is three characters wide, so this cannot answer more than
 * 999 whatever the volume holds, and the DOSes do not even agree about
 * what they put there when it will not fit: SpartaDOS 3.2g prints the
 * low three digits (1,001 reads back as 1), SpartaDOS X stops at 999.
 * Both were measured on the gates' 2048-sector disk (test-m14,
 * test-m15).  Nothing better is reachable through CIO; a volume big
 * enough to matter will want a DOS call this seam does not have yet. */
#define LINEMAX 24

/* Whether `word` (upper case) occurs in `s`, in any case. */
static WORD has_word(const char *s, const char *word)
{
    for (; *s; s++) {
        const char *a = s, *w = word;
        while (*w && *a && ((*a >= 'a' && *a <= 'z') ? *a - 32 : *a) == *w)
            a++, w++;
        if (!*w)
            return 1;
    }
    return 0;
}

/* ---- Dfree, out of the file system --------------------------------------
 * What a volume has left is a number the file system keeps, and until now
 * this asked the DIRECTORY LISTING for it: the trailer's "nnn FREE
 * SECTORS", which is three characters wide and all CIO offers.  Three
 * characters cannot say 16116, and the two DOSes do not even agree what
 * they put there when it overflows -- SpartaDOS 3.2g prints the low three
 * digits, SDX stops at 999 -- so on anything bigger than a floppy the
 * answer was wrong.  The CF card made that visible (docs/shipping.md,
 * section 3).
 *
 * So read the file system's own count, one sector through the OS's SIO
 * (src/sys/cio.c, dsk_read) -- the path a PBI hard disk answers on as
 * well as a floppy, so it reaches the card's partitions too:
 *
 *   DOS 2     the VTOC, sector 360: bytes 3-4 the free count, 1-2 the
 *             total.  On an ENHANCED disk (128-byte sectors, 1040 of
 *             them) that count covers the lower half only and DOS 2.5
 *             keeps the upper half's in VTOC2, sector 1024, bytes
 *             122-123 -- Altirra's diskfsdos2.cpp and tools/atr.py both
 *             say so, and test-m15d boots such a disk.
 *   SDFS      the superblock, sector 1: byte 7 is $80 or $40, bytes
 *             13-14 the free count, 11-12 the total.
 *
 * The geometry comes from the drive itself (PERCOM), because the sector
 * size decides how much to read and, for a DOS 2 disk, whether there is a
 * second VTOC at all.
 *
 * A drive that will not answer SIO -- a DOS's own virtual drive, which is
 * what SDX's are -- falls back to the listing, which is why that code is
 * still here.  It is the slow path in both senses: it reads the whole
 * directory. */
#define VTOC_SECTOR    360
#define VTOC2_SECTOR  1024
#define SDFS_SUPER       1
#define ED_SECTORS    1040      /* an enhanced-density floppy */
#define SDFS_SIGN_A   0x80      /* superblock byte 7 */
#define SDFS_SIGN_B   0x40

/* The drive's own geometry: bytes per sector, and how many there are.
 * PERCOM is big-endian in its words.  0 for a drive that does not
 * answer. */
static uint16_t gd_geometry(uint8_t unit, uint32_t *total)
{
    uint8_t percom[12];

    *total = 0;
    if (dsk_percom(unit, percom) != SIO_OK)
        return 0;
    *total = (uint32_t)percom[0] * (uint32_t)(percom[4] + 1)
             * (uint32_t)(((uint16_t)percom[2] << 8) | percom[3]);
    return (uint16_t)(((uint16_t)percom[6] << 8) | percom[7]);
}

/* TRUE and the counts filled in, or FALSE and the caller reads the
 * listing instead. */
static WORD gd_dfree_fs(WORD d, uint32_t *pfree, uint32_t *ptotal,
                        uint16_t *psecsize)
{
    uint8_t unit = (uint8_t)(d + 1);        /* A: is D1: */
    uint32_t geom = 0;
    uint16_t secsize = gd_geometry(unit, &geom);
    uint16_t mark = pool_mark();
    uint8_t *sec;
    WORD ok = 0;

    if (!secsize || secsize > GD_SECMAX)
        return 0;
    sec = pool_alloc(secsize, 2);
    if (!sec)
        return 0;
    if (dos.kind == DOS_2) {
        if (dsk_read(unit, VTOC_SECTOR, sec, secsize) == SIO_OK) {
            *pfree = (uint32_t)sec[3] | ((uint32_t)sec[4] << 8);
            *ptotal = (uint32_t)sec[1] | ((uint32_t)sec[2] << 8);
            ok = 1;
            /* the upper half of an enhanced disk, counted apart */
            if (secsize == 128 && geom == ED_SECTORS
                && dsk_read(unit, VTOC2_SECTOR, sec, secsize) == SIO_OK) {
                *pfree += (uint32_t)sec[122] | ((uint32_t)sec[123] << 8);
                *ptotal = geom;
            }
        }
    } else if (dsk_read(unit, SDFS_SUPER, sec, secsize) == SIO_OK
               && (sec[7] == SDFS_SIGN_A || sec[7] == SDFS_SIGN_B)) {
        *pfree = (uint32_t)sec[13] | ((uint32_t)sec[14] << 8);
        *ptotal = (uint32_t)sec[11] | ((uint32_t)sec[12] << 8);
        ok = 1;
    }
    pool_release(mark);
    *psecsize = secsize;
    return ok;
}

static LONG gd_dfree(LONG buf, WORD drv)
{
    char *full = gw->full, *cio = gw->cio, *line = gw->in, *in = gw->in + 32;
    WORD d = drv ? drv - 1 : gd_drive, ln = 0, fresh = 0;
    int16_t iocb;
    uint16_t mark = pool_mark(), room = pool_room(), n, got, k;
    uint8_t *slice = 0;
    WORD st;                            /* not a byte: B8 */
    uint32_t sectors = 0;

    if (d < 0 || d >= GD_DRIVES)
        return GD_EDRIVE;
    {   /* the file system's own count, when the drive will say */
        uint32_t nfree = 0, ntotal = 0;
        uint16_t secsize = 0;
        if (gd_dfree_fs(d, &nfree, &ntotal, &secsize)) {
            wr32((uint32_t)buf, (LONG)nfree);
            wr32((uint32_t)buf + 4, (LONG)ntotal);
            wr32((uint32_t)buf + 8, secsize);
            wr32((uint32_t)buf + 12, 1);
            return 0;
        }
    }
    full[0] = (char)('A' + d);
    strcpy(full + 1, ":\\*.*");
    dos_cioname(full, cio);
    iocb = cio_open(cio, CIO_A_DIR, 0);
    if (iocb < 0)
        return gd_err((uint8_t)-iocb, 1);
    n = room > 1024 ? 1024 : (uint16_t)(room & ~1);
    if (n >= 64)
        slice = pool_alloc(n, 2);
    line[0] = 0;
    do {
        if (slice) {                    /* the last complete line, as the
                                         * listing streams by */
            st = cio_read(iocb, slice, n, &got);
            if (st != CIO_OK && st != CIO_OK_EOF && st != CIO_E_EOF)
                break;
            for (k = 0; k < got && k < n; k++) {
                WORD c = slice[k];
                if (c == CIO_EOL) {
                    line[ln] = 0;
                    ln = 0;
                    fresh = 1;
                } else {
                    if (fresh) {
                        ln = 0;
                        fresh = 0;
                    }
                    if (ln < LINEMAX - 1)
                        line[ln++] = (char)c;
                }
            }
        } else {
            st = cio_getrec(iocb, in, LINEMAX, &got);
            if (st != CIO_OK && st != CIO_OK_EOF && st != CIO_E_EOF)
                break;
            if (got) {
                if (got >= LINEMAX)
                    got = LINEMAX - 1;
                memcpy(line, in, got);
                line[got] = 0;
            }
        }
    } while (st == CIO_OK && got);
    cio_close(iocb);
    if (slice) {
        if (!fresh)                     /* a last line without its EOL */
            line[ln] = 0;
        pool_release(mark);
    }
    /* "   173 FREE SECTORS": leading spaces, digits, the word in
     * whichever case the DOS spells it */
    for (k = 0; line[k] == ' '; k++)
        ;
    for (; line[k] >= '0' && line[k] <= '9'; k++)
        sectors = sectors * 10 + (line[k] - '0');
    if (!has_word(line, "FREE"))
        sectors = 0;
    wr32((uint32_t)buf, (LONG)sectors);    /* b_free */
    wr32((uint32_t)buf + 4, 0);            /* b_total: unknown */
    wr32((uint32_t)buf + 8, 128);          /* b_secsiz */
    wr32((uint32_t)buf + 12, 1);           /* b_clsiz */
    return 0;
}

/* Dsetdrv's return, Drvmap's: which drives are there.  DOS 2 keeps a
 * bit per drive it will talk to; a SpartaDOS keeps no such map (SDX
 * Programming Guide 4.50, chapter 5, has none), and asking each unit
 * costs a SIO timeout for every one that is absent, so A and B are
 * claimed and DESKTOP.INF is left to say which icons there are. */
static LONG gd_drvmap(void)
{
    if (dos.kind == DOS_2)
        return DRVBYT ? DRVBYT : 1;
    return 0x03;
}

static LONG gd_malloc(LONG n)
{
    uint32_t a;

    if (n < 0)
        return (LONG)(((uint32_t)(farmem.last_bank + 1) << 16) - farmem.brk);
    if (n == 0)
        return 0;
    a = far_alloc((uint32_t)n);
    return a ? (LONG)a : GD_ENSMEM;
}

/* Fattrib: setting read-only is the DOS's lock; the rest of the ST's
 * attributes have no counterpart CIO reaches, and reading them back is
 * what Fsfirst is for. */
static LONG gd_fattrib(LONG path, WORD wflag, WORD attr)
{
    if (!wflag)
        return GD_EINVFN;
    return gd_xio((attr & FA_RDONLY) ? CIO_X_LOCK : CIO_X_UNLOCK, path, 0);
}

/* ---- entry ---------------------------------------------------------- */

void gemdos_init(void)
{
    WORD i;

    gd_dirs = far_alloc(GD_DRIVES * GD_DIRMAX);
    gd_dta0 = far_alloc((uint32_t)NUM_PROCS * DTA_SIZE);
    gd_slots = far_alloc((uint32_t)GD_SLOTS * SL_SIZE);
    gd_files = far_alloc((uint32_t)CIO_IOCBS * FH_SIZE);
    gd_scratch = far_alloc(DTA_SIZE + GD_KEEPNAME);
    for (i = 0; i < GD_DRIVES; i++) {
        far_write8(gd_dirs + (uint32_t)i * GD_DIRMAX, 0);
        gd_dirset[i] = 0;
    }
    for (i = 0; i < GD_SLOTS; i++) {
        wr32(gd_slot(i) + SL_OWNER, 0);
        far_write8(gd_slot(i) + SL_IOCB, 0);
        far_write8(gd_slot(i) + SL_AGE, 0);
    }
    for (i = 0; i < NUM_PROCS; i++) {
        PROC *p = &proc_tab[i];
        p->p_gddta = gd_dta0 + (uint32_t)i * DTA_SIZE;
        p->p_gdowned = p->p_gdateof = 0;
    }
    gd_drive = 0;
    gd_age = 0;
}

/* The RUNNING process's files, searches and DTA.  app_free calls it when
 * a program has ended, and at that moment the running process is that
 * program -- an application is process 0 and the shell runs in its
 * context -- so an accessory's open files are left exactly where they
 * are, which is what an accessory needs to hold one at all. */
void gemdos_release(void)
{
    WORD i;

    for (i = 1; i < CIO_IOCBS; i++)
        if (gd_owned & (1 << i))
            cio_close((int16_t)i);
    gd_owned = gd_ateof = 0;
    for (i = 0; i < GD_SLOTS; i++)
        if ((uint32_t)rd32(gd_slot(i) + SL_OWNER) == gd_dta)
            gd_slot_free(gd_slot(i));
    gd_dta = gd_dta0 + (uint32_t)proc_pid(rlr) * DTA_SIZE;
}

void gemdos_call(uint32_t pb)
{
    LONG r;
    WORD fn;
    uint16_t mark = pool_mark();

    gd_pb = pb;
    gemdos_calls++;
    fn = arg_w(4);
    gw = (gd_work_t *)pool_alloc(sizeof(gd_work_t), 2);
    if (!gw) {
        wr32(gd_pb, GD_ENSMEM);
        return;
    }
    switch (fn) {
    case GD_DSETDRV:
        if (arg_w(6) < 0 || arg_w(6) >= GD_DRIVES) {
            r = GD_EDRIVE;
            break;
        }
        gd_drive = (uint8_t)arg_w(6);
        r = gd_drvmap();
        break;
    case GD_DGETDRV:
        r = gd_drive;
        break;
    case GD_FSETDTA:
        gd_dta = (uint32_t)arg_l(6);
        r = 0;
        break;
    case GD_FGETDTA:
        r = (LONG)gd_dta;
        break;
    case GD_SVERSION:
        r = 0x1500;                     /* what TOS 1.04 says */
        break;
    case GD_DFREE:
        r = gd_dfree(arg_l(6), arg_w(10));
        break;
    case GD_DCREATE:                    /* a flat DOS cannot */
        r = dos.dirsep ? gd_xio(CIO_X_MKDIR, arg_l(6), 1) : GD_EACCDN;
        break;
    case GD_DDELETE:
        r = dos.dirsep ? gd_xio(CIO_X_RMDIR, arg_l(6), 1) : GD_EPTHNF;
        break;
    case GD_DSETPATH:
        r = gd_setpath(arg_l(6));
        break;
    case GD_FCREATE:
        r = gd_open(arg_l(6), CIO_A_WRITE);
        break;
    case GD_FOPEN:
        r = gd_open(arg_l(6), (arg_w(10) & 3) == GD_O_READ
                                  ? CIO_A_READ : CIO_A_UPDATE);
        break;
    case GD_FCLOSE:
        r = gd_close(arg_w(6));
        break;
    case GD_FREAD:
        r = gd_xfer(arg_w(6), arg_l(8), arg_l(12), 0);
        break;
    case GD_FWRITE:
        r = gd_xfer(arg_w(6), arg_l(8), arg_l(12), 1);
        break;
    case GD_FDELETE:
        r = gd_xio(CIO_X_DELETE, arg_l(6), 0);
        if (r == GD_EFILNF) {
            /* SpartaDOS erases around a protected file and reports
             * "not found" ($AA, measured on 3.2): a file still there
             * was refused, which the ST calls EACCDN. */
            int16_t iocb = cio_open(gw->cio, CIO_A_READ, 0);
            if (iocb >= 0) {
                cio_close(iocb);
                r = GD_EACCDN;
            }
        }
        break;
    case GD_FATTRIB:
        r = gd_fattrib(arg_l(6), arg_w(10), arg_w(12));
        break;
    case GD_DGETPATH:
        r = gd_getpath(arg_l(6), arg_w(10));
        break;
    case GD_MALLOC:
        r = gd_malloc(arg_l(6));
        break;
    case GD_MFREE:
        r = 0;
        break;
    case GD_FSFIRST:
        r = gd_fsfirst(arg_l(6), arg_w(10));
        break;
    case GD_FSNEXT:
        r = gd_fsnext();
        break;
    case GD_FRENAME:
        r = gd_rename(arg_l(8), arg_l(12));
        break;
    case GD_FSEEK:
        r = gd_seek(arg_l(6), arg_w(10), arg_w(12));
        break;
    case GD_TGETDATE: {
        /* The machine's clock, if it has one (src/sys/clock.c): the
         * Ultimate 1MB's DS1305.  Without one this is the ST's epoch,
         * 1 January 1980, which is what a TOS with a dead clock says. */
        CLOCK now;
        clock_read(&now);
        r = (LONG)(UWORD)(((now.year - 1980) << 9) | (now.month << 5) | now.day);
        break;
    }
    case GD_TGETTIME: {
        CLOCK now;
        clock_read(&now);
        r = (LONG)(UWORD)((now.hour << 11) | (now.minute << 5) | (now.second >> 1));
        break;
    }
    case GD_FDATIME:
        r = gd_datime(arg_l(6), arg_w(10), arg_w(12));
        break;
    default:                            /* Tset*, Fforce, Pexec: not here */
        gemdos_bad++;
        r = GD_EINVFN;
        break;
    }
    pool_release(mark);
    wr32(gd_pb, r);
}
