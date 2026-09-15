;;; ---------------------------------------------------------------------------
;;; m11_cop.s -- one COP that is not gem4xe's, for the m11 gate.
;;;
;;; COP #$01 is Rapidus OS's kmem call: a function code on the stack, the
;;; status back in Y.  $7FFF is not one of its functions, and its
;;; specification says an unassigned code answers -110.  gem4xe must hand
;;; this COP to the OS when the OS is there (src/sys/abi.s) -- so the -110
;;; comes back -- and refuse it when it is not, when Y comes back as it went
;;; in.  Nothing the application owns is touched either way.
;;;
;;; WORD m11_cop01(void): Y as the COP left it.
;;; ---------------------------------------------------------------------------
              .rtmodel version, "1"
              .rtmodel core, "*"
              .public m11_cop01

              .section farcode
m11_cop01:    ldy     ##0x1234        ; what a refused COP gives back
              lda     ##0x7fff        ; not a kmem function ...
              pha                     ; ... on the stack, as the OS reads it
              cop     #0x01
              plx                     ; the argument off the stack
              tya
              rtl
