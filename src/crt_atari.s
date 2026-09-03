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
;;; The sources are switched off here and, once the program is up, switched
;;; on again by src/sys/irq.c -- which first copies the OS ROM into the RAM
;;; under it and fills the native-mode vectors.  That is also what makes a
;;; return to DOS possible: _sys_exit below is the last thing gem4xe runs,
;;; after irq_remove() has given the ROM back and rapidus_restore() has
;;; written $0000-$3FFF down to the motherboard.  It puts the CPU back in
;;; emulation mode with D, S, POKMSK, IRQEN and NMIEN as this stub found
;;; them, rebuilds the E: screen that gem4xe's buffers ran over, and returns
;;; through the RTS DOS is waiting on.
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern __program_start, _fl_ok
              .public _atari_entry, _sys_exit

#define NMIEN  0xD40E                 /* ANTIC: VBI / DLI enable */
#define IRQEN  0xD20E                 /* POKEY: IRQ enable */
#define POKMSK 0x0010                 /* OS shadow of IRQEN */
#define ICCOM  0x0342                 /* IOCB #0: command */
#define ICBAL  0x0344                 /*          buffer address */
#define ICAX1  0x034A                 /*          aux 1: open mode */
#define ICAX2  0x034B
#define CIOV   0xE456
#define CIO_OPEN  3
#define CIO_CLOSE 12
#define OPEN_RW   12

              .section code, root
_atari_entry:
;;; src/farload.s clears _fl_ok when the machine cannot run gem4xe -- no 65816,
;;; or no RAM where a chunk was going, either of which means the far code
;;; never arrived (or not all of it).
;;; It has already said so on screen, so return to DOS quietly.  This must come
;;; BEFORE the interrupt sources are switched off, or DOS gets its machine back
;;; with no VBI and no SIO.
              lda     _fl_ok
              bne     ae_go
              rts
ae_go:
              tsx
              stx     ae_sp           ; DOS's stack pointer, for _sys_exit
              lda     POKMSK
              sta     ae_pokmsk       ; and what it had enabled
              sei                     ; no maskable IRQs
              lda     #0
              sta     IRQEN           ; POKEY: no timer/serial/key IRQs
              sta     POKMSK          ; keep the OS shadow consistent
              sta     NMIEN           ; ANTIC: no VBI, no DLI
              jmp     __program_start ; into the library startup (does the xce)

ae_sp:        .byte   0
ae_pokmsk:    .byte   0

;;; ---------------------------------------------------------------------------
;;; _sys_exit -- back to DOS.  Far-called from C; never returns to it.
;;; Everything above has already been undone (src/sys/irq.c, src/sys/rapidus.c):
;;; the OS ROM is in, its vectors are the ones in use, the interrupt sources
;;; are still off and I is still set.  What is left is the CPU's own state.
;;; ---------------------------------------------------------------------------
_sys_exit:
              sei
              rep     #0x30
              lda     ##0
              tcd                     ; D = $0000: the OS's page zero again
              sep     #0x30
              lda     #0
              pha
              plb                     ; DB = $00 (the handlers restore it, but
                                      ; nothing after this point should trust it)
              sec
              xce                     ; 6502 emulation mode; S is $01xx again
              ldx     ae_sp
              txs                     ; DOS's stack, its return address on top
              lda     ae_pokmsk
              sta     POKMSK
              sta     IRQEN           ; POKEY sources as DOS had them
              lda     #0x40
              sta     NMIEN           ; the VBI: RTCLOK and the shadows again
              cli
;;; The test runner's buffers sit over DOS's display list and screen
;;; ($BC20-$BFFF), so E: is closed and opened afresh: that rebuilds both from
;;; RAMTOP and clears the screen.  CIOV runs from the ROM, in emulation mode,
;;; which is why the ROM had to be back before this point.
              ldx     #0              ; IOCB #0
              lda     #CIO_CLOSE
              sta     ICCOM
              jsr     CIOV
              ldx     #0
              lda     #CIO_OPEN
              sta     ICCOM
              lda     #.byte0 ae_edev
              sta     ICBAL
              lda     #.byte1 ae_edev
              sta     ICBAL+1
              lda     #OPEN_RW
              sta     ICAX1
              lda     #0
              sta     ICAX2
              jsr     CIOV
              rts                     ; to DOS

ae_edev:      .byte   "E:", 0x9b
