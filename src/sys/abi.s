;;; ---------------------------------------------------------------------------
;;; abi.s -- the COP handler and app_run (65C816 native)
;;;
;;; An application calls gem4xe with COP #$73 (VDI) or COP #$C8 (AES), the
;;; parameter block's address in X:C (src/app/gem.h).  The CPU pushes PB,
;;; PC and P, sets I, clears the decimal flag, and arrives here through the
;;; bank-$00 stub in src/sys/irq.s with M and X as the caller had them.
;;;
;;; What this does: save the caller's registers, D and DB; switch to
;;; gem4xe's direct page and data bank; note where the parameter block is
;;; and which signature byte follows the COP; move onto gem4xe's stack if
;;; an application is running and this is the outermost call; give
;;; interrupts back if the caller had them; call gem_entry() (src/sys/abi.c,
;;; the copy-in / copy-out shim); undo all of it; RTI.  A, X and Y are
;;; returned as they were, so the ABI clobbers nothing the caller sees --
;;; results travel through the block.
;;;
;;; The stack switch is what makes an application's stack its own business.
;;; The application runs on a 256-byte stack in its bank-$00 pool; the VDI
;;; and the AES want more than that below them (objc_draw recurses).  So a
;;; COP from the application is served on gem4xe's stack, at the depth
;;; app_run left it: everything below gem_api_sp is free, because app_run
;;; records S at the very moment of the jsl and the application's crt takes
;;; its own stack before pushing anything more.  A nested COP -- the AES
;;; calling back into the application is not a thing today, but gem_depth
;;; costs nothing -- stays where it is.
;;;
;;; gem4xe's pseudo registers _Dp[0..7] are scratch here: they are caller-
;;; saved in Calypsi's convention, so nothing in gem4xe holds a value in
;;; them across the app_run() call that an application runs inside.
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern _DirectPageStart, _Dp
              .extern gem_entry, gem_pb, gem_which, gem_api_sp, gem_depth
              .public gem_cop, app_run

              .section zdata, bss
app_entry:    .space  4               ; the application's entry, for JML [abs]

              .section farcode, root

;;; ---------------------------------------------------------------------------
;;; The COP handler.
;;; ---------------------------------------------------------------------------
gem_cop:      rep     #0x30
              pha
              phx
              phy
              phd
              phb
;;; The frame:  1,s B   2,s D   4,s Y   6,s X   8,s A   10,s P   11,s PC   13,s PB
              sep     #0x20
              lda     #0
              pha
              plb                     ; DB = $00
              rep     #0x20
              lda     ##_DirectPageStart
              tcd                     ; gem4xe's direct page
              lda     8,s             ; the block: X:C at the COP
              sta     abs:gem_pb
              lda     6,s
              sta     abs:gem_pb+2
              lda     11,s            ; PC is past the signature byte ...
              dec     a
              sta     dp:.tiny(_Dp+0)
              sep     #0x20
              lda     13,s            ; ... in bank PB
              sta     dp:.tiny(_Dp+2)
              lda     [.tiny _Dp]
              sta     abs:gem_which
              lda     10,s            ; the caller's I, read before the
              and     #0x04           ; frame goes out of reach
              sta     dp:.tiny(_Dp+4)
              stz     dp:.tiny(_Dp+5)
              lda     abs:gem_depth
              inc     a
              sta     abs:gem_depth
              dec     a
              rep     #0x20
              bne     gem_cop_stay    ; nested: already on gem4xe's stack
              lda     abs:gem_api_sp
              beq     gem_cop_stay    ; no application running: nowhere to go
              tsc
              tax
              lda     abs:gem_api_sp
              tcs
              phx                     ; the caller's S, on gem4xe's stack
              bra     gem_cop_call
gem_cop_stay: tsc
              tax
              phx                     ; the same S, so the exit is one path
gem_cop_call: lda     dp:.tiny(_Dp+4)
              bne     gem_cop_go
              cli                     ; the caller ran with interrupts on
gem_cop_go:   jsl     gem_entry
              sei
              sep     #0x20
              dec     abs:gem_depth
              rep     #0x30
              pla
              tcs                     ; the caller's stack
              plb
              pld
              ply
              plx
              pla
              rti

;;; ---------------------------------------------------------------------------
;;; int16_t app_run(uint32_t entry) -- __simple_call, entry in X:C.
;;;
;;; Calls the application's entry as a far subroutine and returns what its
;;; main() returned.  The application's crt (src/app/crt_gemapp.s) saves
;;; and restores S, D and DB itself, so this side has nothing to unwind.
;;; gem_api_sp is taken INSIDE the trampoline, after the jsl has pushed
;;; app_run's return address: S there is the first free byte, and it is
;;; the S the crt records as well, so the two agree on where gem4xe's stack
;;; resumes.  Cleared on return: a COP with no application running stays on
;;; the caller's stack.
;;; ---------------------------------------------------------------------------
app_run:      sta     abs:app_entry
              txa
              sta     abs:app_entry+2
              sep     #0x20
              stz     abs:gem_depth
              rep     #0x20
              jsl     app_tramp
              tax                     ; main's return value
              stz     abs:gem_api_sp
              txa
              rtl
app_tramp:    tsc
              sta     abs:gem_api_sp
              jmp     [app_entry]     ; JML through the bank-$00 long pointer
