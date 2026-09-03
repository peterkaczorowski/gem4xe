;;; ---------------------------------------------------------------------------
;;; irq.s -- gem4xe native-mode interrupt handlers (65C816)
;;;
;;; The vectors at $FFE4-$FFEF are 16-bit and bank $00, so what they point at
;;; must be in bank $00: five 4-byte JML stubs, in `code`.  The handlers
;;; themselves are `farcode` -- fast SRAM on a Rapidus, where the timer
;;; handler runs some four thousand times a second and must not be in the
;;; slow 16 KB window that the application pool shares with the MEMAC window.
;;;
;;; Every handler: save what it uses, force DB = $00 (an interrupt can land
;;; inside an MVN with DB pointing at a far bank), address all state absolute
;;; (never through D: the direct page is wherever the interrupted code put
;;; it), RTI.  The state lives in C (src/sys/irq.c) so the consumers can
;;; declare it volatile; see src/sys/irq.h for what each field means.
;;; (Calypsi's C symbols carry no underscore; the stubs and this file's own
;;; labels do, as src/farload.s's do.)
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern irq_frames, irq_timer, irq_qlo, irq_qhi
              .extern irq_kb, irq_kb_head, irq_kb_tail, irq_kb_count
              .extern irq_fault, irq_ptr_on, irq_plo, irq_phi, irq_qtab
              .extern irq_prev_lo, irq_prev_hi, irq_pend, irq_tmp
              .extern gem_cop
              .public _irq_vec_cop, _irq_vec_brk, _irq_vec_abort
              .public _irq_vec_nmi, _irq_vec_irq
              .public irq_kput

#define NMIRES 0xD40F                 /* ANTIC: any write clears NMIST */
#define IRQST  0xD20E                 /* POKEY: read, 0 = pending */
#define IRQEN  0xD20E                 /* POKEY: write */
#define KBCODE 0xD209
#define PORTA  0xD300
#define POKMSK 0x0010                 /* OS shadow of IRQEN, kept consistent */

;;; ---------------------------------------------------------------------------
;;; The stubs: what the vectors hold.  Bank $00, 4 bytes each.
;;; ---------------------------------------------------------------------------
              .section code, root
_irq_vec_cop:   jmp     long:gem_cop    ; the application ABI, src/sys/abi.s
_irq_vec_brk:   jmp     long:irq_brk
_irq_vec_abort: jmp     long:irq_abort
_irq_vec_nmi:   jmp     long:irq_nmi
_irq_vec_irq:   jmp     long:irq_irq

              .section farcode, root

;;; ---------------------------------------------------------------------------
;;; NMI -- the vertical blank.  NMIEN enables only the VBI, so there is
;;; nothing to distinguish; RESET on an XL/XE is a real reset, not an NMI.
;;; ---------------------------------------------------------------------------
irq_nmi:
              rep     #0x20
              pha
              phb
              sep     #0x20
              lda     #0
              pha
              plb                     ; DB = $00
              sta     NMIRES          ; drop the latch
              rep     #0x20
              inc     abs:irq_frames
              plb
              pla
              rti

;;; ---------------------------------------------------------------------------
;;; IRQ -- POKEY.  Whatever is pending among the sources POKMSK enabled is
;;; acknowledged in one go (bit low then high in IRQEN, as the OS does),
;;; then timer 1 and the keyboard are served.  An IRQ from anything else --
;;; there is nothing else today; a blitter-complete line would arrive here
;;; -- finds nothing pending and returns.
;;; ---------------------------------------------------------------------------
irq_irq:
              rep     #0x30
              pha
              phx
              phy
              phb
              sep     #0x30
              lda     #0
              pha
              plb                     ; DB = $00
              lda     IRQST
              eor     #0xff
              and     abs:POKMSK
              bne     irq_pokey
              brl     irq_done        ; not POKEY's
irq_pokey:    sta     abs:irq_pend
              eor     #0xff
              and     abs:POKMSK
              sta     IRQEN           ; the pending latches low ...
              lda     abs:POKMSK
              sta     IRQEN           ; ... and armed again

;;; Timer 1: the pointer sampler.  One PORTA read, two axes decoded through
;;; the tables the pointer layer filled in (src/vdi/pointer.c), two counters.
              lda     abs:irq_pend
              lsr     a
              bcc     irq_key
              rep     #0x20
              inc     abs:irq_timer
              sep     #0x20
              lda     abs:irq_ptr_on
              beq     irq_key
              lda     PORTA
              and     #0x0f
              tax
              lda     abs:irq_plo,x  ; this axis's line pair, 0..3
              sta     abs:irq_tmp
              ora     abs:irq_prev_lo    ; (prev << 2) | now
              tay
              lda     abs:irq_tmp
              asl     a
              asl     a
              sta     abs:irq_prev_lo
              lda     abs:irq_qtab,y
              beq     irq_hi
              bmi     irq_lo_dec
              rep     #0x20
              inc     abs:irq_qlo
              sep     #0x20
              bra     irq_hi
irq_lo_dec:   rep     #0x20
              dec     abs:irq_qlo
              sep     #0x20
irq_hi:       lda     abs:irq_phi,x
              sta     abs:irq_tmp
              ora     abs:irq_prev_hi
              tay
              lda     abs:irq_tmp
              asl     a
              asl     a
              sta     abs:irq_prev_hi
              lda     abs:irq_qtab,y
              beq     irq_key
              bmi     irq_hi_dec
              rep     #0x20
              inc     abs:irq_qhi
              sep     #0x20
              bra     irq_key
irq_hi_dec:   rep     #0x20
              dec     abs:irq_qhi
              sep     #0x20

;;; Keyboard: the raw code into the ring.
irq_key:      lda     abs:irq_pend
              and     #0x40
              beq     irq_done
              lda     KBCODE
              jsl     irq_kput

irq_done:     rep     #0x30
              plb
              ply
              plx
              pla
              rti

;;; ---------------------------------------------------------------------------
;;; irq_kput -- one raw key code, in A, into the ring.  A full ring drops the
;;; newest key rather than the oldest -- the count still says it arrived.
;;; Called with A and X 8 bits wide and DB = $00; clobbers A and X.  A far
;;; subroutine so that the CIO trampoline (src/sys/cio.s), which finds the
;;; keys the OS collected in CH while it had the keyboard, can deliver them
;;; the same way.
;;; ---------------------------------------------------------------------------
irq_kput:     ldx     abs:irq_kb_tail
              sta     abs:irq_kb,x
              inx
              txa
              and     #7
              cmp     abs:irq_kb_head
              beq     irq_kfull
              sta     abs:irq_kb_tail
irq_kfull:    inc     abs:irq_kb_count
              rtl

;;; ---------------------------------------------------------------------------
;;; BRK, ABORT -- neither is expected.  Say which and park, with I set, so
;;; the STATUS block can be read: ABORT is a Rapidus hardware-protect
;;; violation and would re-execute forever if returned from.  (COP is the
;;; application ABI's entry and goes to src/sys/abi.s; fault code 1 is
;;; retired with it.)
;;; ---------------------------------------------------------------------------
irq_brk:      sep     #0x20
              lda     #2
              bra     irq_park
irq_abort:    sep     #0x20
              lda     #3
irq_park:     sta     long:irq_fault
irq_spin:     bra     irq_spin

;;; ---------------------------------------------------------------------------
;;; What goes to $FFE4-$FFEF: COP, BRK, ABORT, NMI, (reserved), IRQ.  The
;;; stubs are bank $00 labels, so their low 16 bits are the whole address.
;;; src/sys/irq.c copies these twelve bytes as they are.
;;; ---------------------------------------------------------------------------
              .section cdata, rodata
              .public irq_vectab
irq_vectab:  .word   _irq_vec_cop, _irq_vec_brk, _irq_vec_abort
              .word   _irq_vec_nmi, 0, _irq_vec_irq
