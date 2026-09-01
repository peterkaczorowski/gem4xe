;;; ---------------------------------------------------------------------------
;;; crt_atari.s -- gem4xe Atari entry stub (65C816 / Rapidus)
;;;
;;; WHY THIS EXISTS
;;;
;;; Calypsi's library cstartup begins `clc; xce` to enter 65816 native mode.
;;; That is correct in general and fatal on an Atari, because the 65816 uses
;;; DIFFERENT interrupt vectors in native mode:
;;;
;;;     emulation mode   NMI $FFFA   RESET $FFFC   IRQ/BRK $FFFE
;;;     native mode      NMI $FFEA   ---           IRQ     $FFEE   BRK $FFE6
;;;
;;; The Atari OS ROM only fills the emulation-mode vectors.  So the first VBI
;;; after `xce` -- i.e. within 20 ms, always -- vectors through $FFEA, reads
;;; whatever bytes happen to live there, and derails.  Observed symptom: the
;;; program starts (a breakpoint at __program_start is hit), then the CPU ends
;;; up at $FFFE with RAM overwritten by a repeating pattern.
;;;
;;; So the very first thing that must happen, while still in emulation mode and
;;; while the Atari's own vectors are still the ones in use, is to turn the
;;; interrupt sources off.  DOS enters us here through the .xex run vector at
;;; $02E0 (tools/mkxex.py emits _atari_entry as the run address); we then fall
;;; into the library startup, which may safely do its `xce`.
;;;
;;; This program never returns to DOS -- it parks -- so IRQEN/NMIEN are simply
;;; switched off rather than saved.  A gem4xe that wants to return to DOS will
;;; need to install native-mode vector stubs instead; that belongs with the VDI
;;; interrupt work, not here.
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern __program_start, _fl_ok
              .public _atari_entry

#define NMIEN  0xD40E                 /* ANTIC: VBI / DLI enable */
#define IRQEN  0xD20E                 /* POKEY: IRQ enable */
#define POKMSK 0x0010                 /* OS shadow of IRQEN */

              .section code, root
_atari_entry:
;;; src/farload.s clears _fl_ok when the machine cannot run gem4xe -- no 65816,
;;; or no RAM in bank $01, either of which means the far code never arrived.
;;; It has already said so on screen, so return to DOS quietly.  This must come
;;; BEFORE the interrupt sources are switched off, or DOS gets its machine back
;;; with no VBI and no SIO.
              lda     _fl_ok
              bne     ae_go
              rts
ae_go:
              sei                     ; no maskable IRQs
              lda     #0
              sta     IRQEN           ; POKEY: no timer/serial/key IRQs
              sta     POKMSK          ; keep the OS shadow consistent
              sta     NMIEN           ; ANTIC: no VBI, no DLI
              jmp     __program_start ; into the library startup (does the xce)
