/* menu.c -- the AES menu library (EmuTOS aes/gemmnlib.c, without the
 * submenu extension).
 *
 * A menu tree is the shape the RCS builds and the AES trusts: the root
 * has two children, the bar (THEBAR) and the box of drop-downs; the bar
 * holds THEACTIVE, whose children are the titles in order; the drop-down
 * box's children are the drop-downs in the same order, the first the
 * Desk menu (THEDESK is its title), whose children the AES rebuilds to
 * fit the accessories -- none here, so it keeps the one "About" item
 * and its separator goes.  A title's drop-down is found by walking as
 * many siblings as the title is titles in (menu_sub): the titles must
 * be contiguous in the array, the items may be anywhere.
 *
 * mn_do is the state machine that runs a drop-down: it is entered by
 * the control manager when the pointer comes into the active bar with
 * the buttons up (event.c's ct_poll), takes the mouse (ct_mouse), and
 * pulls each menu down as the pointer crosses its title, saving what is
 * under it with bb_save and putting it back with bb_restore.  It leaves
 * on a button transition off the title, reporting the item if one is
 * under the pointer and enabled; the control manager sends MN_SELECTED.
 * The bar and the drop-downs are drawn with the clip off (gl_rzero) as
 * the donor draws them: they are outside every window.
 *
 * WHAT IS NOT HERE: accessories (menu_register returns -1), the Atari
 * corpus's later hierarchical menus, and the ROM's 25-column save
 * buffer with its overflow on a wide drop-down -- the VDI's save buffer
 * is a whole screen (vdi_save_form).  The other corpus landmine stands:
 * the save rectangle is the drop-down grown by MENU_THICKNESS and NOT
 * clipped by the donor, so a drop-down off an edge of the screen is the
 * RCS's fix_menu_bar's job to prevent; here the VDI clips the copy to
 * the screen (vro_cpyfm, source-clipped), so the cost is a stripe left
 * unrestored, not a crash.
 */
#include "aes.h"

/* the objects every menu tree has in these positions */
#define THESCREEN   0
#define THEBAR      1
#define THEACTIVE   2
#define THEDESK     3

#define MENU_THICKNESS  1       /* the frame bb_save keeps around a drop-down */

/* mn_do's states: where the pointer is */
#define START_STATE     1       /* in the bar, off the titles */
#define INTITLE_STATE   2       /* on a title */
#define INITEM_STATE    3       /* on an item */
#define OUTSIDE_STATE   4       /* off the bar and the items, menu down */

OBJECT *gl_mntree;              /* the menu bar showing, or 0 */
MOBLK   gl_ctwait;              /* the rectangle that wakes the menu: the
                                 * active bar while there is one, else
                                 * gl_rmenu (which nothing enters, as a
                                 * press there goes to the control
                                 * manager before the menu could) */

/* The drop-down for a title: the titles and the drop-downs are children
 * of THEACTIVE and of the box after the bar, in the same order. */
static WORD menu_sub(OBJECT *tree, WORD ititle)
{
    WORD themenus, imenu, i;

    themenus = tree[THESCREEN].ob_tail;
    imenu = tree[themenus].ob_head;
    for (i = ititle - THEACTIVE; i > 1; i--)
        imenu = tree[imenu].ob_next;
    return imenu;
}

/* Rebuild the Desk drop-down's chain for the accessories: none, so the
 * one child is the application's "About" item, and the box is one line
 * high.  The separator and the six accessory slots are unlinked, not
 * moved: they stay where the RCS put them, at dabox+2..dabox+8. */
static void menu_fixup(void)
{
    OBJECT *tree = gl_mntree;
    WORD themenus, dabox;

    if (tree == 0)
        return;
    themenus = tree[THESCREEN].ob_tail;
    dabox = tree[themenus].ob_head;
    tree[dabox].ob_head = tree[dabox].ob_tail = NIL;
    ob_add(tree, dabox, dabox + 1);
    tree[dabox].ob_height = gl_hchar;
}

/* A mouse rectangle wait on an object: leave it if x, else enter it. */
static void rect_change(OBJECT *tree, MOBLK *prmob, WORD iob, WORD x)
{
    ob_actxywh(tree, iob, &prmob->m_gr);
    prmob->m_out = x;
}

/* Set or clear a state bit on an object, redrawing it with the clip off
 * if dodraw; FALSE, and nothing done, when chkdisabled and the object is
 * disabled.  The menu_icheck/ienable/tnormal calls come here directly. */
WORD do_chg(OBJECT *tree, WORD iitem, UWORD chgvalue, WORD dochg,
            WORD dodraw, WORD chkdisabled)
{
    UWORD curr_state;

    curr_state = tree[iitem].ob_state;
    if (chkdisabled && (curr_state & DISABLED))
        return FALSE;
    if (dochg)
        curr_state |= chgvalue;
    else
        curr_state &= ~chgvalue;
    if (dodraw)
        gsx_sclip(&gl_rzero);
    ob_change(tree, iitem, curr_state, dodraw);
    return TRUE;
}

static WORD item_changed(WORD last_item, WORD cur_item)
{
    if (last_item == NIL)
        return FALSE;
    if (last_item == cur_item)
        return FALSE;
    return TRUE;
}

/* Select or deselect last_item, if it is something and not cur_item.
 * Called with the two swapped to select the new one: then it is "the new
 * one, if it is something and not the old one". */
static WORD menu_select(OBJECT *tree, WORD last_item, WORD cur_item,
                        WORD setit)
{
    if (item_changed(last_item, cur_item))
        return do_chg(tree, last_item, SELECTED, setit, TRUE, TRUE);
    return FALSE;
}

/* Save or restore what is under a drop-down, one pixel of frame
 * included on the left, right and bottom. */
static void menu_sr(WORD saveit, OBJECT *tree, WORD imenu)
{
    GRECT t;

    gsx_sclip(&gl_rzero);
    ob_actxywh(tree, imenu, &t);
    t.g_x -= MENU_THICKNESS;
    t.g_w += 2 * MENU_THICKNESS;
    t.g_h += 2 * MENU_THICKNESS;
    if (saveit)
        bb_save(&t);
    else
        bb_restore(&t);
}

/* Pull a title's menu down: the title selected, the screen under the
 * drop-down saved, the drop-down drawn.  A disabled title gets none of
 * it.  Returns the drop-down's object. */
static WORD menu_down(OBJECT *tree, WORD ititle)
{
    WORD imenu;

    imenu = menu_sub(tree, ititle);
    if (do_chg(tree, ititle, SELECTED, TRUE, TRUE, TRUE)) {
        menu_sr(TRUE, tree, imenu);
        ob_draw(tree, imenu, MAX_DEPTH);
    }
    return imenu;
}

/* Run the menu bar from the pointer's arrival in it until a button
 * transition ends it or the pointer leaves with nothing down.  TRUE with
 * the title and item when an enabled item was chosen.  The button's
 * state is left as it is: the control manager holds the mouse until it
 * is up (event.c). */
WORD mn_do(WORD *ptitle, WORD *pitem)
{
    OBJECT  *tree;
    uint32_t buparm;
    WORD    mnu_flags, done, main_rect;
    WORD    cur_menu, cur_item, last_item;
    WORD    cur_title, last_title;
    UWORD   ev_which;
    MOBLK   p1mor, p2mor;
    WORD    menu_state, leave_flag;
    WORD    rets[6];

    menu_state = START_STATE;
    done = FALSE;
    buparm = 0x00010101UL;              /* a press */
    cur_title = cur_menu = cur_item = NIL;
    tree = gl_mntree;

    ct_mouse(TRUE);

    while (!done) {
        mnu_flags = MU_BUTTON | MU_M1;

        switch (menu_state) {
        case START_STATE:
            /* the pointer into the titles, or out of the bar */
            mnu_flags |= MU_M2;
            rect_change(tree, &p2mor, THEBAR, TRUE);
            main_rect = THEACTIVE;
            leave_flag = FALSE;
            break;
        case OUTSIDE_STATE:
            /* the pointer into the titles, or into the drop-down */
            mnu_flags |= MU_M2;
            rect_change(tree, &p2mor, cur_menu, FALSE);
            main_rect = THEACTIVE;
            leave_flag = FALSE;
            break;
        case INITEM_STATE:
            /* the pointer off the item; the button the other way */
            main_rect = cur_item;
            buparm = (button & 0x0001) ? 0x00010100UL : 0x00010101UL;
            leave_flag = TRUE;
            break;
        default:                        /* INTITLE_STATE */
            main_rect = cur_title;
            leave_flag = TRUE;
            break;
        }
        rect_change(tree, &p1mor, main_rect, leave_flag);

        ev_which = ev_multi(mnu_flags, &p1mor, &p2mor, 0UL, buparm, 0, rets);

        /* A button: in the bar off the titles it is nothing.  On a title
         * it flips the state waited for and the menu goes on.  Anywhere
         * else it ends the menu. */
        if (ev_which & MU_BUTTON) {
            if (menu_state == START_STATE)
                continue;
            if (menu_state != INTITLE_STATE)
                break;
            buparm ^= 0x00000001UL;
        }

        last_title = cur_title;
        last_item = cur_item;

        /* where the pointer is now */
        cur_title = ob_find(tree, THEACTIVE, 1, rets[0], rets[1]);
        if (cur_title != NIL && cur_title != THEACTIVE) {
            menu_state = INTITLE_STATE;
            cur_item = NIL;
        } else {
            cur_title = last_title;
            if (cur_menu == NIL)        /* no menu ever shown: nothing */
                cur_title = NIL;
            if (cur_title == NIL) {
                done = TRUE;
            } else {
                cur_item = ob_find(tree, cur_menu, 1, rets[0], rets[1]);
                if (cur_item != NIL) {
                    menu_state = INITEM_STATE;
                } else if (tree[cur_title].ob_state & DISABLED) {
                    cur_title = NIL;
                    done = TRUE;
                } else {
                    menu_state = OUTSIDE_STATE;
                }
            }
        }

        /* the old item off; the old title off and its menu up; the new
         * title on and its menu down; the new item on */
        menu_select(tree, last_item, cur_item, FALSE);
        if (menu_select(tree, last_title, cur_title, FALSE))
            menu_sr(FALSE, tree, cur_menu);
        if (menu_select(tree, cur_title, last_title, TRUE))
            cur_menu = menu_down(tree, cur_title);
        menu_select(tree, cur_item, last_item, TRUE);
    }

    /* Clean up: the menu up, and the item reported only if it is one
     * and enabled -- then the title stays selected for the application
     * to menu_tnormal, else it is deselected here. */
    done = FALSE;
    if (cur_title != NIL) {
        menu_sr(FALSE, tree, cur_menu);
        if (cur_item != NIL
            && do_chg(tree, cur_item, SELECTED, FALSE, FALSE, TRUE)) {
            *ptitle = cur_title;
            *pitem = cur_item;
            done = TRUE;
        } else {
            do_chg(tree, cur_title, SELECTED, FALSE, TRUE, TRUE);
        }
    }

    ct_mouse(FALSE);
    return done;
}

/* menu_bar: show the bar -- the tree fixed up for the accessories, the
 * bar object stretched to the right edge, drawn with the clip off, and
 * the line under it drawn black in replace mode whatever the tree says
 * -- or hide it, which is only to forget it: the application redraws. */
void mn_bar(OBJECT *tree, WORD showit)
{
    if (showit) {
        gl_mntree = tree;
        menu_fixup();
        tree[THEBAR].ob_width = gl_width - tree[THEBAR].ob_x;
        ob_actxywh(tree, THEACTIVE, &gl_ctwait.m_gr);
        gsx_sclip(&gl_rzero);
        ob_draw(tree, THEBAR, MAX_DEPTH);
        gsx_attr(FALSE, MD_REPLACE, BLACK);
        gsx_cline(0, gl_hbox - 1, gl_width - 1, gl_hbox - 1);
    } else {
        gl_mntree = 0;
        gl_ctwait.m_gr = gl_rmenu;
    }
}

/* menu_text: a new string for an item, copied over the old one, which
 * the caller made long enough. */
void mn_text(OBJECT *tree, WORD item, const char *text)
{
    char *d = (char *)(uint16_t)tree[item].ob_spec;

    while ((*d++ = *text++) != 0)
        ;
}

/* menu_register: no accessories, so no menu id -- -1, as the AES answers
 * when the Desk menu is full. */
WORD mn_register(WORD pid, const char *pstr)
{
    (void)pid;
    (void)pstr;
    return -1;
}

/* No bar; the wake rectangle is the menu bar's row, after gsx_start has
 * measured it. */
void mn_init(void)
{
    gl_mntree = 0;
    gl_ctwait.m_out = FALSE;
    gl_ctwait.m_gr = gl_rmenu;
}
