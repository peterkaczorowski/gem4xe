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
#define CPDATA_LEN  128                 /* the shell buffer's first bytes: the
                                         * donor keeps copy/paste data there,
                                         * and the INF text follows them */
#define INF_REV_LEVEL 2                 /* "#R 02": the donor's DESKTOP.INF */
#define INF_NAME    "DESKTOP.INF"       /* ...and what it is called on the
                                         * boot drive, once it is a file */
#define SH_TAILLEN  128                 /* a command tail, as shel_write copies it */
#define LEN_ZFNAME  14                  /* "FILENAME.EXT" and its NUL */
#define LEN_ZINFO   36                  /* " 1234567 bytes used in 12 items." */
#define NUM_FNODES  64                  /* a window lists this many at most */
/* What an operation is doing, in the walk and in the dialog's title.
 * OP_COUNT is the pass that says what the others will do. */
#define OP_COUNT   0
#define OP_DELETE  1
#define OP_COPY    2
#define OP_MOVE    3

/* A file is copied through this much far memory at a time (the arena).
 * GEMDOS takes a far buffer and shuttles it through a pool slice
 * (src/sys/gemdos.c, gd_xfer), so the desktop's near memory pays
 * nothing for it. */
#define COPY_BUF   1024

#define MAX_DELLEVEL 4                  /* folders inside folders a delete
                                         * walks: a DTA (and a search slot,
                                         * src/sys/gemdos.c) per level */
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

/* A window's place, kept between programs (the donor's WSAVE): the
 * desktop writes them into the shell buffer as "#W" lines of
 * DESKTOP.INF when it exits to run a program, and opens the windows
 * again from them when the shell loads it back.  In far memory. */
typedef struct {
    WORD x_save, y_save, w_save, h_save;
    WORD hsl_save, vsl_save;            /* the view: the row shown first */
    char pth_save[LEN_ZPATH];           /* "" for a free slot */
} WSAVE;

typedef struct {
    WSAVE cs_wnode[NUM_WNODES];
} CSAVE;

/* What an item object's ob_spec points at: its own ICONBLK, a copy of
 * the resource's with the label and the letter filled in. */
typedef struct {
    ICONBLK icon;
    char    label[LABEL_LEN];
} SCREENINFO;

typedef struct {
    OBJECT  *a_menu;                    /* ADMENU */
    OBJECT  *a_info;                    /* ADDINFO */
    OBJECT  *a_mkdir;                   /* ADMKDBOX */
    OBJECT  *a_delete;                  /* ADDELDIA */
    OBJECT  *a_finfo;                   /* ADFINFO */
    ICONBLK *a_iblist;                  /* IB_HARD .. IB_DOCU */
    WORD     g_handle;                  /* the AES's VDI handle */
    WORD     g_wchar, g_hchar, g_wbox, g_hbox;
    GRECT    g_desk;                    /* the desk under the menu bar */
    WORD     g_wicon, g_hicon;          /* an icon's cell: image plus label */
    WORD     g_icw, g_ich;              /* the grid the cells snap to */
    WORD     g_screenfree;              /* the free chain's head */
    WORD     g_rmsg[8];                 /* evnt_multi's message */
    WORD     g_wcnt;                    /* windows open */
    LONG     g_nfiles, g_ndirs;         /* what a delete counted, then what
                                         * is left of it */
    LONG     g_opsize;                  /* ...and their bytes together, which
                                         * is what Show info calls a folder's
                                         * size */
    DTA __far *g_dta;                   /* the listing's DTA, then the FNODEs */
    DTA __far *g_opdta;                 /* MAX_DELLEVEL of them, one per level
                                         * of the walk: our GEMDOS keeps a
                                         * search's state by the DTA that owns
                                         * it, as the ST does */
    CSAVE __far *g_cnxsave;             /* the windows' places between programs */
    char __far *g_shelbuf;              /* the desktop's copy of the shell buffer */
    char __far *g_copybuf;              /* COPY_BUF of it, for file copies */
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
/* A click's effect on the selection: SHIFT toggles one item, a plain
 * click makes one the selection, and a click on nothing clears it. */
void act_bsclick(WORD wh, WORD root, WORD obj, WORD kstate);
WORD act_count(WORD root, WORD *pfirst);
/* ...and what a rubber band leaves: everything the box touches. */
void act_allselect(WORD wh, WORD root, const GRECT *box);
WORD do_open(WORD wh, WORD obj);
WORD do_aopen(WNODE *pw, WORD curr, const char __far *name);
void win_rebld(WNODE *pw);
/* The listing entry an item object shows, or 0. */
FNODE __far *win_fnode(WNODE *pw, WORD obj);
void hndl_wmsg(const WORD *msg);
void app_start(void);
void app_save(void);
/* Options -> Save desktop, and Options -> Read .INF file: the same
 * layout the shell buffer carries between programs, kept on the disk so
 * that it survives the machine being switched off. */
WORD inf_save(void);
WORD inf_read(void);
void cnx_get(void);
void cnx_put(void);

/* deskfun.c: what the desktop says, and what the File menu does to files */
WORD fun_alert(WORD defbut, WORD stnum);
void fun_mkdir(WNODE *pw);
/* An item dragged out of pw and let go over (dst_wh, dst_obj): a copy,
 * a move when SHIFT is held, a delete over the trash. */
void fun_file2any(WNODE *pw, WORD dst_wh, WORD dst_obj, WORD kstate);
void fun_del(WNODE *pw);
/* File -> Show info: what the selected item is, and the two things the
 * dialog can change about it -- its name and its read-only bit. */
void fun_info(WNODE *pw);

#endif /* GEM4XE_DESK_H */
