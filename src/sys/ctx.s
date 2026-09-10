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
;;;     tsc                 S as the caller left it.  The jsl's own return
;;;                         address is at S+1..S+3, so it travels with the
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
              beq     ctx_ret         ; already the running one
              sta     abs:ctx_to      ; neither store touches the stack,
              tsc                     ; so S is still the caller's
              sta     abs:ctx_sp_in
              lda     abs:ctx_to
              jsl     ctx_park
              cmp     ##0
              beq     ctx_fresh
              cmp     ##0xffff
              beq     ctx_ret         ; refused: S never moved
              tcs
              jsl     ctx_unpark
ctx_ret:      rtl

;;; Its first turn: the whole shared stack, and a program that is not
;;; expected to return.  If it does, ctx_boot switches away and never
;;; comes back here either.
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
