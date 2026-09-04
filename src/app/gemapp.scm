;;; gemapp.scm -- linker rules for a gem4xe application.
;;;
;;; An application is linked at PLACEHOLDER addresses and relocated by the
;;; loader: its near region -- direct page, stack, data -- to a page-aligned
;;; slice of gem4xe's application pool in bank $00, and its far region --
;;; the code -- to a bank of the far heap.  tools/mkg4a.py links the program
;;; three times, once here and once with each region shifted (a page, a
;;; bank), and the bytes that moved are the fixups; so the placeholders
;;; below are never seen at run time, and nothing in the program may depend
;;; on them.  The near region is one page for the direct page, then what
;;; carries no bits (the stack, data, zdata) and then what does (constants,
;;; the initial values of data, the init table): the linker will not mix the
;;; two kinds in one memory, and the loader wants one extent, so the split
;;; is fixed here.  The loader relocates by pages, which is why the direct
;;; page -- the only thing whose low byte matters -- sits at a page boundary.
;;;
;;; The two sizes are the application's budget: 2 KB near (gem4xe's pool is
;;; that big -- src/gem4xe.scm) and one bank of code.  Both are limits the
;;; link enforces and the loader checks again against what it has.  The
;;; split of the 2 KB -- a page of direct page, 1.5 KB without bits, 256
;;; bytes with -- is what the gate application needs; another application
;;; moves the boundary, and the loader never sees it.

(define (app-layout near far)
  (list
    (list 'memory 'AppDP
          (list 'address (cons near (+ near #xff)))
          '(section (registers ztiny)))
    (list 'memory 'AppBss
          (list 'address (cons (+ near #x100) (+ near #x6ff)))
          '(section stack data zdata heap))
    (list 'memory 'AppBits
          (list 'address (cons (+ near #x700) (+ near #x7ff)))
          '(section cdata idata data_init_table))
    ;; Two far memories, either side of the bank's $D5 page: the emulator's
    ;; native-mode branch reads a wrong-page address folded into bank $00,
    ;; and $D5xx there is cartridge control (src/gem4xe.scm, THE HOLE).
    (list 'memory 'AppFar
          (list 'address (cons far (+ far #xd4ff)))
          '(section farcode switch cfar libcode code))
    (list 'memory 'AppFarH
          (list 'address (cons (+ far #xd600) (+ far #xffff)))
          '(section farcode switch cfar libcode code))
    '(block stack (size #x0100))
    '(block heap  (size #x0000))
    '(base-address _DirectPageStart AppDP 0)))

(define memories (app-layout #x1000 #x020000))
