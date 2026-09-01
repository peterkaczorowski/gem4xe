/* objc.c -- the AES object library: tree walking, drawing and hit testing.
 *
 * objc_draw is the centre of the AES.  Everything visible in GEM -- dialogs,
 * menus, the desktop, window contents -- is an object tree drawn by this, so
 * it is the piece worth getting exactly right before anything above it.
 *
 * All drawing goes through the VDI by filling the parameter block and calling
 * vdi(), exactly as an application would.  The AES has no private path to the
 * screen, which is what keeps the device seam meaningful.
 */
#include "aes.h"
#include "../vdi/vdi.h"

/* Crack the colour word: border 15-12, text 11-8, mode bit 7,
 * pattern 6-4, inside 3-0.  (EmuTOS aes/gemgraf.c gr_crack.) */
void gr_crack(UWORD color, WORD *pbc, WORD *ptc, WORD *pip, WORD *pic, WORD *pmd)
{
    *pbc = (WORD)((color >> 12) & 0x0F);
    *ptc = (WORD)((color >> 8) & 0x0F);
    *pmd = (WORD)((color & 0x80) ? MD_REPLACE : MD_TRANS);
    *pip = (WORD)((color >> 4) & 0x07);
    *pic = (WORD)(color & 0x0F);
}

/* ---- small VDI helpers ------------------------------------------------ */

static void v_call(WORD op, WORD npts, WORD nint)
{
    contrl[0] = op;
    contrl[1] = npts;
    contrl[3] = nint;
    contrl[6] = 1;
    vdi();
}

static void set1(WORD op, WORD v)
{
    intin[0] = v;
    v_call(op, 0, 1);
}

static void gr_rect(WORD x, WORD y, WORD w, WORD h, WORD color)
{
    if (w <= 0 || h <= 0)
        return;
    set1(VSF_COLOR, color);
    set1(VSF_INTERIOR, 1);
    ptsin[0] = x;  ptsin[1] = y;
    ptsin[2] = (WORD)(x + w - 1);
    ptsin[3] = (WORD)(y + h - 1);
    v_call(VR_RECFL, 2, 0);
}

/* A box border of `th` pixels.  GEM's sign convention: a POSITIVE thickness
 * grows inward from the rectangle, a NEGATIVE one grows outward.  Getting the
 * sign wrong makes every dialog frame land one pixel off, which is exactly the
 * kind of thing nobody notices until the whole screen looks subtly wrong. */
static void gr_border(WORD x, WORD y, WORD w, WORD h, WORD th, WORD color)
{
    WORD i, n = (WORD)(th < 0 ? -th : th);
    if (!n)
        return;
    for (i = 0; i < n; i++) {
        WORD ox = (WORD)(th > 0 ? i : -(i + 1));
        WORD bx = (WORD)(x + ox), by = (WORD)(y + ox);
        WORD bw = (WORD)(w - 2 * ox), bh = (WORD)(h - 2 * ox);
        if (bw <= 0 || bh <= 0)
            continue;
        gr_rect(bx, by, bw, 1, color);
        gr_rect(bx, (WORD)(by + bh - 1), bw, 1, color);
        gr_rect(bx, by, 1, bh, color);
        gr_rect((WORD)(bx + bw - 1), by, 1, bh, color);
    }
}

static WORD str_len(const char *s)
{
    WORD n = 0;
    while (s[n] && n < 128)
        n++;
    return n;
}

/* Text, positioned by justification within a width. */
static void gr_text(WORD x, WORD y, WORD w, const char *s, WORD color,
                    WORD just, WORD mode)
{
    WORD n = str_len(s), i, tx;
    if (!n)
        return;
    if (just == 2)                      /* centred */
        tx = (WORD)(x + (w - n * FONT_W) / 2);
    else if (just == 1)                 /* right */
        tx = (WORD)(x + w - n * FONT_W);
    else
        tx = x;
    set1(VSWR_MODE, mode);
    set1(VST_COLOR, color);
    for (i = 0; i < n; i++)
        intin[i] = (WORD)(uint8_t)s[i];
    ptsin[0] = tx;
    ptsin[1] = (WORD)(y + FONT_TOP);    /* y is the cell top; VDI wants baseline */
    v_call(V_GTEXT, 1, n);
    set1(VSWR_MODE, MD_TRANS);
}

/* ---- tree walking ----------------------------------------------------- */

/* Absolute position of an object: GEM stores ob_x/ob_y relative to the parent,
 * so this walks up.  There is no parent pointer -- the tree is right-threaded,
 * and the last child's ob_next points back AT the parent -- so finding a
 * parent means scanning.  That is what the AES does too. */
static WORD ob_parent(OBJECT *tree, WORD obj)
{
    WORD i;
    for (i = 0; i < 512; i++) {
        WORD c = tree[i].ob_head;
        while (c != NIL) {
            if (c == obj)
                return i;
            if (c == tree[c].ob_next && c == tree[i].ob_tail)
                break;
            if (c == tree[i].ob_tail)
                break;
            c = tree[c].ob_next;
        }
        if (tree[i].ob_flags & LASTOB)
            break;
    }
    return NIL;
}

void ob_offset(OBJECT *tree, WORD obj, WORD *px, WORD *py)
{
    WORD x = 0, y = 0, o = obj, guard = 0;
    while (o != NIL && guard++ < 64) {
        x = (WORD)(x + tree[o].ob_x);
        y = (WORD)(y + tree[o].ob_y);
        o = ob_parent(tree, o);
    }
    *px = x;
    *py = y;
}

/* ---- drawing ---------------------------------------------------------- */

static void ob_draw(OBJECT *tree, WORD obj, WORD x, WORD y)
{
    OBJECT *ob = &tree[obj];
    WORD w = ob->ob_width, h = ob->ob_height;
    WORD bc, tc, ip, ic, md, th;
    UWORD type = (UWORD)(ob->ob_type & 0xFF);
    UWORD color;
    const char *s;

    switch (type) {
    case G_BOX:
    case G_IBOX:
    case G_BOXCHAR:
        color = (UWORD)(ob->ob_spec & 0xFFFF);
        th = (WORD)(int8_t)((ob->ob_spec >> 16) & 0xFF);
        gr_crack(color, &bc, &tc, &ip, &ic, &md);
        if (type != G_IBOX)             /* G_IBOX is border only */
            gr_rect(x, y, w, h, ic);
        gr_border(x, y, w, h, th, bc);
        if (type == G_BOXCHAR) {
            char c[2];
            c[0] = (char)((ob->ob_spec >> 24) & 0xFF);
            c[1] = 0;
            if (c[0])
                gr_text(x, (WORD)(y + (h - FONT_H) / 2), w, c, tc, 2, MD_TRANS);
        }
        break;

    case G_BUTTON:
        /* A button's border thickness is COMPUTED, never stored: -1 normally,
         * one more for EXIT and one more again for DEFAULT.  Negative means
         * the border grows OUTWARD, which is why GEM's default button wears a
         * visibly thicker ring than its neighbours. */
        s = (const char *)(uint16_t)ob->ob_spec;
        th = -1;
        if (ob->ob_flags & EXIT)    th--;
        if (ob->ob_flags & DEFAULT) th--;
        gr_rect(x, y, w, h, 0);
        gr_border(x, y, w, h, th, 1);
        gr_text(x, (WORD)(y + (h - FONT_H) / 2), w, s, 1, 2, MD_TRANS);
        break;

    case G_TITLE:
        s = (const char *)(uint16_t)ob->ob_spec;
        gr_border(x, y, w, h, 1, 1);
        gr_text(x, y, w, s, 1, 0, MD_TRANS);
        break;

    case G_STRING:
        s = (const char *)(uint16_t)ob->ob_spec;
        gr_text(x, y, w, s, 1, 0, MD_TRANS);
        break;

    case G_TEXT:
    case G_BOXTEXT:
    case G_FTEXT:
    case G_FBOXTEXT: {
        TEDINFO *ted = (TEDINFO *)(uint16_t)ob->ob_spec;
        gr_crack((UWORD)ted->te_color, &bc, &tc, &ip, &ic, &md);
        if (type == G_BOXTEXT || type == G_FBOXTEXT) {
            gr_rect(x, y, w, h, ic);
            gr_border(x, y, w, h, ted->te_thickness, bc);
        }
        s = (const char *)(uint16_t)ted->te_ptext;
        gr_text(x, (WORD)(y + (h - FONT_H) / 2), w, s, tc, ted->te_just, md);
        break;
    }
    default:
        break;
    }

    /* States that apply on top of any type. */
    if (ob->ob_state & DISABLED) {
        /* GEM greys a disabled object with a 50% stipple.  With no pattern
         * fill in the VDI yet, a dotted line grid is the honest stand-in --
         * it reads as "unavailable" and does not pretend to be the real
         * hatch.  Replace when vsf_udpat lands. */
        WORD i;
        set1(VSL_COLOR, 0);
        set1(VSL_UDSTY, (WORD)0xAAAA);
        set1(VSL_TYPE, 7);
        for (i = 0; i < h; i += 2) {
            ptsin[0] = x;  ptsin[1] = (WORD)(y + i);
            ptsin[2] = (WORD)(x + w - 1);  ptsin[3] = (WORD)(y + i);
            v_call(V_PLINE, 2, 0);
        }
        set1(VSL_TYPE, 1);
    }
    if (ob->ob_state & OUTLINED)
        gr_border((WORD)(x - 3), (WORD)(y - 3), (WORD)(w + 6), (WORD)(h + 6), 1, 1);
    if (ob->ob_state & SHADOWED) {
        gr_rect((WORD)(x + w), (WORD)(y + 2), 2, h, 1);
        gr_rect((WORD)(x + 2), (WORD)(y + h), w, 2, 1);
    }
    if (ob->ob_state & CHECKED) {
        gr_text(x, y, FONT_W, "\010", 1, 0, MD_TRANS);   /* GEM's check glyph */
    }
    if (ob->ob_state & SELECTED) {
        /* Selection is an inversion of the whole object -- how every GEM menu
         * item and pressed button looks. */
        WORD i;
        set1(VSL_COLOR, 1);
        set1(VSWR_MODE, MD_XOR);
        for (i = 0; i < h; i++) {
            ptsin[0] = x;  ptsin[1] = (WORD)(y + i);
            ptsin[2] = (WORD)(x + w - 1);  ptsin[3] = (WORD)(y + i);
            v_call(V_PLINE, 2, 0);
        }
        set1(VSWR_MODE, MD_REPLACE);
    }
}

/* Walk the tree depth-first, drawing.  Children are drawn after their parent
 * and clipped to it, which is what makes a dialog's contents sit inside its
 * box without any per-object clipping bookkeeping. */
static void ob_walk(OBJECT *tree, WORD obj, WORD depth, WORD px, WORD py)
{
    WORD child;
    WORD x, y;
    if (obj == NIL || (tree[obj].ob_flags & HIDETREE))
        return;
    x = (WORD)(px + tree[obj].ob_x);
    y = (WORD)(py + tree[obj].ob_y);
    ob_draw(tree, obj, x, y);
    if (depth <= 0)
        return;
    for (child = tree[obj].ob_head; child != NIL; child = tree[child].ob_next) {
        ob_walk(tree, child, (WORD)(depth - 1), x, y);
        if (child == tree[obj].ob_tail)
            break;
    }
}

void objc_draw(OBJECT *tree, WORD start, WORD depth, GRECT *clip)
{
    WORD px = 0, py = 0;
    if (clip) {
        ptsin[0] = clip->g_x;
        ptsin[1] = clip->g_y;
        ptsin[2] = (WORD)(clip->g_x + clip->g_w - 1);
        ptsin[3] = (WORD)(clip->g_y + clip->g_h - 1);
        intin[0] = 1;
        v_call(VS_CLIP, 2, 1);
    }
    if (start != 0) {
        WORD parent = ob_parent(tree, start);
        if (parent != NIL)
            ob_offset(tree, parent, &px, &py);
    }
    ob_walk(tree, start, depth, px, py);
    intin[0] = 0;
    v_call(VS_CLIP, 2, 1);
}

/* Hit test: the DEEPEST object containing the point wins, which is why the
 * children are tested before the parent is accepted. */
WORD objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my)
{
    WORD x, y, child, found = NIL;
    if (start == NIL || (tree[start].ob_flags & HIDETREE))
        return NIL;
    ob_offset(tree, start, &x, &y);
    if (mx < x || my < y ||
        mx >= (WORD)(x + tree[start].ob_width) ||
        my >= (WORD)(y + tree[start].ob_height))
        return NIL;
    found = start;
    if (depth > 0) {
        for (child = tree[start].ob_head; child != NIL;
             child = tree[child].ob_next) {
            WORD hit = objc_find(tree, child, (WORD)(depth - 1), mx, my);
            if (hit != NIL)
                found = hit;
            if (child == tree[start].ob_tail)
                break;
        }
    }
    return found;
}
