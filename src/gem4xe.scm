;;; gem4xe linker rules -- Atari XL/XE bank $00, milestone 1.
;;;
;;; Bank $00 map (BASIC off, DOS resident, XL OS on):
;;;   $0000-$00FF  OS zero page          -- not ours
;;;   $0100-$01FF  6502 stack page       -- not ours (we run a 16-bit stack elsewhere)
;;;   $0200-$06FF  OS vars / page 6      -- not ours ($02E0 is the .xex run vector)
;;;   $0700-$1FFF  DOS resident          -- not ours
;;;   $2000-$20FF  direct page           <- ours
;;;   $2100-$2FFF  stack / data / heap   <- ours
;;;   $3000-$3FFF  code                  <- ours
;;;   $4000-$7FFF  RESERVED: U1MB PORTB banking window -- never place anything here
;;;   $8000-$9FFF  RESERVED: VBXE MEMAC A window       -- never place anything here
;;;   $A000-$BFFF  code (BASIC off => RAM)             <- ours
;;;   $C000-$CFFF  OS ROM
;;;   $D000-$D7FF  hardware (VBXE regs at $D640/$D740)
;;;   $D800-$FFFF  OS ROM
;;;
;;; No `reset` section: a .xex is entered through the DOS loader's run vector
;;; at $02E0, which tools/mkxex.py emits pointing at __program_start.

(define memories
  '((memory DirectPage (address (#x2000 . #x20ff))
            (section (registers ztiny)))
    (memory LoRAM      (address (#x2100 . #x2fff))
            (section stack data zdata heap))
    (memory Code       (address (#x3000 . #x3fff))
            (section code cdata data_init_table idata switch))
    ;; Code overflows here.  A memory with (type ANY) does NOT absorb sections
    ;; that are named explicitly elsewhere, so the section list is repeated:
    ;; the linker fills Code first, then spills into HiCode.
    (memory HiCode     (address (#xa000 . #xbffd))
            (section code cdata data_init_table idata switch))
    ;; $4000-$7FFF is where U1MB banks its extended memory, so the SHIPPING
    ;; DRIVER must never place anything here.  The conformance runner is not
    ;; the shipping driver -- it never enables U1MB banking -- and bank $00
    ;; offers only 12 KB of code space between $3000-$3FFF and $A000-$BFFD,
    ;; which the AES has now outgrown.
    ;;
    ;; ⚠ This is a SCAFFOLD, not the answer.  The real answer is the one the
    ;; plan has always named: put code in Rapidus banks $01+ with
    ;; --code-model=large and copy it up at load time, because a .xex loader
    ;; cannot place anything outside bank $00.  Until that exists, the runner
    ;; borrows this region and the limit is ~28 KB rather than ~12 KB.
    (memory Code2      (address (#x4000 . #x73ff))
            (section code cdata data_init_table idata switch))
    (memory TestStage  (address (#x7400 . #x7fff))
            (section teststage))
    ;; The library cstartup always emits a `reset` section (a word pointing at
    ;; __program_start).  A .xex has no reset vector -- DOS enters through the
    ;; run address at $02E0 -- so this is inert filler that simply has to land
    ;; somewhere.  Phase 0 replaces cstartup with an Atari one and drops it.
    (memory Vector     (address (#xbffe . #xbfff))
            (section (reset #xbffe)))
    (block stack (size #x0400))
    (block heap  (size #x0000))     ;; nothing here calls malloc
    (base-address _DirectPageStart DirectPage 0)
    ))
