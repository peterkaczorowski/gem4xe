/* desktop.c -- DESKTOP.G4A, the GEM Desktop.
 *
 * A gem4xe application like any other (src/app/gem.h): the shell loop
 * in src/aes/shel.c loads it first, runs whatever it asks for with
 * shel_write, and loads it again when that returns, until it asks to
 * shut down.  The shape is the donor's deskmain.c: take the desk over
 * with a screen tree of drive icons and the trash, put the menu bar
 * up, and answer evnt_multi until Quit.
 *
 * Milestone 4 of docs/phase14.md is this much: the bar, the icons, the
 * About dialog, Quit, and an icon that selects when clicked.  The items
 * that open windows and act on files are in the menu, disabled, until
 * the milestones that bring them (NOT_YET_ITEMS, build/deskrsc.h).
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

static void desk_busy(WORD on)
{
    wind_update(BEG_UPDATE);
    graf_mouse(on ? HOURGLASS : ARROW, 0);
    wind_update(END_UPDATE);
}

/* -- the desk ---------------------------------------------------------- */

/* Draw the screen tree from obj, once per rectangle of the desktop's
 * list that meets *pc: the donor's do_wredraw for DESKWH. */
static void desk_redraw(WORD obj, const GRECT *pc)
{
    GRECT r;

    wind_get(DESKWH, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    while (r.g_w && r.g_h) {
        WORD x0 = r.g_x > pc->g_x ? r.g_x : pc->g_x;
        WORD y0 = r.g_y > pc->g_y ? r.g_y : pc->g_y;
        WORD x1 = r.g_x + r.g_w < pc->g_x + pc->g_w ? r.g_x + r.g_w : pc->g_x + pc->g_w;
        WORD y1 = r.g_y + r.g_h < pc->g_y + pc->g_h ? r.g_y + r.g_h : pc->g_y + pc->g_h;
        if (x0 < x1 && y0 < y1)
            objc_draw(G.g_screen, obj, MAX_DEPTH, x0, y0, (WORD)(x1 - x0), (WORD)(y1 - y0));
        wind_get(DESKWH, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
    }
}

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

/* An icon on the desk at grid (gx, gy): a copy of the resource's ICONBLK
 * with the label and the letter, the image centred in the cell -- the
 * donor's app_blddesk for one ANODE. */
static WORD desk_icon(WORD gx, WORD gy, WORD which, const char *label, WORD letter)
{
    WORD x, y, obid;
    OBJECT *pob;
    SCREENINFO *si;
    ICONBLK *pic;
    char *d;

    snap_icon(gx, gy, &x, &y);
    obid = obj_ialloc(DROOT, x, y, G.g_wicon, G.g_hicon);
    if (!obid)
        return 0;
    pob = &G.g_screen[obid];
    pob->ob_state = NORMAL;
    pob->ob_flags = NONE;
    pob->ob_type = G_ICON;
    si = obj_info(obid);
    pic = &si->icon;
    *pic = G.a_iblist[which];
    pob->ob_spec = (LONG)(uint16_t)pic;
    pic->ib_xicon = (WORD)((G.g_wicon - pic->ib_wicon) / 2);
    pic->ib_ytext = pic->ib_hicon;
    pic->ib_wtext = (WORD)(MAX_ICONTEXT_WIDTH * G.g_wchar);
    pic->ib_htext = (WORD)(G.g_hchar + 2);
    pic->ib_char = (WORD)((pic->ib_char & 0xFF00) | letter);
    d = si->label;
    while (*label && d < si->label + LABEL_LEN - 1)
        *d++ = *label++;
    *d = 0;
    pic->ib_ptext = (LONG)(uint16_t)si->label;
    return obid;
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

/* Select one icon and no other, or none (obj 0); redrawn in place. */
static void desk_select(WORD obj)
{
    WORD i;

    for (i = G.g_screen[DROOT].ob_head; i >= WOBS_START; i = G.g_screen[i].ob_next) {
        UWORD state = G.g_screen[i].ob_state;
        UWORD want = (i == obj) ? (state | SELECTED) : (state & ~SELECTED);
        if (want != state)
            objc_change(G.g_screen, i, 0, G.g_desk.g_x, G.g_desk.g_y,
                        G.g_desk.g_w, G.g_desk.g_h, (WORD)want, TRUE);
    }
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
    if (item == QUITITEM) {
        shel_write(SHW_SHUTDOWN, 0, 0, "", "\0");
        return TRUE;
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

static WORD hndl_button(WORD clicks, WORD mx, WORD my)
{
    WORD wh, obj;

    (void)clicks;
    wh = wind_find(mx, my);
    if (wh != DESKWH)
        return FALSE;
    obj = objc_find(G.g_screen, DROOT, MAX_DEPTH, mx, my);
    desk_select(obj >= WOBS_START ? obj : 0);
    return FALSE;
}

static WORD hndl_msg(void)
{
    WORD *msg = G.g_rmsg;

    switch (msg[0]) {
    case MN_SELECTED:
        return hndl_menu(msg[3], msg[4]);
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
        form_alert(1, "[3][DESKTOP.RSC is not on|the boot disk.][ Quit ]");
        shel_write(SHW_SHUTDOWN, 0, 0, "", "\0");
        appl_exit();
        return 1;
    }
    rsrc_gaddr(R_TREE, ADMENU, (void **)&G.a_menu);
    rsrc_gaddr(R_TREE, ADDINFO, (void **)&G.a_info);
    rsrc_gaddr(R_ICONBLK, 0, (void **)&G.a_iblist);
    set_version();
    for (i = 0; i < N_NOT_YET; i++)
        menu_ienable(G.a_menu, not_yet[i], 0);

    obj_init();
    desk_build();
    wind_newdesk(G.g_screen, DROOT);
    wind_update(BEG_UPDATE);
    desk_redraw(DROOT, &G.g_desk);
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

    menu_bar(G.a_menu, 0);
    wind_newdesk(0, ROOT);
    rsrc_free();
    appl_exit();
    return 0;
}
