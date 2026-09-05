/* vdi.h -- GEM VDI for gem4xe.
 *
 * Structure follows the Digital Research / EmuTOS screen driver: a fixed
 * parameter block of five arrays, a flat jump table indexed by opcode, and
 * handlers that take no arguments and read the globals.
 *
 * That 1984 ABI is a gift on this machine.  DRI's own entry.a86 copies the
 * caller's arrays into driver-owned globals and runs the whole VDI on a
 * private 256-byte stack; every handler is `void f(void)`.  Nothing here needs
 * argument passing, and nothing needs reentrancy.
 *
 * Only the 37 opcodes the AES and the GEM Desktop actually use are planned;
 * DRI's own shipping driver v_nop'd ten opcodes and so do we.
 */
#ifndef GEM4XE_VDI_H
#define GEM4XE_VDI_H

#include <stdint.h>

typedef int16_t  WORD;
typedef uint16_t UWORD;

/* ---- parameter block -------------------------------------------------- */
/* contrl[] indices, per the VDI spec:
 *   0 opcode                     1 # of points in ptsin
 *   2 # of points out            3 # of words in intin
 *   4 # of words out             5 sub-opcode
 *   6 workstation handle         7-8  pointer parameter 1 (source MFDB)
 *   9-10 pointer parameter 2 (destination MFDB)                          */
#define CONTRL_SIZE 12
#define INTIN_SIZE  128
#define PTSIN_SIZE  128
#define INTOUT_SIZE 64
#define PTSOUT_SIZE 32

extern WORD contrl[CONTRL_SIZE];
extern WORD intin[INTIN_SIZE];
extern WORD ptsin[PTSIN_SIZE];
extern WORD intout[INTOUT_SIZE];
extern WORD ptsout[PTSOUT_SIZE];

/* ---- MFDB ------------------------------------------------------------- */
/* Kept byte-for-byte as the VDI defines it, including fd_wdwidth in 16-bit
 * WORDS, because every GEM application builds one of these by hand and a
 * changed layout breaks all of them.
 *
 * On this device it is only a hint: the driver computes the real byte stride
 * itself (width/2 for 4bpp chunky).  fd_stand likewise has nothing to
 * distinguish -- there is exactly one form here -- so vr_trnfm is a no-op. */
typedef struct {
    /* fd_addr is 32 bits BY SPECIFICATION -- the MFDB is a GEM/68000
     * structure and every application builds one by hand at that layout.
     * It must NOT be a native pointer: Calypsi's small data model makes
     * `void *` 16 bits, which silently shifts every field after it by two
     * bytes.  (Found the hard way: vrt_cpyfm read fd_w out of fd_addr's
     * upper half and drew garbage.)  On this target only the low 16 bits
     * are meaningful, since forms live in bank $00. */
    uint32_t fd_addr;       /* 0 = the physical screen */
    WORD   fd_w;            /* width in pixels */
    WORD   fd_h;            /* height in pixels */
    WORD   fd_wdwidth;      /* (fd_w + 15) / 16 -- WORDS, per the spec */
    WORD   fd_stand;        /* 0 device-specific, 1 VDI standard */
    WORD   fd_nplanes;
    WORD   fd_r1, fd_r2, fd_r3;
} MFDB;

/* ---- workstation ------------------------------------------------------ */
/* A subset of EmuTOS's Vwk, with the field names kept so the port stays
 * recognisable against the donor. */
typedef struct {
    WORD handle;
    WORD clip;              /* clipping enabled */
    WORD xmn_clip, ymn_clip, xmx_clip, ymx_clip;
    WORD wrt_mode;          /* 0 replace, 1 transparent, 2 XOR, 3 rev-trans */
    WORD line_color;
    WORD line_width;
    WORD line_index;        /* line style, 1..7 */
    WORD fill_color;
    WORD fill_style;        /* interior: FIS_HOLLOW .. FIS_USER (donor name) */
    WORD fill_index;        /* vsf_style's index MINUS ONE, as the donor keeps it */
    WORD fill_per;          /* outline fill area */
    WORD text_color;
    /* What the two above resolve to: the 16-bit rows of the current fill
     * pattern and the mask that turns a y coordinate into a row index
     * (3 for the 4-row dithers, 7 for the OEM and coarse hatches, 15 for
     * the fine hatches and the user pattern).  Bit 15 is the LEFTMOST
     * pixel of a 16-aligned screen word. */
    const UWORD *patptr;
    WORD  patmsk;
    UWORD ud_patrn[16];     /* vsf_udpat's pattern, FIS_USER */
    UWORD ud_ls;            /* vsl_udsty's line style, index 7 */
} Vwk;

/* The physical workstation's handle, by specification; v_opnvwk hands
 * out the ones above it. */
#define VDI_PHYS_HANDLE 1

/* fill interior styles (vsf_interior) */
#define FIS_HOLLOW  0
#define FIS_SOLID   1
#define FIS_PATTERN 2
#define FIS_HATCH   3
#define FIS_USER    4
/* vsf_style limits: 24 patterns (8 dithers + 16 OEM), 12 hatches; an index
 * out of range becomes 1, the way the donor's vsf_style does it. */
#define MAX_FILL_PATTERN 24
#define MAX_FILL_HATCH   12
/* The standard tables, generated from the donor by tools/patconv.py into
 * src/vdi/fillpat.c: 8 dithers of 4 rows, 16 OEM patterns of 8, 6 coarse
 * hatches of 8, 6 fine hatches of 16. */
extern const UWORD fill_dither[32];
extern const UWORD fill_oem[128];
extern const UWORD fill_hatch0[48];
extern const UWORD fill_hatch1[96];

/* The workstation a call names in contrl[6]: every VDI routine reads and
 * writes this one, and the dispatcher copies the right one in (vdi.c). */
extern Vwk vwk;

/* writing modes, as vswr_mode takes them (1-based) */
#define MD_REPLACE 1
#define MD_TRANS   2
#define MD_XOR     3
#define MD_ERASE   4

/* ---- opcodes we implement -------------------------------------------- */
#define V_OPNWK        1
#define V_CLSWK        2
#define V_CLRWK        3
#define V_PLINE        6
#define V_GTEXT        8
#define VST_HEIGHT    12
#define VQT_ATTRIBUTES 38
#define VST_ALIGNMENT 39
#define VSL_TYPE      15
#define VSL_WIDTH     16
#define VSL_COLOR     17
#define VST_COLOR     22
#define VSF_INTERIOR  23
#define VSF_STYLE     24
#define VSF_COLOR     25
#define VSWR_MODE     32
#define VSF_UDPAT    112
#define V_OPNVWK     100
#define V_CLSVWK     101
#define VQ_EXTND     102
#define VRO_CPYFM    109
#define VR_TRNFM     110
#define VR_RECFL     114
#define VRT_CPYFM    121
#define V_ESCAPE       5
#define V_CHOICE      30
#define V_STRING      31
#define VSIN_MODE     33
#define VQIN_MODE    115
#define VEX_TIMV     118
#define VSL_UDSTY    113
#define VEX_BUTV     125
#define VEX_MOTV     126
#define VEX_CURV     127
#define VQ_KEY_S     128
#define VSC_FORM     111
#define V_SHOW_C     122
#define V_HIDE_C     123
#define VQ_MOUSE     124
#define VS_CLIP      129

/* ---- entry point ------------------------------------------------------ */
/* GEM 8x8 system font (from EmuTOS bios/fnt_st_8x8.c via tools/fontconv.py).
 * A 1bpp strip: FONT_STRIDE bytes per row, FONT_H rows, character N's byte on
 * row r at r*FONT_STRIDE + N. */
#define FONT_W        8
#define FONT_H        8
#define FONT_STRIDE 256
#define FONT_TOP      6     /* Fonthead.top: baseline to top of cell */
extern const uint8_t __far font8x8[FONT_STRIDE * FONT_H];

void vdi(void);             /* dispatch on contrl[0]; the GSX "SCREEN" entry */
void vdi_init(void);        /* one-time bring-up of the physical workstation */
void vdi_close_virtuals(void); /* every virtual workstation closed: a program's, at its end */
void vdi_font_expand(void); /* 1bpp -> 4bpp glyph masks into VRAM; call once */

/* Mouse cursor.  vdi_cursor_move() is what an input poll calls after
 * ptr_poll(): it erases, repositions and redraws only if something changed. */
void vdi_cursor_move(void);

/* The driver's save buffer for the AES -- what a drop-down or an alert
 * saves the screen under itself into (bb_save / bb_restore).  The donor
 * gsx_malloc()s one 25 character columns wide from the OS; here it is a
 * whole screen in VRAM, laid out like the screen, where the blitter can
 * reach it, and only the driver knows where.  Fills in an MFDB naming it,
 * for vro_cpyfm. */
void vdi_save_form(MFDB *m);

/* Drive the input devices once.  Polls the pointer and the keyboard, moves the
 * cursor, and calls whichever vex_* vectors are installed.
 *
 * The application calls this in its own loop.  Since Phase 9 the sampling
 * that cannot wait for the loop is done under interrupt (src/sys/irq.s: the
 * timer IRQ decodes a quadrature device, the keyboard IRQ latches the key) and
 * this call only consumes what the handlers counted -- so the loop may be as
 * slow as it likes without losing anything.  Moving the whole call under the
 * VBI is still possible; the AES above would not notice, which is the point
 * of it being a single entry point. */
void vdi_input_poll(void);

/* Just the keyboard: move POKEY's one-key latch into the driver's queue and
 * re-arm it.  Part of vdi_input_poll(); on its own it is what an idle loop
 * that must not disturb the pointer or the cursor calls, so that a key
 * pressed between two events is still there when someone asks. */
void vdi_key_poll(void);

/* The gem4xe ABI for vex_* handlers: they take no arguments and read
 * ptr_state / the VDI globals.  GEM's 68000 convention passes x and y in
 * d0/d1, which has no meaning here; a documented platform convention is
 * better than a fake one. */
typedef void (*VDI_VEC)(void);

#endif /* GEM4XE_VDI_H */
