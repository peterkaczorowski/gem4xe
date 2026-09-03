/* gemlib.c -- an application's GEM bindings, over the two COP entries.
 *
 * The shape is the classic GEM library's: one set of arrays, a parameter
 * block pointing at them, a binding per call that fills the arrays, makes
 * the call and unpacks the results.  The control words an AES binding
 * fills -- opcode and the four counts -- are the ST's: gem4xe reads the
 * counts to know how much to copy in and out, exactly as the AES does.
 *
 * The arrays are sized for what THIS library's bindings need, not for the
 * VDI's maxima: v_opnvwk's 45 intout and 12 ptsout words set the two
 * output sizes, and nothing here passes more than 16 points or 32 words.
 * An application that adds a binding with larger needs grows them.
 */
#include "gem.h"

WORD contrl[12], intin[32], ptsin[16], intout[45], ptsout[12];
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
    while (*s && n < 32)
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

WORD evnt_timer(UWORD lo, UWORD hi)
{
    int_in[0] = (WORD)lo;
    int_in[1] = (WORD)hi;
    return aes(24, 2, 1, 0, 0);
}
