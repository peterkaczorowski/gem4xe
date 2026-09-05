/* deskobj.c -- the desktop's screen tree.
 *
 * The donor's deskobj.c: one OBJECT array holds the desk, the window
 * boxes and every item, and the items not in use hang on a free chain
 * through ob_next.  A window's items are freed by moving the whole
 * chain of them to the head of the free chain -- one relink, however
 * many there are.
 */
#include "desk.h"

static const OBJECT gl_sampob[2] = {
    { NIL, NIL, NIL, G_IBOX, NONE, NORMAL, 0L,        0, 0, 0, 0 },
    { NIL, NIL, NIL, G_BOX,  NONE, NORMAL, DESK_SPEC, 0, 0, 0, 0 },
};

static void r_set(OBJECT *obj, WORD x, WORD y, WORD w, WORD h)
{
    obj->ob_x = x;
    obj->ob_y = y;
    obj->ob_width = w;
    obj->ob_height = h;
}

/* The AES's objc_add: link obj as the last child of parent. */
static void obj_add(OBJECT *tree, WORD parent, WORD obj)
{
    OBJECT *pp = &tree[parent];
    WORD last = pp->ob_tail;

    tree[obj].ob_next = parent;
    if (last == NIL)
        pp->ob_head = obj;
    else
        tree[last].ob_next = obj;
    pp->ob_tail = obj;
}

/* Every non-item object with its links cut, the items chained free,
 * ROOT a G_IBOX over the whole screen, DROOT and the window boxes
 * zero-size children of it. */
void obj_init(void)
{
    WORD i;
    OBJECT *obj;

    for (i = 0, obj = G.g_screen; i < WOBS_START; i++, obj++)
        obj->ob_head = obj->ob_next = obj->ob_tail = NIL;
    for (; i < NUM_SOBS - 1; i++, obj++)
        obj->ob_next = (WORD)(i + 1);
    obj->ob_next = NIL;
    G.g_screenfree = WOBS_START;

    G.g_screen[ROOT] = gl_sampob[0];
    r_set(&G.g_screen[ROOT], 0, 0,
          (WORD)(G.g_desk.g_x + G.g_desk.g_w), (WORD)(G.g_desk.g_y + G.g_desk.g_h));
    for (i = 0, obj = &G.g_screen[DROOT]; i < NUM_WNODES + 1; i++, obj++) {
        *obj = gl_sampob[1];
        obj_add(G.g_screen, ROOT, (WORD)(DROOT + i));
    }
}

/* A window object: the first one with no size, from DROOT+1. */
WORD obj_walloc(WORD x, WORD y, WORD w, WORD h)
{
    WORD i;
    OBJECT *obj;

    for (i = DROOT + 1, obj = &G.g_screen[i]; i < WOBS_START; i++, obj++) {
        if (!(obj->ob_width && obj->ob_height)) {
            r_set(obj, x, y, w, h);
            return i;
        }
    }
    return 0;
}

/* Resize a window object and free its children; a zero size frees it. */
void obj_wfree(WORD obj, WORD x, WORD y, WORD w, WORD h)
{
    OBJECT *window = &G.g_screen[obj];
    OBJECT *item;
    WORD i, oldfree;

    r_set(window, x, y, w, h);
    if (window->ob_head >= WOBS_START) {
        oldfree = G.g_screenfree;
        G.g_screenfree = window->ob_head;
        for (i = window->ob_head; ; i = item->ob_next) {
            item = &G.g_screen[i];
            if (item->ob_next < WOBS_START) {  /* the last child: it links to the parent */
                item->ob_next = oldfree;
                break;
            }
        }
    }
    window->ob_head = window->ob_tail = NIL;
}

/* An item at x/y/w/h under wparent, off the free chain: its number, or
 * 0 when there is none left. */
WORD obj_ialloc(WORD wparent, WORD x, WORD y, WORD w, WORD h)
{
    WORD objnum = G.g_screenfree;
    OBJECT *obj;

    if (objnum < WOBS_START)
        return 0;
    obj = &G.g_screen[objnum];
    G.g_screenfree = obj->ob_next;
    obj->ob_next = obj->ob_head = obj->ob_tail = NIL;
    obj_add(G.g_screen, wparent, objnum);
    r_set(obj, x, y, w, h);
    return objnum;
}

/* The desk icon of a drive letter, or 0. */
WORD obj_get_obid(WORD drive)
{
    WORD objnum;

    for (objnum = G.g_screen[DROOT].ob_head; objnum >= WOBS_START;
         objnum = G.g_screen[objnum].ob_next) {
        if (G.g_screen[objnum].ob_type == G_ICON
         && (obj_info(objnum)->icon.ib_char & 0xFF) == drive)
            return objnum;
    }
    return 0;
}

SCREENINFO *obj_info(WORD obj)
{
    return &G.g_screeninfo[obj - WOBS_START];
}
