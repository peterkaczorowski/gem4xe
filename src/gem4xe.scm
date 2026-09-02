;;; gem4xe linker rules -- Atari XL/XE, 65C816 with linear RAM.
;;;
;;; Bank $00 (BASIC off, DOS resident, XL OS on):
;;;   $0000-$00FF  OS zero page          -- not ours
;;;   $0100-$01FF  6502 stack page       -- not ours (we run a 16-bit stack elsewhere)
;;;   $0200-$06FF  OS vars / page 6      -- not ours ($02E0/$02E2 are the .xex vectors)
;;;   $0700-$1FFF  DOS resident          -- not ours
;;;   $2000-$20FF  direct page           <- ours
;;;   $2100-$37FF  stack / data / zdata  <- ours
;;;   $3800-$3FFF  near code and rodata  <- ours
;;;   $4000-$7FFF  RESERVED: U1MB PORTB banking window -- nothing may go here
;;;   $8000-$9FFF  RESERVED: VBXE MEMAC A window       -- nothing may RUN here,
;;;                but it is plain RAM until vbxe_init() opens the window, so
;;;                farload's staging buffer borrows it at LOAD time (farstage)
;;;   $A000-$BFFF  near code and rodata (BASIC off => RAM)   <- ours
;;;   $C000-$CFFF  OS ROM
;;;   $D000-$D7FF  hardware (VBXE regs at $D640/$D740)
;;;   $D800-$FFFF  OS ROM
;;;
;;; Bank $01: the far code.  A probe (src/sys/farmem.c) finds one unbroken run
;;; of RAM from bank $01 to $EF on a Rapidus; bank $01 is the first of it.
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

(define memories
  '((memory DirectPage (address (#x2000 . #x20ff))
            (section (registers ztiny)))
    (memory LoRAM      (address (#x2100 . #x37ff))
            (section stack data zdata heap))

    ;; Near code: the entry stub, farload, the C startup and every library
    ;; routine that is not compiled far -- plus all constant data.  Near2
    ;; takes the overflow, and it is SLOW: it shares the accelerator's 16 KB
    ;; window with the MEMAC window, which has to stay on the bus (see
    ;; src/sys/rapidus.h), so nothing that runs often should land there.
    (memory Near       (address (#x3800 . #x3fff))
            (section code libcode cdata idata data_init_table))
    (memory Near2      (address (#xa000 . #xafff))
            (section code libcode cdata idata data_init_table))

    ;; The conformance runner's host-poked buffers.  A bss section cannot share
    ;; a memory with sections that carry bits, so it gets its own -- which also
    ;; means nothing else can land in it by accident.
    (memory TestStage  (address (#xb000 . #xbffb))
            (section teststage))

    ;; The load-time staging buffer, inside the MEMAC A window.  It holds no
    ;; linked content -- it is bss, and tools/mkxex.py writes it a chunk at a
    ;; time from the .xex -- so placing it over a region the driver later maps
    ;; VBXE VRAM onto costs nothing.
    (memory Stage      (address (#x8000 . #x9fff))
            (section farstage))

    ;; Bank $01: the far code.  A .xex cannot load here; src/farload.s copies
    ;; it up as DOS reads the file.
    (memory FarCode    (address (#x010000 . #x01ffff))
            (section cfar farcode switch))

    ;; The library cstartup always emits a `reset` section -- a word pointing
    ;; at __program_start.  A .xex has no reset vector, so this is inert filler
    ;; that simply has to land somewhere the loader will not mind.
    (memory Vector     (address (#xbffc . #xbffd))
            (section (reset #xbffc)))

    (block stack (size #x0400))
    (block heap  (size #x0000))     ;; nothing here calls malloc
    (base-address _DirectPageStart DirectPage 0)
    ))
