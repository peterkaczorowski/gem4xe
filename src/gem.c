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
 * The one message it can print is the refusal: a machine without a VBXE
 * has no screen for GEM to draw on, and _sys_exit says so on the way out
 * (src/crt_atari.s, _exit_msg) -- after DOS's own screen is back, so the
 * line is not cleared with the reopen of E:.
 */
#include <stdint.h>
#include "vdi/vdi.h"
#include "vdi/pointer.h"
#include "vdi/font.h"
#include "aes/aes.h"
#include "sys/farmem.h"
#include "sys/rapidus.h"
#include "sys/irq.h"
#include "sys/app.h"
#include "sys/cio.h"
#include "sys/dos.h"
#include "sys/gemdos.h"
#include "vbxe/vbxe.h"

extern void _sys_exit(void);
extern uint16_t _exit_msg;           /* src/crt_atari.s: a line for the way out */

/* The pointing device.  An ST mouse on the joystick port is what a GEM
 * machine has; the quadrature is counted from the timer interrupt
 * irq_install sets up (src/sys/irq.h), so it needs nothing polled. */
#define GEM_POINTER PTR_ST_MOUSE

static const char no_vbxe[] = "gem4xe: no VBXE found, GEM needs one\x9b";

__task void main(void)
{
    dos_ident();
    rapidus_speedup();
    irq_install();
    if (!vbxe_detect()) {
        irq_remove();
        rapidus_restore();
        _exit_msg = (uint16_t)no_vbxe;
        _sys_exit();
    }
    /* A blit list started in uninitialised VRAM ($FF) never stops, because
     * the "next" bit is always set.  Clear the control region first. */
    vram_fill(VR_XDL, 0x00, 0x1000);
    vbxe_xdl_hr(VR_SCREEN0);
    vdi_font_default();         /* the linked 8x8 into VRAM */
    vdi_init();
    ptr_init(GEM_POINTER, SCR_W / 2, SCR_H / 2);
    farmem_probe();
    gemdos_init();              /* its far state below any application's */
    blit_fill(VR_SCREEN0, SCR_STRIDE, SCR_STRIDE, SCR_H, 0x00);
    blit_run();

    gsx_start();
    ev_init();
    wm_init();
    mn_init();
    sh_init();                  /* far buffers: before any app_load */
    lang_init();                /* LANG.RSC, or the English in the image */
    fs_start();
    sh_main();

    /* The way back, in the reverse of the way in: interrupts off and the
     * ROM in, the overlay off, the accelerator's windows written back and
     * slow, then the CPU and the stack as DOS had them. */
    irq_remove();
    vbxe_off();
    rapidus_restore();
    _sys_exit();
}
