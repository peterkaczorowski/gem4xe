/* abi.c -- the application binary interface: what a COP does.
 *
 * gem_entry() is the DRI entry shim.  The 1984 screen driver's handlers are
 * argument-free and read fixed CONTRL/INTIN/PTSIN arrays; the shim copies
 * the caller's arrays in before dispatch and its results back after
 * (vdi/entry.a86 in the GEM/3 tree; EmuTOS's aes/gemsuper.c xif() does the
 * same for the AES).  gem4xe keeps that discipline for the reason it was
 * invented: the caller's arrays can then be anywhere -- here, anywhere in
 * the 16 MB -- and the handlers never learn.
 *
 * VDI (COP #$73): contrl[0..11], intin[0..contrl[3]-1] and
 * ptsin[0..2*contrl[1]-1] come in; contrl, intout[0..contrl[4]-1] and
 * ptsout[0..2*contrl[2]-1] go back.  The counts are capped at gem4xe's
 * array sizes, never at the caller's: a caller declares the sizes it
 * built, as on the ST.
 *
 * AES (COP #$C8): control[0..4], int_in[0..control[1]-1] and
 * addr_in[0..control[3]-1] come in, int_out[0..control[2]-1] goes back, and
 * global is written on appl_init.  The dispatch is EmuTOS's crysbind()
 * shape with the same int_in/addr_in packing (include/aesdefs.h there):
 * that packing IS the AES binding contract, and the one an application
 * built for GEM expects.  int_out[0] is the return value, 1 for a call that
 * has none -- crysbind's `ret = TRUE` default -- and -1 for an opcode
 * gem4xe does not have.
 *
 * Trees, strings and forms an application passes by address must be in
 * bank $00 -- the AES addresses them near, as the small data model does for
 * everything -- so the loader gives an application a bank-$00 pool
 * (src/sys/app.c).  An address with a bank byte is refused and counted in
 * gem_bad rather than truncated to something that would draw garbage.
 * Message buffers and the parameter block's own arrays are written through
 * far pointers and can be anywhere.
 *
 * GEMDOS (COP #$01): the block is the ST's trap #1 frame with the result
 * in front of it (src/sys/gemdos.h); gemdos_call() reads its arguments
 * through far pointers and writes the result back, so nothing is copied
 * here.
 */
#include "vdi/vdi.h"
#include "aes/aes.h"
#include "aes/proc.h"
#include "sys/abi.h"
#include "sys/gemdos.h"

uint32_t gem_pb;
uint8_t  gem_which;
uint16_t gem_api_sp;
uint8_t  gem_depth;
uint16_t gem_calls;
uint16_t app_calls;             /* of those, the application's: see abi.h */
uint16_t gem_bad;

/* The parameter blocks as they lie in the caller's memory: 32-bit
 * addresses, the ST's layout (src/app/gem.h). */
typedef struct {
    uint32_t contrl, intin, ptsin, intout, ptsout;
} VDIPB_IMG;

typedef struct {
    uint32_t control, global, int_in, int_out, addr_in, addr_out;
} AESPB_IMG;

/* EmuTOS's crysbind sizes (aes/funcdef.h): what an AES call can carry. */
#define C_SIZE  5
#define I_SIZE  16
#define O_SIZE  7
#define AI_SIZE 3

/* AES_VERSION 0x0140 is AES 1.40, TOS 1.04's, which is what EmuTOS reports
 * when built with none of the later extensions (menu popups, 3D objects,
 * window colours) -- gem4xe has none of them either, and the number is a
 * promise about which functions exist. */
#define AES_VERSION 0x0140

/* ---- VDI ---------------------------------------------------------------- */

static void vdi_entry(const VDIPB_IMG __far *pb)
{
    WORD __far *c  = (WORD __far *)pb->contrl;
    WORD __far *ii = (WORD __far *)pb->intin;
    WORD __far *pi = (WORD __far *)pb->ptsin;
    WORD __far *io = (WORD __far *)pb->intout;
    WORD __far *po = (WORD __far *)pb->ptsout;
    WORD k, n;

    for (k = 0; k < CONTRL_SIZE; k++)
        contrl[k] = c[k];
    n = contrl[3];
    if (n > INTIN_SIZE)
        n = INTIN_SIZE;
    for (k = 0; k < n; k++)
        intin[k] = ii[k];
    n = (WORD)(contrl[1] * 2);
    if (n > PTSIN_SIZE)
        n = PTSIN_SIZE;
    for (k = 0; k < n; k++)
        ptsin[k] = pi[k];

    vdi();

    for (k = 0; k < CONTRL_SIZE; k++)
        c[k] = contrl[k];
    n = contrl[4];
    if (n > INTOUT_SIZE)
        n = INTOUT_SIZE;
    for (k = 0; k < n; k++)
        io[k] = intout[k];
    n = (WORD)(contrl[2] * 2);
    if (n > PTSOUT_SIZE)
        n = PTSOUT_SIZE;
    for (k = 0; k < n; k++)
        po[k] = ptsout[k];
}

/* ---- AES ---------------------------------------------------------------- */

/* A near address the caller passed as a int32_t: bank $00 or nothing. */
static void *near_of(int32_t a)
{
    if ((uint32_t)a >> 16) {
        gem_bad++;
        return 0;
    }
    return (void *)(uint16_t)a;
}

/* rsrc_gaddr's answer, for aes_entry to copy to addr_out[0] -- the one
 * AES call that returns an address (the donor's ad_rso). */
static uint32_t ad_rso;

static WORD crysbind(WORD opcode, WORD __far *global, const WORD *int_in,
                     WORD *int_out, const int32_t *addr_in)
{
    OBJECT *tree = 0;
    GRECT clip;
    WORD ret = 1;                   /* TRUE unless the call says otherwise */
    WORD k;

    /* Every op that takes a tree takes it in addr_in[0]. */
    switch (opcode) {
    case 30: case 31: case 32: case 33: case 34:
    case 42: case 43: case 44: case 45: case 46: case 47:
    case 50: case 54: case 55: case 56:
    case 75:
    case 114:
        tree = (OBJECT *)near_of(addr_in[0]);
        if (!tree)
            return -1;
        break;
    default:
        break;
    }

    switch (opcode) {
    /* Application manager */
    case 10:                        /* appl_init */
        global[0] = AES_VERSION;
        global[1] = 1;              /* concurrent applications */
        global[2] = 0;              /* ap_id: the one application */
        for (k = 3; k < 15; k++)
            global[k] = 0;
        global[10] = gl_nplanes;
        ret = proc_pid(rlr);        /* ap_id */
        break;
    case 12:                        /* appl_write: id, len, buffer */
        {
            const WORD __far *m = (const WORD __far *)addr_in[0];
            WORD msg[8];
            for (k = 0; k < 8; k++)
                msg[k] = m[k];
            /* The destination was read and thrown away while there was
             * one process; it is honoured now, and word 1 says who sent
             * it -- which for appl_write is the only place in gem4xe
             * where a real sender exists. */
            msg[1] = proc_pid(rlr);
            mq_put(proc_of(int_in[0]), msg);
        }
        break;
    case 19:                        /* appl_exit */
        break;

    /* Event manager */
    case 20:                        /* evnt_keybd */
        ret = ev_keybd();
        break;
    case 21:                        /* evnt_button: clicks, mask, state */
        ret = ev_button(int_in[0], (UWORD)int_in[1], (UWORD)int_in[2],
                        &int_out[1]);
        break;
    case 22:                        /* evnt_mouse: MOBLK */
        ev_mouse((const MOBLK *)&int_in[0], &int_out[1]);
        break;
    case 23: {                      /* evnt_mesag: buffer */
        WORD __far *m = (WORD __far *)addr_in[0];
        WORD msg[8];
        ev_mesag(msg);
        for (k = 0; k < 8; k++)
            m[k] = msg[k];
        break;
    }
    case 24:                        /* evnt_timer: lo, hi */
        ev_timer((uint32_t)(uint16_t)int_in[0]
                 | ((uint32_t)(uint16_t)int_in[1] << 16));
        break;
    case 25: {                      /* evnt_multi */
        WORD __far *m = (WORD __far *)addr_in[0];
        WORD msg[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        uint32_t ms = 0;
        if (int_in[0] & MU_TIMER)
            ms = (uint32_t)(uint16_t)int_in[14]
               | ((uint32_t)(uint16_t)int_in[15] << 16);
        ret = ev_multi(int_in[0], (const MOBLK *)&int_in[4],
                       (const MOBLK *)&int_in[9], ms,
                       combine_cms(int_in[1], (UWORD)int_in[2],
                                   (UWORD)int_in[3]),
                       msg, &int_out[1]);
        if (ret & MU_MESAG)
            for (k = 0; k < 8; k++)
                m[k] = msg[k];
        break;
    }
    case 26:                        /* evnt_dclick: rate, setit */
        ret = ev_dclick(int_in[0], int_in[1]);
        break;

    /* Menu manager */
    case 30:                        /* menu_bar: tree, showit */
        mn_bar(tree, int_in[0]);
        break;
    case 31:                        /* menu_icheck: tree, item, check */
        ret = do_chg(tree, int_in[0], CHECKED, int_in[1], FALSE, FALSE);
        break;
    case 32:                        /* menu_ienable: tree, item, enable */
        ret = do_chg(tree, int_in[0] & 0x7fff, DISABLED, !int_in[1],
                     (int_in[0] & 0x8000) != 0, FALSE);
        break;
    case 33:                        /* menu_tnormal: tree, title, normal */
        ret = do_chg(tree, int_in[0], SELECTED, !int_in[1], TRUE, TRUE);
        break;
    case 34: {                      /* menu_text: tree, item, text */
        const char *s = (const char *)near_of(addr_in[1]);
        if (!s)
            return -1;
        mn_text(tree, int_in[0], s);
        break;
    }
    case 35: {                      /* menu_register: pid, string */
        const char *s = (const char *)near_of(addr_in[0]);
        if (!s)
            return -1;
        ret = mn_register(int_in[0], s);
        break;
    }

    /* Object manager */
    case 42:                        /* objc_draw: start, depth, clip */
        clip.g_x = int_in[2]; clip.g_y = int_in[3];
        clip.g_w = int_in[4]; clip.g_h = int_in[5];
        objc_draw(tree, int_in[0], int_in[1], &clip);
        break;
    case 43:                        /* objc_find: start, depth, mx, my */
        ret = objc_find(tree, int_in[0], int_in[1], int_in[2], int_in[3]);
        break;
    case 44:                        /* objc_offset: obj */
        objc_offset(tree, int_in[0], &int_out[1], &int_out[2]);
        break;
    case 45:                        /* objc_order: obj, newpos */
        ret = ob_order(tree, int_in[0], int_in[1]);
        break;
    case 46: {                      /* objc_edit: obj, char, idx, kind */
        WORD idx = int_in[2];
        gsx_sclip(&gl_rfull);
        ret = objc_edit(tree, int_in[0], int_in[1], &idx, int_in[3]);
        int_out[1] = idx;
        break;
    }
    case 47:                        /* objc_change: obj, -, clip, state, redraw */
        clip.g_x = int_in[2]; clip.g_y = int_in[3];
        clip.g_w = int_in[4]; clip.g_h = int_in[5];
        objc_change(tree, int_in[0], &clip, (UWORD)int_in[6], int_in[7]);
        break;

    /* Form manager */
    case 50:                        /* form_do: start */
        ret = form_do(tree, int_in[0]);
        break;
    case 51: {                      /* form_dial: type, pi, pt */
        GRECT pi, pt;
        pi.g_x = int_in[1]; pi.g_y = int_in[2];
        pi.g_w = int_in[3]; pi.g_h = int_in[4];
        pt.g_x = int_in[5]; pt.g_y = int_in[6];
        pt.g_w = int_in[7]; pt.g_h = int_in[8];
        ret = form_dial(int_in[0], &pi, &pt);
        break;
    }
    case 52: {                      /* form_alert: defbut, string */
        const char *s = (const char *)near_of(addr_in[0]);
        if (!s)
            return -1;
        ret = fm_alert(int_in[0], s);
        break;
    }
    case 53:                        /* form_error: number */
        ret = fm_error(int_in[0]);
        break;
    case 54:                        /* form_center */
        ob_center(tree, &clip);
        int_out[1] = clip.g_x; int_out[2] = clip.g_y;
        int_out[3] = clip.g_w; int_out[4] = clip.g_h;
        break;
    case 55: {                      /* form_keybd: obj, char, nxtob */
        WORD ch = int_in[1], nxt = int_in[2];
        gsx_sclip(&gl_rfull);
        ret = form_keybd(tree, int_in[0], &ch, &nxt);
        int_out[1] = nxt;
        int_out[2] = ch;
        break;
    }
    case 56: {                      /* form_button: obj, clks */
        WORD nxt = 0;
        gsx_sclip(&gl_rfull);
        ret = form_button(tree, int_in[0], int_in[1], &nxt);
        int_out[1] = nxt;
        break;
    }

    /* Graphics manager */
    case 70:                        /* graf_rubbox */
        gr_rubbox(int_in[0], int_in[1], int_in[2], int_in[3],
                  &int_out[1], &int_out[2]);
        break;
    case 71: {                      /* graf_dragbox */
        GRECT pc;
        pc.g_x = int_in[4]; pc.g_y = int_in[5];
        pc.g_w = int_in[6]; pc.g_h = int_in[7];
        gr_dragbox(int_in[0], int_in[1], int_in[2], int_in[3], &pc,
                   &int_out[1], &int_out[2]);
        break;
    }
    case 73:                        /* graf_growbox */
    case 74: {                      /* graf_shrinkbox */
        GRECT pi, pt;
        pi.g_x = int_in[0]; pi.g_y = int_in[1];
        pi.g_w = int_in[2]; pi.g_h = int_in[3];
        pt.g_x = int_in[4]; pt.g_y = int_in[5];
        pt.g_w = int_in[6]; pt.g_h = int_in[7];
        if (opcode == 73)
            gr_growbox(&pi, &pt);
        else
            gr_shrinkbox(&pi, &pt);
        break;
    }
    case 75:                        /* graf_watchbox: -, obj, in, out */
        ret = gr_watchbox(tree, int_in[1], int_in[2], int_in[3]);
        break;
    case 77:                        /* graf_handle */
        int_out[1] = gl_wchar;
        int_out[2] = gl_hchar;
        int_out[3] = gl_wbox;
        int_out[4] = gl_hbox;
        ret = gl_handle;
        break;
    case 78: {                      /* graf_mouse: mode, form */
        const WORD *form = 0;
        if (int_in[0] == USER_DEF) {
            form = (const WORD *)near_of(addr_in[0]);
            if (!form)
                return -1;
        }
        gr_mouse(int_in[0], form);
        break;
    }
    case 79:                        /* graf_mkstate */
        gr_mkstate(&int_out[1], &int_out[2], &int_out[3], &int_out[4]);
        break;

    /* Window manager */
    case 100:                       /* wind_create: kind, rect */
        clip.g_x = int_in[1]; clip.g_y = int_in[2];
        clip.g_w = int_in[3]; clip.g_h = int_in[4];
        ret = wm_create(int_in[0], &clip);
        break;
    case 101:                       /* wind_open: handle, rect */
        clip.g_x = int_in[1]; clip.g_y = int_in[2];
        clip.g_w = int_in[3]; clip.g_h = int_in[4];
        ret = wm_open(int_in[0], &clip);
        break;
    case 102:                       /* wind_close */
        ret = wm_close(int_in[0]);
        break;
    case 103:                       /* wind_delete */
        ret = wm_delete(int_in[0]);
        break;
    case 104:                       /* wind_get: handle, field */
        ret = wm_get(int_in[0], int_in[1], &int_out[1], &int_in[2]);
        break;
    case 105: {                     /* wind_set: handle, field, words */
        WORD w[4];
        for (k = 0; k < 4; k++)
            w[k] = int_in[2 + k];
        ret = wm_set(int_in[0], int_in[1], w);
        break;
    }
    case 106:                       /* wind_find: x, y */
        ret = wm_find(int_in[0], int_in[1]);
        break;
    case 107:                       /* wind_update */
        wm_update(int_in[0]);
        break;
    case 108:                       /* wind_calc */
        wm_calc(int_in[0], (UWORD)int_in[1], int_in[2], int_in[3],
                int_in[4], int_in[5],
                &int_out[1], &int_out[2], &int_out[3], &int_out[4]);
        break;

    /* File selector: the path and name are the application's buffers,
     * the button its word. */
    case 90:                        /* fsel_input: path, sel */
    case 91: {                      /* fsel_exinput: path, sel, label */
        char *path = near_of(addr_in[0]);
        char *sel = near_of(addr_in[1]);
        const char *label = (opcode == 91) ? near_of(addr_in[2]) : 0;
        if (path && sel && (opcode == 90 || label))
            ret = fs_input(path, sel, &int_out[1], label);
        else
            ret = 0;
        break;
    }

    /* Resource library.  The global's ap_ptree / ap_rscmem / ap_rsclen
     * (words 5-6, 7-8, 9) are what the donor's rs_load leaves there; the
     * addresses are bank $00, so the high words are 0. */
    case 110: {                     /* rsrc_load: name */
        const char *name = near_of(addr_in[0]);
        ret = name ? rs_load(name) : 0;
        if (ret) {
            global[5] = (WORD)((uint16_t)rs_loaded() + rs_loaded()->rsh_trindex);
            global[6] = 0;
            global[7] = (WORD)(uint16_t)rs_loaded();
            global[8] = 0;
            global[9] = (WORD)rs_loaded()->rsh_rssize;
        }
        break;
    }
    case 111:                       /* rsrc_free */
        ret = rs_free();
        for (k = 5; k < 10; k++)
            global[k] = 0;
        break;
    case 112:                       /* rsrc_gaddr: type, index -> addr_out */
        ret = rs_gaddr((UWORD)int_in[0], (UWORD)int_in[1], &ad_rso);
        break;
    case 113:                       /* rsrc_saddr: type, index, addr */
        ret = rs_saddr((UWORD)int_in[0], (UWORD)int_in[1], (uint32_t)addr_in[0]);
        break;
    case 114:                       /* rsrc_obfix: tree, object */
        rs_obfix(tree, int_in[0]);
        break;

    /* Shell library */
    case 120: {                     /* shel_read: cmd, tail */
        char *cmd = near_of(addr_in[0]);
        char *tail = near_of(addr_in[1]);
        if (cmd && tail)
            sh_read(cmd, tail);
        else
            ret = 0;
        break;
    }
    case 121: {                     /* shel_write: doex, isgr, iscr, cmd, tail */
        const char *cmd = near_of(addr_in[0]);
        const char *tail = near_of(addr_in[1]);
        if (int_in[0] == 1 && !(cmd && tail))
            ret = 0;
        else
            ret = sh_write(int_in[0], int_in[1], int_in[2], cmd, tail);
        break;
    }
    case 122: {                     /* shel_get: buffer, len -- the buffer
                                     * may be far: the AES only copies bytes */
        uint32_t buf = (uint32_t)addr_in[0];
        if (buf)
            sh_get(buf, int_in[0]);
        else
            ret = 0;
        break;
    }
    case 123: {                     /* shel_put: data, len */
        uint32_t buf = (uint32_t)addr_in[0];
        if (buf)
            sh_put(buf, int_in[0]);
        else
            ret = 0;
        break;
    }
    case 124: {                     /* shel_find: path (unchanged here) */
        char *path = near_of(addr_in[0]);
        ret = path ? sh_find(path) : 0;
        break;
    }
    case 125: {                     /* shel_envrn: &value, name */
        const char **pp = near_of(addr_in[0]);
        const char *name = near_of(addr_in[1]);
        if (pp && name)
            sh_envrn(pp, name);
        else
            ret = 0;
        break;
    }

    default:
        gem_bad++;
        ret = -1;
        break;
    }
    return ret;
}

static void aes_entry(const AESPB_IMG __far *pb)
{
    const WORD __far *ctl = (const WORD __far *)pb->control;
    const WORD __far *ii  = (const WORD __far *)pb->int_in;
    const int32_t __far *ai  = (const int32_t __far *)pb->addr_in;
    WORD __far *io = (WORD __far *)pb->int_out;
    WORD control[C_SIZE];
    WORD int_in[I_SIZE];
    WORD int_out[O_SIZE];
    int32_t addr_in[AI_SIZE];
    WORD k, n;

    for (k = 0; k < C_SIZE; k++)
        control[k] = ctl[k];
    for (k = 0; k < I_SIZE; k++)
        int_in[k] = 0;
    for (k = 0; k < O_SIZE; k++)
        int_out[k] = 0;
    for (k = 0; k < AI_SIZE; k++)
        addr_in[k] = 0;
    n = control[1];
    if (n > I_SIZE)
        n = I_SIZE;
    for (k = 0; k < n; k++)
        int_in[k] = ii[k];
    n = control[3];
    if (n > AI_SIZE)
        n = AI_SIZE;
    for (k = 0; k < n; k++)
        addr_in[k] = ai[k];

    int_out[0] = crysbind(control[0], (WORD __far *)pb->global,
                          int_in, int_out, addr_in);

    n = control[2];
    if (n > O_SIZE)
        n = O_SIZE;
    for (k = 0; k < n; k++)
        io[k] = int_out[k];
    if (control[0] == 112 && control[4] > 0)
        *(uint32_t __far *)pb->addr_out = ad_rso;
}

/* ---- the entry ---------------------------------------------------------- */

void gem_entry(void)
{
    gem_calls++;
    /* ...and the application's own count, which is what a gate means when
     * it asks "which call is the desktop inside".  gem_calls counts every
     * process's, and the moment an accessory was resident that stopped
     * identifying anybody's progress: test-boot put the desktop five
     * calls past its first wait, which is exactly the accessory's
     * appl_init, rsrc_load, rsrc_gaddr, menu_register and evnt_mesag. */
    if (rlr == proc_app)
        app_calls++;
    switch (gem_which) {
    case ABI_VDI:
        vdi_entry((const VDIPB_IMG __far *)gem_pb);
        break;
    case ABI_AES:
        aes_entry((const AESPB_IMG __far *)gem_pb);
        break;
    case ABI_GEMDOS:
        gemdos_call(gem_pb);
        break;
    default:
        gem_bad++;
        break;
    }
}
