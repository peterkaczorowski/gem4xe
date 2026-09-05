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
#define LASTOB     0x0020
#define NORMAL     0x0000
#define SELECTED   0x0001
#define NIL        (-1)

/* wind_create kinds and wind_get fields */
#define NAME    0x0001
#define CLOSER  0x0002
#define FULLER  0x0004
#define MOVER   0x0008
#define INFO    0x0010
#define SIZER   0x0020
#define WF_WORKXYWH 4
#define WF_CURRXYWH 5

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
WORD graf_handle(WORD *wchar, WORD *hchar, WORD *wbox, WORD *hbox);
WORD objc_draw(OBJECT *tree, WORD start, WORD depth, WORD x, WORD y, WORD w, WORD h);
WORD wind_create(WORD kind, WORD x, WORD y, WORD w, WORD h);
WORD wind_open(WORD handle, WORD x, WORD y, WORD w, WORD h);
WORD wind_get(WORD handle, WORD field, WORD *o1, WORD *o2, WORD *o3, WORD *o4);
WORD wind_close(WORD handle);
WORD wind_delete(WORD handle);
WORD evnt_timer(UWORD lo, UWORD hi);
WORD evnt_keybd(void);          /* scan code << 8 | ASCII, as on the ST */

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
