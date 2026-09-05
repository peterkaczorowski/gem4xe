/* gem.h -- what a gem4xe application sees of GEM.
 *
 * An application is a separate program: it is linked against nothing of
 * gem4xe's and knows no address inside it.  It reaches the VDI and the AES
 * the way an ST program reaches them through trap #2 -- by a software
 * interrupt, here the 65816's COP, with a parameter block whose five (VDI)
 * or six (AES) fields point at the caller's own arrays:
 *
 *     COP #$73   VDI     X:C = address of a VDIPB     (vdi_call)
 *     COP #$C8   AES     X:C = address of an AESPB    (aes_call)
 *     COP #$01   GEMDOS  X:C = address of a GDPB      (dos_call)
 *
 * The signature bytes and the block layouts are the ST's (the AES's and
 * GEMDOS's function numbers too), so a GEM binding written for the ST
 * needs only its trap replaced.  gem4xe copies the caller's arrays in before the call
 * and out after it -- the DRI entry discipline -- so an application's
 * arrays may be exactly as large as its own calls need, and nothing of the
 * application's is ever read while the call is not in progress.
 *
 * On return A, X and Y are undefined; D, DB, S and P are as they were.
 * Trees, strings and forms the AES reads IN PLACE are addressed with 16
 * bits inside gem4xe (the small data model), so they must be in bank $00 --
 * which is where an application's data is, its near region being a slice
 * of bank $00 (src/app/gemapp.scm).
 */
#ifndef GEM4XE_APP_GEM_H
#define GEM4XE_APP_GEM_H

#include <stdint.h>

typedef short          WORD;
typedef unsigned short UWORD;
typedef long           LONG;

/* The five VDI arrays and the six AES arrays, by far pointer: 32 bits in
 * memory each, which makes the block the ST's byte for byte. */
typedef struct {
    WORD __far *contrl;
    WORD __far *intin;
    WORD __far *ptsin;
    WORD __far *intout;
    WORD __far *ptsout;
} VDIPB;

typedef struct {
    WORD __far *control;
    WORD __far *global;
    WORD __far *int_in;
    WORD __far *int_out;
    LONG __far *addr_in;
    LONG __far *addr_out;
} AESPB;

/* GEMDOS's block is the ST's trap #1 stack frame with the result in front
 * of it: the function number, then the arguments in the ST's order and
 * sizes (WORD 2, LONG 4), little-endian.  16 bytes holds the longest,
 * Fread's. */
typedef struct {
    LONG ret;
    WORD fn;
    WORD arg[5];
} GDPB;

/* The three entry points (src/app/gemabi.s).  __simple_call puts the
 * block's address in X:C, which is where the COP handler looks. */
__simple_call void vdi_call(VDIPB __far *pb);
__simple_call void aes_call(AESPB __far *pb);
__simple_call void dos_call(GDPB __far *pb);

typedef struct {
    WORD g_x, g_y, g_w, g_h;
} GRECT;

/* -- The AES object, as in aes.h: ob_spec is a LONG whose low word holds
 * a bank-$00 address for the types that point at something. */
typedef struct {
    WORD  ob_next, ob_head, ob_tail;
    UWORD ob_type, ob_flags, ob_state;
    LONG  ob_spec;
    WORD  ob_x, ob_y, ob_width, ob_height;
} OBJECT;

#define G_BOX      20
#define G_TEXT     21
#define G_BOXTEXT  22
#define G_IMAGE    23
#define G_IBOX     25
#define G_BUTTON   26
#define G_BOXCHAR  27
#define G_STRING   28
#define G_FTEXT    29
#define G_FBOXTEXT 30
#define G_ICON     31
#define G_TITLE    32

#define NONE       0x0000
#define SELECTABLE 0x0001
#define DEFAULT    0x0002
#define EXIT       0x0004
#define EDITABLE   0x0008
#define RBUTTON    0x0010
#define LASTOB     0x0020
#define TOUCHEXIT  0x0040
#define HIDETREE   0x0080
#define NORMAL     0x0000
#define SELECTED   0x0001
#define CROSSED    0x0002
#define CHECKED    0x0004
#define DISABLED   0x0008
#define OUTLINED   0x0010
#define SHADOWED   0x0020
#define NIL        (-1)
#define TRUE       1
#define FALSE      0
#define ROOT       0
#define MAX_DEPTH  8

/* ICONBLK, what a G_ICON's ob_spec points at: 34 bytes, the ST's.  The
 * three pointers are LONGs holding bank-$00 addresses; the mask and the
 * data are 1-bit rows of ib_wicon/16 words, the text a C string. */
typedef struct {
    LONG ib_pmask;
    LONG ib_pdata;
    LONG ib_ptext;
    WORD ib_char;               /* colour << 12 | the letter drawn on it */
    WORD ib_xchar, ib_ychar;
    WORD ib_xicon, ib_yicon, ib_wicon, ib_hicon;
    WORD ib_xtext, ib_ytext, ib_wtext, ib_htext;
} ICONBLK;

/* evnt_multi: its flags, a mouse rectangle (five words, as the AES takes
 * them), and the messages the desktop answers. */
#define MU_KEYBD    0x0001
#define MU_BUTTON   0x0002
#define MU_M1       0x0004
#define MU_M2       0x0008
#define MU_MESAG    0x0010
#define MU_TIMER    0x0020

typedef struct {
    WORD m_out;                 /* report leaving the rectangle, not entering */
    WORD m_x, m_y, m_w, m_h;
} MOBLK;

#define MN_SELECTED 10
#define WM_REDRAW   20
#define WM_TOPPED   21
#define WM_CLOSED   22
#define WM_FULLED   23
#define WM_ARROWED  24
#define WM_HSLID    25
#define WM_VSLID    26
#define WM_SIZED    27
#define WM_MOVED    28
#define WM_NEWTOP   29
#define AC_OPEN     40
#define AC_CLOSE    41

/* wind_create kinds, wind_get / wind_set fields, wind_update codes */
#define NAME    0x0001
#define CLOSER  0x0002
#define FULLER  0x0004
#define MOVER   0x0008
#define INFO    0x0010
#define SIZER   0x0020
#define WF_KIND     1
#define WF_NAME     2
#define WF_INFO     3
#define WF_WORKXYWH 4
#define WF_CURRXYWH 5
#define WF_PREVXYWH 6
#define WF_FULLXYWH 7
#define WF_TOP      10
#define WF_FIRSTXYWH 11
#define WF_NEXTXYWH 12
#define WF_NEWDESK  14
#define WC_BORDER   0
#define WC_WORK     1
#define END_UPDATE  0
#define BEG_UPDATE  1
#define END_MCTRL   2
#define BEG_MCTRL   3

/* form_dial's types; graf_mouse's shapes and modes; rsrc_gaddr's types */
#define FMD_START   0
#define FMD_GROW    1
#define FMD_SHRINK  2
#define FMD_FINISH  3
#define ARROW       0
#define TEXT_CRSR   1
#define HOURGLASS   2
#define BUSYBEE     2
#define POINT_HAND  3
#define FLAT_HAND   4
#define THIN_CROSS  5
#define THICK_CROSS 6
#define OUTLN_CROSS 7
#define USER_DEF    255
#define M_OFF       256
#define M_ON        257
#define M_SAVE      258
#define M_RESTORE   259
#define M_PREVIOUS  260
#define R_TREE      0
#define R_OBJECT    1
#define R_ICONBLK   3
#define R_STRING    5
#define R_FRSTR     15

/* -- The bindings an application uses; src/app/gemlib.c.  The arrays are
 * the application's own and are left in place after each call so that a
 * caller can look at what came back. */
extern WORD contrl[12], intin[128], ptsin[16], intout[45], ptsout[12];
extern WORD control[5], global[15], int_in[16], int_out[7];
extern LONG addr_in[3], addr_out[1];

void v_opnvwk(WORD *work_in, WORD *handle, WORD *work_out);
void v_clsvwk(WORD handle);
void vr_recfl(WORD handle, WORD *pxy);
void v_pline(WORD handle, WORD count, WORD *pxy);
void v_gtext(WORD handle, WORD x, WORD y, const char *s);
WORD vsf_color(WORD handle, WORD color);
WORD vsf_interior(WORD handle, WORD style);
WORD vsl_color(WORD handle, WORD color);
WORD vst_color(WORD handle, WORD color);

WORD appl_init(void);
WORD appl_exit(void);

WORD evnt_keybd(void);          /* scan code << 8 | ASCII, as on the ST */
WORD evnt_button(WORD clicks, UWORD mask, UWORD state,
                 WORD *mx, WORD *my, WORD *mb, WORD *ks);
WORD evnt_mesag(WORD *msg);     /* eight words */
WORD evnt_timer(UWORD lo, UWORD hi);
/* m1 and m2 may be 0 when MU_M1 / MU_M2 are not asked for; msg is eight
 * words, filled when MU_MESAG comes back. */
WORD evnt_multi(UWORD flags, WORD bclk, UWORD bmsk, UWORD bst,
                const MOBLK *m1, const MOBLK *m2, WORD *msg,
                UWORD tlo, UWORD thi,
                WORD *mx, WORD *my, WORD *mb, WORD *ks, WORD *kr, WORD *br);

WORD menu_bar(OBJECT *tree, WORD showit);
WORD menu_icheck(OBJECT *tree, WORD item, WORD check);
WORD menu_ienable(OBJECT *tree, WORD item, WORD enable);
WORD menu_tnormal(OBJECT *tree, WORD title, WORD normal);

WORD objc_draw(OBJECT *tree, WORD start, WORD depth, WORD x, WORD y, WORD w, WORD h);
WORD objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my);
WORD objc_offset(OBJECT *tree, WORD obj, WORD *x, WORD *y);
WORD objc_change(OBJECT *tree, WORD obj, WORD resvd, WORD x, WORD y, WORD w, WORD h,
                 WORD state, WORD redraw);

WORD form_do(OBJECT *tree, WORD start);
WORD form_dial(WORD type, WORD x1, WORD y1, WORD w1, WORD h1,
               WORD x2, WORD y2, WORD w2, WORD h2);
WORD form_alert(WORD defbut, const char *s);   /* s in bank $00 (near) */
WORD form_error(WORD n);
WORD form_center(OBJECT *tree, WORD *x, WORD *y, WORD *w, WORD *h);

WORD graf_handle(WORD *wchar, WORD *hchar, WORD *wbox, WORD *hbox);
WORD graf_mouse(WORD mode, const WORD *form);  /* form only for USER_DEF */
WORD graf_mkstate(WORD *mx, WORD *my, WORD *mb, WORD *ks);

WORD wind_create(WORD kind, WORD x, WORD y, WORD w, WORD h);
WORD wind_open(WORD handle, WORD x, WORD y, WORD w, WORD h);
WORD wind_get(WORD handle, WORD field, WORD *o1, WORD *o2, WORD *o3, WORD *o4);
WORD wind_set(WORD handle, WORD field, WORD w1, WORD w2, WORD w3, WORD w4);
WORD wind_close(WORD handle);
WORD wind_delete(WORD handle);
WORD wind_find(WORD x, WORD y);
WORD wind_update(WORD code);
WORD wind_calc(WORD type, WORD kind, WORD x, WORD y, WORD w, WORD h,
               WORD *ox, WORD *oy, WORD *ow, WORD *oh);
/* wind_set's WF_NEWDESK takes the tree as the ST does: its address in
 * two words, high first (0 here: the tree is in bank $00), then the
 * object to draw from. */
#define wind_newdesk(tree, root) \
    wind_set(0, WF_NEWDESK, 0, (WORD)(uint16_t)(tree), (root), 0)

/* rsrc_load takes the resource from gem4xe's application pool; the name
 * is a near string.  rsrc_gaddr answers a bank-$00 address. */
WORD rsrc_load(const char *name);
WORD rsrc_free(void);
WORD rsrc_gaddr(WORD type, WORD index, void **addr);

/* shel_write's doex: what the shell does once this program returns.  The
 * tail is the ST's: a length byte, then the characters. */
#define SHW_NOEXEC   0          /* back to the desktop */
#define SHW_EXEC     1          /* run cmd, then the desktop again */
#define SHW_SHUTDOWN 4          /* leave GEM (the desktop's Quit) */
WORD shel_write(WORD doex, WORD isgr, WORD iscr, const char *cmd, const char *tail);

/* -- GEMDOS, the ST's osbind names.  Pointers are far so that a buffer
 * Malloc gave out -- which is far memory -- can be read into directly;
 * a near pointer widens to one.  Errors are the ST's negative numbers,
 * src/sys/gemdos.h; Fseek, Fdatime and Tget* answer EINVFN today. */
#define E_OK      0L
#define EINVFN  -32L
#define EFILNF  -33L
#define EPTHNF  -34L
#define ENHNDL  -35L
#define EACCDN  -36L
#define EIHNDL  -37L
#define ENSMEM  -39L
#define EDRIVE  -46L
#define ENMFIL  -49L

#define FA_RDONLY  0x01
#define FA_HIDDEN  0x02
#define FA_SYSTEM  0x04
#define FA_VOLUME  0x08
#define FA_SUBDIR  0x10
#define FA_ARCHIVE 0x20

typedef struct {                /* the ST's, 44 bytes */
    char  d_reserved[21];
    char  d_attrib;
    UWORD d_time;
    UWORD d_date;
    LONG  d_length;             /* unsigned on the ST; long is enough here */
    char  d_fname[14];
} DTA;

typedef struct {                /* Dfree's answer, in clusters of 1 sector */
    LONG b_free, b_total, b_secsiz, b_clsiz;
} DISKINFO;

WORD Sversion(void);
WORD Dsetdrv(WORD drive);       /* returns the drive map */
WORD Dgetdrv(void);
LONG Dsetpath(const char __far *path);
LONG Dgetpath(char __far *buf, WORD drive);
LONG Dcreate(const char __far *path);
LONG Ddelete(const char __far *path);
LONG Dfree(DISKINFO __far *info, WORD drive);
void Fsetdta(DTA __far *dta);
DTA __far *Fgetdta(void);
LONG Fsfirst(const char __far *spec, WORD attr);
LONG Fsnext(void);
LONG Fopen(const char __far *name, WORD mode);
LONG Fcreate(const char __far *name, WORD attr);
LONG Fclose(WORD handle);
LONG Fread(WORD handle, LONG count, void __far *buf);
LONG Fwrite(WORD handle, LONG count, const void __far *buf);
LONG Fseek(LONG offset, WORD handle, WORD mode);
LONG Fdelete(const char __far *name);
LONG Frename(const char __far *oldname, const char __far *newname);
LONG Fattrib(const char __far *name, WORD wflag, WORD attr);
LONG Malloc(LONG size);         /* -1 asks how much is left */
LONG Mfree(void __far *block);

#endif /* GEM4XE_APP_GEM_H */
