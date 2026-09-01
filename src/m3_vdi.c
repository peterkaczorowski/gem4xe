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
#include "vbxe/vbxe.h"

#define STATUS ((volatile unsigned char *) 0x0600)

/* STATUS[0..1] 'V','D'   STATUS[2] stage   STATUS[3] go   STATUS[4] done
 * The host writes STATUS[3]; the runner answers on STATUS[4].              */
#define ST_STAGE 2
#define ST_GO    3
#define ST_DONE  4

/* Both buffers are host-poked staging, so they go in the `teststage` section,
 * which src/gem4xe.scm maps to $4000-$7FFF.  That region is off limits to the
 * driver -- U1MB banks its extended memory there -- but this runner never
 * enables banking, and putting the buffers in their own named section means
 * nothing else can end up there by accident. */
#define SCRIPT_WORDS 512
__attribute__((section("teststage")))
volatile WORD vdi_script[SCRIPT_WORDS];

#define SCRATCH_BYTES 768
__attribute__((section("teststage")))
volatile unsigned char vdi_scratch[SCRATCH_BYTES];

/* Per-call output record, so the harness can check what an opcode RETURNS and
 * not only what it draws.  Without this the input and inquiry opcodes -- most
 * of what is left for the AES -- would be untestable.
 *   [0] contrl[2]  [1] contrl[4]  [2..4] intout[0..2]  [3..7] ptsout[0..2] */
#define RESULT_WORDS 8
#define MAX_RESULTS  48
__attribute__((section("teststage")))
volatile WORD vdi_results[MAX_RESULTS * RESULT_WORDS];
__attribute__((section("teststage")))
volatile WORD vdi_result_count;

/* GEM's standard 16-colour palette order: pen 0 is WHITE and pen 1 is BLACK,
 * which is the opposite of what a framebuffer usually assumes and is what
 * every GEM application is written against. */
static const unsigned char gem_pal[16 * 3] = {
    0xFF, 0xFF, 0xFF,   /*  0 white        */
    0x00, 0x00, 0x00,   /*  1 black        */
    0xFF, 0x00, 0x00,   /*  2 red          */
    0x00, 0xFF, 0x00,   /*  3 green        */
    0x00, 0x00, 0xFF,   /*  4 blue         */
    0x00, 0xFF, 0xFF,   /*  5 cyan         */
    0xFF, 0xFF, 0x00,   /*  6 yellow       */
    0xFF, 0x00, 0xFF,   /*  7 magenta      */
    0xBB, 0xBB, 0xBB,   /*  8 light grey   */
    0x77, 0x77, 0x77,   /*  9 dark grey    */
    0xBB, 0x00, 0x00,   /* 10 dark red     */
    0x00, 0xBB, 0x00,   /* 11 dark green   */
    0x00, 0x00, 0xBB,   /* 12 dark blue    */
    0x00, 0xBB, 0xBB,   /* 13 dark cyan    */
    0xBB, 0xBB, 0x00,   /* 14 dark yellow  */
    0xBB, 0x00, 0xBB    /* 15 dark magenta */
};

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
            contrl[2] = 0;
            contrl[4] = 0;
            switch (op - 1000) {
            case 42:                        /* objc_draw */
                clip.g_x = ptsin[0]; clip.g_y = ptsin[1];
                clip.g_w = ptsin[2]; clip.g_h = ptsin[3];
                objc_draw(tree, intin[0], intin[1], &clip);
                break;
            case 43:                        /* objc_find */
                intout[0] = objc_find(tree, intin[0], intin[1],
                                      ptsin[0], ptsin[1]);
                contrl[4] = 1;
                break;
            default:
                break;
            }
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
            r[2] = (WORD)(contrl[4] > 0 ? intout[0] : 0);
            r[3] = (WORD)(contrl[4] > 1 ? intout[1] : 0);
            r[4] = (WORD)(contrl[4] > 2 ? intout[2] : 0);
            r[5] = (WORD)(contrl[2] > 0 ? ptsout[0] : 0);
            r[6] = (WORD)(contrl[2] > 0 ? ptsout[1] : 0);
            r[7] = (WORD)(contrl[2] > 1 ? ptsout[2] : 0);
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

    if (!vbxe_detect()) {
        STATUS[ST_STAGE] = 0xEE;
        for (;;)
            ;
    }
    /* A blit list started in uninitialised VRAM ($FF) never stops, because the
     * "next" bit is always set.  Clear the control region before first use. */
    vram_fill(VR_XDL, 0x00, 0x1000);
    vbxe_palette(1, 0, gem_pal, 16);
    vbxe_xdl_hr(VR_SCREEN0);
    vdi_font_expand();          /* 1bpp GEM font -> 4bpp masks in VRAM */
    vdi_init();
    /* PTR_TABLET is the one back end usable today: an absolute device needs
     * only one POT read per frame from a polling loop, whereas a quadrature
     * mouse needs an interrupt and gem4xe still runs with NMI/IRQ off. */
    ptr_init(PTR_TABLET, SCR_W / 2, SCR_H / 2);

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

    /* Start from a known screen: pen 0 (white), as a GEM desktop would. */
    blit_fill(VR_SCREEN0, SCR_STRIDE, SCR_STRIDE, SCR_H, 0x00);
    blit_run();

    /* Touch the scratch area so the linker keeps it: nothing on the target
     * references it -- the host is the only writer. */
    vdi_scratch[0] = 0;
    vdi_results[0] = 0;
    STATUS[ST_STAGE] = 1;                       /* ready for scripts */

    for (;;) {
        if (STATUS[ST_GO]) {
            STATUS[ST_GO] = 0;
            STATUS[ST_DONE] = 0;
            run_script();
            STATUS[ST_DONE] = 0xA5;
        }
    }
}
