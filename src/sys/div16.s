;;; ---------------------------------------------------------------------------
;;; div16.s -- _Div16 and _Mod16 that return with N and Z set from the result.
;;;
;;; WHY THIS EXISTS
;;;
;;; At -O2, cc65816 branches on the flags a runtime divide leaves behind:
;;; `if (a / b)` compiles to `jsl _Div16; beq`.  (This said "-O1 and above"
;;; until the sweep measured every level on 5.18.2 and found -O1 clean; the
;;; matrix is in tools/ccbug/README.md.)  The library's
;;; _Div16 ends, for a non-negative result, with `plx; bpl; rtl` -- so N and Z
;;; are those of the sign word (dividend ^ divisor), not of the quotient.
;;; `8 / 8` therefore tests as zero and `7 / 8` as non-zero; `_Mod16` is the
;;; same with the dividend in place of the sign word, so `a % b == 0` is
;;; false for every POSITIVE non-zero a.  The AES trips on it in gsx_tcalc(),
;;; where a
;;; string in a box exactly one character tall (`*ph / hc`) would not draw.
;;;
;;; tools/ccbug/ carries the reproducer and `make check-cc` runs it against
;;; the library, then against this file, in the vendor's own simulator:
;;; three truth tests, which is what it has always run.  (This comment used
;;; to claim 365 dividend/divisor pairs; no such sweep ever existed.)  When a
;;; Calypsi release passes the library half, this file and the two
;;; `--override` linker flags in the Makefile can go.
;;;
;;; The arithmetic is the library's own: magnitudes through _UDivMod16, then
;;; the sign.  Only the exit differs -- `tay; tya` sets N and Z from A and
;;; leaves C alone.
;;;
;;; In:  A = dividend, X = divisor.   Out: A = quotient / remainder.
;;; Destroys X and Y, as the library versions do.
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel cpu, "*"
              .rtmodel codeModel, "large"
              .rtmodel dataModel, "small"

              .extern _UDivMod16
              .public _Div16, _Mod16

              .section farcode, noreorder
_Div16:       phx                     ; divisor
              pha                     ; dividend
              eor     3,s
              sta     3,s             ; sign of the quotient = dividend ^ divisor
              pla
              jsl     divmod16
              txa                     ; quotient
              bra     sign16

_Mod16:       pha                     ; sign of the remainder = sign of the dividend
              jsl     divmod16
sign16:       plx
              bpl     10$
              eor     ##0xffff
              inc     a               ; N and Z from the negated result
              rtl
10$:          tay
              tya                     ; N and Z from the result, C untouched
              rtl

;;; Unsigned divide of the magnitudes: X = quotient, A = remainder.
divmod16:     tay
              bpl     10$
              eor     ##0xffff        ; negate the dividend
              inc     a
10$:          txy
              bpl     20$
              tay
              txa
              eor     ##0xffff        ; negate the divisor
              inc     a
              tax
              tya
20$:          jsl     _UDivMod16
              rtl
