;;; ---------------------------------------------------------------------------
;;; apppool.s -- where the bank-$00 application pool is.
;;;
;;; The pool is a block in src/gem4xe.scm; its bounds are the linker's to
;;; decide and this file's to report, the way src/farload.s reports where
;;; the far code ends.  src/sys/app.c reads the two words rather than
;;; restating the map.  app_pool_hi is one past the pool, like _fl_top
;;; (.sectionEnd is the last address, so one is added).
;;; ---------------------------------------------------------------------------
              .rtmodel version, "1"
              .rtmodel core, "*"

              .section apppool
              .section cdata, rodata
              .public app_pool_lo, app_pool_hi
app_pool_lo:  .word   .sectionStart apppool
app_pool_hi:  .word   .sectionEnd apppool + 1
