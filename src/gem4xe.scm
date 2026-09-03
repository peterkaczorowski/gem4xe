;;; gem4xe linker rules -- Atari XL/XE, 65C816 with linear RAM.
;;;
;;; Bank $00 (BASIC off, DOS resident, XL OS on):
;;;   $0000-$00FF  OS zero page          -- not ours
;;;   $0100-$01FF  6502 stack page       -- not ours (we run a 16-bit stack elsewhere)
;;;   $0200-$06FF  OS vars / page 6      -- not ours ($02E0/$02E2 are the .xex vectors)
;;;   $0700-$1FFF  DOS resident          -- not ours
;;;   $2000-$20FF  direct page           <- ours
;;;   $2100-$35FF  stack / data / zdata  <- ours
;;;   $3600-$3FFF  near code and rodata  <- ours
;;;   $4000-$7FFF  RESERVED: U1MB PORTB banking window -- nothing may go here
;;;   $8000-$9FFF  RESERVED: VBXE MEMAC A window       -- nothing may RUN here,
;;;                but it is plain RAM until vbxe_init() opens the window, so
;;;                farload's staging buffer borrows it at LOAD time (farstage)
;;;   $A000-$A7FF  the application pool (BASIC off => RAM)    <- ours
;;;   $A800-$BFFB  the test runner's host-poked buffers        <- ours
;;;   $C000-$CFFF  OS ROM
;;;   $D000-$D7FF  hardware (VBXE regs at $D640/$D740)
;;;   $D800-$FFFF  OS ROM
;;;
;;; Banks $01-$0F: the far code, one memory per bank.  A probe
;;; (src/sys/farmem.c) finds one unbroken run of RAM from bank $01 to $EF on a
;;; Rapidus, but only the first megabyte of it is the accelerator's SRAM; the
;;; far image is kept inside that, and the far heap starts in the bank after
;;; the last one the image reached.
;;;
;;; ONE MEMORY PER BANK, NOT ONE MEMORY SPANNING THEM.  The 65816 program
;;; counter wraps within its bank, so a function that straddles $01FFFF /
;;; $020000 is executed as two unrelated halves.  The linker does not know
;;; that: given a single memory $010000-$0FFFFF it will happily place a
;;; function across the seam (tried, and it did).  Given fifteen memories it
;;; fills them in the order they are defined and never splits a fragment, so
;;; every function lands whole inside one bank and the image spills into the
;;; next bank only when the current one cannot hold the next whole function.
;;; The cost is that `.sectionEnd farcode` no longer means anything -- the
;;; section is in several memories -- so the loader RECORDS how far it wrote
;;; (src/farload.s, _fl_top) rather than the linker predicting it.
;;;
;;; WHAT GOES FAR AND WHAT MUST NOT
;;;
;;; `farcode`, `switch` and `cfar` are the only sections that move.  Everything
;;; a far function reaches for its DATA stays in bank $00, because the small
;;; data model addresses globals and constants ABSOLUTE, through the data bank
;;; register -- so `cdata`, `idata`, `data_init_table`, `data` and `zdata` are
;;; all bank $00 by requirement, not by preference.  A `switch` table is read
;;; with long addressing and could sit anywhere; it travels with the code it
;;; belongs to.  `cfar` holds what is declared `__far const` -- the system
;;; font -- and is read with long addressing too, at the cost of a far
;;; pointer in the two places that read it.  Bank $00 is 3.8 KB of data for
;;; everything, and the window manager's tables alone want 2 KB of it.
;;;
;;; No `reset` section is used at run time: a .xex is entered through the DOS
;;; run vector at $02E0, which tools/mkxex.py points at _atari_entry.

;;; One far memory per bank.  `first` is where bank $01's memory starts: its
;;; bottom for the real build, and higher for a test link -- `make test-m6`
;;; links a second image with `--memories-expression "(layout #x01c000)"`,
;;; which leaves bank $01 too small for the code and forces the spill, so the
;;; mechanism is exercised long before the code grows into it on its own.
(define (far-bank b first)
  (list 'memory (string->symbol (string-append "FarCode" (number->string b 16)))
        (list 'address (cons first (+ (* b #x10000) #xffff)))
        '(section cfar farcode switch)))

(define (far-banks first)
  (cons (far-bank 1 first)
        (map (lambda (b) (far-bank b (* b #x10000)))
             '(2 3 4 5 6 7 8 9 10 11 12 13 14 15))))

(define (layout far-start)
  (append (far-banks far-start) bank0))

;;; Bank $00.  The far memories go ahead of these in the final list.  The
;;; linker fills same-section memories in definition order, and nothing here
;;; shares a section with them, so only the far memories' order among
;;; themselves matters.
(define bank0
  '((memory DirectPage (address (#x2000 . #x20ff))
            (section (registers ztiny)))
    ;; The stack and the data.  The linker will not mix sections that carry
    ;; bits with BSS in one memory, so bank $00 is two memories with a
    ;; boundary that has to be moved by hand when one side outgrows it --
    ;; a link that does fails, loudly.  Both are tight: 7.9 KB is all of
    ;; bank $00 that is gem4xe's, and see the note on $4000-$7FFF above.
    (memory LoRAM      (address (#x2100 . #x35ff))
            (section stack data zdata heap))

    ;; Near code: the entry stub, farload, the C startup, the CIO
    ;; trampoline (src/sys/cio.s: emulation mode returns to bank $00, so
    ;; it cannot be far) and every library routine that is not compiled
    ;; far -- plus all constant data.  There is no overflow memory: the
    ;; 2 KB at $A000 that used to be one (never needed) is the application
    ;; pool now, and a link that outgrows this memory fails rather than
    ;; spilling into the slow window.
    (memory Near       (address (#x3600 . #x3fff))
            (section code libcode cdata idata data_init_table))

    ;; The application pool: where a loaded application's near part -- its
    ;; direct page, stack and data -- goes (src/sys/app.c).  Nothing is
    ;; linked into it; the block only reserves the extent, and
    ;; src/sys/apppool.s reports the bounds the linker gave it, so the
    ;; loader learns them from the map rather than restating them.  It is
    ;; SLOW: it shares the accelerator's 16 KB window with the MEMAC window,
    ;; which has to stay on the bus (see src/sys/rapidus.h).  The size is
    ;; the one an application is linked to fit (src/app/gemapp.scm).
    (memory AppPool    (address (#xa000 . #xa7ff))
            (section apppool))

    ;; The conformance runner's host-poked buffers.  A bss section cannot share
    ;; a memory with sections that carry bits, so it gets its own -- which also
    ;; means nothing else can land in it by accident.
    (memory TestStage  (address (#xa800 . #xbffb))
            (section teststage))

    ;; The load-time staging buffer, inside the MEMAC A window.  It holds no
    ;; linked content -- it is bss, and tools/mkxex.py writes it a chunk at a
    ;; time from the .xex -- so placing it over a region the driver later maps
    ;; VBXE VRAM onto costs nothing.
    (memory Stage      (address (#x8000 . #x9fff))
            (section farstage))

    ;; The library cstartup always emits a `reset` section -- a word pointing
    ;; at __program_start.  A .xex has no reset vector, so this is inert filler
    ;; that simply has to land somewhere the loader will not mind.
    (memory Vector     (address (#xbffc . #xbffd))
            (section (reset #xbffc)))

    (block stack   (size #x0400))
    (block heap    (size #x0000))   ;; nothing here calls malloc
    (block apppool (size #x0800))   ;; one application's near part
    (base-address _DirectPageStart DirectPage 0)
    ))

;;; The far code -- banks $01-$0F, filled from the bottom of bank $01.  A .xex
;;; cannot load above $FFFF; src/farload.s copies the image up as DOS reads
;;; the file.
(define memories (layout #x010000))
