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
