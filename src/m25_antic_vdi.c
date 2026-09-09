/* m25_antic_vdi.c -- the VDI itself, running on the ANTIC device.
 *
 * The same src/vdi/vdi.c the VBXE build uses, compiled with
 * GEM4XE_DEV_ANTIC and linked against src/vdi/dev_antic.c instead of
 * dev_vbxe.c.  Nothing in the VDI was changed to make this happen; that
 * is what the seam was for.
 *
 * Everything here goes through the VDI's own interface -- the contrl,
 * intin and ptsin arrays and a call to vdi() -- so what is being tested
 * is the dispatcher, the workstation state, the clipping and the
 * attributes on top of a device they were not written for.
 */
#include "vdi/vdi.h"
#include "aes/aes.h"

#define STATUS ((volatile unsigned char *) 0x0600)

static void call(WORD op, WORD npts, WORD nint)
{
    contrl[0] = op;
    contrl[1] = npts;
    contrl[3] = nint;
    contrl[6] = VDI_PHYS_HANDLE;
    vdi();
}

/* the attribute setters this milestone uses */
static void set1(WORD op, WORD v)
{
    intin[0] = v;
    call(op, 0, 1);
}

static void rect(WORD op, WORD x1, WORD y1, WORD x2, WORD y2)
{
    ptsin[0] = x1; ptsin[1] = y1; ptsin[2] = x2; ptsin[3] = y2;
    call(op, 2, 0);
}

__task void main(void)
{
    STATUS[0] = 'A';
    STATUS[1] = 'V';
    STATUS[2] = 0;

    vdi_init();                         /* opens the workstation: the
                                         * device's palette call is what
                                         * brings ANTIC up */

    /* a filled rectangle in pen 1 */
    set1(VSF_COLOR, 1);
    set1(VSF_INTERIOR, FIS_SOLID);
    set1(VSWR_MODE, MD_REPLACE);
    rect(VR_RECFL, 20, 20, 200, 60);

    /* the same rectangle again in XOR: a hole in the middle of it */
    set1(VSWR_MODE, MD_XOR);
    rect(VR_RECFL, 60, 30, 160, 50);

    /* text */
    set1(VSWR_MODE, MD_REPLACE);
    set1(VST_COLOR, 1);
    {
        static const char s[] = "GEM ON ANTIC";
        WORD i;
        for (i = 0; s[i]; i++)
            intin[i] = (WORD)(unsigned char)s[i];
        ptsin[0] = 20;
        ptsin[1] = 80;
        call(V_GTEXT, 1, (WORD)i);
    }

    /* -- and the AES on top of it ------------------------------------
     * gsx_start is where the AES learns what device it is on: it asks
     * the VDI for the extent, the depth and the system font's cell, and
     * everything it lays out afterwards -- the menu bar's height, the
     * middle of the screen, a dialog's box -- comes off those numbers.
     * It is the whole reason a GEM ports to a second screen at all, so
     * the gate reads them back rather than trusting them. */
    gsx_start();
    {
        volatile WORD *g = (volatile WORD *)0x0610;
        g[0] = gl_width;      g[1] = gl_height;
        g[2] = gl_nplanes;    g[3] = gl_wchar;
        g[4] = gl_hchar;      g[5] = gl_wbox;
        g[6] = gl_hbox;       g[7] = gl_rmenu.g_w;
        g[8] = gl_rmenu.g_h;  g[9] = gl_rfull.g_h;
    }

    STATUS[2] = 'K';
    for (;;)
        ;
}
