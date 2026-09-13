/* bootinfo.c -- the boot screen.  bootinfo.h says what it is for; this
 * is how it is drawn, which is one CIO write a row on E:, the row built
 * on the stack first because forty PUTCHRs a row through the OS is
 * forty trips to the slow bus and back.
 *
 * No row is ever the full forty columns: E: wraps a fortieth character
 * onto the next row by itself and the EOL behind it would then leave a
 * blank one.  Everything is INSET columns in from the left and at most
 * WIDTH across, which is 36 -- and E: has a left margin of its own,
 * LMARGN, two as the OS sets it, which counts towards the inset: the
 * rows carry only what E: does not add, so the block sits where it was
 * drawn under a DOS that has moved the margin.
 *
 * Everything constant here is FAR, the logo and the names alike: bank
 * $00 has 2,430 bytes for every near constant the system owns
 * (src/gem4xe.scm) and a screen shown once at boot has no claim on
 * them.  The names are arrays for the reason src/sys/config.c gives --
 * a table of pointers would put the text back in bank $00.
 */
#include "portab.h"
#include <string.h>
#include "sys/bootinfo.h"
#include "sys/cio.h"
#include "sys/irq.h"
#include "sys/rapidus.h"
#include "sys/farmem.h"
#include "sys/dos.h"
#include "sys/config.h"
#include "sys/clock.h"
#include "vbxe/vbxe.h"
#include "vdi/pointer.h"
#include "aes/aes.h"
#include "lang_rsc.h"
#include "version.h"

/* The screen, in columns. */
#define COLS     40
#define INSET    2                      /* in from the left, LMARGN included */
#define WIDTH    (COLS - 2 * INSET)     /* the rule, and the widest row */
#define LABEL    (LANG_BOOT_LABEL + 1)  /* the label column, gap included */
#define VALUE    (WIDTH - LABEL)        /* what is left for the value */

static WORD margin;                     /* the inset less what E: adds */
static uint8_t dos_ink;                 /* COLOR1 as DOS had it */

/* E:, and what it makes of a byte. */
#define E_IOCB    0                     /* the OS opens it on this one */
#define E_CLEAR   0x7D                  /* clear the screen, cursor home */
#define E_EOL     0x9B
#define E_INVERSE 0x80                  /* the bit that inverts a glyph */

#define CRSINH   (*(volatile uint8_t *)0x02F0)  /* 1: E: draws no cursor */
#define LMARGN   (*(volatile uint8_t *)0x0052)  /* E:'s own left margin  */
#define COLOR1   (*(volatile uint8_t *)0x02C5)  /* the OS's COLPF1 shadow:
                                                 * its VBI's first stage
                                                 * copies it, CRITIC or no */
#define COLPF1   (*(volatile uint8_t *)0xD017)  /* the ink's luminance   */
#define COLPF2   (*(volatile uint8_t *)0xD018)  /* the paper             */
#define COLBK    (*(volatile uint8_t *)0xD01A)  /* the border            */
#define PAL      (*(volatile uint8_t *)0xD014)  /* GTIA: bits 1-3 clear on PAL */
#define VCOUNT   (*(volatile uint8_t *)0xD40B)
#define SKSTAT   (*(volatile uint8_t *)0xD20F)  /* bit 2 low: a key held */
#define IRQEN    (*(volatile uint8_t *)0xD20E)  /*   bit 3 low: SHIFT    */
#define POKMSK   (*(volatile uint8_t *)0x0010)  /* the OS's IRQEN shadow */

#define INK      0x00                   /* GEM's black on white */
#define PAPER    0x0E

#define HOLD_SECONDS 3                  /* EmuTOS's, and enough to read */

/* GEM4XE in block capitals, five rows of five cells a letter, one bit a
 * cell, the top row first. */
#define LOGO_ROWS 5
#define LOGO_LETTERS 6
static const uint8_t FAR logo[LOGO_LETTERS][LOGO_ROWS] = {
    { 0x0F, 0x10, 0x13, 0x11, 0x0F },   /* G */
    { 0x1F, 0x10, 0x1E, 0x10, 0x1F },   /* E */
    { 0x11, 0x1B, 0x15, 0x11, 0x11 },   /* M */
    { 0x11, 0x11, 0x1F, 0x01, 0x01 },   /* 4 */
    { 0x11, 0x0A, 0x04, 0x0A, 0x11 },   /* X */
    { 0x1F, 0x10, 0x1E, 0x10, 0x1F },   /* E */
};

/* The machine's own names.  The version is build/version.h's, from the
 * VERSION file. */
static const char FAR s_version[] = GEM4XE_VERSION;
static const char FAR s_cpu[]     = "65C816";
static const char FAR s_rapidus[] = ", Rapidus";
static const char FAR s_mb[]      = " MB, ";
static const char FAR s_sdx[]     = "SpartaDOS X";
static const char FAR s_sparta[]  = "SpartaDOS 3";
static const char FAR s_dos2[]    = "DOS 2";
static const char FAR s_cfg[]     = CFG_FILE;
static const char FAR s_lang[]    = LANG_FILE;
static const char FAR s_antic[]   = "ANTIC";
static const char FAR s_vbxe[]    = "VBXE ";
static const char FAR s_u1mb[]    = "U1MB, ";
static const char FAR s_side[]    = "SIDE, ";
static const char FAR s_ps[]      = "PostScript, ";
static const char FAR s_pcl[]     = "PCL 5, ";
static const char FAR s_digit[]   = "0123456789ABCDEF";

typedef char ROW[COLS + 1];             /* one row, built and then written */

/* The first n characters of a row, and the end of the line. */
static void row_out(char *row, WORD n)
{
    row[n] = (char)E_EOL;
    cio_write(E_IOCB, row, (uint16_t)(n + 1));
}

static void blank(void)
{
    ROW row;

    row_out(row, 0);
}

static void rule(void)
{
    ROW row;
    WORD i;

    for (i = 0; i < margin; i++)
        row[i] = ' ';
    for (; i < margin + WIDTH; i++)
        row[i] = '_';
    row_out(row, i);
}

/* A label and its value.  The value is copied first: it may well be
 * lang_str's one buffer, which fetching the label would overwrite. */
static void line(WORD label, const char FAR *value)
{
    ROW row;
    char v[VALUE + 1];
    const char *l;
    WORD n, i;

    for (n = 0; n < VALUE && value[n]; n++)
        v[n] = value[n];
    v[n] = '\0';
    l = lang_str(label);
    for (i = 0; i < margin; i++)
        row[i] = ' ';
    n = margin;
    for (i = 0; i < LABEL - 1 && l[i]; i++)    /* cut, and a gap kept */
        row[n++] = l[i];
    while (n < margin + LABEL)
        row[n++] = ' ';
    for (i = 0; v[i]; i++)
        row[n++] = v[i];
    row_out(row, n);
}

/* ---- the values: built in a buffer wider than a line, and line() cuts -- */

static char *app(char *p, const char FAR *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *dec(char *p, uint16_t v)
{
    char t[5];
    WORD n = 0;

    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = t[--n];
    return p;
}

static char *two(char *p, uint8_t v)    /* two digits, for a date */
{
    *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

static char *hex2(char *p, uint8_t v)
{
    *p++ = s_digit[v >> 4];
    *p++ = s_digit[v & 0x0F];
    return p;
}

/* ---- the screen ------------------------------------------------------ */

void boot_begin(void)
{
    ROW row;
    char v[COLS], *p;
    WORD r, i, k, b;
    uint16_t tenths;

    margin = (WORD)(LMARGN < INSET ? INSET - LMARGN : 0);
    CRSINH = 1;                         /* from the next character on */
    dos_ink = COLOR1;
    COLOR1 = INK;                       /* the shadow: see bootinfo.h */
    COLPF1 = INK;
    COLPF2 = PAPER;
    COLBK  = PAPER;
    row[0] = (char)E_CLEAR;
    cio_write(E_IOCB, row, 1);
    blank();

    for (r = 0; r < LOGO_ROWS; r++) {
        for (i = 0; i < margin; i++)
            row[i] = ' ';
        k = margin;
        for (i = 0; i < LOGO_LETTERS; i++) {
            uint8_t bits = logo[i][r];
            for (b = 4; b >= 0; b--)
                row[k++] = (bits & (1 << b)) ? (char)(' ' | E_INVERSE) : ' ';
            row[k++] = ' ';             /* the gap between letters */
        }
        row_out(row, (WORD)(k - 1));    /* not the last gap */
    }
    rule();
    blank();

    line(LS_BOOT_VERSION, s_version);

    p = app(v, s_cpu);                  /* src/farload.s let it this far */
    if (rapidus.present)
        p = app(p, s_rapidus);
    *p = '\0';
    line(LS_BOOT_CPU, v);

    /* Sixteen banks to the megabyte; one decimal, rounded. */
    tenths = (uint16_t)(((uint16_t)farmem.banks * 10 + 8) / 16);
    p = dec(v, (uint16_t)(tenths / 10));
    *p++ = '.';
    p = dec(p, (uint16_t)(tenths % 10));
    p = app(p, s_mb);
    p = app(p, lang_str(LS_BOOT_BANKS));
    *p++ = ' ';
    *p++ = '$';
    p = hex2(p, farmem.first_bank);
    *p++ = '-';
    *p++ = '$';
    p = hex2(p, farmem.last_bank);
    *p = '\0';
    line(LS_BOOT_MEMORY, v);

    line(LS_BOOT_DOS, dos.kind == DOS_SDX    ? s_sdx
                    : dos.kind == DOS_SPARTA ? s_sparta
                                             : s_dos2);
    if (config.found)
        line(LS_BOOT_CONFIG, s_cfg);
    else
        line(LS_BOOT_CONFIG, lang_str(LS_BOOT_DEFAULTS));
    if (lang_loaded())
        line(LS_BOOT_LANG, s_lang);
    else
        line(LS_BOOT_LANG, lang_str(LS_BOOT_BUILTIN));
}

void boot_video(int16_t video)
{
    char v[COLS], *p;

    if (video != CFG_VIDEO_VBXE) {
        line(LS_BOOT_VIDEO, s_antic);
        return;
    }
    p = app(v, s_vbxe);
    p = dec(p, vbxe_major());
    *p++ = '.';
    p = two(p, vbxe_minor_bcd());       /* 1.26, as the core numbers itself */
    *p++ = ' ';
    *p++ = '(';
    *p++ = '$';                         /* $D640: the first register, which is
                                         * the address the manuals give, not
                                         * the page vbxe_base holds */
    p = hex2(p, (uint8_t)(vbxe_base >> 8));
    p = hex2(p, (uint8_t)(vbxe_base + FX_CORE_REVISION));
    *p++ = ')';
    *p = '\0';
    line(LS_BOOT_VIDEO, v);
}

void boot_clock(void)
{
    char v[COLS], *p;
    CLOCK c;
    uint8_t card = clock_card();

    if (card == CLOCK_NONE) {
        line(LS_BOOT_CLOCK, lang_str(LS_BOOT_NONE));
        return;
    }
    clock_read(&c);
    p = app(v, card == CLOCK_U1MB ? s_u1mb : s_side);
    p = dec(p, c.year);
    *p++ = '-';
    p = two(p, c.month);
    *p++ = '-';
    p = two(p, c.day);
    *p++ = ' ';
    p = two(p, c.hour);
    *p++ = ':';
    p = two(p, c.minute);
    *p++ = ':';
    p = two(p, c.second);
    *p = '\0';
    line(LS_BOOT_CLOCK, v);
}

void boot_pointer(int16_t kind)
{
    WORD s;

    switch (kind) {
    case PTR_ST_MOUSE:    s = LS_BOOT_PTR_ST;     break;
    case PTR_AMIGA_MOUSE: s = LS_BOOT_PTR_AMIGA;  break;
    case PTR_TRAKBALL:    s = LS_BOOT_PTR_CX80;   break;
    case PTR_TABLET:      s = LS_BOOT_PTR_TABLET; break;
    case PTR_XEM1:        s = LS_BOOT_PTR_XEM1;   break;
    default:              s = LS_BOOT_NONE;       break;
    }
    line(LS_BOOT_POINTER, lang_str(s));
}

void boot_printer(void)
{
    char v[COLS + CFG_PRINTTO_MAX], *p;

    if (config.printer == CFG_PRINT_NONE) {
        line(LS_BOOT_PRINTER, lang_str(LS_BOOT_NONE));
        return;
    }
    p = app(v, config.printer == CFG_PRINT_PS ? s_ps : s_pcl);
    p = app(p, config.printto);
    *p = '\0';
    line(LS_BOOT_PRINTER, v);
}

/* ---- the hold -------------------------------------------------------- */

/* Frames, from whichever counter is running: the interrupt regime's when
 * it is up, VCOUNT's wrap when it is not. */
static uint16_t ticks(void)
{
    static uint16_t n;
    static uint8_t last;
    uint8_t vc = VCOUNT;

    if (irq.how != IRQ_OFF)
        return irq_frames;
    if (vc < last)
        n++;
    last = vc;
    return n;
}

static uint8_t key_held(void)
{
    return (uint8_t)((SKSTAT & 0x04) == 0);
}

static uint8_t shift_held(void)
{
    return (uint8_t)((SKSTAT & 0x08) == 0);
}

/* The key that ended the hold is not a keystroke for the desktop: out of
 * the handler's ring, and out of POKEY's own latch (the bit low then
 * high, as the OS clears it).  POKMSK is kept in step with IRQEN by both
 * regimes (src/sys/irq.c), so this changes nothing else. */
static void kb_drain(void)
{
    irq_kb_head = irq_kb_tail;
    IRQEN = (uint8_t)(POKMSK & ~0x40);
    IRQEN = POKMSK;
}

void boot_end(void)
{
    ROW row;
    const char *h;
    WORD n, i, k;
    uint16_t t0, hold;
    uint8_t paused = 0;

    rule();
    blank();
    /* The hint: inverse, centred, as EmuTOS sets it. */
    h = lang_str(LS_BOOT_HOLD);
    n = (WORD)strlen(h);
    if (n > WIDTH)
        n = WIDTH;
    k = (WORD)(margin + (WIDTH - n) / 2);
    for (i = 0; i < k; i++)
        row[i] = ' ';
    for (i = 0; i < n; i++)
        row[k + i] = (char)(h[i] | E_INVERSE);
    row_out(row, (WORD)(k + n));
    CRSINH = 0;                         /* DOS gets its cursor back */

    /* Three seconds of frames -- 50 or 60 to the second -- any key ends
     * it, and SHIFT holds it for as long as SHIFT is held. */
    hold = (uint16_t)(((PAL & 0x0E) == 0 ? 50 : 60) * HOLD_SECONDS);
    t0 = ticks();
    while (!key_held()) {
        if (shift_held())
            paused = 1;
        else if (paused || (uint16_t)(ticks() - t0) >= hold)
            break;
    }
    kb_drain();
    COLOR1 = dos_ink;                   /* the OS's VBI restores the rest */
}
