/* diag.h -- GEMDIAG.COM: the product's start-up with a mark after each step.
 *
 * WHY.  The first run on a real Rapidus machine ended in a blank screen of
 * one colour, and a blank screen says nothing about which of the dozen
 * things start-up does was the one that did not come back.  This build is
 * GEM.COM with a mark before every step, on two channels that need nothing
 * of gem4xe's to be working:
 *
 *   - the step's number, inverse video, at that column of the top line of
 *     whatever ANTIC is showing -- the DOS's own screen, in RAM the
 *     accelerator leaves on the motherboard bus (window 2, kept slow for
 *     the MEMAC window), so ANTIC sees the write whatever the CPU's speed;
 *   - a tone from POKEY channel 4 (channel 1 is the pointer sampler's),
 *     rising a step at a time, for the steps after the overlay has covered
 *     the text.
 *
 * The last digit shown, or the last tone heard, names the step that did
 * not finish.  No digit at all: the loader or the C start-up, before main.
 *
 * The console keys held while the program starts bisect the two things
 * that have only ever run under Altirra: OPTION leaves the accelerator's
 * windows as its firmware set them (no rapidus_speedup), SELECT leaves the
 * interrupts off (no irq_install, the polled regime), START waits for a
 * press of START before each step so the marks can be read at leisure.
 *
 * After step 1 the same line shows what rapidus_speedup found, from
 * column 18: R or - for a Rapidus accepted or not, the eight bytes of the
 * signature at $FF0000 as they read, then the MCR and CMCR as found --
 * the firmware's own setup of the windows, which is the part of a real
 * card that differs from the emulator's.
 *
 * Nothing here is a string anyone reads; the digits are screen codes. */
#ifndef GEM4XE_DIAG_H
#define GEM4XE_DIAG_H
#include <stdint.h>

#define DIAG_STEP          0x01     /* START held: a press before each step */
#define DIAG_SKIP_IRQ      0x02     /* SELECT held: no irq_install()        */
#define DIAG_SKIP_SPEEDUP  0x04     /* OPTION held: no rapidus_speedup()    */

extern uint8_t diag_keys;           /* the CONSOL bits, active high */

void diag_init(void);               /* read the keys: first thing in main */
void diag_mark(uint8_t n);          /* step n is about to run */
void diag_rapidus(void);            /* after step 1: what the accelerator said */

#endif
