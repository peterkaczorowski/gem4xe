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
;;; The sizes are the application's budget: a near region of a page of
;;; direct page plus `bss` bytes without bits plus `bits` bytes with, out
;;; of gem4xe's pool (src/gem4xe.scm), and one bank of code.  All are
;;; limits the link enforces and the loader checks again against what it
;;; has.  The gate application (src/m11_app.c) links with 2 KB and 256
;;; bytes; the desktop asks for more, and the loader never sees the
;;; difference -- it reads the extent from the file's header, rounded up
;;; to whole pages (tools/mkg4a.py), since the loader relocates by pages;
;;; page multiples here waste none of it.

(define (app-layout near far bss bits)
  (list
    (list 'memory 'AppDP
          (list 'address (cons near (+ near #xff)))
          '(section (registers ztiny)))
    (list 'memory 'AppBss
          (list 'address (cons (+ near #x100) (+ near #xff bss)))
          '(section stack data zdata heap))
    (list 'memory 'AppBits
          (list 'address (cons (+ near #x100 bss) (+ near #xff bss bits)))
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

(define memories (app-layout #x1000 #x020000 #x800 #x100))
