/* deskwin.c -- folder windows.
 *
 * The donor's deskwin.c and deskfpd.c, with the window half of its
 * desksupp.c and deskact.c, for the one view this desktop has (icons)
 * and the one order (folders first, then by name).  A window is a
 * WNODE (desk.h): the AES window, its box in the screen tree, the rows
 * of the item grid in view, and the PNODE -- the directory it lists,
 * read through Fsfirst/Fsnext into FNODEs in far memory.
 *
 * Opening a drive icon or a folder lists the directory, names the
 * window after the search spec, puts the total on the information
 * line, and builds the window's items -- a G_ICON per entry in the rows
 * shown -- under its box in the screen tree for the AES to draw when it
 * asks (WM_REDRAW).  Scrolling moves the view a row of the grid and
 * builds the items again; the AES's own rectangle list clips every
 * redraw, so a window under another draws only what shows.
 */
#include "desk.h"

/* The far arena, Malloc'd once: the DTA the listing reads into, then
 * every window's FNODEs, then the windows' saved places (CSAVE), the
 * desktop's copy of the shell buffer, and a DTA per level of a delete's
 * walk (deskfun.c). */
#define ARENA_SIZE  (sizeof(DTA) + (LONG)NUM_WNODES * NUM_FNODES * sizeof(FNODE) \
                     + sizeof(CSAVE) + SIZE_SHELBUF \
                     + (LONG)MAX_DELLEVEL * sizeof(DTA) + COPY_BUF)

/* The INF file's default windows (the donor's desk_inf_data1, "#W"):
 * in character cells, x 2 wide 38 high 12, each one lower. */
#define WIN_XCELL   2
#define WIN_WCELL   38
#define WIN_HCELL   12
static const WORD win_ycell[NUM_WNODES] = { 6, 8, 10, 13 };

/* -- strings and rectangles -------------------------------------------- */

static char *put_str(char *d, const char __far *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}

/* n in decimal at d; the sizes and counts are never negative. */
static char *put_num(char *d, LONG n)
{
    char digits[10];
    WORD i = 0;

    do {
        digits[i++] = (char)('0' + (WORD)(n % 10));
        n /= 10;
    } while (n);
    while (i)
        *d++ = digits[--i];
    *d = 0;
    return d;
}

static WORD far_strcmp(const char __far *a, const char __far *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (WORD)((unsigned char)*a - (unsigned char)*b);
}

/* *p2 becomes the part of it inside *p1; TRUE if any is. */
static WORD rc_intersect(const GRECT *p1, GRECT *p2)
{
    WORD tx, ty, tw, th;

    tw = (WORD)(p2->g_x + p2->g_w < p1->g_x + p1->g_w ? p2->g_x + p2->g_w : p1->g_x + p1->g_w);
    th = (WORD)(p2->g_y + p2->g_h < p1->g_y + p1->g_h ? p2->g_y + p2->g_h : p1->g_y + p1->g_h);
    tx = p2->g_x > p1->g_x ? p2->g_x : p1->g_x;
    ty = p2->g_y > p1->g_y ? p2->g_y : p1->g_y;
    p2->g_x = tx;
    p2->g_y = ty;
    p2->g_w = (WORD)(tw - tx);
    p2->g_h = (WORD)(th - ty);
    return tw > tx && th > ty;
}

static WORD mul_div(WORD m1, WORD m2, WORD d1)
{
    return (WORD)((LONG)m1 * m2 / d1);
}

static void wind_get_grect(WORD wh, WORD field, GRECT *r)
{
    wind_get(wh, field, &r->g_x, &r->g_y, &r->g_w, &r->g_h);
}

static WORD wind_set_grect(WORD wh, WORD field, const GRECT *r)
{
    return wind_set(wh, field, r->g_x, r->g_y, r->g_w, r->g_h);
}

/* -- the windows ------------------------------------------------------- */

/* Before the first window: the arena, and every WNODE free. */
WORD win_start(void)
{
    LONG arena;
    WORD i;

    G.g_wcnt = 0;
    arena = Malloc((LONG)ARENA_SIZE);
    if (arena <= 0)
        return FALSE;
    G.g_dta = (DTA __far *)arena;
    Fsetdta(G.g_dta);
    for (i = 0; i < NUM_WNODES; i++) {
        WNODE *pw = &G.g_wlist[i];
        pw->w_id = 0;
        pw->w_root = (WORD)(DROOT + 1 + i);
        pw->w_path.p_flist =
            (FNODE __far *)(arena + (LONG)sizeof(DTA) + (LONG)i * (NUM_FNODES * sizeof(FNODE)));
    }
    G.g_cnxsave = (CSAVE __far *)(arena + (LONG)sizeof(DTA)
                                  + (LONG)NUM_WNODES * (NUM_FNODES * sizeof(FNODE)));
    G.g_shelbuf = (char __far *)G.g_cnxsave + sizeof(CSAVE);
    G.g_opdta = (DTA __far *)(G.g_shelbuf + SIZE_SHELBUF);
    G.g_copybuf = (char __far *)(G.g_opdta + MAX_DELLEVEL);
    {                                           /* the donor's is zeroed */
        char __far *p = (char __far *)G.g_cnxsave;
        WORD n;
        for (n = 0; n < sizeof(CSAVE); n++)
            *p++ = 0;
    }
    return TRUE;
}

WNODE *win_find(WORD wh)
{
    WORD i;

    for (i = 0; i < NUM_WNODES; i++)
        if (G.g_wlist[i].w_id == wh)
            return &G.g_wlist[i];
    return NULL;
}

/* The window on top: the last of ROOT's children, if it is open. */
WNODE *win_ontop(void)
{
    WORD wob = G.g_screen[ROOT].ob_tail;

    if (G.g_screen[wob].ob_width && G.g_screen[wob].ob_height)
        return &G.g_wlist[wob - (DROOT + 1)];
    return NULL;
}

static void win_top(WNODE *pw)
{
    objc_order(G.g_screen, pw->w_root, NIL);
}

/* Give the window up: the AES's handle, and its box, which goes to the
 * bottom of the stack, right after the desk. */
static void win_free(WNODE *pw)
{
    if (pw->w_id != -1)
        wind_delete(pw->w_id);
    G.g_wcnt--;
    pw->w_id = 0;
    objc_order(G.g_screen, pw->w_root, 1);
    obj_wfree(pw->w_root, 0, 0, 0, 0);
}

/* A window in the place the next saved slot has (the INF file's
 * defaults, or where a window was when the desktop last exited), its
 * box sized to it, created but not yet open; NULL when all NUM_WNODES
 * are out. */
static WNODE *win_alloc(void)
{
    WSAVE __far *pws;
    WNODE *pw;
    WORD wob;
    GRECT r;

    if (G.g_wcnt == NUM_WNODES)
        return NULL;
    pws = &G.g_cnxsave->cs_wnode[G.g_wcnt];
    r.g_x = pws->x_save;
    r.g_y = pws->y_save;
    r.g_w = pws->w_save;
    r.g_h = pws->h_save;
    wob = obj_walloc(r.g_x, r.g_y, r.g_w, r.g_h);
    if (!wob)
        return NULL;
    G.g_wcnt++;
    pw = &G.g_wlist[wob - (DROOT + 1)];
    pw->w_root = wob;
    pw->w_cvrow = 0;
    pw->w_pncol = (WORD)((r.g_w - G.g_wchar) / (G.g_wicon + MIN_WINT));
    pw->w_pnrow = (WORD)((r.g_h - G.g_hchar) / (G.g_hicon + MIN_HINT));
    pw->w_vnrow = 0;
    pw->w_id = wind_create(WINDOW_STYLE, G.g_desk.g_x, G.g_desk.g_y,
                           G.g_desk.g_w, G.g_desk.g_h);
    if (pw->w_id != -1)
        return pw;
    win_free(pw);
    return NULL;
}

/* -- the listing ------------------------------------------------------- */

/* The window's FNODE behind item obj, or NULL. */
FNODE __far *win_fnode(WNODE *pw, WORD obj)
{
    FNODE __far *pf = pw->w_path.p_flist;
    WORD i;

    for (i = 0; i < pw->w_path.p_count; i++, pf++)
        if (pf->f_obid == obj)
            return pf;
    return NULL;
}

/* Folders first, then by name: the donor's pn_fcomp for S_NAME. */
static WORD pn_comp(const FNODE *a, const FNODE __far *b)
{
    if ((a->f_attr ^ b->f_attr) & FA_SUBDIR)
        return (a->f_attr & FA_SUBDIR) ? -1 : 1;
    return far_strcmp(a->f_name, b->f_name);
}

/* Take the search spec; FALSE if it does not fit. */
static WORD pn_open(PNODE *pn, const char *spec)
{
    WORD n = 0;

    while (spec[n])
        n++;
    if (n >= LEN_ZPATH)
        return FALSE;
    put_str(pn->p_spec, spec);
    pn->p_count = 0;
    pn->p_size = 0;
    return TRUE;
}

/* A near FNODE into a far one, byte by byte: cc65816 5.18 cannot
 * compile a struct assignment between near and far objects over 8 bytes
 * at all (tools/ccbug/README.md, B11); far to far it can. */
static void fn_copy(FNODE __far *d, const FNODE *s)
{
    char __far *pd = (char __far *)d;
    const char *ps = (const char *)s;
    WORD n;

    for (n = 0; n < sizeof(FNODE); n++)
        *pd++ = *ps++;
}

/* Read the directory through the DTA into the FNODEs, each one into its
 * place in the order: the count and the bytes together.  An error from
 * Fsfirst lists nothing. */
static void pn_active(PNODE *pn)
{
    DTA __far *dta = G.g_dta;
    FNODE __far *pf;
    FNODE fn;
    LONG ret;
    WORD count = 0, i;

    pn->p_size = 0;                             /* read again after a delete */
    pn->p_count = 0;
    ret = Fsfirst(pn->p_spec, DISPATTR);
    while (ret == E_OK && count < NUM_FNODES) {
        if (dta->d_fname[0] != '.') {
            fn.f_obid = 0;
            fn.f_flags = 0;
            fn.f_attr = (WORD)(unsigned char)dta->d_attrib;
            fn.f_time = dta->d_time;
            fn.f_date = dta->d_date;
            fn.f_size = dta->d_length;
            for (i = 0; i < LEN_ZFNAME - 1 && dta->d_fname[i]; i++)
                fn.f_name[i] = dta->d_fname[i];
            for (; i < LEN_ZFNAME; i++)
                fn.f_name[i] = 0;
            /* insertion: slide the ones after it up one */
            i = count;
            pf = pn->p_flist + count;
            while (i > 0 && pn_comp(&fn, pf - 1) < 0) {
                *pf = *(pf - 1);
                pf--;
                i--;
            }
            fn_copy(pf, &fn);
            count++;
            pn->p_size += fn.f_size;
        }
        ret = Fsnext();
    }
    pn->p_count = count;
}

/* -- the name and information lines ------------------------------------ */

static void win_sname(WNODE *pw)
{
    char *d = put_str(pw->w_name, " ");

    d = put_str(d, pw->w_path.p_spec);
    put_str(d, " ");
}

/* " 12345 bytes used in 6 items." (the donor's STINFOST). */
static void win_sinfo(WNODE *pw)
{
    char *d = put_str(pw->w_info, " ");

    d = put_num(d, pw->w_path.p_size);
    d = put_str(d, " bytes used in ");
    d = put_num(d, pw->w_path.p_count);
    put_str(d, " items.");
    wind_set(pw->w_id, WF_INFO, 0, (WORD)(uint16_t)pw->w_info, 0, 0);
}

/* -- the view ---------------------------------------------------------- */

/* Which icon an entry gets: a folder, a program (.G4A), or a document. */
static WORD win_which(const FNODE __far *pf)
{
    const char __far *s = pf->f_name;

    if (pf->f_attr & FA_SUBDIR)
        return IB_FOLDER;
    while (*s && *s != '.')
        s++;
    if (s[0] == '.' && s[1] == 'G' && s[2] == '4' && s[3] == 'A' && !s[4])
        return IB_APPL;
    return IB_DOCU;
}

/* The window's items for the view over work area *r: the grid that
 * fits (the donor's win_ocalc), then an icon per entry in the rows
 * shown, and the sliders to match. */
static void win_bldview(WNODE *pw, const GRECT *r)
{
    WORD iwspc = (WORD)(G.g_wicon + MIN_WINT);
    WORD ihspc = (WORD)(G.g_hicon + MIN_HINT);
    FNODE __far *pf;
    WORD wfit, hfit, i, n, row, col, obid, which;

    obj_wfree(pw->w_root, r->g_x, r->g_y, r->g_w, r->g_h);

    wfit = r->g_w / iwspc;
    if (wfit < 1)
        wfit = 1;
    hfit = r->g_h / ihspc;
    if (hfit < 1)
        hfit = 1;
    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count; i++, pf++)
        pf->f_obid = 0;
    pw->w_vnrow = (WORD)((pw->w_path.p_count + wfit - 1) / wfit);
    if (pw->w_vnrow < 1)
        pw->w_vnrow = 1;
    pw->w_pncol = wfit;
    pw->w_pnrow = hfit < pw->w_vnrow ? hfit : pw->w_vnrow;
    while (pw->w_vnrow - pw->w_cvrow < pw->w_pnrow)
        pw->w_cvrow--;

    i = (WORD)(pw->w_cvrow * pw->w_pncol);
    n = (WORD)(pw->w_path.p_count - i);
    pf = pw->w_path.p_flist + i;
    hfit = (WORD)(pw->w_vnrow - pw->w_cvrow);
    if (hfit > pw->w_pnrow + 1)
        hfit = (WORD)(pw->w_pnrow + 1);         /* a row may show in part */
    for (row = 0, i = 0; row < hfit && i < n; row++) {
        for (col = 0; col < pw->w_pncol && i < n; col++, i++, pf++) {
            which = win_which(pf);
            obid = obj_icon(pw->w_root, (WORD)(col * iwspc + MIN_WINT),
                            (WORD)(row * ihspc + MIN_HINT), which, pf->f_name, 0);
            if (!obid) {
                row = hfit;                     /* no items left: stop */
                break;
            }
            pf->f_obid = obid;
            G.g_screen[obid].ob_state =
                (UWORD)(WHITEBAK | ((pf->f_flags & F_SELECTED) ? SELECTED : 0));
            G.g_screen[obid].ob_flags = NONE;
        }
    }

    wind_set(pw->w_id, WF_HSLSIZ, 1000, 0, 0, 0);
    wind_set(pw->w_id, WF_VSLSIZ, mul_div(pw->w_pnrow, 1000, pw->w_vnrow), 0, 0, 0);
    wind_set(pw->w_id, WF_VSLIDE,
             pw->w_vnrow > pw->w_pnrow
                 ? mul_div(pw->w_cvrow, 1000, (WORD)(pw->w_vnrow - pw->w_pnrow)) : 0,
             0, 0, 0);
}

/* Build the window's items again over its work area. */
static void desk_verify(WORD wh)
{
    WNODE *pw = win_find(wh);
    GRECT t;

    if (pw) {
        wind_get_grect(wh, WF_WORKXYWH, &t);
        win_bldview(pw, &t);
    }
}

/* Draw wh's tree -- the desk's, or the window's box and items -- once
 * per rectangle of its list that meets *pc. */
void do_wredraw(WORD wh, const GRECT *pc)
{
    WORD root = DROOT;
    GRECT t;

    if (wh != DESKWH) {
        WNODE *pw = win_find(wh);
        if (!pw)
            return;
        root = pw->w_root;
    }
    graf_mouse(M_OFF, 0);
    wind_get_grect(wh, WF_FIRSTXYWH, &t);
    while (t.g_w && t.g_h) {
        if (rc_intersect(pc, &t))
            objc_draw(G.g_screen, root, MAX_DEPTH, t.g_x, t.g_y, t.g_w, t.g_h);
        wind_get_grect(wh, WF_NEXTXYWH, &t);
    }
    graf_mouse(M_ON, 0);
}

/* Show the view from row newcv, if that is a change. */
static void win_scroll(WNODE *pw, WORD newcv)
{
    GRECT t;

    if (newcv > pw->w_vnrow - pw->w_pnrow)
        newcv = (WORD)(pw->w_vnrow - pw->w_pnrow);
    if (newcv < 0)
        newcv = 0;
    if (newcv == pw->w_cvrow)
        return;
    pw->w_cvrow = newcv;
    wind_get_grect(pw->w_id, WF_WORKXYWH, &t);
    win_bldview(pw, &t);
    do_wredraw(pw->w_id, &t);
}

static void win_arrow(WNODE *pw, WORD arrow)
{
    switch (arrow) {
    case WA_UPPAGE:
        win_scroll(pw, (WORD)(pw->w_cvrow - pw->w_pnrow));
        break;
    case WA_DNPAGE:
        win_scroll(pw, (WORD)(pw->w_cvrow + pw->w_pnrow));
        break;
    case WA_UPLINE:
        win_scroll(pw, (WORD)(pw->w_cvrow - 1));
        break;
    case WA_DNLINE:
        win_scroll(pw, (WORD)(pw->w_cvrow + 1));
        break;
    default:                                    /* nothing scrolls sideways */
        break;
    }
}

static void win_slide(WNODE *pw, WORD permille)
{
    win_scroll(pw, mul_div(permille, (WORD)(pw->w_vnrow - pw->w_pnrow), 1000));
}

/* -- selection --------------------------------------------------------- */

/* Select item obj, or deselect it, and redraw it if asked. */
void act_chg(WORD wh, WORD root, WORD obj, WORD set, WORD dodraw)
{
    OBJECT *pob = &G.g_screen[obj];
    UWORD state = pob->ob_state;
    GRECT t;

    state = set ? (UWORD)(state | SELECTED) : (UWORD)(state & ~SELECTED);
    if (state == pob->ob_state)
        return;
    pob->ob_state = state;
    if (root != DROOT) {
        FNODE __far *pf = win_fnode(&G.g_wlist[root - (DROOT + 1)], obj);
        if (pf)
            pf->f_flags = set ? (WORD)(pf->f_flags | F_SELECTED)
                              : (WORD)(pf->f_flags & ~F_SELECTED);
    }
    if (dodraw) {
        objc_offset(G.g_screen, obj, &t.g_x, &t.g_y);
        t.g_w = pob->ob_width;
        t.g_h = pob->ob_height;
        do_wredraw(wh, &t);
    }
}

/* Select item obj under root and no other there; obj 0 selects none. */
void act_select(WORD wh, WORD root, WORD obj)
{
    WORD i;

    for (i = G.g_screen[root].ob_head; i >= WOBS_START; i = G.g_screen[i].ob_next)
        act_chg(wh, root, i, i == obj, TRUE);
}

/* The window, and the window handle, an item is in. */
static WORD obj_parent(WORD obj)
{
    while (obj >= WOBS_START)
        obj = G.g_screen[obj].ob_next;
    return obj;
}

static WORD obj_wh(WORD parent)
{
    return parent == DROOT ? DESKWH : G.g_wlist[parent - (DROOT + 1)].w_id;
}

/* -- opening ----------------------------------------------------------- */

/* Where a window may go: x on a 16-pixel boundary, below the menu bar. */
static void do_xyfix(WORD *px, WORD *py)
{
    *px = (WORD)((*px + 8) & 0xFFF0);
    if (*py < G.g_desk.g_y)
        *py = G.g_desk.g_y;
}

/* Open the window at *pt, growing from the icon curr (which is
 * deselected) when there is one; a window already open moves nothing. */
static void do_wopen(WORD new_win, WORD wh, WORD curr, const GRECT *pt)
{
    GRECT t = *pt, c;

    do_xyfix(&t.g_x, &t.g_y);
    if (curr > 0) {
        WORD croot = obj_parent(curr);
        objc_offset(G.g_screen, curr, &c.g_x, &c.g_y);
        c.g_w = G.g_screen[curr].ob_width;
        c.g_h = G.g_screen[curr].ob_height;
        graf_growbox(c.g_x, c.g_y, c.g_w, c.g_h, t.g_x, t.g_y, t.g_w, t.g_h);
        act_chg(obj_wh(croot), croot, curr, FALSE, new_win);
    }
    if (new_win)
        wind_open(wh, t.g_x, t.g_y, t.g_w, t.g_h);
}

/* The window between its full size and the size before that. */
static void do_wfull(WORD wh)
{
    GRECT curr, prev, full;

    wind_get_grect(wh, WF_CURRXYWH, &curr);
    wind_get_grect(wh, WF_PREVXYWH, &prev);
    wind_get_grect(wh, WF_FULLXYWH, &full);
    if (curr.g_x == full.g_x && curr.g_y == full.g_y
     && curr.g_w == full.g_w && curr.g_h == full.g_h) {
        wind_set_grect(wh, WF_CURRXYWH, &prev);
        graf_shrinkbox(prev.g_x, prev.g_y, prev.g_w, prev.g_h,
                       full.g_x, full.g_y, full.g_w, full.g_h);
    } else {
        graf_growbox(curr.g_x, curr.g_y, curr.g_w, curr.g_h,
                     full.g_x, full.g_y, full.g_w, full.g_h);
        wind_set_grect(wh, WF_CURRXYWH, &full);
    }
}

/* List path in the window and show it: a new window opens at *pt, an
 * open one is redrawn where it is. */
static WORD do_diropen(WNODE *pw, WORD new_win, WORD curr, const char *path,
                       const GRECT *pt, WORD redraw)
{
    GRECT t;

    desk_busy(TRUE);
    if (!pn_open(&pw->w_path, path)) {
        desk_busy(FALSE);
        return FALSE;
    }
    pn_active(&pw->w_path);
    win_sname(pw);
    win_sinfo(pw);
    wind_set(pw->w_id, WF_NAME, 0, (WORD)(uint16_t)pw->w_name, 0, 0);
    do_wopen(new_win, pw->w_id, curr, pt);
    if (new_win)
        win_top(pw);
    desk_verify(pw->w_id);
    if (redraw && !new_win) {
        wind_get_grect(pw->w_id, WF_WORKXYWH, &t);
        do_wredraw(pw->w_id, &t);
    }
    desk_busy(FALSE);
    return TRUE;
}

/* The window's directory listed again, after something on the disk
 * changed it: the same place, the same view, the items rebuilt.  The
 * listing's DTA is put back first -- a delete's walk leaves its own
 * (deskfun.c). */
void win_rebld(WNODE *pw)
{
    GRECT t;

    desk_busy(TRUE);
    Fsetdta(G.g_dta);
    pn_active(&pw->w_path);
    win_sname(pw);
    win_sinfo(pw);
    wind_set(pw->w_id, WF_NAME, 0, (WORD)(uint16_t)pw->w_name, 0, 0);
    desk_verify(pw->w_id);
    wind_get_grect(pw->w_id, WF_WORKXYWH, &t);
    do_wredraw(pw->w_id, &t);
    desk_busy(FALSE);
}

/* A drive icon: its root in a new window. */
static WORD do_dopen(WORD curr)
{
    WNODE *pw;
    char path[8];

    pw = win_alloc();
    if (!pw) {
        fun_alert(1, STNOWIND);
        act_chg(DESKWH, DROOT, curr, FALSE, TRUE);
        return FALSE;
    }
    path[0] = (char)(obj_info(curr)->icon.ib_char & 0xFF);
    path[1] = ':';
    path[2] = '\\';
    path[3] = '*';
    path[4] = '.';
    path[5] = '*';
    path[6] = 0;
    if (!do_diropen(pw, TRUE, curr, path, (const GRECT *)&G.g_screen[pw->w_root].ob_x, TRUE)) {
        win_free(pw);
        act_chg(DESKWH, DROOT, curr, FALSE, TRUE);
        return FALSE;
    }
    return TRUE;
}

/* A folder in a window: its listing in the same window. */
static WORD do_fopen(WNODE *pw, WORD curr, const char __far *name)
{
    char path[LEN_ZPATH];
    const char *spec = pw->w_path.p_spec;
    GRECT t;
    WORD n = 0, i;

    wind_get_grect(pw->w_id, WF_WORKXYWH, &t);
    while (spec[n])                             /* "A:\SUB\*.*" less the "*.*" */
        n++;
    n -= 3;
    for (i = 0; i < n; i++)
        path[i] = spec[i];
    while (*name && i < LEN_ZPATH - 5)
        path[i++] = *name++;
    if (*name)
        return FALSE;
    path[i++] = '\\';
    path[i++] = '*';
    path[i++] = '.';
    path[i++] = '*';
    path[i] = 0;
    return do_diropen(pw, FALSE, curr, path, &t, TRUE);
}

/* A program in a window: its folder becomes the default directory and
 * the program the shell's next command (the donor's do_aopen, pro_run
 * and pro_exec).  TRUE when the shell took it -- the desktop's main
 * loop is then done, and the shell runs the program once the desktop
 * has exited.  The icon shrinks to the desk on the way out, deselected
 * but not redrawn, as the donor has it.
 *
 * Not static: the compiler inlines a static function with one call
 * site, and this one's path and tail (176 bytes) would then sit in
 * do_open's frame under every window it opens -- the desktop's deepest
 * stack, 164 bytes deeper (milestone 6).  External, it keeps its own
 * frame, paid only on the way out to a program. */
WORD do_aopen(WNODE *pw, WORD curr, const char __far *name)
{
    char app_path[LEN_ZPATH];
    char tail[SH_TAILLEN];
    const char *spec = pw->w_path.p_spec;
    WORD n = 0, k = 0, i, ret;

    while (spec[n])                             /* "A:\SUB\*.*" less the "*.*" */
        n++;
    n -= 3;
    while (name[k])
        k++;
    if (n + k >= LEN_ZPATH)                     /* the full path must fit */
        return FALSE;
    for (i = 0; i < n; i++)
        app_path[i] = spec[i];
    app_path[i] = 0;
    desk_busy(TRUE);                            /* set_default_path: disk i/o */
    Dsetdrv((WORD)(app_path[0] - 'A'));
    if (Dsetpath(app_path) < 0) {
        desk_busy(FALSE);
        fun_alert(1, STDEFDIR);
        return FALSE;
    }
    desk_busy(FALSE);
    for (k = 0; name[k]; k++)                   /* the full path */
        app_path[i++] = name[k];
    app_path[i] = 0;
    for (i = 0; i < SH_TAILLEN; i++)            /* pro_run: no arguments, */
        tail[i] = 0;                            /* the CR after the NUL */
    tail[2] = 0x0D;
    desk_busy(TRUE);                            /* pro_exec */
    ret = shel_write(SHW_EXEC, 1, 1, app_path, tail);
    if (!ret)
        desk_busy(FALSE);
    do_wopen(FALSE, pw->w_id, curr, &G.g_desk);
    return ret;
}

/* Open item obj of window wh (DESKWH: the desk): a drive icon in a new
 * window, a folder in its own, a program through the shell.  TRUE only
 * when a program ran, as the donor's do_open answers: the desktop is
 * done then. */
WORD do_open(WORD wh, WORD obj)
{
    WNODE *pw;
    FNODE __far *pf;

    if (wh == DESKWH) {
        if (obj_info(obj)->icon.ib_char & 0xFF)
            do_dopen(obj);
        return FALSE;                           /* else the trash */
    }
    pw = win_find(wh);
    if (!pw)
        return FALSE;
    pf = win_fnode(pw, obj);
    if (!pf)
        return FALSE;
    if (pf->f_attr & FA_SUBDIR) {
        do_fopen(pw, obj, pf->f_name);
        return FALSE;
    }
    if (win_which(pf) == IB_APPL)
        return do_aopen(pw, obj, pf->f_name);
    return FALSE;                               /* a document */
}

/* Close the window, or -- close_window FALSE -- the folder it shows,
 * which lists the folder above it, or closes the window at the root. */
void win_close(WNODE *pw, WORD close_window)
{
    char path[LEN_ZPATH];
    const char *spec = pw->w_path.p_spec;
    GRECT t;
    WORD n = 0, last = 0, i;

    if (!close_window) {
        while (spec[n]) {                       /* the '\' before the "*.*" */
            if (spec[n] == '\\')
                last = n;
            n++;
        }
        for (i = last; i > 0 && spec[i - 1] != '\\'; i--)
            ;
        if (i > 0) {                            /* "A:\SUB\*.*" -> "A:\*.*" */
            for (n = 0; n < i; n++)
                path[n] = spec[n];
            path[n++] = '*';
            path[n++] = '.';
            path[n++] = '*';
            path[n] = 0;
            wind_get_grect(pw->w_id, WF_WORKXYWH, &t);
            do_diropen(pw, FALSE, 0, path, &t, TRUE);
            return;
        }
    }
    wind_close(pw->w_id);
    win_free(pw);
}

/* -- the windows between programs -------------------------------------- */

/* The desktop's DESKTOP.INF lives in the AES's shell buffer, after
 * CPDATA_LEN bytes: "#R 02" and a "#W" line per window slot -- the
 * view, the place in character cells, and the path, "@" ending it
 * (the donor's deskapp.c; the lines for the icons and the preferences
 * are later milestones).  app_save writes it from the slots when the
 * desktop exits to run a program, app_start reads it back into them
 * when the shell loads the desktop again, and cnx_put/cnx_get carry
 * the windows themselves to and from the slots (the donor's
 * deskmain.c).  The slots also give a new window its place. */

static WORD hex_dig(char c)
{
    if (c >= 'A')
        c = (char)(c + 9);
    return (WORD)(c & 0x0F);
}

/* The donor's scan_2: past the spaces, two hex digits (0xFF is -1) or
 * nothing at a CR. */
static WORD scan_2(const char __far **pp)
{
    const char __far *p = *pp;
    WORD v = 0;

    while (*p == ' ')
        p++;
    if (*p != '\r') {
        v = (WORD)(hex_dig(*p++) << 4);
        v |= hex_dig(*p++);
        if (v == 0xFF)
            v = -1;
    }
    *pp = p;
    return v;
}

static char __far *put_hex2(char __far *d, WORD v)
{
    static const char hex[] = "0123456789ABCDEF";

    *d++ = ' ';
    *d++ = hex[(v >> 4) & 0x0F];
    *d++ = hex[v & 0x0F];
    return d;
}

static char __far *put_far(char __far *d, const char __far *s)
{
    while (*s)
        *d++ = *s++;
    return d;
}

/* The file the layout lives in, on the drive the desktop was started
 * from -- the donor's INF_FILE_NAME with the boot drive's letter put
 * into it (deskapp.c read_inf_file).  An absolute path, so that a
 * desktop which has been walking around a disk still writes it where
 * it will be found at the next boot. */
static void inf_name(char *name)
{
    name[0] = (char)('A' + Dgetdrv());
    name[1] = ':';
    name[2] = '\\';
    put_str(name + 3, INF_NAME);
}

/* The INF text from the slots; its length with the NUL. */
static WORD inf_write(void)
{
    char __far *p = G.g_shelbuf + CPDATA_LEN;
    WSAVE __far *pws = G.g_cnxsave->cs_wnode;
    WORD i;

    p = put_far(p, "#R");
    p = put_hex2(p, INF_REV_LEVEL);
    p = put_far(p, "\r\n");
    for (i = 0; i < NUM_WNODES; i++, pws++) {
        p = put_far(p, "#W");
        p = put_hex2(p, pws->hsl_save);
        p = put_hex2(p, pws->vsl_save);
        p = put_hex2(p, (WORD)(pws->x_save / G.g_wchar));
        p = put_hex2(p, (WORD)(pws->y_save / G.g_hchar));
        p = put_hex2(p, (WORD)(pws->w_save / G.g_wchar));
        p = put_hex2(p, (WORD)(pws->h_save / G.g_hchar));
        p = put_hex2(p, 0);
        *p++ = ' ';
        p = put_far(p, pws->pth_save);
        p = put_far(p, "@\r\n");
    }
    *p = 0;
    return (WORD)((uint32_t)p - (uint32_t)G.g_shelbuf + 1);
}

/* The text after CPDATA_LEN, and how long it is without the NUL. */
static WORD inf_len(WORD len)
{
    return (WORD)(len - CPDATA_LEN - 1);
}

/* The file into the shell buffer, FALSE when there is none to read or
 * what is there is not INF text. */
static WORD inf_load(void)
{
    char name[LEN_ZFNAME + 4];
    char __far *buf = G.g_shelbuf + CPDATA_LEN;
    LONG fd, got;

    inf_name(name);
    fd = Fopen(name, 0);
    if (fd < 0)
        return FALSE;
    got = Fread((WORD)fd, (LONG)(SIZE_SHELBUF - CPDATA_LEN - 1), buf);
    Fclose((WORD)fd);
    if (got < 0)
        got = 0;
    buf[got] = 0;
    return (WORD)(buf[0] == '#');
}

/* ...and the buffer out to it.  `len` is inf_write's, so the NUL and
 * the copy/paste bytes in front of the text are not written: what goes
 * on the disk is the text a person could read. */
static WORD inf_store(WORD len)
{
    char name[LEN_ZFNAME + 4];
    LONG fd, put, n = (LONG)inf_len(len);

    inf_name(name);
    fd = Fcreate(name, 0);
    if (fd < 0)
        return FALSE;
    put = Fwrite((WORD)fd, n, G.g_shelbuf + CPDATA_LEN);
    Fclose((WORD)fd);
    return (WORD)(put == n);
}

/* No INF text yet: the donor's desk_inf_data1 windows, each lower
 * than the one before, as text for app_start to read like any other. */
static void build_inf(void)
{
    WSAVE __far *pws = G.g_cnxsave->cs_wnode;
    WORD i;

    for (i = 0; i < NUM_WNODES; i++, pws++) {
        pws->x_save = (WORD)(WIN_XCELL * G.g_wchar);
        pws->y_save = (WORD)(win_ycell[i] * G.g_hchar);
        pws->w_save = (WORD)(WIN_WCELL * G.g_wchar);
        pws->h_save = (WORD)(WIN_HCELL * G.g_hchar);
        pws->hsl_save = 0;
        pws->vsl_save = 0;
        pws->pth_save[0] = 0;
    }
    inf_write();
}

/* The slots from the "#W" lines of INF text. */
static void inf_parse(const char __far *pcurr)
{
    WSAVE __far *pws;
    WORD wincnt = 0, rev, i;

    while (*pcurr) {
        if (*pcurr++ != '#')
            continue;
        switch (*pcurr) {
        case 'R':
            pcurr++;
            rev = scan_2(&pcurr);
            (void)rev;
            break;
        case 'W':
            pcurr++;
            if (wincnt < NUM_WNODES) {
                pws = &G.g_cnxsave->cs_wnode[wincnt];
                pws->hsl_save = scan_2(&pcurr);
                pws->vsl_save = scan_2(&pcurr);
                pws->x_save = (WORD)(scan_2(&pcurr) * G.g_wchar);
                pws->y_save = (WORD)(scan_2(&pcurr) * G.g_hchar);
                pws->w_save = (WORD)(scan_2(&pcurr) * G.g_wchar);
                pws->h_save = (WORD)(scan_2(&pcurr) * G.g_hchar);
                pcurr += 4;                     /* " 00 ", then the path */
                for (i = 0; *pcurr != '@' && i < LEN_ZPATH - 1; i++)
                    pws->pth_save[i] = *pcurr++;
                pws->pth_save[i] = 0;
                wincnt++;
            }
            break;
        default:
            break;
        }
    }
}

/* Where the desktop's layout comes from at start-up, in the order the
 * donor looks: what the shell buffer holds -- which is how a desktop
 * that has just run a program gets its windows back -- then the file,
 * which is how it gets them back after the machine has been off, and
 * then the built-in default. */
void app_start(void)
{
    shel_get(G.g_shelbuf, SIZE_SHELBUF);
    if (G.g_shelbuf[CPDATA_LEN] != '#' && !inf_load())
        build_inf();
    inf_parse(G.g_shelbuf + CPDATA_LEN);
}

/* Options -> Save desktop: the windows as they are now, into the slots,
 * into the text, onto the disk. */
WORD inf_save(void)
{
    cnx_put();
    return inf_store(inf_write());
}

/* Options -> Read .INF file: the file back, and the windows with it --
 * what is open is closed first, so that what comes up is what was
 * saved and not what was saved on top of what is there. */
WORD inf_read(void)
{
    WORD i;

    if (!inf_load())
        return FALSE;
    inf_parse(G.g_shelbuf + CPDATA_LEN);
    for (i = 0; i < NUM_WNODES; i++)
        if (G.g_wlist[i].w_id > 0)
            win_close(&G.g_wlist[i], TRUE);
    cnx_get();
    return TRUE;
}

/* The slots into the shell buffer, for the next desktop. */
void app_save(void)
{
    WORD len = inf_write();

    shel_put(G.g_shelbuf, len);
}

/* The open windows into the slots, bottom-most first (ROOT's children
 * are in stacking order), the rest cleared: the order cnx_get opens
 * them in puts them back as they were. */
void cnx_put(void)
{
    WSAVE __far *pws = G.g_cnxsave->cs_wnode;
    WORD wob, n = 0, i;
    GRECT r;

    for (wob = G.g_screen[ROOT].ob_head; wob > ROOT; wob = G.g_screen[wob].ob_next) {
        WNODE *pw;
        if (wob == DROOT)
            continue;
        pw = &G.g_wlist[wob - (DROOT + 1)];
        if (pw->w_id <= 0)
            continue;
        wind_get_grect(pw->w_id, WF_CURRXYWH, &r);
        do_xyfix(&r.g_x, &r.g_y);
        pws->x_save = r.g_x;
        pws->y_save = r.g_y;
        pws->w_save = r.g_w;
        pws->h_save = r.g_h;
        pws->hsl_save = 0;
        pws->vsl_save = pw->w_cvrow;
        for (i = 0; pw->w_path.p_spec[i]; i++)
            pws->pth_save[i] = pw->w_path.p_spec[i];
        pws->pth_save[i] = 0;
        pws++;
        n++;
    }
    for (; n < NUM_WNODES; n++, pws++)
        pws->pth_save[0] = 0;
}

/* The windows back from the slots, each in its place -- on the desk,
 * at least -- and its view, growing from its drive's icon. */
void cnx_get(void)
{
    WSAVE __far *pws = G.g_cnxsave->cs_wnode;
    WNODE *pw;
    WORD nw, obid, i;
    char path[LEN_ZPATH];
    GRECT r;

    for (nw = 0; nw < NUM_WNODES; nw++, pws++) {
        if (pws->x_save >= G.g_desk.g_w)
            pws->x_save = (WORD)(G.g_desk.g_w / 2);
        if (pws->y_save >= G.g_desk.g_h)
            pws->y_save = (WORD)(G.g_desk.g_h / 2);
        if (pws->w_save <= 0 || pws->w_save > G.g_desk.g_w)
            pws->w_save = G.g_desk.g_w;
        if (pws->h_save <= 0 || pws->h_save > G.g_desk.g_h)
            pws->h_save = G.g_desk.g_h;
        if (!pws->pth_save[0])
            continue;
        obid = obj_get_obid(pws->pth_save[0]);
        pw = win_alloc();
        if (!pw)
            continue;
        pw->w_cvrow = pws->vsl_save;
        r.g_x = pws->x_save;
        r.g_y = pws->y_save;
        do_xyfix(&r.g_x, &r.g_y);
        pws->x_save = r.g_x;
        pws->y_save = r.g_y;
        r.g_w = pws->w_save;
        r.g_h = pws->h_save;
        for (i = 0; pws->pth_save[i]; i++)
            path[i] = pws->pth_save[i];
        path[i] = 0;
        if (!do_diropen(pw, TRUE, obid, path, &r, TRUE))
            win_free(pw);
    }
}

/* -- the window manager's messages ------------------------------------- */

void hndl_wmsg(const WORD *msg)
{
    WORD wh = msg[3];
    WNODE *pw = win_find(wh);
    GRECT t;

    switch (msg[0]) {
    case WM_REDRAW:
        t.g_x = msg[4];
        t.g_y = msg[5];
        t.g_w = msg[6];
        t.g_h = msg[7];
        do_wredraw(wh, &t);
        break;
    case WM_TOPPED:
        wind_set(wh, WF_TOP, 0, 0, 0, 0);
        /* falls through */
    case WM_NEWTOP:
        if (pw)
            win_top(pw);
        break;
    case WM_CLOSED:                             /* the closer is File -> Close:
                                                 * out of the folder, and the
                                                 * window at the root closes */
        if (pw)
            win_close(pw, FALSE);
        break;
    case WM_FULLED:
        do_wfull(wh);
        desk_verify(wh);
        break;
    case WM_ARROWED:
        if (pw)
            win_arrow(pw, msg[4]);
        break;
    case WM_VSLID:
        if (pw)
            win_slide(pw, msg[4]);
        break;
    case WM_SIZED:
    case WM_MOVED:
        if (!pw)
            break;
        t.g_x = msg[4];
        t.g_y = msg[5];
        t.g_w = msg[6];
        t.g_h = msg[7];
        do_xyfix(&t.g_x, &t.g_y);
        wind_set_grect(wh, WF_CURRXYWH, &t);
        if (msg[0] == WM_SIZED) {
            WORD cols = pw->w_pncol;
            desk_verify(wh);
            if (pw->w_pncol != cols) {          /* the items moved: the AES
                                                 * redraws only what it uncovered */
                wind_get_grect(wh, WF_WORKXYWH, &t);
                do_wredraw(wh, &t);
            }
        } else {                                /* the items keep their places
                                                 * in the box: move the box */
            wind_get_grect(wh, WF_WORKXYWH, &t);
            obj_wfree(pw->w_root, t.g_x, t.g_y, t.g_w, t.g_h);
            desk_verify(wh);
        }
        break;
    default:
        break;
    }
}
