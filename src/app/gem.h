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
#include <stddef.h>

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
#define WHITEBAK   0x0040       /* an icon over a white ground: leave it */
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

/* TEDINFO, what a G_TEXT/G_FTEXT/G_BOXTEXT ob_spec points at: 28 bytes,
 * the ST's.  The three pointers are LONGs holding bank-$00 addresses;
 * te_txtlen and te_tmplen are the strings' lengths with the NUL, which
 * the AES fills in at rsrc_load from the file's own text -- so a field
 * an application means to fill in later still carries a buffer of the
 * right length in the resource. */
typedef struct {
    LONG te_ptext;              /* what the field holds: the raw places */
    LONG te_ptmplt;             /* "Name: ________.___" */
    LONG te_pvalid;             /* one class character per place */
    WORD te_font, te_fontid;
    WORD te_just;
    WORD te_color;
    WORD te_fontsize;
    WORD te_thickness;
    WORD te_txtlen, te_tmplen;
} TEDINFO;

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
#define UPARROW 0x0040
#define DNARROW 0x0080
#define VSLIDE  0x0100
#define LFARROW 0x0200
#define RTARROW 0x0400
#define HSLIDE  0x0800
#define WF_KIND     1
#define WF_NAME     2
#define WF_INFO     3
#define WF_WORKXYWH 4
#define WF_CURRXYWH 5
#define WF_PREVXYWH 6
#define WF_FULLXYWH 7
#define WF_HSLIDE   8
#define WF_VSLIDE   9
#define WF_TOP      10
#define WF_FIRSTXYWH 11
#define WF_NEXTXYWH 12
#define WF_NEWDESK  14
#define WF_HSLSIZ   15
#define WF_VSLSIZ   16
/* WM_ARROWED's word 4 */
#define WA_UPPAGE   0
#define WA_DNPAGE   1
#define WA_UPLINE   2
#define WA_DNLINE   3
#define WA_LFPAGE   4
#define WA_RTPAGE   5
#define WA_LFLINE   6
#define WA_RTLINE   7
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

/* An MFDB, the raster calls' form descriptor, laid out as the VDI has
 * had it since 1984 -- with one change forced by the compiler:
 * fd_addr is a 32-bit word, not a pointer, because the small data
 * model makes `void *` 16 bits and would shift every field after it
 * (src/vdi/vdi.h has the account).  A form lives in bank $00 here, so
 * only the low half is ever used; 0 means the screen. */
typedef struct {
    uint32_t fd_addr;
    WORD fd_w, fd_h;
    WORD fd_wdwidth;            /* (fd_w + 15) / 16 -- WORDS, per the spec */
    WORD fd_stand;              /* 0 device-specific, 1 VDI standard */
    WORD fd_nplanes;
    WORD fd_r1, fd_r2, fd_r3;
} MFDB;

/* The rest of the VDI, in the names and the argument order it has had
 * since 1984.  Absent by choice: cell array (10, 27), the valuator
 * (29) and 34, which the driver answers with v_nop -- a binding that
 * silently does nothing is worse than a name that is not there. */
void v_opnwk(WORD *work_in, WORD *handle, WORD *work_out);
void v_clswk(WORD handle);
void v_clrwk(WORD handle);
void v_updwk(WORD handle);      /* a screen has nothing to write out */
void v_enter_cur(WORD handle);
void v_exit_cur(WORD handle);
void v_pmarker(WORD handle, WORD count, const WORD *pxy);
void v_fillarea(WORD handle, WORD count, const WORD *pxy);
void v_bar(WORD handle, const WORD *pxy);
void v_arc(WORD handle, WORD x, WORD y, WORD radius, WORD begang, WORD endang);
void v_pieslice(WORD handle, WORD x, WORD y, WORD radius, WORD begang,
                WORD endang);
void v_circle(WORD handle, WORD x, WORD y, WORD radius);
void v_ellipse(WORD handle, WORD x, WORD y, WORD xrad, WORD yrad);
void v_ellarc(WORD handle, WORD x, WORD y, WORD xrad, WORD yrad,
              WORD begang, WORD endang);
void v_ellpie(WORD handle, WORD x, WORD y, WORD xrad, WORD yrad,
              WORD begang, WORD endang);
void v_rbox(WORD handle, const WORD *pxy);
void v_rfbox(WORD handle, const WORD *pxy);
void v_justified(WORD handle, WORD x, WORD y, const char *s, WORD length,
                 WORD word_space, WORD char_space);
void vst_height(WORD handle, WORD height, WORD *char_width, WORD *char_height,
                WORD *cell_width, WORD *cell_height);
WORD vst_rotation(WORD handle, WORD angle);
void vs_color(WORD handle, WORD index, const WORD *rgb);
WORD vsl_type(WORD handle, WORD style);
WORD vsl_width(WORD handle, WORD width);
WORD vsm_type(WORD handle, WORD symbol);
WORD vsm_height(WORD handle, WORD height);
WORD vsm_color(WORD handle, WORD color);
WORD vst_font(WORD handle, WORD font);
WORD vsf_style(WORD handle, WORD style);
void vq_color(WORD handle, WORD index, WORD flag, WORD *rgb);
WORD v_locator(WORD handle, WORD x, WORD y, WORD *xout, WORD *yout,
               WORD *term);
WORD vsm_choice(WORD handle, WORD *choice);
/* The string device answers ONE key per call, as a GEM key code -- scan
 * code over ASCII, the value evnt_keybd hands out -- or 0 if none is
 * waiting.  That is this driver's convention, not the ST's vsm_string
 * (src/vdi/vdi.c), which is why it does not wear that name. */
WORD v_string(WORD handle, WORD *key);
WORD vswr_mode(WORD handle, WORD mode);
WORD vsin_mode(WORD handle, WORD dev, WORD mode);
void vql_attributes(WORD handle, WORD *attr);   /* type, colour, mode, width */
void vqm_attributes(WORD handle, WORD *attr);   /* type, colour, mode, height */
void vqf_attributes(WORD handle, WORD *attr);   /* style, colour, index,
                                                 * mode, perimeter */
void vqt_attributes(WORD handle, WORD *attr);   /* six words and four points */
void vst_alignment(WORD handle, WORD hin, WORD vin, WORD *hout, WORD *vout);
void vq_extnd(WORD handle, WORD owflag, WORD *work_out);
void v_contourfill(WORD handle, WORD x, WORD y, WORD index);
WORD vsf_perimeter(WORD handle, WORD vis);
void v_get_pixel(WORD handle, WORD x, WORD y, WORD *pel, WORD *index);
WORD vst_effects(WORD handle, WORD effects);
WORD vst_point(WORD handle, WORD point, WORD *char_width, WORD *char_height,
               WORD *cell_width, WORD *cell_height);
void vsl_ends(WORD handle, WORD beg_style, WORD end_style);
void vro_cpyfm(WORD handle, WORD mode, const WORD *pxy, const MFDB *src,
               const MFDB *dst);
void vr_trnfm(WORD handle, const MFDB *src, const MFDB *dst);
void vsc_form(WORD handle, const WORD *form);   /* the 37 words of a cursor */
void vsf_udpat(WORD handle, const WORD *pattern, WORD planes);
void vsl_udsty(WORD handle, WORD pattern);
void vqin_mode(WORD handle, WORD dev, WORD *mode);
void vqt_extent(WORD handle, const char *s, WORD *extent);  /* four points */
WORD vqt_width(WORD handle, WORD ch, WORD *cell_width, WORD *left_delta,
               WORD *right_delta);
/* A handler is 24 bits under the large code model, so a vector is a
 * LONG here rather than a native function pointer. */
WORD vex_timv(WORD handle, LONG newv, LONG *oldv);   /* answers the tick, in ms */
void vex_butv(WORD handle, LONG newv, LONG *oldv);
void vex_motv(WORD handle, LONG newv, LONG *oldv);
void vex_curv(WORD handle, LONG newv, LONG *oldv);
WORD vst_load_fonts(WORD handle, WORD select);
void vst_unload_fonts(WORD handle, WORD select);
void vrt_cpyfm(WORD handle, WORD mode, const WORD *pxy, const MFDB *src,
               const MFDB *dst, const WORD *color);
void v_show_c(WORD handle, WORD reset);
void v_hide_c(WORD handle);
void vq_mouse(WORD handle, WORD *pstatus, WORD *x, WORD *y);
void vq_key_s(WORD handle, WORD *state);
void vs_clip(WORD handle, WORD clip_flag, const WORD *pxy);
WORD vqt_name(WORD handle, WORD element, char *name);   /* name[33] */

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
/* newpos: 0 puts the object first among its siblings (the bottom of the
 * stack), NIL last (the top), n after the nth. */
WORD objc_order(OBJECT *tree, WORD obj, WORD newpos);

WORD form_do(OBJECT *tree, WORD start);
WORD form_dial(WORD type, WORD x1, WORD y1, WORD w1, WORD h1,
               WORD x2, WORD y2, WORD w2, WORD h2);
WORD form_alert(WORD defbut, const char *s);   /* s in bank $00 (near) */
WORD form_error(WORD n);
WORD form_center(OBJECT *tree, WORD *x, WORD *y, WORD *w, WORD *h);

WORD graf_handle(WORD *wchar, WORD *hchar, WORD *wbox, WORD *hbox);
WORD graf_mouse(WORD mode, const WORD *form);  /* form only for USER_DEF */
WORD graf_growbox(WORD x1, WORD y1, WORD w1, WORD h1, WORD x2, WORD y2, WORD w2, WORD h2);
WORD graf_shrinkbox(WORD x1, WORD y1, WORD w1, WORD h1, WORD x2, WORD y2, WORD w2, WORD h2);
/* graf_mkstate's key state, the ST's bits (biosdefs.h).  This machine
 * can only be asked about SHIFT while no key is down: POKEY reports the
 * shift key on a line of its own and control only in the code of a key
 * that is being held (src/vdi/vdi.c, vq_key_s). */
#define MODE_RSHIFT 0x01
#define MODE_LSHIFT 0x02
#define MODE_CTRL   0x04
#define MODE_ALT    0x08

WORD graf_mkstate(WORD *mx, WORD *my, WORD *mb, WORD *ks);
WORD graf_dragbox(WORD w, WORD h, WORD sx, WORD sy,
                  WORD bx, WORD by, WORD bw, WORD bh, WORD *px, WORD *py);

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
/* The shell buffer: 4192 bytes the AES keeps between programs (the ST's
 * SIZE_SHELBUF), where the desktop leaves its DESKTOP.INF text.  The
 * application's copy may be far -- only bytes cross. */
#define SIZE_SHELBUF 4192
WORD shel_get(void __far *buffer, WORD len);
WORD shel_put(const void __far *data, WORD len);

/* The rest of the AES. */
WORD appl_write(WORD id, WORD length, const WORD *msg);
WORD evnt_mouse(WORD flags, WORD x, WORD y, WORD w, WORD h,
                WORD *mx, WORD *my, WORD *button, WORD *kstate);
WORD evnt_dclick(WORD rate, WORD setit);
WORD menu_text(OBJECT *tree, WORD item, const char *text);
WORD menu_register(WORD pid, const char *str);
WORD objc_edit(OBJECT *tree, WORD obj, WORD in_char, WORD *idx, WORD kind);
WORD form_keybd(OBJECT *tree, WORD obj, WORD nxt_obj, WORD thechar,
                WORD *pnxt_obj, WORD *pchar);
WORD form_button(OBJECT *tree, WORD obj, WORD clks, WORD *pnxt_obj);
WORD graf_rubbox(WORD x, WORD y, WORD w, WORD h, WORD *pw, WORD *ph);
WORD graf_watchbox(OBJECT *tree, WORD obj, WORD instate, WORD outstate);
WORD fsel_input(char *path, char *sel, WORD *button);
WORD fsel_exinput(char *path, char *sel, WORD *button, const char *label);
WORD rsrc_saddr(WORD type, WORD index, void *addr);
WORD rsrc_obfix(OBJECT *tree, WORD obj);
WORD shel_read(char *cmd, char *tail);
WORD shel_find(char *path);
WORD shel_envrn(char **value, const char *name);

/* -- GEMDOS, the ST's osbind names.  Pointers are far so that a buffer
 * Malloc gave out -- which is far memory -- can be read into directly;
 * a near pointer widens to one.  Errors are the ST's negative numbers,
 * src/sys/gemdos.h.  Fseek, Fdatime and the clock were the four
 * gaps phase 16 filled; nothing here answers EINVFN any more
 * except Tsetdate, which has no clock to set. */
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

LONG Fdatime(WORD *timeptr, WORD handle, WORD wflag);   /* two words: time, date */
WORD Tgetdate(void);
WORD Tgettime(void);

#endif /* GEM4XE_APP_GEM_H */
