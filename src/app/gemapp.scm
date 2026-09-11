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
;;; page multiples here waste none of it.  The stack's share of `bss` is
;;; the link's --stack-size (the Makefile's g4a macro), over the 256
;;; bytes below: the desktop's calls nest deeper than the gate
;;; application's and ran that out (phase 14, milestone 5).

(define (app-layout near far bss bits)
  (list
    (list 'memory 'AppDP
          (list 'address (cons near (+ near #xff)))
          '(section (registers ztiny)))
    ;; `near`, `znear` and `inear` are the bank-$00 sections of a program
    ;; compiled --data-model=LARGE: there, a global goes far unless it is
    ;; declared __near, and a program that hands the AES a tree or a
    ;; string must declare those, since the ABI takes only bank $00
    ;; (src/sys/abi.c, near_of).  A small-data program has none of them
    ;; and loses nothing by their being named.
    (list 'memory 'AppBss
          (list 'address (cons (+ near #x100) (+ near #xff bss)))
          '(section stack data zdata heap near znear))
    (list 'memory 'AppBits
          (list 'address (cons (+ near #x100 bss) (+ near #xff bss bits)))
          '(section cdata idata data_init_table inear))
    ;; Two far memories, either side of the bank's $D5 page: the emulator's
    ;; native-mode branch reads a wrong-page address folded into bank $00,
    ;; and $D5xx there is cartridge control (src/gem4xe.scm, THE HOLE).
    ;;
    ;; `far` and `zfar` are here for an application compiled
    ;; --data-model=LARGE, where a pointer is 24 bits and the compiler puts
    ;; every global up here instead of in bank $00.  That is the model the
    ;; two programs this project exists for are written to (docs/gacs.md):
    ;; GACS's tables are 22 KB and its sheet another 33, against the 2 KB
    ;; of bank $00 an application gets, so its engine works out of far
    ;; memory -- and it already does, because its own first rule is that a
    ;; shell hands the engine a buffer.  A small-data application has
    ;; neither section and loses nothing by their being named.
    ;;
    ;; `far` carries bits, `zfar` does not, and `ifar` is the INITIALISER
    ;; for `far` -- the bytes the crt's data_init_table walk copies into
    ;; it at start-up, which the linker emits and which has to be placed
    ;; or the link stops with "Failed to place 'ifar'".  That is how this
    ;; list was found to be short: by trying it.
    ;;
    ;; Unlike the near region there is no loader constraint that the
    ;; bits and the no-bits sections be one extent, since the whole bank
    ;; moves together and the fixups are per address.  So all three go in
    ;; both memories and the linker decides.
    (list 'memory 'AppFar
          (list 'address (cons far (+ far #xd4ff)))
          '(section farcode switch cfar libcode code ifar))
    (list 'memory 'AppFarH
          (list 'address (cons (+ far #xd600) (+ far #xffff)))
          '(section farcode switch cfar libcode code ifar))
    ;; ...and the far VARIABLES in the bank above the code, because the
    ;; linker will not put sections that carry bits and sections that do
    ;; not in one memory, and `far`/`zfar` carry none: they are made at
    ;; start-up, `zfar` zeroed and `far` copied from `ifar` by the crt's
    ;; data_init_table walk.  A bank of their own also means an
    ;; application's data can grow without moving its code.
    (list 'memory 'AppFarBss
          (list 'address (cons (+ far #x10000) (+ far #x1d4ff)))
          '(section far zfar))
    '(block stack (size #x0100))
    '(block heap  (size #x0000))
    '(base-address _DirectPageStart AppDP 0)
    ;; Where a __near variable is addressed from.  A large-data program
    ;; reaches its near data through a base register rather than
    ;; absolutely, so the linker has to be told which memory that base
    ;; names; for a small-data one nothing refers to it.
    '(base-address _NearBaseAddress AppBss 0)))

(define memories (app-layout #x1000 #x020000 #x800 #x100))
