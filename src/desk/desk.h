/* desk.h -- the GEM Desktop's own declarations.
 *
 * The desktop is a gem4xe application (src/app/gem.h) in the donor's
 * shape (EmuTOS desk/): one screen tree that the AES draws as the desk
 * under every window -- ROOT, DROOT the desk itself, then a box per
 * window and then the item objects, icons on the desk or in a window --
 * and a struct of globals, G, as the donor's deskglob.h has.  The
 * resource is DESKTOP.RSC, built on the host by tools/deskrsc.py, whose
 * indices come in through build/deskrsc.h.
 */
#ifndef GEM4XE_DESK_H
#define GEM4XE_DESK_H

#include "gem.h"
#include "deskrsc.h"

#define DESKWH      0                   /* the desktop's window handle */
#define DROOT       1                   /* the desk: ROOT's first child */
#define NUM_WNODES  4                   /* window objects, DROOT+1 on */
#define WOBS_START  (DROOT + 1 + NUM_WNODES)
#define NUM_ITEMS   16                  /* icons, on the desk and in windows */
#define NUM_SOBS    (WOBS_START + NUM_ITEMS)

#define MAX_DRIVES  8                   /* D1: to D8: */
#define MAX_ICONTEXT_WIDTH 12           /* an icon's label, in characters */
#define LABEL_LEN   (MAX_ICONTEXT_WIDTH + 1)

#define DESK_SPEC   0x00001143L         /* the desk: green, pattern 4 (the AES's own) */
#define MIN_WINT    4                   /* between icon cells */
#define MIN_HINT    2

/* What an item object's ob_spec points at: its own ICONBLK, a copy of
 * the resource's with the label and the letter filled in. */
typedef struct {
    ICONBLK icon;
    char    label[LABEL_LEN];
} SCREENINFO;

typedef struct {
    OBJECT  *a_menu;                    /* ADMENU */
    OBJECT  *a_info;                    /* ADDINFO */
    ICONBLK *a_iblist;                  /* IB_HARD, IB_FLOPPY, IB_TRASH */
    WORD     g_handle;                  /* the AES's VDI handle */
    WORD     g_wchar, g_hchar, g_wbox, g_hbox;
    GRECT    g_desk;                    /* the desk under the menu bar */
    WORD     g_wicon, g_hicon;          /* an icon's cell: image plus label */
    WORD     g_icw, g_ich;              /* the grid the cells snap to */
    WORD     g_screenfree;              /* the free chain's head */
    WORD     g_rmsg[8];                 /* evnt_multi's message */
    OBJECT     g_screen[NUM_SOBS];
    SCREENINFO g_screeninfo[NUM_ITEMS]; /* by obid - WOBS_START */
} GLOBES;

extern GLOBES G;

/* deskobj.c: the screen tree */
void obj_init(void);
WORD obj_walloc(WORD x, WORD y, WORD w, WORD h);
void obj_wfree(WORD obj, WORD x, WORD y, WORD w, WORD h);
WORD obj_ialloc(WORD wparent, WORD x, WORD y, WORD w, WORD h);
WORD obj_get_obid(WORD drive);
SCREENINFO *obj_info(WORD obj);

#endif /* GEM4XE_DESK_H */
