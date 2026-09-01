/* vdi.c -- GEM VDI dispatcher, workstation state and output primitives.
 *
 * The rendering strategy is the one the hardware forces: VBXE sits on the
 * 1.79 MHz chip bus no matter how fast the 65816 runs, so a primitive's job is
 * to EMIT BLITTER CONTROL BLOCKS, not to write pixels.  Measured, a
 * full-screen fill costs 0.51 of a frame through the blitter; the same work
 * through the MEMAC window would be roughly twenty times slower.
 */
#include "vdi.h"
#include "pointer.h"
#include "../vbxe/vbxe.h"

WORD contrl[CONTRL_SIZE];
WORD intin[INTIN_SIZE];
WORD ptsin[PTSIN_SIZE];
WORD intout[INTOUT_SIZE];
WORD ptsout[PTSOUT_SIZE];
Vwk  vwk;

/* Standard VDI line styles 1..7 (7 is user-defined; we start it solid). */
UWORD line_styles[8] = {
    0xFFFF, 0xFFFF, 0xFFF0, 0xE0E0, 0xFF18, 0xFF00, 0xF191, 0xFFFF
};

/* ---------------------------------------------------------------------- */
/* helpers                                                                */
/* ---------------------------------------------------------------------- */

static void order(WORD *a, WORD *b)
{
    if (*a > *b) { WORD t = *a; *a = *b; *b = t; }
}

/* Clip a rectangle to the workstation's clipping rectangle.
 * Returns 0 if nothing is left. */
static WORD clip_rect(WORD *x1, WORD *y1, WORD *x2, WORD *y2)
{
    if (vwk.clip) {
        if (*x1 < vwk.xmn_clip) *x1 = vwk.xmn_clip;
        if (*y1 < vwk.ymn_clip) *y1 = vwk.ymn_clip;
        if (*x2 > vwk.xmx_clip) *x2 = vwk.xmx_clip;
        if (*y2 > vwk.ymx_clip) *y2 = vwk.ymx_clip;
    }
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 > SCR_W - 1) *x2 = SCR_W - 1;
    if (*y2 > SCR_H - 1) *y2 = SCR_H - 1;
    return (*x1 <= *x2 && *y1 <= *y2);
}

/* Fill a device rectangle in 4bpp chunky, honouring odd-pixel edges.
 *
 * A pixel x lives in byte x>>1: high nibble when x is even, low nibble when
 * odd.  The blitter is byte-granular, so an edge byte that is only half inside
 * the rectangle has to keep its other nibble.  That is done with a pair of
 * constant-source read-modify-write blits -- AND away the nibble being
 * replaced, then OR the colour in -- which works for every colour including 0.
 * (Mode 6's nibble stencil would be one blit instead of two, but it cannot
 * write colour 0: a zero nibble means "transparent".)
 *
 * Rectangle edges are exactly where 4bpp VDI drivers historically went wrong,
 * so tests/emu/m3_vdi.py deliberately exercises odd x1, odd x2, and rectangles
 * one pixel wide inside a single byte.
 */
static void fill_rect_dev(WORD x1, WORD y1, WORD x2, WORD y2, WORD color)
{
    uint32_t base = VR_SCREEN0 + (uint32_t)y1 * SCR_STRIDE;
    uint16_t rows = (uint16_t)(y2 - y1 + 1);
    uint8_t  c    = (uint8_t)(((color & 0x0F) << 4) | (color & 0x0F));
    WORD bl = (WORD)(x1 >> 1), br = (WORD)(x2 >> 1);

    if (bl == br) {
        if ((x1 & 1) == 0 && (x2 & 1) == 1) {           /* whole byte */
            blit_fill(base + bl, SCR_STRIDE, 1, rows, c);
        } else if (x1 & 1) {                            /* low nibble only */
            blit_and(base + bl, SCR_STRIDE, 1, rows, 0xF0);
            blit_or(base + bl, SCR_STRIDE, 1, rows, (uint8_t)(c & 0x0F));
        } else {                                        /* high nibble only */
            blit_and(base + bl, SCR_STRIDE, 1, rows, 0x0F);
            blit_or(base + bl, SCR_STRIDE, 1, rows, (uint8_t)(c & 0xF0));
        }
        return;
    }
    if (x1 & 1) {                                       /* partial left */
        blit_and(base + bl, SCR_STRIDE, 1, rows, 0xF0);
        blit_or(base + bl, SCR_STRIDE, 1, rows, (uint8_t)(c & 0x0F));
        bl++;
    }
    if ((x2 & 1) == 0) {                                /* partial right */
        blit_and(base + br, SCR_STRIDE, 1, rows, 0x0F);
        blit_or(base + br, SCR_STRIDE, 1, rows, (uint8_t)(c & 0xF0));
        br--;
    }
    if (br >= bl)
        blit_fill(base + bl, SCR_STRIDE, (uint16_t)(br - bl + 1), rows, c);
}

/* Single pixel, read-modify-write through the MEMAC window.  The blitter
 * cannot help with one pixel, and a Bresenham line is the one primitive it
 * does not accelerate at all. */
static void plot(WORD x, WORD y, WORD color)
{
    uint32_t a;
    uint8_t b;
    if (vwk.clip && (x < vwk.xmn_clip || x > vwk.xmx_clip ||
                     y < vwk.ymn_clip || y > vwk.ymx_clip))
        return;
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H)
        return;
    a = VR_SCREEN0 + (uint32_t)y * SCR_STRIDE + (uint32_t)(x >> 1);
    b = vram_read8(a);
    if (x & 1)
        b = (uint8_t)((b & 0xF0) | (color & 0x0F));
    else
        b = (uint8_t)((b & 0x0F) | ((color & 0x0F) << 4));
    vram_write8(a, b);
}

/* ---------------------------------------------------------------------- */
/* mouse cursor                                                           */
/* ---------------------------------------------------------------------- */

/* GEM's MFORM, as vsc_form delivers it: 37 words of intin.  Rendering rule is
 * the standard one -- the MASK is painted in mf_bg first, then the DATA over
 * it in mf_fg, so a mask bit with no data bit gives the outline colour and a
 * clear mask bit leaves the screen alone. */
static WORD  cur_xhot, cur_yhot, cur_bg, cur_fg;
static UWORD cur_mask[16], cur_data[16];
static WORD  cur_hide = 1;          /* visible only at 0; starts hidden */
static WORD  cur_drawn;             /* is the saved block valid?        */
static WORD  sv_bx, sv_y, sv_nb, sv_nr;   /* what cursor_save() captured */

static void cursor_save(WORD cx, WORD cy)
{
    WORD bx0, bx1, y0, y1;
    bx0 = (WORD)(cx >> 1);
    bx1 = (WORD)((cx + 15) >> 1);
    y0 = cy;
    y1 = (WORD)(cy + 15);
    if (bx0 < 0) bx0 = 0;
    if (y0 < 0) y0 = 0;
    if (bx1 > SCR_STRIDE - 1) bx1 = SCR_STRIDE - 1;
    if (y1 > SCR_H - 1) y1 = SCR_H - 1;
    if (bx1 < bx0 || y1 < y0) { sv_nb = 0; return; }
    sv_bx = bx0;
    sv_y  = y0;
    sv_nb = (WORD)(bx1 - bx0 + 1);
    sv_nr = (WORD)(y1 - y0 + 1);
    blit_copy(VR_SCREEN0 + (uint32_t)sv_y * SCR_STRIDE + (uint32_t)sv_bx,
              SCR_STRIDE, VR_CURSAVE, VR_CURSAVE_STRIDE,
              (uint16_t)sv_nb, (uint16_t)sv_nr);
    blit_run();
}

static void cursor_restore(void)
{
    if (!sv_nb)
        return;
    blit_copy(VR_CURSAVE, VR_CURSAVE_STRIDE,
              VR_SCREEN0 + (uint32_t)sv_y * SCR_STRIDE + (uint32_t)sv_bx,
              SCR_STRIDE, (uint16_t)sv_nb, (uint16_t)sv_nr);
    blit_run();
    sv_nb = 0;
}

/* Plot the cursor form.  CPU work: 256 pixels is nothing next to a blit-list
 * upload, and the form is not byte-aligned in general. */
static void cursor_paint(WORD cx, WORD cy)
{
    WORD row, col;
    for (row = 0; row < 16; row++) {
        UWORD m = cur_mask[row], d = cur_data[row];
        for (col = 0; col < 16; col++) {
            UWORD bit = (UWORD)(0x8000u >> col);
            if (m & bit)
                plot((WORD)(cx + col), (WORD)(cy + row), cur_bg);
            if (d & bit)
                plot((WORD)(cx + col), (WORD)(cy + row), cur_fg);
        }
    }
}

static void cursor_show_now(void)
{
    WORD cx = (WORD)(ptr_state.x - cur_xhot);
    WORD cy = (WORD)(ptr_state.y - cur_yhot);
    cursor_save(cx, cy);
    cursor_paint(cx, cy);
    cur_drawn = 1;
}

static void cursor_hide_now(void)
{
    if (cur_drawn) {
        cursor_restore();
        cur_drawn = 0;
    }
}

/* Called by the input loop after ptr_poll().  Erase-move-redraw, and only when
 * the pointer actually moved -- redrawing a stationary cursor every frame
 * would cost two blits and 256 plots for nothing. */
void vdi_cursor_move(void)
{
    static WORD lastx = -1, lasty = -1;
    if (cur_hide)
        return;
    if (ptr_state.x == lastx && ptr_state.y == lasty && cur_drawn)
        return;
    cursor_hide_now();
    lastx = ptr_state.x;
    lasty = ptr_state.y;
    cursor_show_now();
}

static void vdi_vsc_form(void)
{
    WORD i;
    cur_xhot = intin[0];
    cur_yhot = intin[1];
    /* intin[2] is nplanes (always 1 here); [3] bg, [4] fg */
    cur_bg = intin[3];
    cur_fg = intin[4];
    for (i = 0; i < 16; i++) {
        cur_mask[i] = (UWORD)intin[5 + i];
        cur_data[i] = (UWORD)intin[21 + i];
    }
}

/* GEM's nesting rule: v_hide_c increments a counter and the cursor is visible
 * only at zero.  v_show_c with intin[0] == 0 forces it visible regardless of
 * how deeply it was hidden; anything else decrements by one. */
static void vdi_v_show_c(void)
{
    if (contrl[3] > 0 && intin[0] != 0) {
        if (cur_hide > 0)
            cur_hide--;
    } else {
        cur_hide = 0;
    }
    if (cur_hide == 0 && !cur_drawn)
        cursor_show_now();
}

static void vdi_v_hide_c(void)
{
    if (cur_hide == 0)
        cursor_hide_now();
    cur_hide++;
}

/* v_locator.  GEM passes the INITIAL locator position in ptsin, which is the
 * standard way to place the pointer, and returns where it ended up.  In sample
 * mode (vsin_mode 2) it returns immediately without waiting for input, which
 * is what the AES uses via gsx_setmousexy(). */
static void vdi_v_locator(void)
{
    if (contrl[1] > 0)
        ptr_warp(ptsin[0], ptsin[1]);
    ptsout[0] = ptr_state.x;
    ptsout[1] = ptr_state.y;
    intout[0] = 0;                  /* terminator: none */
    contrl[2] = 1;
    contrl[4] = 1;
}

static void vdi_vq_mouse(void)
{
    intout[0] = ptr_state.buttons;
    ptsout[0] = ptr_state.x;
    ptsout[1] = ptr_state.y;
    contrl[2] = 1;
    contrl[4] = 1;
}

/* ---------------------------------------------------------------------- */
/* text                                                                   */
/* ---------------------------------------------------------------------- */

/* Expand the 1bpp font strip into 4bpp glyph MASKS in VRAM: $F where ink,
 * $0 where paper.  One mask serves every ink colour -- see draw_glyph().
 *
 * Layout mirrors the source strip so a glyph is a single rectangle:
 *   VR_FONT + row*FONT_VSTRIDE + ch*FONT_BYTES, 4 bytes wide, 8 rows.
 *
 * Expanding on target rather than shipping a 4bpp blob keeps 2 KB linked
 * instead of 8 KB, which matters when the whole program lives in bank $00.
 */
#define FONT_BYTES   (FONT_W / 2)                 /* 4 bytes per glyph row */
#define FONT_VSTRIDE (256 * FONT_BYTES)           /* 1024 bytes per row    */

/* A nibble-pair for every possible bit-pair, so expansion is a table lookup
 * rather than four conditionals per byte. */
static const uint8_t nib2[4] = { 0x00, 0x0F, 0xF0, 0xFF };

void vdi_font_expand(void)
{
    /* The expanded font is 8 KB at VR_FONT: exactly two 4 KB MEMAC pages, and
     * within a page the layout is contiguous.  Streaming through a window
     * pointer keeps this to 16-bit pointer arithmetic; doing it with a
     * vram_write() per glyph costs 32-bit address maths 2,048 times and took
     * several emulated SECONDS. */
    WORD page, row, ch;
    for (page = 0; page < 2; page++) {
        volatile uint8_t *p = vram_win(VR_FONT + (uint32_t)page * 0x1000);
        for (row = (WORD)(page * 4); row < (WORD)(page * 4 + 4); row++) {
            const uint8_t *sr = &font8x8[row * FONT_STRIDE];
            for (ch = 0; ch < 256; ch++) {
                uint8_t b = sr[ch];
                *p++ = nib2[(b >> 6) & 3];
                *p++ = nib2[(b >> 4) & 3];
                *p++ = nib2[(b >> 2) & 3];
                *p++ = nib2[b & 3];
            }
        }
    }
}

/* Draw one glyph with its top-left at (cx, cy), cx EVEN, cell fully visible.
 *
 * Two blits, and they work for every ink colour including 0:
 *
 *   AND  src=mask, and=$FF, xor=$FF, mode 4
 *        c is $00 where ink and $FF where paper.  Mode 4 is the one mode that
 *        WRITES 0 when c == 0 instead of skipping, so this clears exactly the
 *        ink pixels and leaves the paper ones untouched.
 *   OR   src=mask, and=ink*$11, xor=$00, mode 3
 *        c is the ink colour where ink and 0 where paper; a zero c is skipped,
 *        so only ink pixels are painted.
 *
 * With ink == 0 the OR writes nothing -- and it does not need to, because the
 * AND already left colour 0 there.  That is why no inverted mask is needed,
 * and why mode 6's nibble stencil (which cannot write colour 0) is not used.
 */
static void draw_glyph(WORD ch, WORD cx, WORD cy, WORD ink)
{
    uint32_t src = VR_FONT + (uint32_t)(ch & 0xFF) * FONT_BYTES;
    uint32_t dst = VR_SCREEN0 + (uint32_t)cy * SCR_STRIDE + (uint32_t)(cx >> 1);
    uint8_t  c   = (uint8_t)(((ink & 0x0F) << 4) | (ink & 0x0F));

    blit_mask(src, FONT_VSTRIDE, dst, SCR_STRIDE, FONT_BYTES, FONT_H,
              0xFF, 0xFF, BLT_MODE_AND);
    if (c)
        blit_mask(src, FONT_VSTRIDE, dst, SCR_STRIDE, FONT_BYTES, FONT_H,
                  c, 0x00, BLT_MODE_OR);
}

/* Pixel-by-pixel fallback: odd x, or a glyph the clipping rectangle cuts.
 * Correct everywhere, and about twenty times slower than the blitter path. */
static void draw_glyph_cpu(WORD ch, WORD cx, WORD cy, WORD ink, WORD opaque)
{
    WORD row, col;
    for (row = 0; row < FONT_H; row++) {
        uint8_t b = font8x8[row * FONT_STRIDE + (ch & 0xFF)];
        for (col = 0; col < FONT_W; col++) {
            if (b & (0x80 >> col))
                plot((WORD)(cx + col), (WORD)(cy + row), ink);
            else if (opaque)
                plot((WORD)(cx + col), (WORD)(cy + row), 0);
        }
    }
}

/* v_gtext.  Alignment is left/baseline (vst_alignment is not implemented, so
 * only the default applies): the y given is the BASELINE, and the cell top is
 * y - FONT_TOP.
 *
 * Replace mode paints the cell background first; transparent mode leaves it.
 * GEM has no separate text background colour -- replace mode uses pen 0. */
static void vdi_v_gtext(void)
{
    WORD x = ptsin[0], y = ptsin[1];
    WORD n = contrl[3], i;
    WORD cy = (WORD)(y - FONT_TOP);
    WORD opaque = (vwk.wrt_mode == MD_REPLACE - 1);

    for (i = 0; i < n && i < INTIN_SIZE; i++) {
        WORD cx = (WORD)(x + i * FONT_W);
        WORD fits = (cx >= 0 && cy >= 0 &&
                     cx + FONT_W <= SCR_W && cy + FONT_H <= SCR_H);
        if (fits && vwk.clip)
            fits = (cx >= vwk.xmn_clip && cy >= vwk.ymn_clip &&
                    cx + FONT_W - 1 <= vwk.xmx_clip &&
                    cy + FONT_H - 1 <= vwk.ymx_clip);
        if (fits && (cx & 1) == 0) {
            if (opaque)
                fill_rect_dev(cx, cy, (WORD)(cx + FONT_W - 1),
                              (WORD)(cy + FONT_H - 1), 0);
            draw_glyph(intin[i], cx, cy, vwk.text_color);
            blit_run();
        } else {
            draw_glyph_cpu(intin[i], cx, cy, vwk.text_color, opaque);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* opcodes                                                                */
/* ---------------------------------------------------------------------- */

static void v_nop(void) { }

/* work_out: intout[0..44] then ptsout[0..11].  Values describe this device --
 * a 640x240, 16-colour, non-scalable raster screen with no GDPs yet. */
static void fill_workout(void)
{
    WORD i;
    for (i = 0; i < 45; i++)
        intout[i] = 0;
    for (i = 0; i < 12; i++)
        ptsout[i] = 0;
    intout[0]  = SCR_W - 1;         /* xres - 1 */
    intout[1]  = SCR_H - 1;         /* yres - 1 */
    intout[2]  = 0;                 /* device is precisely scalable */
    intout[3]  = 372;               /* pixel width  in microns (approx) */
    intout[4]  = 372;               /* pixel height in microns */
    intout[5]  = 1;                 /* one character height */
    intout[6]  = 7;                 /* line types */
    intout[7]  = 1;                 /* line widths (1 = continuous) */
    intout[8]  = 6;                 /* marker types */
    intout[9]  = 8;                 /* marker sizes */
    intout[10] = 1;                 /* faces */
    intout[11] = 24;                /* patterns */
    intout[12] = 12;                /* hatches */
    intout[13] = 16;                /* colours available at once */
    intout[14] = 0;                 /* GDPs supported -- none yet */
    intout[35] = 1;                 /* can do colour */
    intout[36] = 0;                 /* no text rotation */
    intout[37] = 1;                 /* can do fill area */
    intout[38] = 0;                 /* no cell array */
    intout[39] = 16;                /* palette: >2 means colour */
    intout[40] = 1;                 /* locators */
    intout[41] = 1;                 /* valuators */
    intout[42] = 1;                 /* choice devices */
    intout[43] = 1;                 /* string devices */
    intout[44] = 2;                 /* workstation type: input/output */
    ptsout[0] = 8;  ptsout[1] = 8;  /* min char width / height */
    ptsout[2] = 8;  ptsout[3] = 8;  /* max char width / height */
    ptsout[4] = 1;  ptsout[5] = 0;  /* min line width */
    ptsout[6] = 1;  ptsout[7] = 0;  /* max line width */
    contrl[2] = 6;
    contrl[4] = 45;
}

static void vdi_v_opnwk(void)
{
    vwk.handle     = 1;
    vwk.clip       = 0;
    vwk.xmn_clip   = 0;
    vwk.ymn_clip   = 0;
    vwk.xmx_clip   = SCR_W - 1;
    vwk.ymx_clip   = SCR_H - 1;
    vwk.wrt_mode   = MD_REPLACE - 1;
    vwk.line_color = 1;
    vwk.line_width = 1;
    vwk.line_index = 1;
    vwk.fill_color = 1;
    vwk.fill_index = 1;
    vwk.fill_style = 1;
    vwk.fill_per   = 1;
    vwk.text_color = 1;
    /* Opening a workstation resets the driver, cursor included: the saved
     * block under the pointer belongs to a screen that no longer applies. */
    cur_hide  = 1;
    cur_drawn = 0;
    sv_nb     = 0;
    fill_workout();
    contrl[6] = vwk.handle;
}

static void vdi_vq_extnd(void)
{
    WORD i;
    if (intin[0] == 0) {
        fill_workout();
        return;
    }
    for (i = 0; i < 45; i++)
        intout[i] = 0;
    for (i = 0; i < 12; i++)
        ptsout[i] = 0;
    intout[0] = 0;                  /* screen type: not a separate buffer */
    intout[1] = 16;                 /* background colours */
    intout[2] = 0;                  /* text effects */
    intout[3] = 0;                  /* scaling: raster, not scalable */
    intout[4] = 4;                  /* PLANES -- the AES reads this one */
    intout[5] = 1;                  /* lookup table present */
    intout[6] = 1;                  /* performance: rough */
    contrl[2] = 6;
    contrl[4] = 45;
}

/* v_clrwk.  If the cursor is up, the block saved underneath it describes a
 * screen that is about to cease to exist, so it must be thrown away rather
 * than restored -- restoring it would stamp stale pixels onto a cleared
 * screen.  The cursor is then repainted so it survives the clear, which is
 * what GEM applications expect. */
static void vdi_v_clrwk(void)
{
    WORD was_drawn = cur_drawn;
    sv_nb = 0;                          /* discard, do not restore */
    cur_drawn = 0;
    blit_fill(VR_SCREEN0, SCR_STRIDE, SCR_STRIDE, SCR_H, 0x00);
    blit_run();
    if (was_drawn && cur_hide == 0)
        cursor_show_now();
}

static void vdi_vs_clip(void)
{
    WORD x1 = ptsin[0], y1 = ptsin[1], x2 = ptsin[2], y2 = ptsin[3];
    vwk.clip = intin[0];
    if (vwk.clip) {
        order(&x1, &x2);
        order(&y1, &y2);
        vwk.xmn_clip = x1;  vwk.ymn_clip = y1;
        vwk.xmx_clip = x2;  vwk.ymx_clip = y2;
    } else {
        vwk.xmn_clip = 0;   vwk.ymn_clip = 0;
        vwk.xmx_clip = SCR_W - 1;
        vwk.ymx_clip = SCR_H - 1;
    }
}

/* vr_recfl -- the AES's workhorse.  It never calls v_fillarea or the bar GDP;
 * every filled box in GEM comes through here. */
static void vdi_vr_recfl(void)
{
    WORD x1 = ptsin[0], y1 = ptsin[1], x2 = ptsin[2], y2 = ptsin[3];
    order(&x1, &x2);
    order(&y1, &y2);
    if (!clip_rect(&x1, &y1, &x2, &y2))
        return;
    if (vwk.fill_index == 0)                    /* hollow: nothing to draw */
        return;
    fill_rect_dev(x1, y1, x2, y2, vwk.fill_color);
    blit_run();
}

/* v_pline.  Horizontal and vertical runs go through the blitter as 1-row and
 * 1-column rectangles; that covers essentially every line the AES draws, since
 * it builds boxes out of axis-aligned polylines and never calls the GDPs.
 * Diagonals fall back to Bresenham through the MEMAC window and are slow. */
static void draw_line(WORD x1, WORD y1, WORD x2, WORD y2)
{
    UWORD mask = line_styles[(vwk.line_index >= 1 && vwk.line_index <= 7)
                             ? vwk.line_index : 1];
    WORD dx, dy, sx, sy, err, e2, bit = 0;

    if (y1 == y2 && mask == 0xFFFF) {
        WORD a = x1, b = x2;
        order(&a, &b);
        if (clip_rect(&a, &y1, &b, &y2)) {
            fill_rect_dev(a, y1, b, y2, vwk.line_color);
            blit_run();
        }
        return;
    }
    if (x1 == x2 && mask == 0xFFFF) {
        WORD a = y1, b = y2;
        order(&a, &b);
        if (clip_rect(&x1, &a, &x2, &b)) {
            fill_rect_dev(x1, a, x2, b, vwk.line_color);
            blit_run();
        }
        return;
    }

    dx = (WORD)(x2 - x1); if (dx < 0) dx = (WORD)-dx;
    dy = (WORD)(y2 - y1); if (dy < 0) dy = (WORD)-dy;
    sx = (WORD)(x1 < x2 ? 1 : -1);
    sy = (WORD)(y1 < y2 ? 1 : -1);
    err = (WORD)(dx - dy);
    for (;;) {
        if (mask & (1u << (15 - (bit & 15))))
            plot(x1, y1, vwk.line_color);
        bit++;
        if (x1 == x2 && y1 == y2)
            break;
        e2 = (WORD)(err << 1);
        if (e2 > -dy) { err = (WORD)(err - dy); x1 = (WORD)(x1 + sx); }
        if (e2 <  dx) { err = (WORD)(err + dx); y1 = (WORD)(y1 + sy); }
    }
}

static void vdi_v_pline(void)
{
    WORD n = contrl[1], i;
    for (i = 0; i + 1 < n; i++)
        draw_line(ptsin[i * 2], ptsin[i * 2 + 1],
                  ptsin[i * 2 + 2], ptsin[i * 2 + 3]);
}

/* attribute setters -- each returns the value actually selected */
static void vdi_vsl_type(void)
{
    WORD v = intin[0];
    if (v < 1 || v > 7) v = 1;
    vwk.line_index = v;
    intout[0] = v;  contrl[4] = 1;
}
static void vdi_vsl_width(void)
{
    vwk.line_width = 1;                 /* only 1 is supported (see work_out) */
    ptsout[0] = 1;  ptsout[1] = 0;  contrl[2] = 1;
}
static void vdi_vsl_color(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 15) v = 1;
    vwk.line_color = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vsf_interior(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 4) v = 0;
    vwk.fill_index = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vsf_style(void)
{
    WORD v = intin[0];
    if (v < 1) v = 1;
    vwk.fill_style = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vsf_color(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 15) v = 1;
    vwk.fill_color = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vst_color(void)
{
    WORD v = intin[0];
    if (v < 0 || v > 15) v = 1;
    vwk.text_color = v;  intout[0] = v;  contrl[4] = 1;
}
static void vdi_vswr_mode(void)
{
    WORD v = intin[0];
    if (v < MD_REPLACE || v > MD_ERASE) v = MD_REPLACE;
    vwk.wrt_mode = (WORD)(v - 1);
    intout[0] = v;  contrl[4] = 1;
}

/* vro_cpyfm -- opaque raster copy.  The AES uses this for icons and for the
 * menu/alert screen save-restore (bb_save / bb_restore), always in mode 3
 * (S_ONLY, replace), which is the only mode implemented here.
 *
 * THE ALIGNMENT PROBLEM.  The VBXE blitter is a byte engine with no shifter,
 * so it can only move 4bpp pixels between positions of the SAME PARITY.  When
 * source and destination x have the same parity the whole copy is one blit;
 * when they differ, every pixel has to move half a byte and the blitter cannot
 * do it at all -- so that case falls back to the CPU through the MEMAC window
 * and is roughly twenty times slower.
 *
 * This is worth knowing before the window manager is designed: a window that
 * only ever moves by an EVEN number of pixels stays on the fast path, and one
 * that moves by an odd number does not.  Snapping window x to even pixels
 * costs nothing visually at 640 wide and keeps every move a pure blit.
 */
static void vdi_vro_cpyfm(void)
{
    WORD sx1 = ptsin[0], sy1 = ptsin[1], sx2 = ptsin[2], sy2 = ptsin[3];
    WORD dx1 = ptsin[4], dy1 = ptsin[5], dx2 = ptsin[6], dy2 = ptsin[7];
    WORD w, h, y;

    order(&sx1, &sx2); order(&sy1, &sy2);
    order(&dx1, &dx2); order(&dy1, &dy2);
    (void)dx2; (void)dy2;

    w = (WORD)(sx2 - sx1 + 1);
    h = (WORD)(sy2 - sy1 + 1);
    if (w <= 0 || h <= 0)
        return;
    /* keep the destination on screen */
    if (dx1 < 0 || dy1 < 0 || dx1 + w > SCR_W || dy1 + h > SCR_H)
        return;
    if (sx1 < 0 || sy1 < 0 || sx2 >= SCR_W || sy2 >= SCR_H)
        return;

    if (((sx1 ^ dx1) & 1) == 0 && (sx1 & 1) == 0 && (w & 1) == 0) {
        /* byte-aligned both ends: one blit */
        blit_copy(VR_SCREEN0 + (uint32_t)sy1 * SCR_STRIDE + (uint32_t)(sx1 >> 1),
                  SCR_STRIDE,
                  VR_SCREEN0 + (uint32_t)dy1 * SCR_STRIDE + (uint32_t)(dx1 >> 1),
                  SCR_STRIDE,
                  (uint16_t)(w >> 1), (uint16_t)h);
        blit_run();
        return;
    }
    /* Unaligned, or an odd width: pixel by pixel through the window.  Copy in
     * the direction that keeps an overlapping move safe. */
    for (y = 0; y < h; y++) {
        WORD sy = (dy1 > sy1) ? (WORD)(sy1 + h - 1 - y) : (WORD)(sy1 + y);
        WORD dy = (dy1 > sy1) ? (WORD)(dy1 + h - 1 - y) : (WORD)(dy1 + y);
        WORD i;
        for (i = 0; i < w; i++) {
            WORD sx = (dx1 > sx1) ? (WORD)(sx1 + w - 1 - i) : (WORD)(sx1 + i);
            WORD dx = (dx1 > sx1) ? (WORD)(dx1 + w - 1 - i) : (WORD)(dx1 + i);
            uint32_t a = VR_SCREEN0 + (uint32_t)sy * SCR_STRIDE + (uint32_t)(sx >> 1);
            uint8_t  v = vram_read8(a);
            plot(dx, dy, (WORD)((sx & 1) ? (v & 0x0F) : (v >> 4)));
        }
    }
}

/* vrt_cpyfm -- transparent raster copy: a ONE-PLANE source expanded into the
 * device's colours.  This is how the AES draws icons and glyph masks.
 *
 * The source form lives in RAM, not VRAM, so the blitter cannot reach it and
 * this is honest CPU work.  That is acceptable: the AES uses it for icons and
 * mouse forms, which are small, and never for anything full-screen.
 *
 * Source bits are MSB-first within each byte, rows are fd_wdwidth WORDS apart
 * -- the VDI's own layout, kept exactly (see the MFDB note in vdi.h).
 */
static void vdi_vrt_cpyfm(void)
{
    MFDB *src = (MFDB *)(uint16_t)contrl[7];   /* forms live in bank $00 */
    WORD mode = intin[0], fg = intin[1], bg = intin[2];
    WORD sx1 = ptsin[0], sy1 = ptsin[1], sx2 = ptsin[2], sy2 = ptsin[3];
    WORD dx1 = ptsin[4], dy1 = ptsin[5];
    const uint8_t *bits;
    WORD w, h, row, col;
    uint16_t stride;

    if (!src || !src->fd_addr)
        return;
    order(&sx1, &sx2); order(&sy1, &sy2);
    w = (WORD)(sx2 - sx1 + 1);
    h = (WORD)(sy2 - sy1 + 1);
    if (w <= 0 || h <= 0)
        return;
    bits   = (const uint8_t *)(uint16_t)src->fd_addr;
    stride = (uint16_t)((uint16_t)src->fd_wdwidth * 2u);   /* wdwidth is WORDS */

    /* NOTE: walk the source with an INCREMENTING pointer.  The obvious form
     *     r = bits + (uint16_t)((sy1 + row) * stride)
     * computed inside the loop produces the wrong address here under Calypsi
     * 5.18 -- row 0 reads correctly and every later row reads unrelated
     * memory.  Reproduced with the offset hoisted into an explicit uint16_t
     * too, so it is not the expression shape.  Not root-caused; the
     * incrementing form below is correct and is what the conformance suite
     * proves.  See docs/phase2b.md. */
    bits += (uint16_t)sy1 * (uint16_t)stride;
    for (row = 0; row < h; row++, bits += (uint16_t)stride) {
        for (col = 0; col < w; col++) {
            WORD sx = (WORD)(sx1 + col);
            uint8_t byte = bits[(uint16_t)sx >> 3];
            WORD on = (byte >> (7 - (sx & 7))) & 1;
            WORD dx = (WORD)(dx1 + col), dy = (WORD)(dy1 + row);
            switch (mode) {
            case MD_TRANS:                      /* fg where set, else leave */
                if (on) plot(dx, dy, fg);
                break;
            case MD_XOR:
                if (on) {
                    uint32_t a = VR_SCREEN0 + (uint32_t)dy * SCR_STRIDE +
                                 (uint32_t)(dx >> 1);
                    uint8_t  v = vram_read8(a);
                    WORD     old = (dx & 1) ? (v & 0x0F) : (v >> 4);
                    plot(dx, dy, (WORD)(old ^ 0x0F));
                }
                break;
            case MD_ERASE:                      /* bg where CLEAR, else leave */
                if (!on) plot(dx, dy, bg);
                break;
            default:                            /* MD_REPLACE: paint both */
                plot(dx, dy, on ? fg : bg);
                break;
            }
        }
    }
}

/* vr_trnfm converts between VDI-standard and device-specific form.  This
 * device has exactly one form, so there is nothing to convert. */
static void vdi_vr_trnfm(void) { }

/* ---------------------------------------------------------------------- */
/* input: modes, vectors, keyboard                                        */
/* ---------------------------------------------------------------------- */

/* Input modes, one per logical device.  1 = request (block until input),
 * 2 = sample (return whatever is there now).  The AES uses sample mode
 * throughout, because it drives its own event loop. */
static WORD in_mode[5] = { 0, 2, 2, 2, 2 };   /* [1]=locator .. [4]=string */

static VDI_VEC vec_motv, vec_butv, vec_curv, vec_timv;
static WORD    last_buttons;

/* Vector exchange.  The new handler arrives in contrl[7..8] and the old one is
 * returned in contrl[9..10] -- the VDI's own convention.  Pointers here are 16
 * bits (small data model), so only contrl[7] and contrl[9] carry address. */
static VDI_VEC vex(VDI_VEC *slot)
{
    VDI_VEC old = *slot;
    *slot = (VDI_VEC)(uint16_t)contrl[7];
    contrl[9]  = (WORD)(uint16_t)old;
    contrl[10] = 0;
    return old;
}

static void vdi_vex_butv(void) { vex(&vec_butv); }
static void vdi_vex_motv(void) { vex(&vec_motv); }
static void vdi_vex_curv(void) { vex(&vec_curv); }
static void vdi_vex_timv(void)
{
    vex(&vec_timv);
    intout[0] = 20;                 /* tick length in ms: 50 Hz PAL frame */
    contrl[4] = 1;
}

static void vdi_vsin_mode(void)
{
    WORD dev = intin[0], mode = intin[1];
    if (dev >= 1 && dev <= 4 && (mode == 1 || mode == 2))
        in_mode[dev] = mode;
    intout[0] = 1;
    contrl[4] = 1;
}

static void vdi_vqin_mode(void)
{
    WORD dev = intin[0];
    intout[0] = (dev >= 1 && dev <= 4) ? in_mode[dev] : 2;
    contrl[4] = 1;
}

/* The keyboard is read straight from POKEY, not through the OS.
 *
 * The OS's CH ($02FC) is filled by the keyboard IRQ, and gem4xe has IRQs
 * switched off (src/crt_atari.s), so CH never updates.  SKSTAT bit 2 is 0
 * while a key is down and KBCODE holds the raw code, which polls fine with no
 * interrupt at all -- the same story as the pointer: polling works today,
 * interrupts would be better. */
#define SKSTAT  (*(volatile uint8_t *)0xD20F)
#define KBCODE  (*(volatile uint8_t *)0xD209)
#define SHFLOK  (*(volatile uint8_t *)0x02BE)   /* OS shadow, still readable */

static WORD kb_last = -1;

static WORD kb_read(void)
{
    uint8_t st = SKSTAT;
    if (st & 0x04) {                /* bit 2 high: no key down */
        kb_last = -1;
        return -1;
    }
    {
        WORD code = (WORD)(KBCODE & 0x3F);
        WORD mods = (WORD)((KBCODE & 0x40) ? 1 : 0);    /* control */
        if ((KBCODE & 0x80) == 0)
            mods |= 2;                                  /* shift  */
        if (code == kb_last)
            return -1;              /* held, not a fresh press */
        kb_last = code;
        return (WORD)(code | (mods << 8));
    }
}

/* vq_key_s -- the AES calls this to learn the modifier state without
 * consuming a keystroke.  Bits: 1 right shift, 2 left shift, 4 control,
 * 8 alt.  The Atari has one shift key, reported as left. */
static void vdi_vq_key_s(void)
{
    uint8_t k = KBCODE;
    WORD st = 0;
    if ((SKSTAT & 0x04) == 0) {
        if ((k & 0x40) == 0) st |= 4;       /* control */
        if ((k & 0x80) == 0) st |= 2;       /* shift (reported as left) */
    }
    if (SHFLOK & 0x40)
        st |= 2;
    intout[0] = st;
    contrl[4] = 1;
}

/* v_string.  Sample mode returns whatever is buffered right now (possibly
 * nothing); request mode would block, which this driver does not do -- the AES
 * uses sample mode throughout and a blocking VDI has nowhere to yield to. */
static void vdi_v_string(void)
{
    WORD c = kb_read();
    if (c < 0) {
        contrl[4] = 0;
        return;
    }
    intout[0] = (WORD)(c & 0xFF);
    contrl[4] = 1;
}

static void vdi_v_choice(void)
{
    intout[0] = 0;
    contrl[4] = 1;
}

/* vsl_udsty -- the user-defined line pattern, which is line type 7.  The AES
 * uses it for rubber-band and drag outlines. */
static void vdi_vsl_udsty(void)
{
    line_styles[7] = (UWORD)intin[0];
}

/* Escape.  Only sub-opcodes 2 and 3 matter to the AES (leave and enter the
 * alpha cursor when switching between text and graphics); this driver is
 * always graphical, so they are genuine no-ops rather than stubs. */
static void vdi_v_escape(void) { }

/* vst_height.  This driver has one fixed 8x8 font, so the requested height is
 * ignored and the actual cell is reported -- which is exactly what the VDI
 * contract asks for: the caller must use what comes back, not what it asked
 * for.  Reported values match ptsout[] in fill_workout(). */
static void vdi_vst_height(void)
{
    ptsout[0] = FONT_W;             /* character width  */
    ptsout[1] = FONT_H;             /* character height */
    ptsout[2] = FONT_W;             /* cell width       */
    ptsout[3] = FONT_H;             /* cell height      */
    contrl[2] = 2;                  /* two POINTS */
}

/* vqt_attributes -- the AES asks for the current text settings before drawing
 * a string, rather than tracking them itself. */
static void vdi_vqt_attributes(void)
{
    intout[0] = 1;                  /* font id: the system font */
    intout[1] = vwk.text_color;
    intout[2] = 0;                  /* rotation: none supported */
    intout[3] = 0;                  /* horizontal alignment: left */
    intout[4] = 0;                  /* vertical alignment: baseline */
    intout[5] = (WORD)(vwk.wrt_mode + 1);
    ptsout[0] = FONT_W;
    ptsout[1] = FONT_H;
    ptsout[2] = FONT_W;
    ptsout[3] = FONT_H;
    contrl[2] = 2;
    contrl[4] = 6;
}

void vdi_input_poll(void)
{
    ptr_poll();
    if (vec_curv)
        vec_curv();
    else
        vdi_cursor_move();
    if (vec_motv)
        vec_motv();
    if (ptr_state.buttons != last_buttons) {
        last_buttons = ptr_state.buttons;
        if (vec_butv)
            vec_butv();
    }
    if (vec_timv)
        vec_timv();
}

/* ---------------------------------------------------------------------- */
/* dispatch                                                               */
/* ---------------------------------------------------------------------- */

typedef void (*VDI_OP)(void);

/* Two flat tables, split exactly as DRI split them: 1..39 and 100..137. */
static const VDI_OP jmptb1[] = {
    vdi_v_opnwk,     /*  1 */  v_nop,           /*  2 v_clswk        */
    vdi_v_clrwk,     /*  3 */  v_nop,           /*  4 v_updwk  (nop) */
    vdi_v_escape,    /*  5 */                   vdi_v_pline,     /*  6 */
    v_nop,           /*  7 v_pmarker */         vdi_v_gtext,     /*  8 */
    v_nop,           /*  9 v_fillarea */        v_nop,           /* 10 cellarray (nop) */
    v_nop,           /* 11 v_gdp */             vdi_vst_height,  /* 12 */
    v_nop,           /* 13 vst_rotation */      v_nop,           /* 14 vs_color */
    vdi_vsl_type,    /* 15 */                   vdi_vsl_width,   /* 16 */
    vdi_vsl_color,   /* 17 */                   v_nop,           /* 18 vsm_type */
    v_nop,           /* 19 vsm_height */        v_nop,           /* 20 vsm_color */
    v_nop,           /* 21 vst_font */          vdi_vst_color,   /* 22 */
    vdi_vsf_interior,/* 23 */                   vdi_vsf_style,   /* 24 */
    vdi_vsf_color,   /* 25 */                   v_nop,           /* 26 vq_color */
    v_nop,           /* 27 vq_cellarray (nop)*/ vdi_v_locator,   /* 28 */
    v_nop,           /* 29 valuator (nop) */    vdi_v_choice,    /* 30 */
    vdi_v_string,    /* 31 */                   vdi_vswr_mode,   /* 32 */
    vdi_vsin_mode,   /* 33 */                   v_nop,           /* 34 (does not exist) */
    v_nop,           /* 35 vql_attributes */    v_nop,           /* 36 vqm_attributes */
    v_nop,           /* 37 vqf_attributes */    vdi_vqt_attributes, /* 38 */
    v_nop            /* 39 vst_alignment */
};

static const VDI_OP jmptb2[] = {
    vdi_v_opnwk,     /* 100 v_opnvwk -- same device, one workstation */
    v_nop,           /* 101 v_clsvwk (nop, as in DRI's own driver) */
    vdi_vq_extnd,    /* 102 */
    v_nop,           /* 103 v_contourfill (nop) */
    v_nop,           /* 104 vsf_perimeter */
    v_nop,           /* 105 v_get_pixel (nop) */
    v_nop,           /* 106 vst_effects */
    v_nop,           /* 107 vst_point */
    v_nop,           /* 108 vsl_ends */
    vdi_vro_cpyfm,   /* 109 */
    vdi_vr_trnfm,    /* 110 */
    vdi_vsc_form,    /* 111 */
    v_nop,           /* 112 vsf_udpat */
    vdi_vsl_udsty,   /* 113 */
    vdi_vr_recfl,    /* 114 */
    vdi_vqin_mode,   /* 115 */
    v_nop,           /* 116 vqt_extent */
    v_nop,           /* 117 vqt_width */
    vdi_vex_timv,    /* 118 */
    v_nop,           /* 119 vst_load_fonts */
    v_nop,           /* 120 vst_unload_fonts */
    vdi_vrt_cpyfm,   /* 121 */
    vdi_v_show_c,    /* 122 */
    vdi_v_hide_c,    /* 123 */
    vdi_vq_mouse,    /* 124 */
    vdi_vex_butv,    /* 125 */
    vdi_vex_motv,    /* 126 */
    vdi_vex_curv,    /* 127 */
    vdi_vq_key_s,    /* 128 */
    vdi_vs_clip      /* 129 */
};

#define N1 ((WORD)(sizeof jmptb1 / sizeof jmptb1[0]))
#define N2 ((WORD)(sizeof jmptb2 / sizeof jmptb2[0]))

void vdi(void)
{
    WORD op = contrl[0];
    contrl[2] = 0;                  /* no points out unless a handler says so */
    contrl[4] = 0;                  /* no ints out   ditto */
    if (op >= 1 && op < 1 + N1)
        jmptb1[op - 1]();
    else if (op >= 100 && op < 100 + N2)
        jmptb2[op - 100]();
}

void vdi_init(void)
{
    contrl[0] = V_OPNWK;
    contrl[1] = 0;
    contrl[3] = 11;
    vdi();
}
