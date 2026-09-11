/* config.h -- GEM4XE.CFG, read before there is a screen to complain on.
 *
 * WHY A FILE AND NOT A DIALOG.  Everything else gem4xe can be told is in
 * DESKTOP.INF, which the desktop writes and reads with a GEM already on
 * the screen.  This is the one thing that cannot be: what the screen IS.
 * A machine whose monitor will not lock to the VBXE's output shows
 * nothing at all, and there is no dialog to be had on a display that is
 * not there -- so the answer has to be reachable from the DOS prompt,
 * which means a PLAIN TEXT FILE a DOS text editor can open.
 *
 * That is the whole design brief, and it is why the format is what it
 * is: one KEY=VALUE per line, '#' or ';' to the end of a line is a
 * comment, blank lines ignored, case does not matter, and an unknown key
 * or value is SKIPPED rather than refused.  Somebody typing into ED on a
 * 40-column screen should not be able to make the machine unbootable by
 * misspelling something, and a file written for a later gem4xe should
 * still boot this one.
 *
 *     # gem4xe boot options
 *     VIDEO=AUTO          ; AUTO, VBXE or ANTIC
 *     MOUSE=AUTO          ; AUTO, ST, AMIGA, TRAKBALL, TABLET, XEM1, NONE
 *
 * THE RECOVERY PATH is a reboot to the DOS prompt and an edit here.  On
 * media that auto-starts GEM -- a SpartaDOS batch, DOS 2's AUTORUN.SYS
 * -- GEM comes straight back, so the answer there is to boot another
 * disk and edit the file from that (docs/shipping.md).
 *
 * WHERE IT IS LOOKED FOR: "D:GEM4XE.CFG", which is the current drive and
 * on a SpartaDOS the current directory -- the same place GEM.COM was
 * started from, in the usual case.  No file at all is not an error; it
 * means every default below stands, and the common machine never needs
 * one.
 */
#ifndef GEM4XE_CONFIG_H
#define GEM4XE_CONFIG_H

#include <stdint.h>

#define CFG_FILE "GEM4XE.CFG"

/* VIDEO. */
#define CFG_VIDEO_AUTO   0      /* a VBXE if the machine has one */
#define CFG_VIDEO_VBXE   1      /* the VBXE, and refuse without one */
#define CFG_VIDEO_ANTIC  2      /* stock ANTIC even where there is a VBXE:
                                 * the safe mode, for a monitor that will
                                 * not take 640x240 */

/* MOUSE: a PTR_* from src/vdi/pointer.h, or this. */
#define CFG_MOUSE_AUTO  (-1)

/* PRINTER -- what a page is written IN.  docs/printing.md says why these
 * and not others: PCL 5 and PostScript are row-oriented and take the
 * page's rows as they are, and PCL 6 is a different (binary) protocol
 * that every device speaking it also speaks PCL 5 for. */
#define CFG_PRINT_NONE   0      /* v_updwk does nothing.  The default:
                                 * a machine with no printer must not
                                 * hang waiting for one */
#define CFG_PRINT_PCL    1      /* PCL 5 raster */
#define CFG_PRINT_PS     2      /* PostScript */

#define CFG_PRINTTO_MAX  20     /* "D1:PAGE.PS" and room to spare */

typedef struct {
    int16_t video;
    int16_t mouse;
    int16_t printer;            /* CFG_PRINT_* */
    char    printto[CFG_PRINTTO_MAX];   /* where it goes; "P:" by default */
} CONFIG;

/* What the file said, or the defaults.  Readable after config_read(). */
extern CONFIG config;

/* Read it.  Never fails: a missing, unreadable or nonsensical file
 * leaves the defaults, because the alternative is a machine that will
 * not start because of a typo. */
void config_read(void);

#endif /* GEM4XE_CONFIG_H */
