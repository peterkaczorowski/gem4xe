/* gem.c -- GEM.COM, the product entry point.
 *
 * The same bring-up as the conformance runner (src/m3_vdi.c) with no host
 * in the loop: DOS loads it, it takes the machine over, hands the screen
 * to the shell loop (src/aes/shel.c: DESKTOP.G4A, then whatever the
 * desktop asks for, until the desktop asks to shut down), and gives the
 * machine back exactly as it found it.  Nothing here is poked from the
 * bridge, so the runner's STATUS block, its script buffers and its test
 * stage do not exist in this link: the application pool has the whole of
 * $4000-$7FFF (src/gem4xe.scm, the `layout` function's second argument).
 *
 * ONE BINARY, TWO SCREENS.  Both VDI devices are linked (src/vdi/vdidev.h)
 * and this file is where the choice is made -- the only place it is made,
 * which is why `vdev` is set here and not by the VDI finding out for
 * itself.  A machine with a VBXE gets 640x240 in sixteen colours; one
 * without gets ANTIC mode F, 320x168 in two, on Atari's condensed face.
 * GEM4XE.CFG overrides either way (src/sys/config.h): VIDEO=ANTIC is the
 * safe mode for a monitor that will not lock to the VBXE's output, and
 * it is a plain text file precisely because that machine has no screen
 * to put a dialog on.
 *
 * WHAT IS STILL REQUIRED EITHER WAY is the accelerator: this program's
 * code lives in bank $01 and a 6502 cannot reach it (src/farload.s
 * refuses the machine before its first store).  The ANTIC device is the
 * answer to "no VBXE", not to "no Rapidus".
 *
 * The one message it can print is a refusal, and _sys_exit says it on the
 * way out (src/crt_atari.s, _exit_msg) -- after DOS's own screen is back,
 * so the line is not cleared with the reopen of E:.
 */
#include <stdint.h>
#include "vdi/vdi.h"
#include "vdi/vdidev.h"             /* the seam: which device, and both */
#include "vdi/pointer.h"
#include "vdi/font.h"
#include "aes/aes.h"
#include "aes/proc.h"
#include "sys/farmem.h"
#include "sys/rapidus.h"
#include "sys/irq.h"
#include "sys/app.h"
#include "sys/cio.h"
#include "sys/dos.h"
#include "sys/config.h"
#include "vdi/print.h"
#include "sys/gemdos.h"
#include "vbxe/vbxe.h"
#include "antic/antic.h"

extern void _sys_exit(void);
extern uint16_t _exit_msg;           /* src/crt_atari.s: a line for the way out */

/* The pointing device.  An ST mouse on the joystick port is what a GEM
 * machine has; the quadrature is counted from the timer interrupt
 * irq_install sets up (src/sys/irq.h), so it needs nothing polled.
 * MOUSE= in GEM4XE.CFG names another. */
#define GEM_POINTER PTR_ST_MOUSE

/* Mode F's two colours: a set bit takes COLPF1's LUMINANCE over COLPF2's
 * hue, so this is GEM's white paper and black ink and there is no third
 * choice to make (src/antic/antic.h). */
#define AN_INK    0x00
#define AN_PAPER  0x0E

static const char no_vbxe[] =
    "gem4xe: VIDEO=VBXE, and no VBXE in this machine\x9b";

__task void main(void)
{
    WORD video;

    dos_ident();
    rapidus_speedup();
    irq_install();
    config_read();              /* before the screen: it says which one */

    /* WHICH SCREEN.  AUTO takes the VBXE when the machine has one, which
     * is the only case that needs no file.  VBXE asked for and not found
     * is the one refusal: the alternative is to quietly give somebody
     * half the pixels they asked for and let them wonder. */
    video = config.video;
    if (video == CFG_VIDEO_AUTO)
        video = vbxe_detect() ? CFG_VIDEO_VBXE : CFG_VIDEO_ANTIC;
    else if (video == CFG_VIDEO_VBXE && !vbxe_detect()) {
        irq_remove();
        rapidus_restore();
        _exit_msg = (uint16_t)no_vbxe;
        _sys_exit();
    }

    if (video == CFG_VIDEO_VBXE) {
        vdev = &vdev_vbxe;
        /* A blit list started in uninitialised VRAM ($FF) never stops,
         * because the "next" bit is always set.  Clear the control
         * region first. */
        vram_fill(VR_XDL, 0x00, 0x1000);
        vbxe_xdl_hr(VR_SCREEN0);
        antic_suspend();            /* its DMA off the bus: antic.h */
    } else {
        vdev = &vdev_antic;
        antic_init(AN_INK, AN_PAPER);       /* builds the list and clears */
    }

    vdi_font_default();         /* the device's own face, into the device */
    vdi_init();
    ptr_init(config.mouse == CFG_MOUSE_AUTO ? GEM_POINTER
                                            : (WORD)config.mouse,
             SCR_W / 2, SCR_H / 2);
    /* The printer, which is a destination and a language and nothing
     * else until a program opens the workstation. */
    pr_kind = config.printer;
    pr_dest = config.printto;
    farmem_probe();
    /* The processes, and the mark the context switch measures from.
     * ctx_init() MUST be called from here and not from inside
     * proc_init(): the mark it takes is its own caller's S, and no
     * context may ever park above it -- src/sys/ctx.h has the argument
     * and test-m27 caught it being got wrong.  main() is the shallowest
     * place in gem4xe that can ever change hands. */
    if (!ctx_regs_ok() || !proc_init(pool_alloc(PROC_STORE, 2))
        || !ctx_make(&proc_app->p_ctx, 0))
        _sys_exit();
    ctx_init(&proc_app->p_ctx);
    gemdos_init();              /* its far state below any application's */
    dev_clear_screen();         /* whichever screen it is */

    gsx_start();
    ev_init();
    wm_init();
    mn_init();
    mn_start();     /* the registry: once, before any accessory */
    sh_init();                  /* far buffers: before any app_load */
    lang_init();                /* LANG.RSC, or the English in the image */
    fs_start();
    sh_main();

    /* The way back, in the reverse of the way in: interrupts off and the
     * ROM in, the screen off, the accelerator's windows written back and
     * slow, then the CPU and the stack as DOS had them. */
    irq_remove();
    if (video == CFG_VIDEO_VBXE) {
        vbxe_off();
        antic_resume();
    } else
        antic_off();
    rapidus_restore();
    _sys_exit();
}
