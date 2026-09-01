/* aes.h -- GEM AES object model for gem4xe.
 *
 * Structures are the GEM ones, byte for byte, because a .RSC resource file is
 * a dump of exactly these and every GEM application builds trees at this
 * layout.  Where a field is a 32-bit pointer in GEM it stays 32 bits here for
 * the same reason the MFDB's fd_addr did -- see the note in vdi/vdi.h.
 */
#ifndef GEM4XE_AES_H
#define GEM4XE_AES_H

#include <stdint.h>
#include "../vdi/vdi.h"

typedef struct {
    WORD g_x, g_y, g_w, g_h;
} GRECT;

/* The object.  24 bytes: three link words, three attribute words, a 32-bit
 * ob_spec, then the rectangle. */
typedef struct {
    WORD  ob_next;          /* -1 = none */
    WORD  ob_head;          /* first child, -1 = none */
    WORD  ob_tail;          /* last child,  -1 = none */
    UWORD ob_type;
    UWORD ob_flags;
    UWORD ob_state;
    /* ob_spec.  For the box types this packs three things, and the order is
     * the 68000's byte order inside the LONG, which is what every .RSC on
     * disk contains:
     *     bits 31-24  character   (G_BOXCHAR only)
     *     bits 23-16  thickness   (signed byte)
     *     bits 15-0   colour word
     * (EmuTOS aes/gemobjop.c ob_sst: `th = *(((char *)pspec)+1)` and
     * `return *(char *)pspec`, with gr_crack taking the low word.)
     * For the text and button types it is an address instead. */
    uint32_t ob_spec;
    WORD  ob_x, ob_y;       /* relative to the PARENT */
    WORD  ob_width, ob_height;
} OBJECT;

/* ob_type */
#define G_BOX       20
#define G_TEXT      21
#define G_BOXTEXT   22
#define G_IMAGE     23
#define G_USERDEF   24
#define G_IBOX      25
#define G_BUTTON    26
#define G_BOXCHAR   27
#define G_STRING    28
#define G_FTEXT     29
#define G_FBOXTEXT  30
#define G_ICON      31
#define G_TITLE     32

/* ob_flags */
#define NONE        0x0000
#define SELECTABLE  0x0001
#define DEFAULT     0x0002
#define EXIT        0x0004
#define EDITABLE    0x0008
#define RBUTTON     0x0010
#define LASTOB      0x0020
#define TOUCHEXIT   0x0040
#define HIDETREE    0x0080
#define INDIRECT    0x0100

/* ob_state */
#define NORMAL      0x0000
#define SELECTED    0x0001
#define CROSSED     0x0002
#define CHECKED     0x0004
#define DISABLED    0x0008
#define OUTLINED    0x0010
#define SHADOWED    0x0020

#define NIL         (-1)

/* TEDINFO, for the G_TEXT family.  Only the fields the drawing code reads are
 * used; the rest are kept so the layout matches a .RSC. */
typedef struct {
    uint32_t te_ptext;
    uint32_t te_ptmplt;
    uint32_t te_pvalid;
    WORD te_font, te_fontid;
    WORD te_just;           /* 0 left, 1 right, 2 centred */
    WORD te_color;
    WORD te_fontsize;
    WORD te_thickness;
    WORD te_txtlen, te_tmplen;
} TEDINFO;

void gr_crack(UWORD color, WORD *pbc, WORD *ptc, WORD *pip, WORD *pic, WORD *pmd);
void ob_offset(OBJECT *tree, WORD obj, WORD *px, WORD *py);
void objc_draw(OBJECT *tree, WORD start, WORD depth, GRECT *clip);
WORD objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my);

#endif /* GEM4XE_AES_H */
