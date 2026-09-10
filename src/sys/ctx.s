;;; ---------------------------------------------------------------------------
;;; ctx.s -- the three instructions C cannot write.
;;;
;;; src/sys/ctx.h says why the contexts share one stack; src/sys/ctx.c does
;;; all the bookkeeping.  What is left here is what C has no way to say:
;;; read S, write S, and return through a stack that arrived after this
;;; routine started running.
;;;
;;; The shape of a switch:
;;;
;;;     push _Dp            the compiler's own register file, which is per
;;;                         context exactly as a 68000 AES process's d0-a6
;;;                         are (EmuTOS struct.h, UDA).  It lives in the
;;;                         direct page at a fixed address, shared by
;;;                         everybody, and a context parked in the middle
;;;                         of a function has ITS values in it.  Pushing
;;;                         it onto the outgoing stack is what makes it
;;;                         travel: the park copies the stack, so the
;;;                         registers go with it, and no CTX field and no
;;;                         struct offset has to be written down twice.
;;;                         It also has to be assembly and not C, because
;;;                         a C function restores the register file from
;;;                         its own frame on the way out -- it would undo
;;;                         the restore as its last act.
;;;     tsc                 S as the caller left it, less what was just
;;;                         pushed.  The jsl's own return address is at
;;;                         the top of the extent, so it travels with the
;;;                         context and the RTL at the end of a LATER
;;;                         switch is the one that lands back in the
;;;                         caller of THIS one.
;;;     ctx_park()          copies [S+1 .. ctx_base] out to far memory and
;;;                         answers where the incoming context left its S.
;;;     tcs                 the stack the outgoing context was standing on
;;;                         no longer exists.  Nothing below this may use
;;;                         a frame from above it.
;;;     ctx_unpark()        copies the incoming extent back.  Its own frame
;;;                         is below the new S and the extent is above it,
;;;                         so it is not overwriting itself.
;;;     pull _Dp            the incoming context's register file, off its
;;;                         own restored stack.
;;;     rtl                 through the restored return address.
;;;
;;; A context that has never run has no extent to restore: it starts at the
;;; top of the shared stack and is entered through ctx_boot().
;;;
;;; ctx_park answers three things, and they are distinguished by value
;;; because the stack lives at $2E38-$3637 and can be neither:
;;;     0       this context has never run -- start it fresh
;;;     $FFFF   nothing to do -- the park was refused (ctx_over)
;;;     else    the S to resume at
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern ctx_cur, ctx_base, ctx_park, ctx_unpark, ctx_boot
              .extern ctx_start0
              .public ctx_switch, ctx_init

;;; How much of the direct page travels.  The register file is `registers`
;;; -- _Dp and _FillInd -- and this is a round number ABOVE its real size
;;; rather than the size itself, because the difference of two section
;;; symbols is not something the object format can carry in an immediate.
;;; The bytes past the section are unallocated direct page and cost
;;; nothing to carry; what would cost something is the section growing
;;; past this, so ctx_init() checks the linker's own bounds against it and
;;; refuses to start if it ever does.
#define CTX_REGS  32

;;; Named so that this file may refer to its bounds; nothing is added to
;;; it here.
              .section registers, bss

              .section cdata, rodata
              .public ctx_reg_lo, ctx_reg_hi
ctx_reg_lo:   .word   .sectionStart registers
ctx_reg_hi:   .word   .sectionEnd registers + 1

              .section zdata, bss
ctx_to:       .space  2               ; the target, held across the TSC
ctx_sp_in:    .space  2               ; S as ctx_switch was entered
              .public ctx_sp_in

              .section farcode, root

;;; ---------------------------------------------------------------------------
;;; void ctx_switch(CTX *to) -- __simple_call, the target in C.
;;; ---------------------------------------------------------------------------
ctx_switch:   rep     #0x30
              cmp     abs:ctx_cur
              beq     ctx_out         ; already the running one
              sta     abs:ctx_to      ; does not touch the stack

              sep     #0x20           ; the register file, onto this stack
              ldy     ##CTX_REGS-1
10$:          lda     abs:(.sectionStart registers),y
              pha
              dey
              bpl     10$
              rep     #0x30

              tsc
              sta     abs:ctx_sp_in
              lda     abs:ctx_to
              jsl     ctx_park
              cmp     ##0
              beq     ctx_fresh
              cmp     ##0xffff
              beq     ctx_back        ; refused: S never moved, so this
                                      ; stack still carries our own copy
              tcs
              jsl     ctx_unpark
ctx_back:     sep     #0x20           ; the register file back off the stack
              ldy     ##0
20$:          pla
              sta     abs:(.sectionStart registers),y
              iny
              cpy     ##CTX_REGS
              bne     20$
              rep     #0x30
ctx_out:      rtl

;;; Its first turn: the whole shared stack, and a program that is not
;;; expected to return.  If it does, ctx_boot switches away and never
;;; comes back here either.  Nothing is pulled back here: this context
;;; has no saved register file yet, and its program is entered with
;;; whatever the file holds, which is what a fresh call would get.
ctx_fresh:    lda     abs:ctx_base
              tcs
              jsl     ctx_boot
              bra     ctx_fresh

;;; ---------------------------------------------------------------------------
;;; void ctx_init(CTX *first) -- __simple_call, the record in C.
;;;
;;; ctx_base has to be taken HERE and not in C, and the reason is the first
;;; thing this gate caught: base must be at or above every S that will ever
;;; be parked, and a C ctx_init() would take it INSIDE itself -- below its
;;; own caller, so the caller's later switches would park at a HIGHER
;;; address and `base - sp` would run backwards through zero.  The caller's
;;; S, before the jsl that got here, is the shallowest point that can ever
;;; switch, so that is the mark.
;;; ---------------------------------------------------------------------------
ctx_init:     rep     #0x30
              sta     abs:ctx_to      ; the record, across the TSC
              tsc
              clc
              adc     ##3             ; undo the return address this jsl pushed
              sta     abs:ctx_base
              lda     abs:ctx_to
              jsl     ctx_start0
              rtl
