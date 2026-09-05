/* desk.h -- the GEM Desktop's own declarations.
 *
 * The desktop is a gem4xe application (src/app/gem.h) in the donor's
 * shape (EmuTOS desk/): one screen tree that the AES draws as the desk
 * under every window -- ROOT, DROOT the desk itself, then a box per
 * window and then the item objects, icons on the desk or in a window --
 * and a struct of globals, G, as the donor's deskglob.h has.  The
 * resource is DESKTOP.RSC, built on the host by tools/deskrsc.py, whose
 * indices come in through build/deskrsc.h.
 *
 * A folder window is the donor's WNODE: the AES window, its box in the
 * screen tree, the view (which rows of the item grid are shown), and a
 * PNODE, the directory listed -- its search spec and its FNODEs, one per
 * entry read through Fsfirst/Fsnext.  The FNODEs live in far memory,
 * an arena Malloc'd once at start (Mfree is a no-op, src/sys/gemdos.c),
 * because a window's worth of them is more than the near data allows;
 * the AES reads nothing there.  What it does read -- the name and the
 * information lines, the labels behind ib_ptext -- stays in bank $00.
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
#define WINDOW_SPEC 0x00001100L         /* a window's box: white, no pattern */
#define MIN_WINT    4                   /* between icon cells */
#define MIN_HINT    2

#define WINDOW_STYLE (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | \
                      UPARROW | DNARROW | VSLIDE | LFARROW | RTARROW | HSLIDE)

#define LEN_ZPATH   48                  /* "A:\DIR\*.*" and its NUL */
#define LEN_ZFNAME  14                  /* "FILENAME.EXT" and its NUL */
#define LEN_ZINFO   36                  /* " 1234567 bytes used in 12 items." */
#define NUM_FNODES  64                  /* a window lists this many at most */
#define DISPATTR    FA_SUBDIR           /* what a window lists: folders too */
#define F_SELECTED  0x0001              /* f_flags */

/* One directory entry, as Fsfirst/Fsnext gave it: the DTA's fields
 * and the item object showing it, if it is in view.  In far memory. */
typedef struct {
    WORD  f_obid;                       /* 0: not in view */
    WORD  f_flags;                      /* F_SELECTED */
    WORD  f_attr;                       /* FA_* */
    UWORD f_time, f_date;
    LONG  f_size;
    char  f_name[LEN_ZFNAME];
} FNODE;

/* A directory as a window lists it: the search spec and what it found,
 * folders first then by name, as the donor sorts by name (S_NAME). */
typedef struct {
    WORD  p_count;                      /* entries listed */
    LONG  p_size;                       /* their bytes together */
    char  p_spec[LEN_ZPATH];            /* "A:\SUB\*.*" */
    FNODE __far *p_flist;               /* NUM_FNODES of them */
} PNODE;

/* A folder window.  The one at index k has the box DROOT+1+k in the
 * screen tree (w_root); the window on top is the box last in ROOT's
 * children, objc_order keeping the tree in stacking order. */
typedef struct {
    WORD  w_id;                         /* the AES's handle; 0 = free */
    WORD  w_root;                       /* its box in g_screen */
    WORD  w_cvrow;                      /* the first row of items shown */
    WORD  w_pncol, w_pnrow;             /* the grid the window shows */
    WORD  w_vnrow;                      /* the rows the listing needs */
    PNODE w_path;
    char  w_name[LEN_ZPATH + 2];        /* " A:\SUB\*.* " */
    char  w_info[LEN_ZINFO];
} WNODE;

/* What an item object's ob_spec points at: its own ICONBLK, a copy of
 * the resource's with the label and the letter filled in. */
typedef struct {
    ICONBLK icon;
    char    label[LABEL_LEN];
} SCREENINFO;

typedef struct {
    OBJECT  *a_menu;                    /* ADMENU */
    OBJECT  *a_info;                    /* ADDINFO */
    ICONBLK *a_iblist;                  /* IB_HARD .. IB_DOCU */
    WORD     g_handle;                  /* the AES's VDI handle */
    WORD     g_wchar, g_hchar, g_wbox, g_hbox;
    GRECT    g_desk;                    /* the desk under the menu bar */
    WORD     g_wicon, g_hicon;          /* an icon's cell: image plus label */
    WORD     g_icw, g_ich;              /* the grid the cells snap to */
    WORD     g_screenfree;              /* the free chain's head */
    WORD     g_rmsg[8];                 /* evnt_multi's message */
    WORD     g_wcnt;                    /* windows open */
    DTA __far *g_dta;                   /* the listing's DTA, then the FNODEs */
    WNODE    g_wlist[NUM_WNODES];       /* by w_root - (DROOT + 1) */
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
WORD obj_icon(WORD wparent, WORD x, WORD y, WORD which,
              const char __far *label, WORD letter);

/* desktop.c */
void desk_busy(WORD on);

/* deskwin.c: folder windows */
WORD win_start(void);
WNODE *win_find(WORD wh);
WNODE *win_ontop(void);
void win_close(WNODE *pw, WORD close_window);
void do_wredraw(WORD wh, const GRECT *pc);
void act_chg(WORD wh, WORD root, WORD obj, WORD set, WORD dodraw);
void act_select(WORD wh, WORD root, WORD obj);
WORD do_open(WORD wh, WORD obj);
void hndl_wmsg(const WORD *msg);

#endif /* GEM4XE_DESK_H */
