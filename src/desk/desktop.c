/* desktop.c -- DESKTOP.G4A, the GEM Desktop.
 *
 * A gem4xe application like any other (src/app/gem.h): the shell loop
 * in src/aes/shel.c loads it first, runs whatever it asks for with
 * shel_write, and loads it again when that returns, until it asks to
 * shut down.  The shape is the donor's deskmain.c: take the desk over
 * with a screen tree of drive icons and the trash, put the menu bar
 * up, and answer evnt_multi until Quit.
 *
 * Milestone 4 of docs/phase14.md was the bar, the icons, the About
 * dialog, Quit, and an icon that selects when clicked; milestone 5 the
 * folder windows (deskwin.c) a double-click on a drive icon opens;
 * milestone 6 a program run from its icon -- the desktop exits with
 * its windows' places in the shell buffer, and opens them again when
 * the shell loads it back; milestone 7 New folder and Delete
 * (deskfun.c), the first things the desktop does TO a disk; then the
 * drag that copies or moves, and Show info, which is also the rename.
 * The items of later milestones are in the menu, disabled, until the
 * milestone that brings them (NOT_YET_ITEMS, build/deskrsc.h).
 */
#include "desk.h"

GLOBES G;

/* -- dialogs ----------------------------------------------------------- */

static GRECT dlg;

static void start_dialog(OBJECT *tree)
{
    form_center(tree, &dlg.g_x, &dlg.g_y, &dlg.g_w, &dlg.g_h);
    form_dial(FMD_START, 0, 0, 0, 0, dlg.g_x, dlg.g_y, dlg.g_w, dlg.g_h);
    objc_draw(tree, ROOT, MAX_DEPTH, dlg.g_x, dlg.g_y, dlg.g_w, dlg.g_h);
}

static void end_dialog(void)
{
    form_dial(FMD_FINISH, 0, 0, 0, 0, dlg.g_x, dlg.g_y, dlg.g_w, dlg.g_h);
}

void desk_busy(WORD on)
{
    wind_update(BEG_UPDATE);
    graf_mouse(on ? HOURGLASS : ARROW, 0);
    wind_update(END_UPDATE);
}

/* -- the desk ---------------------------------------------------------- */

/* The donor's snap_icon: grid position (gx, gy) as a pixel position,
 * the spare pixels shared out between the columns and the rows. */
static void snap_icon(WORD gx, WORD gy, WORD *px, WORD *py)
{
    WORD columns = G.g_desk.g_w / G.g_icw;
    WORD rows = G.g_desk.g_h / G.g_ich;
    WORD spare;
    /* Clamped into fresh locals, not back into the parameters: cc65816
     * 5.18 at -O2 inlines this into desk_icon and, with the parameters
     * reassigned, reads both of them back from a stack slot it never
     * wrote (tools/ccbug/bugs.c, B10).  The first icon came out right only
     * because that slot happened to hold zero. */
    WORD cx = gx > columns - 1 ? (WORD)(columns - 1) : gx;
    WORD cy = gy > rows - 1 ? (WORD)(rows - 1) : gy;

    spare = (WORD)(G.g_desk.g_w - columns * G.g_icw);
    *px = (WORD)(cx * G.g_icw + spare / columns);
    spare = (WORD)(G.g_desk.g_h - rows * G.g_ich);
    *py = (WORD)(cy * G.g_ich + spare / rows + G.g_desk.g_y);
}

/* An icon on the desk at grid (gx, gy). */
static WORD desk_icon(WORD gx, WORD gy, WORD which, const char *label, WORD letter)
{
    WORD x, y;

    snap_icon(gx, gy, &x, &y);
    return obj_icon(DROOT, x, y, which, label, letter);
}

/* The desk: a disk icon per drive in GEMDOS's map, floppies for the
 * first two, across the top from the left; the trash bottom-left, or
 * bottom-right when the disks reach that row (the donor's build_inf). */
static void desk_build(void)
{
    WORD xcnt, ycnt, drive, n, gx, gy;
    char label[LABEL_LEN];
    char *disk, *trash;
    UWORD map;

    G.g_wicon = (WORD)(MAX_ICONTEXT_WIDTH * G.g_wchar + 2 * G.a_iblist[0].ib_xtext);
    G.g_hicon = (WORD)(G.a_iblist[0].ib_hicon + G.g_hchar + 2);
    xcnt = G.g_desk.g_w / (G.g_wicon + MIN_WINT);
    G.g_icw = G.g_desk.g_w / xcnt;
    ycnt = G.g_desk.g_h / (G.g_hicon + MIN_HINT);
    G.g_ich = G.g_desk.g_h / ycnt;

    obj_wfree(DROOT, 0, 0, (WORD)(G.g_desk.g_x + G.g_desk.g_w),
              (WORD)(G.g_desk.g_y + G.g_desk.g_h));
    G.g_screen[DROOT].ob_spec = DESK_SPEC;

    rsrc_gaddr(R_STRING, STDISK, (void **)&disk);
    rsrc_gaddr(R_STRING, STTRASH, (void **)&trash);

    map = (UWORD)Dsetdrv(Dgetdrv());
    gx = gy = 0;
    for (drive = 0, n = 0; drive < MAX_DRIVES; drive++) {
        if (!(map & (1U << drive)))
            continue;
        gx = (WORD)(n % xcnt);
        gy = (WORD)(n / xcnt);
        {
            char *d = label;
            const char *s = disk;
            while (*s && d < label + LABEL_LEN - 3)
                *d++ = *s++;
            *d++ = ' ';
            *d++ = (char)('A' + drive);
            *d = 0;
        }
        desk_icon(gx, gy, drive > 1 ? IB_HARD : IB_FLOPPY, label, (WORD)('A' + drive));
        n++;
    }
    gx = 0;
    gy = (WORD)(ycnt - 1);
    if (n && (WORD)((n - 1) / xcnt) >= gy)
        gx = (WORD)(xcnt - 1);
    desk_icon(gx, gy, IB_TRASH, trash, 0);
}

/* The selected item under root, or 0. */
static WORD sel_item(WORD root)
{
    WORD i;

    for (i = G.g_screen[root].ob_head; i >= WOBS_START; i = G.g_screen[i].ob_next)
        if (G.g_screen[i].ob_state & SELECTED)
            return i;
    return 0;
}

/* -- the menu ---------------------------------------------------------- */

static WORD do_deskmenu(WORD item)
{
    OBJECT *tree = G.a_info;

    if (item == ABOUITEM) {
        start_dialog(tree);
        form_do(tree, 0);
        tree[DEOK].ob_state = NORMAL;
        end_dialog();
    }
    return FALSE;
}

static WORD do_filemenu(WORD item)
{
    WNODE *pw = win_ontop();
    WORD obj;

    switch (item) {
    case OPENITEM:                              /* the top window's selection,
                                                 * else the desk's */
        obj = pw ? sel_item(pw->w_root) : 0;
        if (obj)
            return do_open(pw->w_id, obj);
        if ((obj = sel_item(DROOT)) != 0)
            return do_open(DESKWH, obj);
        break;
    case SHOWITEM:                              /* what the selection is,
                                                 * and its name to change */
        if (pw)
            fun_info(pw);
        break;
    case NFOLITEM:                              /* a folder in the top
                                                 * window's directory */
        if (pw)
            fun_mkdir(pw);
        break;
    case DELTITEM:                              /* what is selected there */
        if (pw)
            fun_del(pw);
        break;
    case CLOSITEM:
        if (pw)
            win_close(pw, FALSE);
        break;
    case CLSWITEM:
        if (pw)
            win_close(pw, TRUE);
        break;
    case QUITITEM:
        shel_write(SHW_SHUTDOWN, 0, 0, "", "\0");
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

static WORD hndl_menu(WORD title, WORD item)
{
    WORD done = FALSE;

    switch (title) {
    case DESKMENU:
        done = do_deskmenu(item);
        break;
    case FILEMENU:
        done = do_filemenu(item);
        break;
    default:
        break;
    }
    menu_tnormal(G.a_menu, title, 1);
    return done;
}

/* -- events ------------------------------------------------------------ */

/* An item taken out of a window and let go somewhere.  The AES drags
 * the outline and answers where the button came up; where the POINTER
 * came up is what says which window and which icon, so the state is
 * asked for again afterwards -- the box is snapped to the grid the item
 * came from and is not where the hand is.
 *
 * Dropped in its own window on nothing, or on itself, it is the click
 * it started as.  Anywhere else the file layer takes over: another
 * window or a folder in one is a copy, a copy with SHIFT held is a
 * move, and the trash is a delete. */
static void hndl_drag(WNODE *pw, WORD obj, WORD wh)
{
    OBJECT *pob = &G.g_screen[obj];
    WORD x, y, mstate, kstate, dwh, dobj, bx, by;
    GRECT d;

    graf_mkstate(&x, &y, &mstate, &kstate);
    if (!(mstate & 1))                          /* already let go: a click */
        return;
    objc_offset(G.g_screen, obj, &bx, &by);
    wind_get(0, WF_WORKXYWH, &d.g_x, &d.g_y, &d.g_w, &d.g_h);
    graf_dragbox(pob->ob_width, pob->ob_height, bx, by,
                 d.g_x, d.g_y, d.g_w, d.g_h, &x, &y);
    graf_mkstate(&x, &y, &mstate, &kstate);

    dwh = wind_find(x, y);
    if (dwh == DESKWH) {
        dobj = objc_find(G.g_screen, DROOT, MAX_DEPTH, x, y);
        if (dobj < WOBS_START)
            return;                             /* the bare desk */
    } else {
        WNODE *pd = win_find(dwh);
        if (!pd)
            return;
        dobj = objc_find(G.g_screen, pd->w_root, MAX_DEPTH, x, y);
        if (dobj < WOBS_START)
            dobj = 0;                           /* the window itself */
        if (dwh == wh && (dobj == 0 || dobj == obj))
            return;                             /* where it already is */
    }
    fun_file2any(pw, dwh, dobj, kstate);
}

/* A press on the desk or in a window's work area (the control manager
 * keeps the gadgets and the windows under the top one): select the
 * item under it and no other there; two clicks open it, and opening
 * a program is what ends the desktop's loop.  One click on an item in
 * a window, with the button still down, is a drag. */
static WORD hndl_button(WORD clicks, WORD mx, WORD my)
{
    WORD wh, root, obj;
    WNODE *pw = 0;

    wh = wind_find(mx, my);
    if (wh == DESKWH) {
        root = DROOT;
    } else {
        pw = win_find(wh);
        if (!pw)
            return FALSE;
        root = pw->w_root;
    }
    obj = objc_find(G.g_screen, root, MAX_DEPTH, mx, my);
    if (obj < WOBS_START)
        obj = 0;
    act_select(wh, root, obj);
    if (obj && clicks == 2)
        return do_open(wh, obj);
    if (obj && pw)
        hndl_drag(pw, obj, wh);
    return FALSE;
}

static WORD hndl_msg(void)
{
    WORD *msg = G.g_rmsg;

    switch (msg[0]) {
    case MN_SELECTED:
        return hndl_menu(msg[3], msg[4]);
    case WM_REDRAW:
    case WM_TOPPED:
    case WM_CLOSED:
    case WM_FULLED:
    case WM_ARROWED:
    case WM_HSLID:
    case WM_VSLID:
    case WM_SIZED:
    case WM_MOVED:
    case WM_NEWTOP:
        hndl_wmsg(msg);
        return FALSE;
    default:
        return FALSE;
    }
}

/* The AES's version, from global[0] as the ST has it (0x0140 is 1.40),
 * written over the resource's "0.00". */
static void set_version(void)
{
    char *v = (char *)(uint16_t)G.a_info[DEVERSN].ob_spec;
    UWORD g = (UWORD)global[0];
    static const char hex[] = "0123456789ABCDEF";

    v[0] = hex[(g >> 8) & 0x0F];
    v[2] = hex[(g >> 4) & 0x0F];
    v[3] = hex[g & 0x0F];
}

int main(void)
{
    static const WORD not_yet[N_NOT_YET] = NOT_YET_ITEMS;
    WORD ev_which, mx, my, button, kstate, kret, bret;
    WORD i, done;

    appl_init();
    G.g_handle = graf_handle(&G.g_wchar, &G.g_hchar, &G.g_wbox, &G.g_hbox);
    wind_get(DESKWH, WF_WORKXYWH, &G.g_desk.g_x, &G.g_desk.g_y,
             &G.g_desk.g_w, &G.g_desk.g_h);
    desk_busy(TRUE);

    if (!rsrc_load("DESKTOP.RSC")) {
        desk_busy(FALSE);
        /* the one string that cannot come from the resource */
        form_alert(1, "[3][DESKTOP.RSC is not on|the boot disk.][ Quit ]");
        shel_write(SHW_SHUTDOWN, 0, 0, "", "\0");
        appl_exit();
        return 1;
    }
    rsrc_gaddr(R_TREE, ADMENU, (void **)&G.a_menu);
    rsrc_gaddr(R_TREE, ADDINFO, (void **)&G.a_info);
    rsrc_gaddr(R_TREE, ADMKDBOX, (void **)&G.a_mkdir);
    rsrc_gaddr(R_TREE, ADDELDIA, (void **)&G.a_delete);
    rsrc_gaddr(R_TREE, ADFINFO, (void **)&G.a_finfo);
    rsrc_gaddr(R_ICONBLK, 0, (void **)&G.a_iblist);
    set_version();
    for (i = 0; i < N_NOT_YET; i++)
        menu_ienable(G.a_menu, not_yet[i], 0);

    obj_init();
    desk_build();
    if (!win_start()) {
        desk_busy(FALSE);
        fun_alert(1, STNOMEM);
        rsrc_free();
        shel_write(SHW_SHUTDOWN, 0, 0, "", "\0");
        appl_exit();
        return 1;
    }
    app_start();
    wind_newdesk(G.g_screen, DROOT);
    wind_update(BEG_UPDATE);
    do_wredraw(DESKWH, &G.g_desk);
    cnx_get();
    menu_bar(G.a_menu, 1);
    wind_update(END_UPDATE);
    desk_busy(FALSE);

    done = FALSE;
    while (!done) {
        ev_which = evnt_multi(MU_BUTTON | MU_MESAG | MU_KEYBD, 0x02, 0x01, 0x01,
                              0, 0, G.g_rmsg, 0, 0,
                              &mx, &my, &button, &kstate, &kret, &bret);
        wind_update(BEG_UPDATE);
        if (ev_which & MU_BUTTON)
            if (hndl_button(bret, mx, my))
                done = TRUE;
        while ((ev_which & MU_MESAG) && !done) {
            if (hndl_msg())
                done = TRUE;
            ev_which = evnt_multi(MU_MESAG | MU_TIMER, 0x02, 0x01, 0x01,
                                  0, 0, G.g_rmsg, 0, 0,
                                  &mx, &my, &button, &kstate, &kret, &bret);
        }
        wind_update(END_UPDATE);
    }

    /* The windows stay open, as the donor leaves them: the shell's
     * reinitialisation takes the screen back, and their places are in
     * the shell buffer for the next desktop. */
    cnx_put();
    app_save();
    menu_bar(G.a_menu, 0);
    wind_newdesk(0, ROOT);
    rsrc_free();
    appl_exit();
    return 0;
}
