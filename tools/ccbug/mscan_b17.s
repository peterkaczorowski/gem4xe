;;; mscan_b17.s -- the shape B17 actually takes, as a fixture for the scan.
;;;
;;; Lifted from RetroWP's linebreak.s (5.18.2, -O2, --data-model=large),
;;; the instructions before the call that BRKs.  Its point is the JOIN at
;;; `?L340`: a single pass has to call that label unknown and so misses
;;; the call entirely, but BOTH predecessors are narrow -- the
;;; fall-through after `sep #32`, and the `?L40` path which widens for an
;;; immediate and narrows again at once.  A scan that meets its
;;; predecessors knows the join is narrow.  tools/ccbug/check.py requires
;;; this file to report exactly one call.
plat_measure_text:
            rtl

wp_line_break:
            sep     #32
            lda     5,s
            bra     `?L340`
`?L40`:     lda     ##1
            sep     #32
`?L340`:    sta     1,s
            lda     1,s
            sta     5,s
`?L30`:     lda     46,s
            pha
            lda     44,s
            pha
            lda     42,s
            sta     dp:.tiny _Dp
            jsl     long:plat_measure_text
            rtl
