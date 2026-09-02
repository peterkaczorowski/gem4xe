/* m3_vdi.c -- VDI conformance runner.
 *
 * Brings up the VBXE surface and a VDI workstation, then executes scripts of
 * VDI calls that the host pokes into vdi_script[] over the Altirra bridge.
 * The host runs the identical script through tools/vdiref.py and the two
 * framebuffers are compared pixel for pixel.
 *
 * Poking scripts rather than compiling them in is what makes the suite worth
 * having: a new case costs a line of Python, not a rebuild and a reflash.
 * This is the pattern vbxetxtadv's conformance_test.py established, and the
 * plan calls it the single most valuable thing in that rig.
 *
 * Script format, all 16-bit little-endian:
 *     WORD opcode        (0 ends the script)
 *     WORD n_pts         number of POINTS in ptsin (so 2*n words follow)
 *     WORD n_int         number of words in intin
 *     WORD contrl[7..10] four words: the two MFDB pointers, 0 when unused
 *     WORD ptsin[2*n_pts]
 *     WORD intin[n_int]
 *
 * An opcode of 1000+n is an AES call rather than a VDI one, using GEM's own
 * AES function numbers (objc_draw = 42, objc_find = 43).  The object tree
 * address travels in the contrl[7] slot, the same place an MFDB does.
 *
 * vdi_scratch[] is where the host stages MFDBs and their bitmaps, so a case
 * can pass a real source form without anything being compiled in.
 */
#include "vdi/vdi.h"
#include "vdi/pointer.h"
#include "aes/aes.h"
#include "sys/farmem.h"
#include "sys/rapidus.h"
#include "vbxe/vbxe.h"

#define STATUS ((volatile unsigned char *) 0x0600)

/* STATUS[0..1] 'V','D'   STATUS[2] stage   STATUS[3] go   STATUS[4] done
 * The host writes STATUS[3]; the runner answers on STATUS[4].              */
#define ST_STAGE 2
#define ST_GO    3
#define ST_DONE  4

/* Both buffers are host-poked staging, so they go in the `teststage` section,
 * which src/gem4xe.scm gives a memory of its own.  Naming the section means
 * nothing else can end up in the region the host pokes, and it keeps the
 * runner's map identical to the driver's everywhere else. */
/* From src/farload.s -- reports the bank the far code is running in. */
extern unsigned int _fl_running_bank(void);

#define SCRIPT_WORDS 1024
__attribute__((section("teststage")))
volatile WORD vdi_script[SCRIPT_WORDS];

#define SCRATCH_BYTES 2048
__attribute__((section("teststage")))
volatile unsigned char vdi_scratch[SCRATCH_BYTES];

/* Per-call output record, so the harness can check what an opcode RETURNS and
 * not only what it draws.  Without this the input and inquiry opcodes -- most
 * of what is left for the AES -- would be untestable.
 *   [0] contrl[2]  [1] contrl[4]  [2..16] intout[0..14]  [17..19] ptsout[0..2]
 * Fifteen intout words because evnt_multi returns seven and then the
 * eight-word message a MU_MESAG delivered (tools/vdiref.py RESULT_WORDS). */
#define RESULT_WORDS 20
#define RESULT_INTOUT 15
#define MAX_RESULTS  48
__attribute__((section("teststage")))
volatile WORD vdi_results[MAX_RESULTS * RESULT_WORDS];
__attribute__((section("teststage")))
volatile WORD vdi_result_count;


static void run_script(void)
{
    WORD i = 0;
    vdi_result_count = 0;
    while (i < SCRIPT_WORDS) {
        WORD op    = vdi_script[i++];
        WORD npts, nint, k;
        if (op == 0)
            return;
        npts = vdi_script[i++];
        nint = vdi_script[i++];
        contrl[7]  = vdi_script[i++];
        contrl[8]  = vdi_script[i++];
        contrl[9]  = vdi_script[i++];
        contrl[10] = vdi_script[i++];
        for (k = 0; k < npts * 2 && k < PTSIN_SIZE; k++)
            ptsin[k] = vdi_script[i++];
        for (k = 0; k < nint && k < INTIN_SIZE; k++)
            intin[k] = vdi_script[i++];
        if (op >= 1000) {
            OBJECT *tree = (OBJECT *)(uint16_t)contrl[7];
            GRECT clip;
            WORD c2 = 0, c4 = 0;
            /* The AES calls the VDI underneath, which sets contrl[2]/[4]
             * and intout[0] for its own calls; the record wants what the
             * AES op declares, so its counts are set after it returns and
             * its outputs go through a local first. */
            WORD out[8] = {0, 0, 0, 0, 0, 0, 0, 0}; /* what an op leaves alone reads 0 */
            /* AES ops are 1000 + the AES function number, with the tree
             * address in the contrl[7] slot, the clip in ptsin and the
             * scalars in intin.  Results land in intout/ptsout so the
             * record below captures them like a VDI call's.  Op 1000 is
             * the AES's own start-up: it belongs in the script so that the
             * AES's attribute cache and the workstation it caches are set
             * up together, on both sides, every case. */
            switch (op - 1000) {
            case 0:                         /* gsx_start, events, windows, menus */
                gsx_start();
                ev_init();
                wm_init();
                mn_init();
                break;
            case 12:                        /* appl_write: id, len, msg[8] */
                mq_put(&intin[2]);
                intout[0] = 1;
                c4 = 1;
                break;
            /* The event calls block until the host injects input; the host
             * knows one is in progress because vdi_result_count has reached
             * this op's index and not passed it.  Results follow the AES's
             * int_out layout: [0] the return value, then the outputs. */
            case 20:                        /* evnt_keybd */
                intout[0] = ev_keybd();
                c4 = 1;
                break;
            case 21:                        /* evnt_button */
                out[0] = ev_button(intin[0], (UWORD)intin[1],
                                   (UWORD)intin[2], &out[1]);
                for (k = 0; k < 5; k++)
                    intout[k] = out[k];
                c4 = 5;
                break;
            case 22:                        /* evnt_mouse */
                out[0] = ev_mouse((const MOBLK *)&intin[0], &out[1]);
                for (k = 0; k < 5; k++)
                    intout[k] = out[k];
                c4 = 5;
                break;
            case 23:                        /* evnt_mesag: the message */
                ev_mesag(out);
                for (k = 0; k < 8; k++)
                    intout[k] = out[k];
                c4 = 8;
                break;
            case 24: {                      /* evnt_timer: lo, hi */
                uint32_t ms = (uint32_t)(uint16_t)intin[0]
                            | ((uint32_t)(uint16_t)intin[1] << 16);
                intout[0] = ev_timer(ms);
                c4 = 1;
                break;
            }
            case 25: {                      /* evnt_multi, GEM int_in order:
                                             * flags clicks mask state
                                             * MOBLK1[5] MOBLK2[5] lo hi */
                uint32_t ms = (uint32_t)(uint16_t)intin[14]
                            | ((uint32_t)(uint16_t)intin[15] << 16);
                WORD msg[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                intout[0] = ev_multi(intin[0], (const MOBLK *)&intin[4],
                                     (const MOBLK *)&intin[9], ms,
                                     combine_cms(intin[1], (UWORD)intin[2],
                                                 (UWORD)intin[3]),
                                     msg, out);
                for (k = 0; k < 6; k++)
                    intout[1 + k] = out[k];
                /* the message, if one was delivered, else zeros */
                for (k = 0; k < 8; k++)
                    intout[7 + k] = msg[k];
                c4 = 15;
                break;
            }
            case 26:                        /* evnt_dclick */
                intout[0] = ev_dclick(intin[0], intin[1]);
                c4 = 1;
                break;
            /* The menu library, AES 30..35, the tree in contrl[7] as for
             * the object calls.  menu_text's string is at intin[1], a
             * bank-$00 address the host staged. */
            case 30:                        /* menu_bar: showit */
                mn_bar(tree, intin[0]);
                intout[0] = 1;
                c4 = 1;
                break;
            case 31:                        /* menu_icheck: item, check */
                intout[0] = do_chg(tree, intin[0], CHECKED, intin[1],
                                   FALSE, FALSE);
                c4 = 1;
                break;
            case 32:                        /* menu_ienable: item, enable */
                intout[0] = do_chg(tree, intin[0] & 0x7fff, DISABLED,
                                   !intin[1], (intin[0] & 0x8000) != 0,
                                   FALSE);
                c4 = 1;
                break;
            case 33:                        /* menu_tnormal: title, normal */
                intout[0] = do_chg(tree, intin[0], SELECTED, !intin[1],
                                   TRUE, TRUE);
                c4 = 1;
                break;
            case 34:                        /* menu_text: item, text addr */
                mn_text(tree, intin[0], (const char *)(uint16_t)intin[1]);
                intout[0] = 1;
                c4 = 1;
                break;
            case 35:                        /* menu_register: pid, str addr */
                intout[0] = mn_register(intin[0],
                                        (const char *)(uint16_t)intin[1]);
                c4 = 1;
                break;
            case 42:                        /* objc_draw */
                clip.g_x = ptsin[0]; clip.g_y = ptsin[1];
                clip.g_w = ptsin[2]; clip.g_h = ptsin[3];
                objc_draw(tree, intin[0], intin[1], &clip);
                break;
            case 43:                        /* objc_find */
                intout[0] = objc_find(tree, intin[0], intin[1],
                                      ptsin[0], ptsin[1]);
                c4 = 1;
                break;
            case 44:                        /* objc_offset */
                objc_offset(tree, intin[0], &intout[0], &intout[1]);
                c4 = 2;
                break;
            case 46: {                      /* objc_edit */
                WORD idx = intin[2];
                intout[1] = objc_edit(tree, intin[0], intin[1], &idx, intin[3]);
                intout[0] = idx;
                c4 = 2;
                break;
            }
            case 47:                        /* objc_change */
                clip.g_x = ptsin[0]; clip.g_y = ptsin[1];
                clip.g_w = ptsin[2]; clip.g_h = ptsin[3];
                objc_change(tree, intin[0], &clip, (UWORD)intin[1], intin[2]);
                break;
            case 50:                        /* form_do */
                intout[0] = form_do(tree, intin[0]);
                c4 = 1;
                break;
            case 51: {                      /* form_dial: type, pi, pt */
                GRECT pi, pt;
                pi.g_x = intin[1]; pi.g_y = intin[2];
                pi.g_w = intin[3]; pi.g_h = intin[4];
                pt.g_x = intin[5]; pt.g_y = intin[6];
                pt.g_w = intin[7]; pt.g_h = intin[8];
                intout[0] = form_dial(intin[0], &pi, &pt);
                c4 = 1;
                break;
            }
            case 54:                        /* form_center (ob_center) */
                ob_center(tree, &clip);
                intout[0] = clip.g_x; intout[1] = clip.g_y; intout[2] = clip.g_w;
                ptsout[0] = clip.g_h; ptsout[1] = 0;
                c4 = 3;
                c2 = 1;
                break;
            case 55: {                      /* form_keybd: obj, char, nxtob */
                WORD ch = intin[1], nxt = intin[2];
                intout[0] = form_keybd(tree, intin[0], &ch, &nxt);
                intout[1] = ch;
                intout[2] = nxt;
                c4 = 3;
                break;
            }
            case 56: {                      /* form_button: obj, clks */
                WORD nxt = 0;
                intout[0] = form_button(tree, intin[0], intin[1], &nxt);
                intout[1] = nxt;
                c4 = 2;
                break;
            }
            case 70:                        /* graf_rubbox: x,y,minw,minh */
                gr_rubbox(intin[0], intin[1], intin[2], intin[3],
                          &out[0], &out[1]);
                intout[0] = 1;
                intout[1] = out[0];
                intout[2] = out[1];
                c4 = 3;
                break;
            case 71: {                      /* graf_dragbox: w,h,sx,sy,bound */
                GRECT pc;
                pc.g_x = intin[4]; pc.g_y = intin[5];
                pc.g_w = intin[6]; pc.g_h = intin[7];
                gr_dragbox(intin[0], intin[1], intin[2], intin[3], &pc,
                           &out[0], &out[1]);
                intout[0] = 1;
                intout[1] = out[0];
                intout[2] = out[1];
                c4 = 3;
                break;
            }
            case 73:                        /* graf_growbox: pi, pt */
            case 74: {                      /* graf_shrinkbox */
                GRECT pi, pt;
                pi.g_x = intin[0]; pi.g_y = intin[1];
                pi.g_w = intin[2]; pi.g_h = intin[3];
                pt.g_x = intin[4]; pt.g_y = intin[5];
                pt.g_w = intin[6]; pt.g_h = intin[7];
                if (op - 1000 == 73)
                    gr_growbox(&pi, &pt);
                else
                    gr_shrinkbox(&pi, &pt);
                intout[0] = 1;
                c4 = 1;
                break;
            }
            case 75:                        /* graf_watchbox: obj, in, out */
                intout[0] = gr_watchbox(tree, intin[0], intin[1], intin[2]);
                c4 = 1;
                break;
            case 79:                        /* graf_mkstate */
                gr_mkstate(&out[0], &out[1], &out[2], &out[3]);
                intout[0] = 1;
                for (k = 0; k < 4; k++)
                    intout[1 + k] = out[k];
                c4 = 5;
                break;
            /* The window manager, in GEM int_in order.  Rectangles are
             * four words; wind_get returns 1 then its four words; wind_set
             * takes the field's words as they stand in intin, WF_NAME and
             * WF_NEWDESK with their address high word first as on the
             * 68000 (only the low word can mean anything here). */
            case 100: {                     /* wind_create: kind, rect */
                GRECT t;
                t.g_x = intin[1]; t.g_y = intin[2];
                t.g_w = intin[3]; t.g_h = intin[4];
                intout[0] = wm_create(intin[0], &t);
                c4 = 1;
                break;
            }
            case 101: {                     /* wind_open: wh, rect */
                GRECT t;
                t.g_x = intin[1]; t.g_y = intin[2];
                t.g_w = intin[3]; t.g_h = intin[4];
                intout[0] = wm_open(intin[0], &t);
                c4 = 1;
                break;
            }
            case 102:                       /* wind_close: wh */
                intout[0] = wm_close(intin[0]);
                c4 = 1;
                break;
            case 103:                       /* wind_delete: wh */
                intout[0] = wm_delete(intin[0]);
                c4 = 1;
                break;
            case 104:                       /* wind_get: wh, field */
                intout[0] = wm_get(intin[0], intin[1], out, &intin[2]);
                for (k = 0; k < 4; k++)
                    intout[1 + k] = out[k];
                c4 = 5;
                break;
            case 105:                       /* wind_set: wh, field, words */
                intout[0] = wm_set(intin[0], intin[1], &intin[2]);
                c4 = 1;
                break;
            case 106:                       /* wind_find: x, y */
                intout[0] = wm_find(intin[0], intin[1]);
                c4 = 1;
                break;
            case 107:                       /* wind_update: what */
                wm_update(intin[0]);
                intout[0] = 1;
                c4 = 1;
                break;
            case 108:                       /* wind_calc: type, kind, rect */
                wm_calc(intin[0], (UWORD)intin[1], intin[2], intin[3],
                        intin[4], intin[5], &out[0], &out[1], &out[2], &out[3]);
                intout[0] = 1;
                for (k = 0; k < 4; k++)
                    intout[1 + k] = out[k];
                c4 = 5;
                break;
            default:
                break;
            }
            contrl[2] = c2;
            contrl[4] = c4;
        } else {
        contrl[0] = op;
        contrl[1] = npts;
        contrl[3] = nint;
        contrl[6] = 1;
        vdi();
        }
        if (vdi_result_count < MAX_RESULTS) {
            volatile WORD *r = &vdi_results[vdi_result_count * RESULT_WORDS];
            /* Only the words the call DECLARES are meaningful.  The VDI
             * contract is that intout/ptsout are valid up to contrl[4] and
             * contrl[2]; anything beyond is leftover from an earlier call and
             * a caller must not read it.  Recording it verbatim would make the
             * harness stricter than the contract and fail correct code. */
            r[0] = contrl[2];
            r[1] = contrl[4];
            for (k = 0; k < RESULT_INTOUT; k++)
                r[2 + k] = (WORD)(contrl[4] > k ? intout[k] : 0);
            r[2 + RESULT_INTOUT]     = (WORD)(contrl[2] > 0 ? ptsout[0] : 0);
            r[2 + RESULT_INTOUT + 1] = (WORD)(contrl[2] > 0 ? ptsout[1] : 0);
            r[2 + RESULT_INTOUT + 2] = (WORD)(contrl[2] > 1 ? ptsout[2] : 0);
            vdi_result_count++;
        }
    }
}

__task void main(void)
{
    STATUS[0] = 'V';
    STATUS[1] = 'D';
    STATUS[ST_STAGE] = 0;
    STATUS[ST_GO] = 0;
    STATUS[ST_DONE] = 0;

    /* First, before the MEMAC window exists and before anything is timed:
     * switch the accelerator's SRAM in over bank $00.  Reported so the
     * harness can refuse a target that is still running its data at
     * 1.79 MHz -- six phases did, unnoticed. */
    rapidus_speedup();
    STATUS[25] = rapidus.present;
    STATUS[26] = rapidus.mcr_before;
    STATUS[27] = rapidus.mcr_after;
    STATUS[28] = rapidus.cmcr_after;
    STATUS[29] = rapidus.synced;

    if (!vbxe_detect()) {
        STATUS[ST_STAGE] = 0xEE;
        for (;;)
            ;
    }
    /* A blit list started in uninitialised VRAM ($FF) never stops, because the
     * "next" bit is always set.  Clear the control region before first use. */
    vram_fill(VR_XDL, 0x00, 0x1000);
    vbxe_xdl_hr(VR_SCREEN0);
    vdi_font_expand();          /* 1bpp GEM font -> 4bpp masks in VRAM */
    vdi_init();
    /* No pointing device: the harness IS the pointer.  It writes ptr_state
     * directly (position and buttons), and PTR_NONE keeps ptr_poll() from
     * overwriting that with a POT or joystick read.  v_locator still warps. */
    ptr_init(PTR_NONE, SCR_W / 2, SCR_H / 2);

    /* Discover linear RAM above bank $00.  Reported at STATUS[16..23] so the
     * harness can check what was actually found on this machine. */
    farmem_probe();
    STATUS[16] = farmem.kind;
    STATUS[17] = farmem.first_bank;
    STATUS[18] = farmem.last_bank;
    STATUS[19] = farmem.banks;
    STATUS[20] = (unsigned char)(farmem.bytes >> 16);
    STATUS[21] = (unsigned char)(farmem.bytes >> 24);
    {   /* prove it is really usable: a round trip near the top of the range */
        uint32_t a = far_alloc(4096);
        unsigned char ok = 0;
        if (a) {
            far_write8(a, 0xC3);
            far_write8(a + 4095, 0x3C);
            ok = (unsigned char)((far_read8(a) == 0xC3) &&
                                 (far_read8(a + 4095) == 0x3C));
        }
        STATUS[22] = ok;
        STATUS[23] = (unsigned char)(a >> 16);
    }

    /* Which bank is this code actually executing in?  src/farload.s copies it
     * up at load time and nothing else can confirm that it landed: the bridge
     * reports a 16-bit PC and no K register. */
    STATUS[24] = (unsigned char)_fl_running_bank();

    /* Start from a known screen: pen 0 (white), as a GEM desktop would. */
    blit_fill(VR_SCREEN0, SCR_STRIDE, SCR_STRIDE, SCR_H, 0x00);
    blit_run();

    /* Touch the scratch area so the linker keeps it: nothing on the target
     * references it -- the host is the only writer. */
    vdi_scratch[0] = 0;
    vdi_results[0] = 0;
    STATUS[ST_STAGE] = 1;                       /* ready for scripts */

    for (;;) {
        /* Keys the harness injects between scripts must not be lost: POKEY
         * latches one, so keep draining it into the driver's queue.  Only
         * the keyboard -- moving the cursor here would change what a script
         * left on screen. */
        vdi_key_poll();
        if (STATUS[ST_GO]) {
            STATUS[ST_GO] = 0;
            STATUS[ST_DONE] = 0;
            run_script();
            STATUS[ST_DONE] = 0xA5;
        }
    }
}
