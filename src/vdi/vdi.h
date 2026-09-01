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
    WORD fill_index;        /* fill style: 0 hollow, 1 solid, ... */
    WORD fill_style;
    WORD fill_per;          /* outline fill area */
    WORD text_color;
} Vwk;

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
#define VST_ALIGNMENT 39
#define VSL_TYPE      15
#define VSL_WIDTH     16
#define VSL_COLOR     17
#define VST_COLOR     22
#define VSF_INTERIOR  23
#define VSF_STYLE     24
#define VSF_COLOR     25
#define VSWR_MODE     32
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
extern const uint8_t font8x8[FONT_STRIDE * FONT_H];

void vdi(void);             /* dispatch on contrl[0]; the GSX "SCREEN" entry */
void vdi_init(void);        /* one-time bring-up of the physical workstation */
void vdi_font_expand(void); /* 1bpp -> 4bpp glyph masks into VRAM; call once */

/* Mouse cursor.  vdi_cursor_move() is what an input poll calls after
 * ptr_poll(): it erases, repositions and redraws only if something changed. */
void vdi_cursor_move(void);

/* Drive the input devices once.  Polls the pointer and the keyboard, moves the
 * cursor, and calls whichever vex_* vectors are installed.
 *
 * Today the application calls this in its own loop, because gem4xe runs with
 * interrupts off (src/crt_atari.s).  When native-mode vector stubs exist this
 * is what the VBI will call instead -- the AES above it will not notice the
 * difference, which is the point of it being a single entry point. */
void vdi_input_poll(void);

/* The gem4xe ABI for vex_* handlers: they take no arguments and read
 * ptr_state / the VDI globals.  GEM's 68000 convention passes x and y in
 * d0/d1, which has no meaning here; a documented platform convention is
 * better than a fake one. */
typedef void (*VDI_VEC)(void);

#endif /* GEM4XE_VDI_H */
