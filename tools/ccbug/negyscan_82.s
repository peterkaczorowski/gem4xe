;;; The fixture negyscan.py must find, so that its silence means something.
;;;
;;; Reduced from the MicroPython SNES port's rot_three listing (Calypsi
;;; #82, found on 5.17, fixed for 5.18): a below-pointer access through a
;;; far pointer compiled to a negative Y with long indexed addressing, so
;;; the 65816 added Y to the 24-bit base as UNSIGNED 16 bits and the access
;;; landed one bank away.
;;;
;;; EXACTLY TWO SITES.  The first is deliberately on the same line as its
;;; label, because that is how Calypsi emits a function's first
;;; instruction -- and the first version of this scan could not see it.

;;; ---- site 1: negative Y, decimal, at a function's entry instruction ----
rot3:       ldy     ##-8
            lda     [(_Dp+8)],y
            sta     1,s

;;; ---- site 2: the same thing written as the hex the assembler prints ----
            ldy     ##0xfff8
            sta     [(_Dp+8)],y
            rtl

;;; ---- controls: none of the below may be reported ----

;;; the safe form -- adjust the base, then a non-indexed long access
safe_base:  sec
            lda     dp:.tiny _Dp
            sbc     ##4
            sta     dp:.tiny _Dp
            lda     [.tiny _Dp]
            rtl

;;; a negative Y with SHORT addressing is fine: the address is 16-bit and
;;; wrapping is what C's pointer arithmetic does anyway
safe_short: ldy     ##-8
            lda     0x2000,y
            sta     (0x80),y
            rtl

;;; Y is reloaded before any long access, so the negative value is dead
safe_reload:
            ldy     ##-8
            ldy     ##4
            lda     [(_Dp+8)],y
            rtl

;;; a POSITIVE Y with long addressing is the ordinary, correct case
safe_pos:   ldy     ##8
            lda     [(_Dp+8)],y
            rtl
