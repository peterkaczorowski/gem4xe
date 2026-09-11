/* emit.c -- the page, off the machine.
 *
 * src/vdi/dev_print.c rasterises 640 x 800 dots into far memory; this
 * turns that into something a printer will take, and v_updwk is when.
 * GEM4XE.CFG chooses:
 *
 *     PRINTER = NONE | PCL | PS       what a page is written in
 *     PRINTTO = P:                    where it goes; a filename works
 *
 * NONE is the default and does nothing, because a machine with no
 * printer must not stop in v_updwk waiting for one.
 *
 * WHY THESE TWO.  Both are ROW-oriented and take the page's rows as they
 * are; ESC/P, the dot-matrix language, is column-oriented -- each byte
 * eight vertical dots -- so it wants the page transposed, which makes the
 * oldest format the awkward one here and the two modern ones nearly
 * free.  docs/printing.md has the rest of the argument, including why
 * PCL 6 is not on the list.
 *
 * WHERE THEY REACH.  PCL 5 to `P:` is a laser printer, or a FujiNet
 * passing the bytes through, or Altirra's 825.  PostScript to a FILE is
 * every modern printer there is, by way of the machine the user copies
 * it to: CUPS, Ghostscript, a mail client.  Neither needs gem4xe to know
 * anything about networks.
 *
 * ONE ASYMMETRY WORTH KNOWING.  A 1 bit in this page means ink.  PCL
 * agrees -- a 1 raster bit prints a dot -- so its rows go out as they
 * lie.  PostScript's `image` in DeviceGray reads 0 as black, so the
 * bytes are INVERTED on the way out.  That is done rather than argued
 * about with `imagemask`'s polarity, which is the sort of thing that is
 * only ever discovered on paper.
 */
#include "vdi.h"
#include "print.h"
#include "../sys/cio.h"
#include "../sys/farmem.h"

/* Set by src/gem.c out of GEM4XE.CFG.  The VDI does not read the config
 * file: every other setting reaches its driver as an argument from main,
 * and these two arrive the same way.  `pr_dest` points INTO the config
 * record rather than copying it -- two bytes instead of twenty, in the
 * bank that has none to spare. */
WORD        pr_kind = PR_NONE;
const char *pr_dest = 0;

/* One row's worth, near, because CIO writes from bank $00. */
static uint8_t row[PR_STRIDE];
static char    hex[PR_STRIDE * 2 + 2];
static char    sbuf[80];        /* a literal, on its way through */

static const char __far digits[] = "0123456789ABCDEF";

/* THE LITERALS ARE FAR.  Bank $00 holds 2.4 KB of near code and rodata
 * for the whole engine and this file's PostScript preamble alone would
 * not place in it -- the link said so, in nine bytes of cdata it could
 * not find a home for.  So they live in `cfar` beside the system font
 * and come through a near buffer on the way to CIO, which writes from
 * bank $00 and no further. */
static void put(int16_t fd, const char __far *s)
{
    uint16_t n = 0;

    while (s[n]) {
        sbuf[n] = (char)s[n];
        if (++n == sizeof sbuf - 1) {
            cio_write(fd, sbuf, n);
            s += n;
            n = 0;
        }
    }
    if (n)
        cio_write(fd, sbuf, n);
}

/* A number, in as many digits as it has.  No printf here: an application
 * links no libc and neither does this. */
static const char __far L_PCL_HEAD[] = "\033E\033*t";
static const char __far L_PCL_RES[] = "R\033*r0A";
static const char __far L_PCL_SKIP[] = "\033*b";
static const char __far L_PCL_Y[] = "Y";
static const char __far L_PCL_W[] = "W";
static const char __far L_PCL_END[] = "\033*rC\033E";
static const char __far L_PS1[] = "%!PS-Adobe-3.0\n%%BoundingBox: 76 108 537 684\n"
                          "%%Creator: gem4xe\n%%Pages: 1\n%%EndComments\n/picstr ";
static const char __far L_PS2[] = " string def\ngsave\n76 108 translate\n460.8 576 scale\n";
static const char __far L_SP[] = " ";
static const char __far L_SP0[] = " 0 ";
static const char __far L_PS3[] = " 1 [";
static const char __far L_PS4[] = " 0 0 -";
static const char __far L_PS5[] = "]\n{currentfile picstr readhexstring pop} image\n";
static const char __far L_PS_END[] = "grestore\nshowpage\n%%EOF\n";

static void putnum(int16_t fd, uint16_t v)
{
    char b[6];
    int16_t i = 6;

    b[--i] = 0;
    do {
        b[--i] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && i);
    put(fd, &b[i]);
}

/* The page's row r into `row`, near.  The page is one far bank, so the
 * offset is 16 bits (src/vdi/print.h). */
static void row_get(WORD r)
{
    const uint8_t __far *p = pr_page + (uint16_t)((uint16_t)r * PR_STRIDE);
    WORD i;

    for (i = 0; i < PR_STRIDE; i++)
        row[i] = p[i];
}

static WORD row_blank(void)
{
    WORD i;
    for (i = 0; i < PR_STRIDE; i++)
        if (row[i])
            return 0;
    return 1;
}

/* ---- PCL 5 -------------------------------------------------------------
 * ESC E            reset -- and, at the end, eject the page
 * ESC *t100R       the raster is 100 dpi; the printer scales to its own
 * ESC *r0A         start, at the left margin
 * ESC *b<n>Y       skip n blank rows -- which is most of a page of GEM,
 *                  and the difference between 64,000 bytes over a serial
 *                  line and a few thousand
 * ESC *b<n>W ...   one row, n bytes
 * ESC *rC          end
 */
static void emit_pcl(int16_t fd)
{
    WORD y, skip = 0;

    put(fd, L_PCL_HEAD);
    putnum(fd, PR_DPI);
    put(fd, L_PCL_RES);
    for (y = 0; y < PR_H; y++) {
        row_get(y);
        if (row_blank()) {
            skip++;
            continue;
        }
        if (skip) {
            put(fd, L_PCL_SKIP);
            putnum(fd, (uint16_t)skip);
            put(fd, L_PCL_Y);
            skip = 0;
        }
        put(fd, L_PCL_SKIP);
        putnum(fd, PR_STRIDE);
        put(fd, L_PCL_W);
        cio_write(fd, row, PR_STRIDE);
    }
    put(fd, L_PCL_END);
}

/* ---- PostScript --------------------------------------------------------
 * A page of 640 x 800 dots at 100 dpi is 6.4 x 8.0 inches, which is
 * 460.8 x 576 points, centred on whatever paper the printer has by the
 * translate below.  The image matrix flips y, because PostScript's
 * origin is the bottom left and a raster's is the top left.
 */
static void emit_ps(int16_t fd)
{
    WORD y, i;

    put(fd, L_PS1);
    putnum(fd, PR_STRIDE);
    put(fd, L_PS2);
    putnum(fd, PR_W);
    put(fd, L_SP);
    putnum(fd, PR_H);
    put(fd, L_PS3);
    putnum(fd, PR_W);
    put(fd, L_PS4);
    putnum(fd, PR_H);
    put(fd, L_SP0);
    putnum(fd, PR_H);
    put(fd, L_PS5);

    for (y = 0; y < PR_H; y++) {
        row_get(y);
        for (i = 0; i < PR_STRIDE; i++) {
            uint8_t b = (uint8_t)~row[i];   /* 0 is black: see the top */
            hex[i * 2]     = digits[b >> 4];
            hex[i * 2 + 1] = digits[b & 15];
        }
        hex[PR_STRIDE * 2] = '\n';
        hex[PR_STRIDE * 2 + 1] = 0;
        cio_write(fd, hex, PR_STRIDE * 2 + 1);
    }
    put(fd, L_PS_END);
}

/* ---- v_updwk's half ---------------------------------------------------- */

WORD pr_emit(void)
{
    int16_t fd;

    if (!pr_page || pr_kind == PR_NONE || !pr_dest)
        return 0;
    fd = cio_open(pr_dest, CIO_A_WRITE, 0);
    if (fd < 0)
        return 0;                       /* no printer, no file: not a crash */
    if (pr_kind == PR_PCL)
        emit_pcl(fd);
    else
        emit_ps(fd);
    cio_close(fd);
    return 1;
}
