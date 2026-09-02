;;; ---------------------------------------------------------------------------
;;; farload.s -- load-time copy-up of 65816 far code into linear RAM.
;;;
;;; WHY THIS EXISTS
;;;
;;; An Atari DOS loader has no concept of 65816 banks: a .xex segment header is
;;; two 16-bit addresses, so nothing can be loaded above $FFFF.  But bank $00
;;; offers gem4xe about 12 KB of code space once the OS, DOS, the U1MB banking
;;; window and the MEMAC window are subtracted, and the VDI alone is 15 KB.
;;;
;;; So the linker places `farcode` in bank $01 -- where a probe found one
;;; unbroken run of RAM from bank $01 to $EF -- and the far image travels in
;;; the .xex as a series of chunks aimed at a staging buffer in bank $00.  DOS
;;; calls through INITAD ($02E2) after loading a segment, and this is what it
;;; calls: it copies one chunk from the buffer to its real home and returns.
;;; By the time DOS reaches the run vector the far image is assembled.
;;;
;;; tools/mkxex.py builds those chunks and is the other half of this file; the
;;; two share the layout through the linker symbols _fl_hdr / _fl_buf / _fl_scr
;;; rather than a repeated constant.
;;;
;;; THE STAGING BUFFER LIVES AT $8000, IN THE MEMAC A WINDOW.  That region is
;;; already reserved -- no code or data may be placed there, because the driver
;;; maps VBXE VRAM over it at run time -- and it is plain motherboard RAM until
;;; vbxe_init() opens the window.  So staging there costs nothing at all.
;;;
;;; WHAT IT REFUSES TO DO
;;;
;;; Writing bank $01 needs a 65816: on an NMOS 6502 the long store `sta $9F`
;;; is an unstable undocumented opcode, so running this on the wrong machine
;;; would corrupt memory rather than fail.  The CPU is therefore identified
;;; before the first store, and linear RAM is probed rather than assumed --
;;; a 65816 with nothing in bank $01 is just as fatal and much less obvious.
;;; Either failure prints a line and returns to DOS with nothing written.
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .public _fl_copy, _fl_ok
              .public _fl_hdr, _fl_buf, _fl_scr
              .public _fl_heap_bank, _fl_running_bank

FL_CHUNK:     .equ    0x1f00          ; staging payload: 31 whole pages

;;; CIO, for the two failure messages.  DOS is still resident and IOCB #0 is
;;; open on E: at this point, which is the whole reason the diagnostics can be
;;; a printed line rather than a wedged machine.
ICCOM:        .equ    0x0342
ICBAL:        .equ    0x0344
ICBLL:        .equ    0x0348
CIOV:         .equ    0xe456
PUTREC:       .equ    0x09
EOL:          .equ    0x9b

NMIEN:        .equ    0xd40e

;;; --- staging area -----------------------------------------------------------
;;; Placed by src/gem4xe.scm at $8000.  The header and the payload are ADJACENT
;;; ON PURPOSE: that lets mkxex.py describe a whole chunk -- where it goes, how
;;; long it is, and the bytes themselves -- in one .xex segment.
;;;
;;; _fl_scr sits after the payload, out of reach of the chunk segments, and is
;;; the direct page this routine runs on.

              .section farstage, bss
_fl_hdr:      .space  3               ; +0  destination, 24-bit little-endian
_fl_pages:    .space  1               ; +3  length in 256-byte pages, 0 = idle
_fl_buf:      .space  FL_CHUNK        ; +4  the payload
_fl_scr:      .space  16              ; direct page for the copier

;;; Direct page layout, relative to _fl_scr.
DP_SRC:       .equ    0               ; 24-bit source pointer
DP_DST:       .equ    4               ; 24-bit destination pointer
DP_CNT:       .equ    8               ; pages remaining

              .section code, root

;;; _fl_ok -- cleared if this machine cannot run gem4xe.  _atari_entry reads it
;;; and returns to DOS rather than starting a program whose code never arrived.
;;; It is initialised data inside `code`, so DOS loads it before any INITAD.
_fl_ok:       .byte   1
fl_checked:   .byte   0

;;; ---------------------------------------------------------------------------
;;; _fl_copy -- DOS calls this through INITAD after each chunk segment.
;;; ---------------------------------------------------------------------------
_fl_copy:
              lda     fl_checked
              bne     fl_ready
              jsr     fl_check        ; first call: identify the machine
fl_ready:
              lda     _fl_ok
              beq     fl_done         ; wrong machine -- never write bank $01
              lda     _fl_pages
              beq     fl_done         ; nothing staged (the priming call)

              php
              phd
              lda     #.byte1 _fl_scr
              xba
              lda     #.byte0 _fl_scr
              tcd                     ; TCD is 16-bit even in emulation mode

              lda     #.byte0 _fl_buf
              sta     dp:DP_SRC
              lda     #.byte1 _fl_buf
              sta     dp:DP_SRC+1
              lda     #0
              sta     dp:DP_SRC+2     ; the buffer is always in bank $00
              lda     long:_fl_hdr
              sta     dp:DP_DST
              lda     long:_fl_hdr+1
              sta     dp:DP_DST+1
              lda     long:_fl_hdr+2
              sta     dp:DP_DST+2
              lda     long:_fl_pages
              sta     dp:DP_CNT

;;; Both pointers are dereferenced long, so neither the source nor the
;;; destination depends on what DOS left in the data bank register.
fl_page:      ldy     #0
fl_byte:      lda     [dp:DP_SRC],y
              sta     [dp:DP_DST],y
              iny
              bne     fl_byte
              inc     dp:DP_SRC+1     ; += 256; the buffer never crosses a bank
              inc     dp:DP_DST+1
              bne     fl_nowrap
              inc     dp:DP_DST+2
fl_nowrap:    dec     dp:DP_CNT
              bne     fl_page

              pld
              plp
;;; Consume the chunk.  DOS may call INITAD again after a segment that carries
;;; no chunk -- the run vector, for one -- and this is what makes that a no-op.
              lda     #0
              sta     _fl_pages
fl_done:      rts

;;; ---------------------------------------------------------------------------
;;; fl_check -- is this machine a 65816 with RAM where the far image goes?
;;;
;;; Run once, before the first store.  Every instruction up to the point where
;;; a 65816 is confirmed is a plain 6502 instruction.
;;; ---------------------------------------------------------------------------
fl_check:
              lda     #1
              sta     fl_checked

;;; 1. NMOS or CMOS?  Decimal-mode ADC sets N and Z from the BINARY result on
;;;    an NMOS 6502 and from the decimal result on everything later, so
;;;    $99 + $01 = $00 is reported as non-zero by a 6502 alone.  This is the
;;;    only test that is safe to run first: $FB (XCE) is an unstable
;;;    read-modify-write on NMOS, so it cannot be the one that goes first.
              sed
              lda     #0x99
              clc
              adc     #0x01
              cld
              bne     fl_no816

;;; 2. CMOS -- but a 65C02 or a 65816?  On a 65816 `clc xce` returns the old
;;;    E flag in carry; on a 65C02 $FB is a one-byte NOP and carry stays clear.
;;;    The machine is in native mode for the three instructions in between,
;;;    where the interrupt vectors move to $FFEA/$FFEE and the Atari OS has
;;;    never filled them -- so ANTIC's NMI is switched off across the window
;;;    rather than gambling on it (see src/crt_atari.s for the full story).
              lda     NMIEN
              pha
              lda     #0
              sta     NMIEN
              clc
              xce
              php                     ; carry now says what the CPU is
              sec
              xce                     ; ...back to emulation mode immediately
              plp
              pla
              sta     NMIEN
              bcc     fl_no816

;;; 3. A 65816, and now the other half of the requirement: is there any RAM
;;;    where the far image is going?  Probe the destination itself -- the copy
;;;    is about to overwrite it, so the test costs nothing and asks exactly the
;;;    right question, rather than trusting a documented memory map.
              php
              phd
              lda     #.byte1 _fl_scr
              xba
              lda     #.byte0 _fl_scr
              tcd
              lda     long:_fl_hdr
              sta     dp:DP_DST
              lda     long:_fl_hdr+1
              sta     dp:DP_DST+1
              lda     long:_fl_hdr+2
              sta     dp:DP_DST+2
              ldy     #0
              lda     #0xa5
              sta     [dp:DP_DST],y
              cmp     [dp:DP_DST],y
              bne     fl_noram
              lda     #0x5a
              sta     [dp:DP_DST],y
              cmp     [dp:DP_DST],y
              bne     fl_noram
              pld
              plp
              rts

fl_noram:     pld
              plp
              ldx     #.byte0 msg_noram
              ldy     #.byte1 msg_noram
              lda     #msg_noram_end-msg_noram
              bra     fl_fail

fl_no816:     ldx     #.byte0 msg_no816
              ldy     #.byte1 msg_no816
              lda     #msg_no816_end-msg_no816
;;; fl_fail -- print (X,Y) for A bytes on IOCB #0 and mark the machine unusable.
fl_fail:      stx     ICBAL
              sty     ICBAL+1
              sta     ICBLL
              lda     #0
              sta     ICBLL+1
              sta     _fl_ok
              lda     #PUTREC
              sta     ICCOM
              ldx     #0
              jsr     CIOV
              rts

;;; _fl_running_bank -- which bank is the far code EXECUTING in?
;;;
;;; It lives in `farcode` on purpose: `phk` then reports the bank this very
;;; routine was placed in and copied to, so a build where the far image ended
;;; up somewhere else -- or never arrived -- answers differently.  Nothing
;;; else can tell us: the emulator bridge exposes a 16-bit PC and no K
;;; register, so the target has to say it itself.
              .section farcode, root
_fl_running_bank:
              phk
              sep     #0x20
              pla                     ; the pushed bank byte
              rep     #0x20
              and     ##0x00ff        ; B holds leftovers; the C ABI wants 16 bits
              rtl

;;; The first bank above the far image, exported to src/sys/farmem.c so that
;;; the far HEAP starts where the far CODE stops.  Taken from where the code
;;; was actually placed rather than restated as a constant -- the alternative
;;; was found the hard way: farmem_probe() wrote its bank-numbering pattern
;;; into $010100 and far_alloc() handed out $010000, so the program quietly
;;; shot three bytes out of its own text, and which three moved with every
;;; rebuild.  Three VDI conformance cases failed, and a DIFFERENT three at
;;; each optimisation level, which is a very good impression of a compiler
;;; bug.
;;;
;;; `switch` shares the range and is interleaved inside it, so .sectionEnd
;;; farcode already covers it (assembler manual, section operators); `cfar`
;;; is placed ahead of the code (src/gem4xe.scm), so it is covered too.
              .section farcode
              .section cdata, rodata
_fl_heap_bank: .byte  .byte2 (.sectionEnd farcode + 0x10000)

msg_no816:    .byte   "gem4xe: this needs a 65C816 (Rapidus/Antonia)", EOL
msg_no816_end:
msg_noram:    .byte   "gem4xe: no linear RAM in bank $01", EOL
msg_noram_end:
