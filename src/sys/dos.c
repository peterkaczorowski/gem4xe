/* dos.c -- which DOS is behind CIO.  See dos.h. */
#include <string.h>
#include "dos.h"
#include "cio.h"

DOS_INFO dos;

#define DOSVEC   (*(uint8_t **)0x000A)
#define DOSBASE  ((const uint8_t *)0x0700)
#define MEMTOP   (*(uint16_t *)0x02E5)
#define MEMLO    (*(uint16_t *)0x02E7)

extern uint8_t _exit_dosvec;        /* src/crt_atari.s */

void dos_ident(void)
{
    const uint8_t *comtab = DOSVEC;

    dos.kind = DOS_2;
    dos.caps = 0;
    dos.dirsep = 0;
    dos.memlo = MEMLO;
    dos.memtop = MEMTOP;

    if (DOSBASE[0] == 'S' && comtab[3] == 0x4C) {
        /* The X is a cartridge: its RAMTOP is the cartridge's bottom,
         * where a disk SpartaDOS leaves the screen at DOS 2's height.
         * MEMTOP is that less the screen, so $A000 tells them apart
         * without reading either DOS's own tables. */
        dos.kind = (dos.memtop < 0xA000) ? DOS_SDX : DOS_SPARTA;
        dos.caps = DOS_CAP_DIRS | DOS_CAP_RAWDIR | DOS_CAP_STAMPS;
        dos.dirsep = '>';
    }
    /* How to leave (src/crt_atari.s).  A SpartaDOS keeps its command
     * processor resident and is waiting for its loader to return, so a
     * return is what it wants.  An Atari DOS 2 may keep its in DUP.SYS,
     * at $1D00-$3306 -- memory gem4xe runs in -- so it is asked to come
     * back through DOSVEC, which reloads it. */
    _exit_dosvec = (uint8_t)(dos.kind == DOS_2);
}

/* SpartaDOS X: ':' in the second flag column (Programming Guide 4.50,
 * 10.2, the short format's mode $08).  SpartaDOS 3.2: "DIR" with the
 * high bits set in the extension field -- inverse video on the screen
 * the line was made for. */
uint8_t dos_folder_line(const char *line)
{
    const char *ext = line + 10;

    if (!(dos.caps & DOS_CAP_DIRS))
        return DOS_MARK_NONE;
    if (line[1] == ':')
        return DOS_MARK_FLAG;
    if ((uint8_t)ext[0] == ('D' | 0x80) && (uint8_t)ext[1] == ('I' | 0x80)
        && (uint8_t)ext[2] == ('R' | 0x80))
        return DOS_MARK_EXT;
    return DOS_MARK_NONE;
}

void dos_cioname(const char *gem, char *cio)
{
    const char *p = gem, *q;
    uint8_t k = 0;

    if (gem[0] && gem[1] == ':' && gem[2] == '\\') {
        char d = (char)(gem[0] | 0x20);
        if (d >= 'a' && d <= 'h') {
            cio[k++] = 'D';
            cio[k++] = (char)('1' + (d - 'a'));
            cio[k++] = ':';
        }
        p = gem + 3;
        if (dos.dirsep)
            cio[k++] = dos.dirsep;      /* from the root */
    }
    if (!dos.dirsep)                    /* a flat DOS: the last component */
        for (q = p; *q; q++)
            if (*q == '\\')
                p = q + 1;
    for (; *p && k < CIO_NAME_MAX; p++) {
        int c = (uint8_t)*p;            /* not a byte: B8, tools/ccbug */
        if (c == '\\')
            c = (uint8_t)dos.dirsep;
        else if (c >= 'a' && c <= 'z')
            c -= 0x20;
        cio[k++] = (char)c;
    }
    cio[k] = 0;
}

#define DIRLINE 17          /* DOS 2's directory record, less its EOL */

/* A directory field -- the name's 8 columns or the extension's 3 -- less
 * its trailing spaces: its length, or -1 when a character in it is not
 * one DOS 2 puts in a name. */
static int16_t dir_field(const char *p, int16_t n, char *out)
{
    int16_t k;

    while (n > 0 && p[n - 1] == ' ')
        n--;
    for (k = 0; k < n; k++) {
        int c = (uint8_t)p[k];          /* not a byte: B8, tools/ccbug */
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '_' || c == '@'))
            return -1;
        out[k] = (char)c;
    }
    out[n] = 0;
    return n;
}

uint8_t dos_dirline(const char *line, uint16_t got, char *fname,
                    uint16_t *sectors)
{
    char ext[4];
    int16_t n, e;
    uint8_t mark, kind;

    if (got < DIRLINE || line[13] != ' ')
        return DOS_ENT_NONE;
    mark = dos_folder_line(line);
    if (mark == DOS_MARK_NONE && line[1] != ' ')
        return DOS_ENT_NONE;
    n = dir_field(line + 2, 8, fname);
    if (n <= 0 || !(fname[0] >= 'A' && fname[0] <= 'Z'))
        return DOS_ENT_NONE;
    if (sectors) {
        uint16_t v = 0;
        for (e = 14; e < DIRLINE; e++) {
            int c = (uint8_t)line[e];
            if (c >= '0' && c <= '9')
                v = v * 10 + (c - '0');
        }
        *sectors = v;
    }
    kind = (line[0] == '*') ? DOS_ENT_LOCKED : 0;
    if (mark == DOS_MARK_EXT)
        return kind | DOS_ENT_DIR;
    e = dir_field(line + 10, 3, ext);
    if (e < 0)
        return DOS_ENT_NONE;
    if (e) {
        fname[n] = '.';
        strcpy(fname + n + 1, ext);
    }
    return kind | (mark ? DOS_ENT_DIR : DOS_ENT_FILE);
}

uint16_t dos_wildcmp(const char *pattern, const char *filename)
{
    int16_t i;

    for (i = 0; i < 2; i++) {
        for (; *filename && *filename != '.'; filename++) {
            if (*pattern == '*')
                continue;
            if (*pattern == '?' || *pattern == *filename) {
                pattern++;
                continue;
            }
            return 0;
        }
        while (*pattern == '*' || *pattern == '?')
            pattern++;
        if (*pattern == '.')
            pattern++;
        if (*filename == '.')
            filename++;
    }
    return *pattern == *filename;
}
