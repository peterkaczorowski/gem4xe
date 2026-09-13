/* bootinfo.h -- the boot screen: what the machine is, said on the text
 * screen while GEM is still finding out.
 *
 * WHAT IT IS FOR.  A TOS machine shows its EmuTOS boot screen for three
 * seconds -- the version, the processor, the memory, the drives -- and
 * the reason it is worth three seconds is that it is the ONE place a
 * person sees what the system found before the desktop hides it.  On
 * this machine there is more to find and more to get wrong: whether the
 * accelerator is there, how much of its memory the probe accepted,
 * which DOS the disk booted, whether GEM4XE.CFG and LANG.RSC were read
 * at all, which VBXE, which clock.  Somebody whose mouse does not move
 * wants to know what MOUSE= was taken as; somebody whose settings seem
 * ignored wants to know whether the file was found.  This screen says
 * so, once, as each thing is settled -- a line lands right after its
 * probe, so a hang shows where it hung.
 *
 * WHERE IT IS DRAWN.  On E:, the OS's own 40x24 text screen, through
 * CIO -- the screen that exists before either GEM device is brought up,
 * and the one the OS keeps drawing to under the accelerator, because
 * its memory is in the window the speed-up leaves slow (src/sys/rapidus.c).
 * The GTIA registers are written for GEM's colours, white paper and
 * black ink, and NOT the OS shadows, so the way out (the OS's VBI back)
 * restores DOS's blue without anyone having to remember to -- except
 * the ink.  The XL OS copies COLOR1 to COLPF1 in the FIRST stage of its
 * VBI, before the CRITIC test that keeps the other four colours out,
 * and every CIO call runs in emulation mode with that VBI live; a blank
 * landing in one put DOS's grey back in the ink for the whole hold.  So
 * the ink goes through the shadow, saved first and put back at the end.
 *
 * WHAT IT SAYS is in LANG.RSC (tools/langrsc.py, the BOOT_* strings):
 * the labels and the few values that are words.  The rest -- names of
 * hardware and of DOSes, numbers, file names -- are the machine's own.
 *
 * THE HOLD is EmuTOS's: three seconds, any key ends it, SHIFT held
 * pauses it.  Counted in frames from the interrupt regime's counter
 * when it is up (src/sys/irq.h) and from VCOUNT when it is not.
 */
#ifndef GEM4XE_BOOTINFO_H
#define GEM4XE_BOOTINFO_H

#include <stdint.h>

/* The screen, the logo, and the lines for everything settled before a
 * screen was worth having: version, processor, memory, DOS, settings,
 * language.  After farmem_probe() and lang_init(). */
void boot_begin(void);

/* One line each, in the order the machine settles them. */
void boot_video(int16_t video);       /* CFG_VIDEO_VBXE or _ANTIC, as chosen */
void boot_clock(void);              /* probes: which card, and the time    */
void boot_pointer(int16_t kind);      /* a PTR_*, as ptr_init will get it    */
void boot_printer(void);            /* from config                         */

/* The rule, the hint, and the hold. */
void boot_end(void);

#endif /* GEM4XE_BOOTINFO_H */
