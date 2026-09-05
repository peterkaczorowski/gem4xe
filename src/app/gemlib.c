/* gemlib.c -- an application's GEM bindings, over the three COP entries.
 *
 * The shape is the classic GEM library's: one set of arrays, a parameter
 * block pointing at them, a binding per call that fills the arrays, makes
 * the call and unpacks the results.  The control words an AES binding
 * fills -- opcode and the four counts -- are the ST's: gem4xe reads the
 * counts to know how much to copy in and out, exactly as the AES does.
 *
 * The arrays are sized for what THIS library's bindings need, not for the
 * VDI's maxima: v_opnvwk's 45 intout and 12 ptsout words set the two
 * output sizes, nothing here passes more than 16 points, and intin is
 * the VDI's own 128 words (src/vdi/vdi.h) because v_gtext takes a line
 * of text that long -- the desktop's window titles and info lines are
 * up to 80 characters.  An application that adds a binding with larger
 * needs grows them.
 */
#include "gem.h"

WORD contrl[12], intin[128], ptsin[16], intout[45], ptsout[12];
WORD control[5], global[15], int_in[16], int_out[7];
LONG addr_in[3], addr_out[1];

static VDIPB vpb = { contrl, intin, ptsin, intout, ptsout };
static AESPB apb = { control, global, int_in, int_out, addr_in, addr_out };

static void vdi(WORD op, WORD npts, WORD nint, WORD handle)
{
    contrl[0] = op;
    contrl[1] = npts;
    contrl[3] = nint;
    contrl[5] = 0;
    contrl[6] = handle;
    vdi_call(&vpb);
}

static WORD aes(WORD op, WORD nin, WORD nout, WORD nain, WORD naout)
{
    control[0] = op;
    control[1] = nin;
    control[2] = nout;
    control[3] = nain;
    control[4] = naout;
    aes_call(&apb);
    return int_out[0];
}

/* -- VDI ------------------------------------------------------------- */

void v_opnvwk(WORD *work_in, WORD *handle, WORD *work_out)
{
    WORD i;
    for (i = 0; i < 11; i++)
        intin[i] = work_in[i];
    vdi(100, 0, 11, *handle);
    *handle = contrl[6];
    for (i = 0; i < 45; i++)
        work_out[i] = intout[i];
    for (i = 0; i < 12; i++)
        work_out[45 + i] = ptsout[i];
}

void v_clsvwk(WORD handle)
{
    vdi(101, 0, 0, handle);
}

void vr_recfl(WORD handle, WORD *pxy)
{
    WORD i;
    for (i = 0; i < 4; i++)
        ptsin[i] = pxy[i];
    vdi(114, 2, 0, handle);
}

void v_pline(WORD handle, WORD count, WORD *pxy)
{
    WORD i;
    for (i = 0; i < count * 2; i++)
        ptsin[i] = pxy[i];
    vdi(6, count, 0, handle);
}

void v_gtext(WORD handle, WORD x, WORD y, const char *s)
{
    WORD n = 0;
    ptsin[0] = x;
    ptsin[1] = y;
    while (*s && n < 128)
        intin[n++] = (WORD)(unsigned char)*s++;
    vdi(8, 1, n, handle);
}

static WORD attr1(WORD op, WORD handle, WORD v)
{
    intin[0] = v;
    vdi(op, 0, 1, handle);
    return intout[0];
}

WORD vsf_color(WORD handle, WORD color)    { return attr1(25, handle, color); }
WORD vsf_interior(WORD handle, WORD style) { return attr1(23, handle, style); }
WORD vsl_color(WORD handle, WORD color)    { return attr1(17, handle, color); }
WORD vst_color(WORD handle, WORD color)    { return attr1(22, handle, color); }

/* -- AES ------------------------------------------------------------- */

WORD appl_init(void)
{
    return aes(10, 0, 1, 0, 0);
}

WORD appl_exit(void)
{
    return aes(19, 0, 1, 0, 0);
}

WORD graf_handle(WORD *wchar, WORD *hchar, WORD *wbox, WORD *hbox)
{
    WORD h = aes(77, 0, 5, 0, 0);
    *wchar = int_out[1];
    *hchar = int_out[2];
    *wbox  = int_out[3];
    *hbox  = int_out[4];
    return h;
}

WORD objc_draw(OBJECT *tree, WORD start, WORD depth, WORD x, WORD y, WORD w, WORD h)
{
    int_in[0] = start;
    int_in[1] = depth;
    int_in[2] = x;
    int_in[3] = y;
    int_in[4] = w;
    int_in[5] = h;
    addr_in[0] = (LONG)(uint32_t)(OBJECT __far *)tree;
    return aes(42, 6, 1, 1, 0);
}

WORD wind_create(WORD kind, WORD x, WORD y, WORD w, WORD h)
{
    int_in[0] = kind;
    int_in[1] = x;
    int_in[2] = y;
    int_in[3] = w;
    int_in[4] = h;
    return aes(100, 5, 1, 0, 0);
}

WORD wind_open(WORD handle, WORD x, WORD y, WORD w, WORD h)
{
    int_in[0] = handle;
    int_in[1] = x;
    int_in[2] = y;
    int_in[3] = w;
    int_in[4] = h;
    return aes(101, 5, 1, 0, 0);
}

WORD wind_get(WORD handle, WORD field, WORD *o1, WORD *o2, WORD *o3, WORD *o4)
{
    WORD r;
    int_in[0] = handle;
    int_in[1] = field;
    r = aes(104, 2, 5, 0, 0);
    *o1 = int_out[1];
    *o2 = int_out[2];
    *o3 = int_out[3];
    *o4 = int_out[4];
    return r;
}

WORD wind_close(WORD handle)
{
    int_in[0] = handle;
    return aes(102, 1, 1, 0, 0);
}

WORD wind_delete(WORD handle)
{
    int_in[0] = handle;
    return aes(103, 1, 1, 0, 0);
}

WORD wind_set(WORD handle, WORD field, WORD w1, WORD w2, WORD w3, WORD w4)
{
    int_in[0] = handle;
    int_in[1] = field;
    int_in[2] = w1;
    int_in[3] = w2;
    int_in[4] = w3;
    int_in[5] = w4;
    return aes(105, 6, 1, 0, 0);
}

WORD wind_find(WORD x, WORD y)
{
    int_in[0] = x;
    int_in[1] = y;
    return aes(106, 2, 1, 0, 0);
}

WORD wind_update(WORD code)
{
    int_in[0] = code;
    return aes(107, 1, 1, 0, 0);
}

WORD wind_calc(WORD type, WORD kind, WORD x, WORD y, WORD w, WORD h,
               WORD *ox, WORD *oy, WORD *ow, WORD *oh)
{
    WORD r;
    int_in[0] = type;
    int_in[1] = kind;
    int_in[2] = x;
    int_in[3] = y;
    int_in[4] = w;
    int_in[5] = h;
    r = aes(108, 6, 5, 0, 0);
    *ox = int_out[1];
    *oy = int_out[2];
    *ow = int_out[3];
    *oh = int_out[4];
    return r;
}

WORD evnt_timer(UWORD lo, UWORD hi)
{
    int_in[0] = (WORD)lo;
    int_in[1] = (WORD)hi;
    return aes(24, 2, 1, 0, 0);
}

WORD evnt_keybd(void)
{
    return aes(20, 0, 1, 0, 0);
}

WORD evnt_button(WORD clicks, UWORD mask, UWORD state,
                 WORD *mx, WORD *my, WORD *mb, WORD *ks)
{
    WORD r;
    int_in[0] = clicks;
    int_in[1] = (WORD)mask;
    int_in[2] = (WORD)state;
    r = aes(21, 3, 5, 0, 0);
    *mx = int_out[1];
    *my = int_out[2];
    *mb = int_out[3];
    *ks = int_out[4];
    return r;
}

WORD evnt_mesag(WORD *msg)
{
    addr_in[0] = (LONG)(uint32_t)(WORD __far *)msg;
    return aes(23, 0, 1, 1, 0);
}

/* The ST's int_in: flags, the button's clicks/mask/state, the two mouse
 * rectangles as five words each, the timer's two words -- 16 words. */
WORD evnt_multi(UWORD flags, WORD bclk, UWORD bmsk, UWORD bst,
                const MOBLK *m1, const MOBLK *m2, WORD *msg,
                UWORD tlo, UWORD thi,
                WORD *mx, WORD *my, WORD *mb, WORD *ks, WORD *kr, WORD *br)
{
    static const MOBLK none = { 0, 0, 0, 0, 0 };
    const WORD *p;
    WORD r, i;

    int_in[0] = (WORD)flags;
    int_in[1] = bclk;
    int_in[2] = (WORD)bmsk;
    int_in[3] = (WORD)bst;
    p = (const WORD *)((flags & MU_M1) && m1 ? m1 : &none);
    for (i = 0; i < 5; i++)
        int_in[4 + i] = p[i];
    p = (const WORD *)((flags & MU_M2) && m2 ? m2 : &none);
    for (i = 0; i < 5; i++)
        int_in[9 + i] = p[i];
    int_in[14] = (WORD)tlo;
    int_in[15] = (WORD)thi;
    addr_in[0] = (LONG)(uint32_t)(WORD __far *)msg;
    r = aes(25, 16, 7, 1, 0);
    *mx = int_out[1];
    *my = int_out[2];
    *mb = int_out[3];
    *ks = int_out[4];
    *kr = int_out[5];
    *br = int_out[6];
    return r;
}

/* -- the menu, object, form, graphics and resource libraries: a tree in
 * addr_in[0] and words after it. */

static LONG tree_addr(OBJECT *tree)
{
    return (LONG)(uint32_t)(OBJECT __far *)tree;
}

WORD menu_bar(OBJECT *tree, WORD showit)
{
    int_in[0] = showit;
    addr_in[0] = tree_addr(tree);
    return aes(30, 1, 1, 1, 0);
}

WORD menu_icheck(OBJECT *tree, WORD item, WORD check)
{
    int_in[0] = item;
    int_in[1] = check;
    addr_in[0] = tree_addr(tree);
    return aes(31, 2, 1, 1, 0);
}

WORD menu_ienable(OBJECT *tree, WORD item, WORD enable)
{
    int_in[0] = item;
    int_in[1] = enable;
    addr_in[0] = tree_addr(tree);
    return aes(32, 2, 1, 1, 0);
}

WORD menu_tnormal(OBJECT *tree, WORD title, WORD normal)
{
    int_in[0] = title;
    int_in[1] = normal;
    addr_in[0] = tree_addr(tree);
    return aes(33, 2, 1, 1, 0);
}

WORD objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my)
{
    int_in[0] = start;
    int_in[1] = depth;
    int_in[2] = mx;
    int_in[3] = my;
    addr_in[0] = tree_addr(tree);
    return aes(43, 4, 1, 1, 0);
}

WORD objc_offset(OBJECT *tree, WORD obj, WORD *x, WORD *y)
{
    WORD r;
    int_in[0] = obj;
    addr_in[0] = tree_addr(tree);
    r = aes(44, 1, 3, 1, 0);
    *x = int_out[1];
    *y = int_out[2];
    return r;
}

WORD objc_change(OBJECT *tree, WORD obj, WORD resvd, WORD x, WORD y, WORD w, WORD h,
                 WORD state, WORD redraw)
{
    int_in[0] = obj;
    int_in[1] = resvd;
    int_in[2] = x;
    int_in[3] = y;
    int_in[4] = w;
    int_in[5] = h;
    int_in[6] = state;
    int_in[7] = redraw;
    addr_in[0] = tree_addr(tree);
    return aes(47, 8, 1, 1, 0);
}

WORD objc_order(OBJECT *tree, WORD obj, WORD newpos)
{
    int_in[0] = obj;
    int_in[1] = newpos;
    addr_in[0] = tree_addr(tree);
    return aes(45, 2, 1, 1, 0);
}

WORD form_do(OBJECT *tree, WORD start)
{
    int_in[0] = start;
    addr_in[0] = tree_addr(tree);
    return aes(50, 1, 1, 1, 0);
}

WORD form_dial(WORD type, WORD x1, WORD y1, WORD w1, WORD h1,
               WORD x2, WORD y2, WORD w2, WORD h2)
{
    int_in[0] = type;
    int_in[1] = x1;
    int_in[2] = y1;
    int_in[3] = w1;
    int_in[4] = h1;
    int_in[5] = x2;
    int_in[6] = y2;
    int_in[7] = w2;
    int_in[8] = h2;
    return aes(51, 9, 1, 0, 0);
}

WORD form_alert(WORD defbut, const char *s)
{
    int_in[0] = defbut;
    addr_in[0] = (LONG)(uint32_t)(const char __far *)s;
    return aes(52, 1, 1, 1, 0);
}

WORD form_error(WORD n)
{
    int_in[0] = n;
    return aes(53, 1, 1, 0, 0);
}

WORD form_center(OBJECT *tree, WORD *x, WORD *y, WORD *w, WORD *h)
{
    WORD r;
    addr_in[0] = tree_addr(tree);
    r = aes(54, 0, 5, 1, 0);
    *x = int_out[1];
    *y = int_out[2];
    *w = int_out[3];
    *h = int_out[4];
    return r;
}

static WORD graf_box(WORD op, WORD x1, WORD y1, WORD w1, WORD h1,
                     WORD x2, WORD y2, WORD w2, WORD h2)
{
    int_in[0] = x1;
    int_in[1] = y1;
    int_in[2] = w1;
    int_in[3] = h1;
    int_in[4] = x2;
    int_in[5] = y2;
    int_in[6] = w2;
    int_in[7] = h2;
    return aes(op, 8, 1, 0, 0);
}

WORD graf_growbox(WORD x1, WORD y1, WORD w1, WORD h1, WORD x2, WORD y2, WORD w2, WORD h2)
{
    return graf_box(73, x1, y1, w1, h1, x2, y2, w2, h2);
}

WORD graf_shrinkbox(WORD x1, WORD y1, WORD w1, WORD h1, WORD x2, WORD y2, WORD w2, WORD h2)
{
    return graf_box(74, x1, y1, w1, h1, x2, y2, w2, h2);
}

WORD graf_mouse(WORD mode, const WORD *form)
{
    int_in[0] = mode;
    addr_in[0] = (LONG)(uint32_t)(const WORD __far *)form;
    return aes(78, 1, 1, 1, 0);
}

WORD graf_mkstate(WORD *mx, WORD *my, WORD *mb, WORD *ks)
{
    WORD r = aes(79, 0, 5, 0, 0);
    *mx = int_out[1];
    *my = int_out[2];
    *mb = int_out[3];
    *ks = int_out[4];
    return r;
}

WORD rsrc_load(const char *name)
{
    addr_in[0] = (LONG)(uint32_t)(const char __far *)name;
    return aes(110, 0, 1, 1, 0);
}

WORD rsrc_free(void)
{
    return aes(111, 0, 1, 0, 0);
}

WORD rsrc_gaddr(WORD type, WORD index, void **addr)
{
    WORD r;
    int_in[0] = type;
    int_in[1] = index;
    r = aes(112, 2, 1, 0, 1);
    *addr = (void *)(uint16_t)addr_out[0];
    return r;
}

WORD shel_write(WORD doex, WORD isgr, WORD iscr, const char *cmd, const char *tail)
{
    int_in[0] = doex;
    int_in[1] = isgr;
    int_in[2] = iscr;
    addr_in[0] = (LONG)(uint32_t)(const char __far *)cmd;
    addr_in[1] = (LONG)(uint32_t)(const char __far *)tail;
    return aes(121, 3, 1, 2, 0);
}

/* -- GEMDOS ---------------------------------------------------------- */

static GDPB dpb;

/* The arguments go into the block as the ST's trap #1 pushes them: at
 * byte offsets from 6, WORDs two wide and LONGs four, low byte first. */
static void dw(WORD off, WORD v)
{
    dpb.arg[(off - 6) >> 1] = v;
}

static void dl(WORD off, LONG v)
{
    dpb.arg[(off - 6) >> 1] = (WORD)v;
    dpb.arg[(off - 4) >> 1] = (WORD)(v >> 16);
}

static LONG dos(WORD fn)
{
    dpb.fn = fn;
    dos_call(&dpb);
    return dpb.ret;
}

WORD Sversion(void)                       { return (WORD)dos(0x30); }
WORD Dgetdrv(void)                        { return (WORD)dos(0x19); }
WORD Dsetdrv(WORD drive)                  { dw(6, drive); return (WORD)dos(0x0E); }
LONG Dsetpath(const char __far *path)     { dl(6, (LONG)path); return dos(0x3B); }
LONG Dcreate(const char __far *path)      { dl(6, (LONG)path); return dos(0x39); }
LONG Ddelete(const char __far *path)      { dl(6, (LONG)path); return dos(0x3A); }
LONG Fdelete(const char __far *name)      { dl(6, (LONG)name); return dos(0x41); }
LONG Fsnext(void)                         { return dos(0x4F); }
LONG Fclose(WORD handle)                  { dw(6, handle); return dos(0x3E); }
LONG Malloc(LONG size)                    { dl(6, size); return dos(0x48); }
LONG Mfree(void __far *block)             { dl(6, (LONG)block); return dos(0x49); }
void Fsetdta(DTA __far *dta)              { dl(6, (LONG)dta); dos(0x1A); }
DTA __far *Fgetdta(void)                  { return (DTA __far *)dos(0x2F); }

LONG Dgetpath(char __far *buf, WORD drive)
{
    dl(6, (LONG)buf);
    dw(10, drive);
    return dos(0x47);
}

LONG Dfree(DISKINFO __far *info, WORD drive)
{
    dl(6, (LONG)info);
    dw(10, drive);
    return dos(0x36);
}

LONG Fsfirst(const char __far *spec, WORD attr)
{
    dl(6, (LONG)spec);
    dw(10, attr);
    return dos(0x4E);
}

LONG Fopen(const char __far *name, WORD mode)
{
    dl(6, (LONG)name);
    dw(10, mode);
    return dos(0x3D);
}

LONG Fcreate(const char __far *name, WORD attr)
{
    dl(6, (LONG)name);
    dw(10, attr);
    return dos(0x3C);
}

LONG Fread(WORD handle, LONG count, void __far *buf)
{
    dw(6, handle);
    dl(8, count);
    dl(12, (LONG)buf);
    return dos(0x3F);
}

LONG Fwrite(WORD handle, LONG count, const void __far *buf)
{
    dw(6, handle);
    dl(8, count);
    dl(12, (LONG)buf);
    return dos(0x40);
}

LONG Fseek(LONG offset, WORD handle, WORD mode)
{
    dl(6, offset);
    dw(10, handle);
    dw(12, mode);
    return dos(0x42);
}

LONG Frename(const char __far *oldname, const char __far *newname)
{
    dw(6, 0);
    dl(8, (LONG)oldname);
    dl(12, (LONG)newname);
    return dos(0x56);
}

LONG Fattrib(const char __far *name, WORD wflag, WORD attr)
{
    dl(6, (LONG)name);
    dw(10, wflag);
    dw(12, attr);
    return dos(0x43);
}
