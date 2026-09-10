/* wind.c -- the AES window manager (EmuTOS aes/gemwmlib.c + gemwrect.c,
 * single-tasking, no 3D objects, no per-window colours).
 *
 * A window is a slot in gl_win[] and an object in W_TREE, the window
 * tree, whose root is the desktop: the open windows are the root's
 * children, in stacking order, the last child on top.  The frame --
 * title, closer, fuller, info line, scroll bars, sizer -- is not stored
 * per window; W_ACTIVE is one 19-object tree that w_bldactive lays out
 * afresh for whichever window is being drawn, then ob_draw draws it.
 *
 * DIRTY RECTANGLES.  Every window keeps a list of the rectangles of it
 * that are visible (gemwrect.c): newrect() splits the window's area
 * around every window above it, and both the AES and the application
 * draw only inside those pieces -- the AES through do_walk, which draws
 * the frame once per piece with the clip set to it; the application by
 * walking wind_get(WF_FIRSTXYWH/NEXTXYWH) in its WM_REDRAW handler.  On
 * this machine that is not a refinement but the budget: the blitter
 * moves about one screen per frame, so a change to one window must cost
 * its area, not the screen's.
 *
 * MOVING A WINDOW blits it (w_move -> bb_screen -> vro_cpyfm) rather than
 * redrawing it, when it stays inside the screen.  The blitter has no
 * shifter, so that is one blit only when both x's are even: w_snap rounds
 * a window's x down and its width up to even before wm_open and
 * wind_set(WF_CXYWH) use the rectangle.  The application sees the snapped
 * rectangle back from WF_CXYWH/WF_WXYWH, which is what it must draw to;
 * a window it asks to move by an odd amount lands one pixel to the left.
 *
 * The control manager (ctrl.c) hangs off w_setactive: every change of
 * the top window hands it the new work area, and a press outside that
 * is the window manager's -- see event.c for the ownership rules.
 *
 * NOT HERE YET: the menu bar.
 */
#include "aes.h"
#include "proc.h"
#include "../sys/zwin.h"

#define DROP_SHADOW_SIZE    2

#define TOPPED_COLOR    0x11a1      /* opaque, fill pattern 2, fill colour 1 */
#define UNTOPPED_COLOR  0x1100      /* transparent, hollow, fill colour 0 */
#define DESK_SPEC       0x00001143L /* the desktop: a green, pattern 4 G_BOX */

/* the frame's object types and colour words, one per W_* element */
static const WORD gl_watype[NUM_ELEM] = {
    G_IBOX,     /* W_BOX     */
    G_BOX,      /* W_TITLE   */
    G_BOXCHAR,  /* W_CLOSER  */
    G_BOXTEXT,  /* W_NAME    */
    G_BOXCHAR,  /* W_FULLER  */
    G_BOXTEXT,  /* W_INFO    */
    G_IBOX,     /* W_DATA    */
    G_IBOX,     /* W_WORK    */
    G_BOXCHAR,  /* W_SIZER   */
    G_BOX,      /* W_VBAR    */
    G_BOXCHAR,  /* W_UPARROW */
    G_BOXCHAR,  /* W_DNARROW */
    G_BOX,      /* W_VSLIDE  */
    G_BOX,      /* W_VELEV   */
    G_BOX,      /* W_HBAR    */
    G_BOXCHAR,  /* W_LFARROW */
    G_BOXCHAR,  /* W_RTARROW */
    G_BOX,      /* W_HSLIDE  */
    G_BOX       /* W_HELEV   */
};

static const uint32_t gl_waspec[NUM_ELEM] = {
    0x00011101UL,   /* W_BOX     */
    0x00011101UL,   /* W_TITLE   */
    0x05011101UL,   /* W_CLOSER  */
    0x0UL,          /* W_NAME    */
    0x07011101UL,   /* W_FULLER  */
    0x0UL,          /* W_INFO    */
    0x00001101UL,   /* W_DATA    */
    0x00001101UL,   /* W_WORK    */
    0x06011101UL,   /* W_SIZER   */
    0x00011101UL,   /* W_VBAR    */
    0x01011101UL,   /* W_UPARROW */
    0x02011101UL,   /* W_DNARROW */
    0x00011111UL,   /* W_VSLIDE  */
    0x00011101UL,   /* W_VELEV   */
    0x00011101UL,   /* W_HBAR    */
    0x04011101UL,   /* W_LFARROW */
    0x03011101UL,   /* W_RTARROW */
    0x00011111UL,   /* W_HSLIDE  */
    0x00011101UL    /* W_HELEV   */
};

/* the sample TEDINFO both the name and the info line start from */
static const TEDINFO gl_asamp = {
    0, 0, 0, IBM, 0, TE_LEFT, UNTOPPED_COLOR, 0, 1, 80, 80
};

/* ---- state ---------------------------------------------------------- */

ZWIN static OBJECT  W_TREE[NUM_WIN];
ZWIN static OBJECT  W_ACTIVE[NUM_ELEM];
WINDOW         gl_win[NUM_WIN];
static ORECT   gl_olist[NUM_ORECT];
static ORECT  *gl_rul;              /* the free rectangles */
static ORECT   gl_mkrect;           /* the rectangle newrect is breaking */
static TEDINFO gl_aname, gl_ainfo;
static WORD    wind_msg[8];

OBJECT *gl_wtree;
OBJECT *gl_awind;
WORD    gl_wtop;
OBJECT *gl_newdesk;
WORD    gl_newroot;
GRECT   gl_rzero;

/* ---- the rectangle pool (gemwrect.c) ------------------------------------ */

static void or_start(void)
{
    WORD i;

    gl_rul = 0;
    for (i = 0; i < NUM_ORECT; i++) {
        gl_olist[i].o_link = gl_rul;
        gl_rul = &gl_olist[i];
    }
}

static ORECT *get_orect(void)
{
    ORECT *po = gl_rul;

    if (po)
        gl_rul = po->o_link;
    return po;
}

#define TOP     0
#define LEFT    1
#define RIGHT   2
#define BOTTOM  3

/* The piece of `old` on the tlrb side of `new`, as a new list element
 * linked in front of `old`.  NULL if the pool is empty: that piece is then
 * simply not in the list and never drawn into -- the documented limit of
 * NUM_ORECT (the reference raises instead, so a case that hits it fails). */
static ORECT *mkpiece(WORD tlrb, const ORECT *new, ORECT *old)
{
    ORECT *rl = get_orect();
    WORD x, y, w, h;
    WORD oy2, ny2;

    if (!rl)
        return 0;
    rl->o_link = old;
    x = old->o_gr.g_x;
    w = old->o_gr.g_w;
    y = (old->o_gr.g_y > new->o_gr.g_y) ? old->o_gr.g_y : new->o_gr.g_y;
    oy2 = (WORD)(old->o_gr.g_y + old->o_gr.g_h);
    ny2 = (WORD)(new->o_gr.g_y + new->o_gr.g_h);
    h = (WORD)(((oy2 < ny2) ? oy2 : ny2) - y);
    switch (tlrb) {
    case TOP:
        y = old->o_gr.g_y;
        h = (WORD)(new->o_gr.g_y - old->o_gr.g_y);
        break;
    case LEFT:
        w = (WORD)(new->o_gr.g_x - old->o_gr.g_x);
        break;
    case RIGHT:
        x = (WORD)(new->o_gr.g_x + new->o_gr.g_w);
        w = (WORD)((old->o_gr.g_x + old->o_gr.g_w) - x);
        break;
    case BOTTOM:
        y = ny2;
        h = (WORD)(oy2 - ny2);
        break;
    }
    r_set(&rl->o_gr, x, y, w, h);
    return rl;
}

/* Break r around new: the pieces of r not under new replace r in the
 * list after p.  Returns the last piece, or NULL if they do not overlap. */
static ORECT *brkrct(const ORECT *new, ORECT *r, ORECT *p)
{
    WORD have_piece[4];
    WORD i;
    ORECT *piece;

    if (new->o_gr.g_x < r->o_gr.g_x + r->o_gr.g_w &&
        new->o_gr.g_x + new->o_gr.g_w > r->o_gr.g_x &&
        new->o_gr.g_y < r->o_gr.g_y + r->o_gr.g_h &&
        new->o_gr.g_y + new->o_gr.g_h > r->o_gr.g_y) {
        have_piece[TOP]    = (new->o_gr.g_y > r->o_gr.g_y);
        have_piece[LEFT]   = (new->o_gr.g_x > r->o_gr.g_x);
        have_piece[RIGHT]  = (new->o_gr.g_x + new->o_gr.g_w <
                              r->o_gr.g_x + r->o_gr.g_w);
        have_piece[BOTTOM] = (new->o_gr.g_y + new->o_gr.g_h <
                              r->o_gr.g_y + r->o_gr.g_h);
        for (i = 0; i < 4; i++) {
            if (have_piece[i]) {
                piece = mkpiece(i, new, r);
                if (piece)
                    p = p->o_link = piece;
            }
        }
        /* take r out of the list and give it back */
        p->o_link = r->o_link;
        r->o_link = gl_rul;
        gl_rul = r;
        return p;
    }
    return 0;
}

/* Break every rectangle in window wh's list around gl_mkrect. */
static void mkrect(OBJECT *tree, WORD wh, WORD sx, WORD sy)
{
    WINDOW *pwin = &gl_win[wh];
    ORECT *p, *r;

    (void)tree; (void)sx; (void)sy;
    p = (ORECT *)&pwin->w_rlist;    /* o_link is the first field */
    r = p->o_link;
    while (r) {
        p = brkrct(&gl_mkrect, r, p);
        if (p) {
            pwin->w_flags |= VF_BROKEN;
            r = p->o_link;
        } else {
            p = r;
            r = p->o_link;
        }
    }
}

/* Rebuild the rectangle lists for window wh's change of size or position:
 * every window from the desktop up to wh loses what wh now covers; wh's
 * own list becomes its whole true rectangle (its own occluders are broken
 * out of it by the windows above it calling this in turn). */
static void newrect(OBJECT *tree, WORD wh, WORD sx, WORD sy)
{
    WINDOW *pwin = &gl_win[wh];
    ORECT *r, *new;

    (void)sx; (void)sy;
    /* free the old list */
    r = pwin->w_rlist;
    while (r) {
        ORECT *next = r->o_link;
        r->o_link = gl_rul;
        gl_rul = r;
        r = next;
    }
    pwin->w_rlist = 0;
    pwin->w_flags &= (UWORD)~VF_BROKEN;

    w_getsize(WS_TRUE, wh, &gl_mkrect.o_gr);
    if (!(gl_mkrect.o_gr.g_w && gl_mkrect.o_gr.g_h))
        return;
    gl_mkrect.o_link = 0;
    everyobj(tree, ROOT, wh, mkrect, 0, 0, MAX_DEPTH);

    new = get_orect();
    if (new) {
        new->o_link = 0;
        new->o_gr = gl_mkrect.o_gr;
    }
    pwin->w_rlist = new;
}

/* ---- the window tree and the frame -------------------------------------- */

static void w_nilit(WORD num, OBJECT olist[])
{
    while (num--)
        olist[num].ob_next = olist[num].ob_head = olist[num].ob_tail = NIL;
}

static void w_setup(WORD w_handle, WORD kind)
{
    WINDOW *pwin = &gl_win[w_handle];

    pwin->w_flags = VF_INUSE;
    pwin->w_kind = (UWORD)kind;
    pwin->w_pname = "";
    pwin->w_pinfo = "";
    pwin->w_hslide = pwin->w_vslide = 0;
    pwin->w_hslsiz = pwin->w_vslsiz = -1;
}

static GRECT *w_getxptr(WORD which, WORD w_handle)
{
    WINDOW *pwin = &gl_win[w_handle];

    switch (which) {
    case WS_CURR:
    case WS_TRUE:
        return (GRECT *)&W_TREE[w_handle].ob_x;
    case WS_PREV:
        return &pwin->w_prev;
    case WS_WORK:
        return &pwin->w_work;
    default:
        return &pwin->w_full;
    }
}

void w_getsize(WORD which, WORD w_handle, GRECT *pt)
{
    *pt = *w_getxptr(which, w_handle);
    if (which == WS_TRUE && pt->g_w && pt->g_h) {
        pt->g_w = (WORD)(pt->g_w + DROP_SHADOW_SIZE);
        pt->g_h = (WORD)(pt->g_h + DROP_SHADOW_SIZE);
    }
}

void w_setsize(WORD which, WORD w_handle, const GRECT *pt)
{
    *w_getxptr(which, w_handle) = *pt;
}

static void w_adjust(WORD parent, WORD obj, WORD x, WORD y, WORD w, WORD h)
{
    W_ACTIVE[obj].ob_x = x;
    W_ACTIVE[obj].ob_y = y;
    W_ACTIVE[obj].ob_width = w;
    W_ACTIVE[obj].ob_height = h;
    W_ACTIVE[obj].ob_head = W_ACTIVE[obj].ob_tail = NIL;
    ob_add(W_ACTIVE, parent, obj);
}

/* Draw obj of tree, depth deep, once per rectangle of window wh's list
 * that meets pc (the screen if NULL), with the clip set to the piece. */
static void do_walk(WORD wh, OBJECT *tree, WORD obj, WORD depth, GRECT *pc)
{
    ORECT *po;
    GRECT t;

    if (wh == NIL)
        return;
    if (pc)
        rc_intersect(&gl_rfull, pc);
    else
        pc = &gl_rfull;
    for (po = gl_win[wh].w_rlist; po; po = po->o_link) {
        t = po->o_gr;
        if (rc_intersect(pc, &t)) {
            gsx_sclip(&t);
            ob_draw(tree, obj, depth);
        }
    }
}

static WORD w_top(void)
{
    return (gl_wtop != NIL) ? gl_wtop : DESKWH;
}

/* The top window changed: the control manager gets its work area, so
 * that clicks inside it go to the application and clicks elsewhere to
 * the window manager. */
static void w_setactive(void)
{
    GRECT d;

    w_getsize(WS_WORK, w_top(), &d);
    ct_chgown(&d);
}

/* The desktop under the windows: the application's WF_NEWDESK tree if it
 * installed one, else the window tree's root (a plain green box), drawn
 * through the desktop's rectangle list. */
void w_drawdesk(const GRECT *pc)
{
    OBJECT *tree;
    WORD depth, root;
    GRECT c;

    if (gl_newdesk) {
        tree = gl_newdesk;
        depth = MAX_DEPTH;
        root = gl_newroot;
    } else {
        tree = gl_wtree;
        depth = 0;
        root = ROOT;
    }
    c = *pc;
    do_walk(DESKWH, tree, root, depth, &c);
}

/* Build the vertical bar: arrows, slide and elevator, inside W_DATA. */
static void w_bldvbar(UWORD kind, WORD istop, const WINDOW *pw,
                      WORD x, WORD y, WORD w, WORD h)
{
    WORD size, posn;

    w_adjust(W_DATA, W_VBAR, x, y, gl_wbox, h);
    x = y = 0;
    if (istop) {
        if (kind & UPARROW) {
            w_adjust(W_VBAR, W_UPARROW, x, y, gl_wbox, gl_hbox);
            y = (WORD)(y + gl_hbox - 1);
            h = (WORD)(h - (gl_hbox - 1));
        }
        if (kind & DNARROW) {
            w = (WORD)(w - (gl_wbox - 1));
            h = (WORD)(h - (gl_hbox - 1));
            w_adjust(W_VBAR, W_DNARROW, x, (WORD)(y + h - 1), gl_wbox, gl_hbox);
        }
        if (kind & VSLIDE) {
            w_adjust(W_VBAR, W_VSLIDE, x, y, gl_wbox, h);
            if (pw->w_vslsiz == -1) {
                size = gl_hbox;
            } else {
                size = mul_div_round(h, pw->w_vslsiz, 1000);
                if (size < gl_hbox)
                    size = gl_hbox;
            }
            posn = mul_div_round((WORD)(h - size), pw->w_vslide, 1000);
            w_adjust(W_VSLIDE, W_VELEV, 0, posn, gl_wbox, size);
        }
    }
}

/* The horizontal bar, likewise. */
static void w_bldhbar(UWORD kind, WORD istop, const WINDOW *pw,
                      WORD x, WORD y, WORD w, WORD h)
{
    WORD size, posn;

    w_adjust(W_DATA, W_HBAR, x, y, w, gl_hbox);
    x = y = 0;
    if (istop) {
        if (kind & LFARROW) {
            w_adjust(W_HBAR, W_LFARROW, x, y, gl_wbox, gl_hbox);
            x = (WORD)(x + gl_wbox - 1);
            w = (WORD)(w - (gl_wbox - 1));
        }
        if (kind & RTARROW) {
            w = (WORD)(w - (gl_wbox - 1));
            h = (WORD)(h - (gl_hbox - 1));
            w_adjust(W_HBAR, W_RTARROW, (WORD)(x + w - 1), y, gl_wbox, gl_hbox);
        }
        if (kind & HSLIDE) {
            w_adjust(W_HBAR, W_HSLIDE, x, y, w, gl_hbox);
            if (pw->w_hslsiz == -1) {
                size = gl_wbox;
            } else {
                size = mul_div_round(w, pw->w_hslsiz, 1000);
                if (size < gl_wbox)
                    size = gl_wbox;
            }
            posn = mul_div_round((WORD)(w - size), pw->w_hslide, 1000);
            w_adjust(W_HSLIDE, W_HELEV, posn, 0, size, gl_hbox);
        }
    }
}

/* Lay W_ACTIVE out for window w_handle: the frame's parts, sized from
 * the window's current rectangle and its kind.  An untopped window gets
 * a disabled title and no arrows, slides or sizer. */
void w_bldactive(WORD w_handle)
{
    WORD istop, kind;
    WORD havevbar, havehbar;
    WORD tempw, sizer_x, sizer_y;
    GRECT t;
    WINDOW *pw;

    if (w_handle == NIL)
        return;
    pw = &gl_win[w_handle];
    istop = (gl_wtop == w_handle);
    kind = (WORD)pw->w_kind;
    w_nilit(NUM_ELEM, W_ACTIVE);

    gl_aname.te_ptext = (uint16_t)pw->w_pname;
    gl_ainfo.te_ptext = (uint16_t)pw->w_pinfo;
    gl_aname.te_just = TE_CNTR;

    /* the outer box, at the window's position */
    w_getsize(WS_CURR, w_handle, &t);
    W_ACTIVE[W_BOX].ob_x = t.g_x;
    W_ACTIVE[W_BOX].ob_y = t.g_y;
    W_ACTIVE[W_BOX].ob_width = t.g_w;
    W_ACTIVE[W_BOX].ob_height = t.g_h;
    t.g_x = t.g_y = 0;

    /* the title bar */
    if (kind & (NAME | CLOSER | FULLER)) {
        w_adjust(W_BOX, W_TITLE, t.g_x, t.g_y, t.g_w, gl_hbox);
        tempw = t.g_w;
        if ((kind & CLOSER) && istop) {
            w_adjust(W_TITLE, W_CLOSER, t.g_x, t.g_y, gl_wbox, gl_hbox);
            t.g_x = (WORD)(t.g_x + gl_wbox);
            tempw = (WORD)(tempw - gl_wbox);
        }
        if ((kind & FULLER) && istop) {
            tempw = (WORD)(tempw - gl_wbox);
            w_adjust(W_TITLE, W_FULLER, (WORD)(t.g_x + tempw), t.g_y,
                     gl_wbox, gl_hbox);
        }
        if (kind & NAME) {
            w_adjust(W_TITLE, W_NAME, t.g_x, t.g_y, tempw, gl_hbox);
            W_ACTIVE[W_NAME].ob_state = istop ? NORMAL : DISABLED;
            gl_aname.te_color = istop ? TOPPED_COLOR : UNTOPPED_COLOR;
        }
        t.g_x = 0;
        t.g_y = (WORD)(t.g_y + gl_hbox - 1);
        t.g_h = (WORD)(t.g_h - (gl_hbox - 1));
    }

    /* the info line */
    if (kind & INFO) {
        w_adjust(W_BOX, W_INFO, t.g_x, t.g_y, t.g_w, gl_hbox);
        t.g_y = (WORD)(t.g_y + gl_hbox - 1);
        t.g_h = (WORD)(t.g_h - (gl_hbox - 1));
    }

    /* the data area: the bars and the work area */
    w_adjust(W_BOX, W_DATA, t.g_x, t.g_y, t.g_w, t.g_h);
    havevbar = kind & (UPARROW | DNARROW | VSLIDE | SIZER);
    havehbar = kind & (LFARROW | RTARROW | HSLIDE | SIZER);
    sizer_x = -1;
    sizer_y = 0;
    if (havevbar && havehbar) {
        sizer_x = (WORD)(t.g_w - gl_wbox);
        sizer_y = (WORD)(t.g_h - gl_hbox);
    }
    t.g_x = t.g_y = 1;
    t.g_w = (WORD)(t.g_w - 2);
    t.g_h = (WORD)(t.g_h - 2);
    if (havevbar)
        t.g_w = (WORD)(t.g_w - (gl_wbox - 1));
    if (havehbar)
        t.g_h = (WORD)(t.g_h - (gl_hbox - 1));
    w_adjust(W_DATA, W_WORK, t.g_x, t.g_y, t.g_w, t.g_h);

    if (havevbar) {
        t.g_x = (WORD)(t.g_x + t.g_w);
        w_bldvbar((UWORD)kind, istop, pw, t.g_x, 0,
                  (WORD)(t.g_w + 2), (WORD)(t.g_h + 2));
    }
    if (havehbar) {
        t.g_y = (WORD)(t.g_y + t.g_h);
        w_bldhbar((UWORD)kind, istop, pw, 0, t.g_y,
                  (WORD)(t.g_w + 2), (WORD)(t.g_h + 2));
    }
    if (sizer_x >= 0) {
        w_adjust(W_DATA, W_SIZER, sizer_x, sizer_y, gl_wbox, gl_hbox);
        W_ACTIVE[W_SIZER].ob_spec &= 0x00ffffffUL;
        if (istop && (kind & SIZER))
            W_ACTIVE[W_SIZER].ob_spec |= 0x06000000UL;
    }
}

/* Draw obj of window wh's frame (the whole frame for W_BOX), clipped to
 * the window's true rectangle, or to the current clip plus the shadow. */
void w_cpwalk(WORD wh, WORD obj, WORD depth, WORD usetrue)
{
    GRECT c;

    if (usetrue) {
        w_getsize(WS_TRUE, wh, &c);
    } else {
        gsx_gclip(&c);
        c.g_w = (WORD)(c.g_w + DROP_SHADOW_SIZE);
        c.g_h = (WORD)(c.g_h + DROP_SHADOW_SIZE);
    }
    w_bldactive(wh);
    do_walk(wh, gl_awind, obj, depth, &c);
}

/* ---- redraw messages ------------------------------------------------------ */

/* pt = the bounding box of window wh's rectangle list; FALSE if empty. */
static WORD w_union(const ORECT *po, GRECT *pt)
{
    if (!po)
        return FALSE;
    *pt = po->o_gr;
    for (po = po->o_link; po; po = po->o_link)
        rc_union(&po->o_gr, pt);
    return TRUE;
}

/* Send window wh a WM_REDRAW for the part of pt inside its work area
 * that it can see. */
static void w_redraw(WORD wh, const GRECT *pt)
{
    GRECT t, d;

    t = *pt;
    w_getsize(WS_WORK, wh, &d);
    if (rc_intersect(&t, &d) &&
        w_union(gl_win[wh].w_rlist, &d) &&
        rc_intersect(&d, &t)) {
        ap_sendmsg(proc_app, wind_msg, WM_REDRAW, wh, t.g_x, t.g_y, t.g_w, t.g_h);
    }
}

/* ---- moving and changing ---------------------------------------------------- */

/* Clip ps to the screen for w_move; if that pushed its x off the left
 * edge, take one column off the other rectangle too.  TRUE if it did. */
static WORD w_mvfix(GRECT *ps, GRECT *pd)
{
    WORD tmpsx = ps->g_x;

    rc_intersect(&gl_rfull, ps);
    if (tmpsx == -1) {
        pd->g_x++;
        pd->g_w--;
        return TRUE;
    }
    return FALSE;
}

/* Move the top window wh from its previous to its current rectangle by
 * blitting it, when both are on the screen; otherwise everything from
 * the desktop up is redrawn instead.  *pstop gets where w_update stops,
 * *prc the rectangle to update; TRUE if it was blitted. */
static WORD w_move(WORD wh, WORD *pstop, GRECT *prc)
{
    GRECT s, d;
    GRECT *pc;
    WORD sminus1, dminus1;

    w_getsize(WS_PREV, wh, &s);
    s.g_w = (WORD)(s.g_w + DROP_SHADOW_SIZE);
    s.g_h = (WORD)(s.g_h + DROP_SHADOW_SIZE);
    w_getsize(WS_TRUE, wh, &d);

    /* dragged back from off the right or bottom edge: no source to blit */
    if ((s.g_x + s.g_w > gl_width && d.g_x < s.g_x) ||
        (s.g_y + s.g_h > gl_height && d.g_y < s.g_y)) {
        rc_union(&s, &d);
        *pstop = DESKWH;
    } else {
        *pstop = wh;
    }
    sminus1 = w_mvfix(&s, &d);
    dminus1 = w_mvfix(&d, &s);

    if (*pstop == wh) {
        gsx_sclip(&gl_rfull);
        bb_screen(s.g_x, s.g_y, d.g_x, d.g_y, s.g_w, s.g_h);
        if (sminus1 != dminus1) {
            if (dminus1)
                s.g_x--;
            if (sminus1) {
                d.g_x--;
                d.g_w = 1;
                gsx_sclip(&d);
                w_cpwalk(gl_wtop, 0, 0, FALSE);
            }
        }
        pc = &s;
    } else {
        pc = &d;
    }
    *prc = *pc;
    return (*pstop == wh);
}

/* Redraw every window from bottom to top, frame and WM_REDRAW, within pt.
 * A window that was moved (blitted) is skipped: its pixels came along. */
void w_update(WORD bottom, GRECT *pt, WORD top, WORD moved)
{
    WORD i, ni, done;

    rc_intersect(&gl_rfull, pt);
    gsx_moff();
    if (bottom == DESKWH)
        bottom = W_TREE[ROOT].ob_head;
    if (bottom != NIL) {
        if (top == DESKWH)
            top = W_TREE[ROOT].ob_tail;
        do {
            if (!(moved && top == gl_wtop)) {
                gsx_sclip(pt);
                w_cpwalk(top, 0, MAX_DEPTH, FALSE);
                w_redraw(top, pt);
            }
            /* step top down to the window below it */
            i = bottom;
            done = (i == top);
            while (i != top) {
                ni = W_TREE[i].ob_next;
                if (ni == top)
                    top = i;
                else
                    i = ni;
            }
        } while (!done);
    }
    gsx_mon();
}

/* Window wh's rectangle becomes *pt: the rectangle lists are rebuilt and
 * the screen brought up to date in the least that is needed -- a blit
 * for a plain move of the top window, the frame alone for a change of
 * top, otherwise a redraw of the union of old and new from wherever in
 * the stack it starts to matter. */
static void draw_change(WORD wh, const GRECT *pt)
{
    GRECT c, pprev;
    WORD start, stop, moved;
    WORD oldtop, clrold, wasclr;
    WINDOW *pwin = &gl_win[wh];

    wasclr = !(pwin->w_flags & VF_BROKEN);
    w_getsize(WS_CURR, wh, &c);
    w_setsize(WS_PREV, wh, &c);
    w_setsize(WS_CURR, wh, pt);
    wm_calc(WC_WORK, pwin->w_kind, pt->g_x, pt->g_y, pt->g_w, pt->g_h,
            &pwin->w_work.g_x, &pwin->w_work.g_y,
            &pwin->w_work.g_w, &pwin->w_work.g_h);
    if (!(pwin->w_flags & VF_ISOPEN))
        return;

    everyobj(gl_wtree, ROOT, NIL, newrect, 0, 0, MAX_DEPTH);
    oldtop = gl_wtop;
    gl_wtop = W_TREE[ROOT].ob_tail;
    w_setactive();

    start = wh;
    stop = DESKWH;
    moved = FALSE;
    if (!rc_equal(&gl_rzero, pt) && pt->g_x == c.g_x && pt->g_y == c.g_y) {
        if (pt->g_w == c.g_w && pt->g_h == c.g_h) {
            /* same place, same size: a change of top, or nothing */
            if (wh != W_TREE[ROOT].ob_tail || wh == oldtop)
                return;
            if (oldtop != NIL) {
                w_cpwalk(oldtop, 0, MAX_DEPTH, TRUE);
                clrold = !(gl_win[oldtop].w_flags & VF_BROKEN);
            } else {
                clrold = TRUE;
            }
            if (clrold && wasclr) {
                w_cpwalk(gl_wtop, 0, MAX_DEPTH, TRUE);
                return;
            }
        } else {
            /* same place, new size */
            if (pt->g_w <= c.g_w && pt->g_h <= c.g_h) {
                stop = wh;
                w_cpwalk(gl_wtop, 0, MAX_DEPTH, TRUE);
                moved = TRUE;
            }
            if (pt->g_w < c.g_w || pt->g_h < c.g_h)
                start = DESKWH;
            c.g_w = (WORD)(((pt->g_w > c.g_w) ? pt->g_w : c.g_w) + DROP_SHADOW_SIZE);
            c.g_h = (WORD)(((pt->g_h > c.g_h) ? pt->g_h : c.g_h) + DROP_SHADOW_SIZE);
        }
    } else {
        /* a move, an open (from nothing) or a close (to nothing) */
        if (!(c.g_w && c.g_h) ||
            (pt->g_x <= c.g_x && pt->g_y <= c.g_y &&
             pt->g_x + pt->g_w >= c.g_x + c.g_w &&
             pt->g_y + pt->g_h >= c.g_y + c.g_h)) {
            c = *pt;
        } else {
            if (pt->g_w == c.g_w && pt->g_h == c.g_h && gl_wtop == wh) {
                moved = w_move(wh, &stop, &c);
                start = DESKWH;
            }
            if (!(pt->g_w && pt->g_h))
                start = DESKWH;
            if (start != DESKWH) {
                rc_union(pt, &c);
                if (!rc_equal(pt, &c))
                    start = DESKWH;
            }
        }
    }

    /* a new top: its frame is redrawn with the rest */
    if (oldtop != W_TREE[ROOT].ob_tail && gl_wtop != NIL) {
        GRECT t;
        w_getsize(WS_CURR, gl_wtop, &t);
        rc_union(&t, &c);
        if (oldtop != NIL && oldtop != wh) {
            w_getsize(WS_PREV, gl_wtop, &pprev);
            if (rc_equal(&pprev, &gl_rzero))
                w_cpwalk(oldtop, 0, MAX_DEPTH, TRUE);
        }
    }

    c.g_w = (WORD)(c.g_w + DROP_SHADOW_SIZE);
    c.g_h = (WORD)(c.g_h + DROP_SHADOW_SIZE);
    if (start == DESKWH)
        w_drawdesk(&c);
    w_update(start, &c, stop, moved);
}

/* Snap a window rectangle to the blitter: x down to even, width up to
 * even, so that the window's bytes start on a byte boundary and a move
 * from one even x to another is one blit.  The width is NOT widened to
 * keep the right edge when x moves: a snapped window asked to move by an
 * odd amount must stay the same size, or draw_change sees a resize and
 * redraws it instead of blitting it.  See the file comment. */
static void w_snap(GRECT *pt)
{
    pt->g_x = (WORD)(pt->g_x & ~1);
    if (pt->g_w & 1)
        pt->g_w++;
}

/* ---- the API -------------------------------------------------------------- */

void wm_init(void)
{
    WORD i;
    ORECT *po;

    or_start();
    for (i = 0; i < NUM_WIN; i++) {
        W_TREE[i].ob_type = G_IBOX;
        W_TREE[i].ob_flags = 0;
        W_TREE[i].ob_state = 0;
        W_TREE[i].ob_spec = 0;
        W_TREE[i].ob_x = W_TREE[i].ob_y = 0;
        W_TREE[i].ob_width = W_TREE[i].ob_height = 0;
        gl_win[i].w_flags = 0;
        gl_win[i].w_rlist = 0;
    }
    w_nilit(NUM_WIN, W_TREE);
    W_TREE[ROOT].ob_type = G_BOX;
    W_TREE[ROOT].ob_spec = DESK_SPEC;

    for (i = 0; i < NUM_ELEM; i++) {
        W_ACTIVE[i].ob_type = gl_watype[i];
        W_ACTIVE[i].ob_flags = 0;
        W_ACTIVE[i].ob_state = 0;
        W_ACTIVE[i].ob_spec = gl_waspec[i];
        W_ACTIVE[i].ob_x = W_ACTIVE[i].ob_y = 0;
        W_ACTIVE[i].ob_width = W_ACTIVE[i].ob_height = 0;
    }
    w_nilit(NUM_ELEM, W_ACTIVE);
    W_ACTIVE[ROOT].ob_state = SHADOWED;

    /* the desktop owns the whole screen below the menu bar */
    po = get_orect();
    po->o_link = 0;
    po->o_gr = gl_rfull;
    gl_win[DESKWH].w_rlist = po;
    w_setup(DESKWH, 0);
    w_setsize(WS_CURR, DESKWH, &gl_rscreen);
    w_setsize(WS_PREV, DESKWH, &gl_rscreen);
    w_setsize(WS_FULL, DESKWH, &gl_rfull);
    w_setsize(WS_WORK, DESKWH, &gl_rfull);

    gl_wtop = NIL;
    gl_wtree = W_TREE;
    gl_awind = W_ACTIVE;
    gl_newdesk = 0;
    gl_newroot = ROOT;
    r_set(&gl_rzero, 0, 0, 0, 0);

    gl_aname = gl_asamp;
    gl_ainfo = gl_asamp;
    gl_aname.te_just = TE_CNTR;
    W_ACTIVE[W_NAME].ob_spec = (uint16_t)&gl_aname;
    W_ACTIVE[W_INFO].ob_spec = (uint16_t)&gl_ainfo;
}

/* A new window of this kind, whose largest size is *pt: its handle, or
 * -1 when all NUM_WIN are in use. */
WORD wm_create(WORD kind, const GRECT *pt)
{
    WORD i;

    for (i = 0; i < NUM_WIN; i++) {
        if (!(gl_win[i].w_flags & VF_INUSE)) {
            w_setup(i, kind);
            w_setsize(WS_CURR, i, &gl_rzero);
            w_setsize(WS_PREV, i, &gl_rzero);
            w_setsize(WS_FULL, i, pt);
            return i;
        }
    }
    return -1;
}

static WINDOW *get_pwin(WORD w_handle)
{
    if (w_handle < 0 || w_handle >= NUM_WIN)
        return 0;
    if (!(gl_win[w_handle].w_flags & VF_INUSE))
        return 0;
    return &gl_win[w_handle];
}

WORD wm_open(WORD w_handle, const GRECT *pt)
{
    WINDOW *pwin = get_pwin(w_handle);
    GRECT t;

    if (!pwin || (pwin->w_flags & VF_ISOPEN))
        return FALSE;
    t = *pt;
    w_snap(&t);
    wm_update(BEG_UPDATE);
    pwin->w_flags |= VF_ISOPEN;
    ob_add(W_TREE, ROOT, w_handle);
    draw_change(w_handle, &t);
    w_setsize(WS_PREV, w_handle, &t);
    wm_update(END_UPDATE);
    return TRUE;
}

WORD wm_close(WORD w_handle)
{
    WINDOW *pwin = get_pwin(w_handle);
    GRECT t;

    if (!pwin || !(pwin->w_flags & VF_ISOPEN))
        return FALSE;
    t = gl_rzero;
    wm_update(BEG_UPDATE);
    ob_delete(gl_wtree, w_handle);
    draw_change(w_handle, &t);
    pwin->w_flags &= (UWORD)~VF_ISOPEN;
    wm_update(END_UPDATE);
    return TRUE;
}

WORD wm_delete(WORD w_handle)
{
    WINDOW *pwin = get_pwin(w_handle);

    if (!pwin)
        return FALSE;
    if (pwin->w_flags & VF_ISOPEN)
        wm_close(w_handle);
    newrect(gl_wtree, w_handle, 0, 0);
    w_setsize(WS_CURR, w_handle, &gl_rscreen);
    w_setsize(WS_PREV, w_handle, &gl_rscreen);
    w_setsize(WS_FULL, w_handle, &gl_rfull);
    w_setsize(WS_WORK, w_handle, &gl_rfull);
    pwin->w_flags = 0;
    return TRUE;
}

/* The rectangle list walk of WF_FIRSTXYWH/WF_NEXTXYWH: the next piece
 * of the list from po that meets pt, into pout, leaving the cursor after
 * it; an empty rectangle when the list is done. */
static void w_owns(WINDOW *pwin, ORECT *po, const GRECT *pt, GRECT *pout)
{
    while (po) {
        *pout = po->o_gr;
        pwin->w_rnext = po = po->o_link;
        if (rc_intersect(pt, pout))
            return;
    }
    pout->g_w = pout->g_h = 0;
}

WORD wm_get(WORD w_handle, WORD w_field, WORD *poutwds, const WORD *pinwds)
{
    WINDOW *pwin;
    GRECT t;
    WORD which;

    (void)pinwds;
    pwin = get_pwin(w_handle);
    if (!pwin && w_field != WF_TOP && w_field != WF_SCREEN)
        return FALSE;

    switch (w_field) {
    case WF_WXYWH:
    case WF_CXYWH:
    case WF_PXYWH:
    case WF_FXYWH:
        which = (w_field == WF_WXYWH) ? WS_WORK :
                (w_field == WF_CXYWH) ? WS_CURR :
                (w_field == WF_PXYWH) ? WS_PREV : WS_FULL;
        w_getsize(which, w_handle, &t);
        poutwds[0] = t.g_x;  poutwds[1] = t.g_y;
        poutwds[2] = t.g_w;  poutwds[3] = t.g_h;
        break;
    case WF_HSLIDE:
        poutwds[0] = pwin->w_hslide;
        break;
    case WF_VSLIDE:
        poutwds[0] = pwin->w_vslide;
        break;
    case WF_HSLSIZ:
        poutwds[0] = pwin->w_hslsiz;
        break;
    case WF_VSLSIZ:
        poutwds[0] = pwin->w_vslsiz;
        break;
    case WF_TOP:
        poutwds[0] = w_top();
        break;
    case WF_FIRSTXYWH:
    case WF_NEXTXYWH:
        w_getsize(WS_WORK, w_handle, &t);
        w_owns(pwin, (w_field == WF_FIRSTXYWH) ? pwin->w_rlist : pwin->w_rnext,
               &t, (GRECT *)poutwds);
        break;
    case WF_SCREEN:
        /* The donor hands out its menu/alert save buffer here for
         * applications to borrow as scratch.  gem4xe's is in VRAM
         * (vdi_save_form), which is not in the address space, so there is
         * nothing to lend: address 0, length 0. */
        poutwds[0] = poutwds[1] = poutwds[2] = poutwds[3] = 0;
        break;
    case WF_OWNER:
        poutwds[0] = 0;                         /* the one process */
        poutwds[1] = (pwin->w_flags & VF_ISOPEN) ? TRUE : FALSE;
        poutwds[2] = gl_wtop;
        poutwds[3] = W_TREE[ROOT].ob_head;
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

/* Bring window wh to the top: last among the root's children, then
 * draw_change with its own rectangle, which redraws just the frames. */
static void wm_mktop(WORD w_handle)
{
    GRECT p, t;

    ob_order(gl_wtree, w_handle, NIL);
    w_getsize(WS_PREV, w_handle, &p);
    w_getsize(WS_CURR, w_handle, &t);
    draw_change(w_handle, &t);
    w_setsize(WS_PREV, w_handle, &p);
}

WORD wm_set(WORD w_handle, WORD w_field, WORD *pinwds)
{
    WINDOW *pwin = get_pwin(w_handle);
    WORD which = -1;
    WORD do_cpwalk = FALSE;
    WORD ret = TRUE;
    GRECT t;

    if (!pwin)
        return FALSE;
    wm_update(BEG_UPDATE);

    /* slider sizes and positions are per mille; -1 asks for the default
     * size */
    if (w_field == WF_HSLSIZ || w_field == WF_VSLSIZ ||
        w_field == WF_HSLIDE || w_field == WF_VSLIDE) {
        if (!((w_field == WF_HSLSIZ || w_field == WF_VSLSIZ) && pinwds[0] == -1)) {
            if (pinwds[0] < 1)
                pinwds[0] = 1;
            if (pinwds[0] > 1000)
                pinwds[0] = 1000;
        }
    }

    switch (w_field) {
    case WF_NAME:
        /* the address is 32 bits in pinwds[0..1], high word first, as
         * in the ROM's intin; only the low word can matter here */
        pwin->w_pname = (const char *)(uint16_t)pinwds[1];
        gl_aname.te_ptext = (uint16_t)pinwds[1];
        if (pwin->w_flags & VF_ISOPEN) {
            which = W_NAME;
            do_cpwalk = TRUE;
        }
        break;
    case WF_INFO:
        pwin->w_pinfo = (const char *)(uint16_t)pinwds[1];
        gl_ainfo.te_ptext = (uint16_t)pinwds[1];
        if (pwin->w_flags & VF_ISOPEN) {
            which = W_INFO;
            do_cpwalk = TRUE;
        }
        break;
    case WF_CXYWH:
        t.g_x = pinwds[0];  t.g_y = pinwds[1];
        t.g_w = pinwds[2];  t.g_h = pinwds[3];
        w_snap(&t);
        draw_change(w_handle, &t);
        break;
    case WF_TOP:
        if (w_handle != gl_wtop)
            wm_mktop(w_handle);
        break;
    case WF_NEWDESK:
        gl_newdesk = (OBJECT *)(uint16_t)pinwds[1];
        gl_newroot = pinwds[2];
        break;
    case WF_HSLSIZ:
        if (pwin->w_hslsiz != pinwds[0]) {
            pwin->w_hslsiz = pinwds[0];
            which = W_HSLIDE;
        }
        break;
    case WF_VSLSIZ:
        if (pwin->w_vslsiz != pinwds[0]) {
            pwin->w_vslsiz = pinwds[0];
            which = W_VSLIDE;
        }
        break;
    case WF_HSLIDE:
        if (pwin->w_hslide != pinwds[0]) {
            pwin->w_hslide = pinwds[0];
            which = W_HSLIDE;
        }
        break;
    case WF_VSLIDE:
        if (pwin->w_vslide != pinwds[0]) {
            pwin->w_vslide = pinwds[0];
            which = W_VSLIDE;
        }
        break;
    default:
        ret = FALSE;
        break;
    }

    /* a slider of the top window is redrawn at once */
    if (w_handle == gl_wtop && (which == W_HSLIDE || which == W_VSLIDE))
        do_cpwalk = TRUE;
    if (do_cpwalk)
        w_cpwalk(w_handle, which, MAX_DEPTH, TRUE);

    wm_update(END_UPDATE);
    return ret;
}

/* The window under (x,y): the desktop's child there, or the desktop. */
WORD wm_find(WORD x, WORD y)
{
    return ob_find(gl_wtree, ROOT, 2, x, y);
}

/* BEG/END_UPDATE hold off other processes' screen updates -- none here;
 * BEG/END_MCTRL take the mouse from the window manager (fm_own). */
static WORD wm_ucount;

void wm_update(WORD beg_update)
{
    if (beg_update < 2) {
        if (beg_update)
            wm_ucount++;
        else if (wm_ucount)
            wm_ucount--;
    } else {
        fm_own((WORD)(beg_update - 2));
    }
}

/* Border rectangle from work rectangle (WC_BORDER) or back (WC_WORK),
 * for a window of this kind. */
void wm_calc(WORD wtype, UWORD kind, WORD x, WORD y, WORD w, WORD h,
             WORD *px, WORD *py, WORD *pw, WORD *ph)
{
    WORD tb, bb, lb, rb;

    tb = bb = rb = lb = 1;
    if (kind & (NAME | CLOSER | FULLER))
        tb = (WORD)(tb + gl_hbox - 1);
    if (kind & INFO)
        tb = (WORD)(tb + gl_hbox - 1);
    if (kind & (UPARROW | DNARROW | VSLIDE | SIZER))
        rb = (WORD)(rb + gl_wbox - 1);
    if (kind & (LFARROW | RTARROW | HSLIDE | SIZER))
        bb = (WORD)(bb + gl_hbox - 1);
    if (wtype == WC_BORDER) {
        lb = (WORD)-lb;  tb = (WORD)-tb;
        rb = (WORD)-rb;  bb = (WORD)-bb;
    }
    *px = (WORD)(x + lb);
    *py = (WORD)(y + tb);
    *pw = (WORD)(w - lb - rb);
    *ph = (WORD)(h - tb - bb);
}
