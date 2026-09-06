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
;;; So the linker places `farcode` in the banks above -- one memory per bank
;;; from $01 upwards, inside the accelerator's first megabyte (src/gem4xe.scm)
;;; -- and the far image travels in the .xex as a series of chunks aimed at a
;;; staging buffer in bank $00.  DOS calls through INITAD ($02E2) after
;;; loading a segment, and this is what it calls: it copies one chunk from
;;; the buffer to its real home and returns.  By the time DOS reaches the run
;;; vector the far image is assembled.
;;;
;;; The copier does not know or care which banks the linker chose: every
;;; chunk carries its own 24-bit destination.  What it does keep is _fl_top,
;;; the highest address it has written past, so that the far heap can start
;;; above whatever actually arrived (src/sys/farmem.c).
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
;;; Writing above bank $00 needs a 65816: on an NMOS 6502 the long store
;;; `sta $9F` is an unstable undocumented opcode, so running this on the wrong
;;; machine would corrupt memory rather than fail.  The CPU is therefore
;;; identified before the first store, and linear RAM is probed rather than
;;; assumed -- a 65816 with nothing where a chunk is going is just as fatal
;;; and much less obvious, so EVERY chunk's destination is probed before it is
;;; written, not only the first.  Either failure prints a line, marks the
;;; machine unusable and returns to DOS.
;;; ---------------------------------------------------------------------------

              .rtmodel version, "1"
              .rtmodel core, "*"

              .public _fl_copy, _fl_ok, _fl_top
              .public _fl_hdr, _fl_buf, _fl_scr
              .public _fl_running_bank

FL_CHUNK:     .equ    0x1b00          ; staging payload: 27 whole pages -- with
                                      ; the 20 bytes around it, what fits the
                                      ; Stage memory (src/gem4xe.scm), which
                                      ; stops under SpartaDOS X's screen

;;; CIO, for the two failure messages.  DOS is still resident and IOCB #0 is
;;; open on E: at this point, which is the whole reason the diagnostics can be
;;; a printed line rather than a wedged machine.
CH:           .equ    0x02fc          ; the OS's last key, $FF when none
ICHID:        .equ    0x0340          ; IOCB #1: the DOS's own load channel
ICCOM:        .equ    0x0342
ICBAL:        .equ    0x0344
ICBLL:        .equ    0x0348
IOCB1:        .equ    0x0010          ; the offset of IOCB #1 from IOCB #0
CIOV:         .equ    0xe456
PUTREC:       .equ    0x09
CLOSE:        .equ    0x0c
EOL:          .equ    0x9b
DOSVEC:       .equ    0x000a          ; the DOS's own re-entry

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

;;; _fl_top -- one past the highest far address written so far, 24-bit
;;; little-endian; the far heap starts in the bank above it.  Where the far
;;; image ends is decided by the linker, but it is spread over several
;;; memories and the linker has no operator for "the end of a section that is
;;; in several memories" -- so the loader records where it actually put
;;; things, which is the more honest number anyway.  Like _fl_ok it lives in
;;; `code`, so DOS loads the zeros fresh with every run and cstartup, which
;;; only touches `data` and `zdata`, never sees it.
_fl_top:      .byte   0, 0, 0

;;; ---------------------------------------------------------------------------
;;; _fl_copy -- DOS calls this through INITAD after each chunk segment.
;;; ---------------------------------------------------------------------------
_fl_copy:
              lda     fl_checked
              bne     fl_ready
              jsr     fl_check        ; first call: identify the machine
fl_ready:
              lda     _fl_ok
              beq     fl_out          ; wrong machine -- never write far RAM
              lda     _fl_pages
              bne     fl_go           ; something is staged
fl_out:       rts                     ; nothing staged (the priming call)
fl_go:

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

;;; Is there RAM where this chunk is going?  Probe the destination itself --
;;; the copy is about to overwrite it, so the test costs nothing and asks
;;; exactly the right question, rather than trusting a documented memory
;;; map.  Every chunk is probed because the image may spill into a further
;;; bank, and the first bank having RAM says nothing about the next.
              ldy     #0
              lda     #0xa5
              sta     [dp:DP_DST],y
              cmp     [dp:DP_DST],y
              bne     fl_noram
              lda     #0x5a
              sta     [dp:DP_DST],y
              cmp     [dp:DP_DST],y
              bne     fl_noram

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

;;; DP_DST is now one past the chunk; raise _fl_top to it if it is higher.
;;; Chunks arrive in address order today, but the tail of a segment is slid
;;; BACKWARDS to a page boundary (tools/mkxex.py), so "the last chunk" and
;;; "the highest chunk" are not the same thing, and a maximum is what is
;;; wanted.
              sec
              lda     dp:DP_DST
              sbc     long:_fl_top
              lda     dp:DP_DST+1
              sbc     long:_fl_top+1
              lda     dp:DP_DST+2
              sbc     long:_fl_top+2
              bcc     fl_nottop       ; DP_DST < _fl_top
              lda     dp:DP_DST
              sta     long:_fl_top
              lda     dp:DP_DST+1
              sta     long:_fl_top+1
              lda     dp:DP_DST+2
              sta     long:_fl_top+2
fl_nottop:
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

;;; 3. A 65816.  The other half of the requirement -- RAM where the image is
;;;    going -- is checked chunk by chunk in _fl_copy, since the image may
;;;    reach banks this first call knows nothing about.
              rts

;;; fl_noram -- a chunk's destination is not RAM.  Reached from _fl_copy with
;;; its direct page still selected and DP_DST naming the bank, which is put
;;; into the message so the user learns WHERE the machine stops, not just
;;; that it does.
fl_noram:     lda     dp:DP_DST+2
              pha
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              jsr     fl_hex
              sta     msg_noram_bank
              pla
              and     #0x0f
              jsr     fl_hex
              sta     msg_noram_bank+1
              pld
              plp
              ldx     #.byte0 msg_noram
              ldy     #.byte1 msg_noram
              lda     #msg_noram_end-msg_noram
              bra     fl_fail

fl_hex:       cmp     #10
              bcc     fl_hex_d
              adc     #6               ; carry is set: +7, so 10 -> 'A'
fl_hex_d:     adc     #'0'
              rts

fl_no816:     ldx     #.byte0 msg_no816
              ldy     #.byte1 msg_no816
              lda     #msg_no816_end-msg_no816
;;; fl_fail -- say why on IOCB #0, wait to be read, and give the machine
;;; back.
;;;
;;; Printing is not enough on its own.  This runs from INITAD, inside the
;;; DOS's own read loop, and the DOS goes on to read the other hundred
;;; kilobytes whatever we do -- a minute of a floppy grinding after a
;;; message nobody is still reading.  So the load is ABANDONED: the DOS's
;;; file is closed (IOCB #1, the channel every DOS 2 and SpartaDOS loads
;;; a binary on) and DOSVEC takes the machine back to its prompt.
;;;
;;; And the message is WAITED ON, because coming back is what wipes it: a
;;; DOS 2 redraws its menu over the top and a SpartaDOS prints its banner.
;;; A key -- read from the OS's own CH, so no IOCB has to be opened at a
;;; moment when the DOS owns them -- is what says it has been read.
;;; _fl_ok stays clear as well, so a DOS that somehow returns here finds
;;; the entry point refusing too.
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
              ldx     #.byte0 msg_key
              ldy     #.byte1 msg_key
              stx     ICBAL
              sty     ICBAL+1
              lda     #msg_key_end-msg_key
              sta     ICBLL
              lda     #0
              sta     ICBLL+1
              lda     #PUTREC
              sta     ICCOM
              ldx     #0
              jsr     CIOV
              lda     #0xff
              sta     CH              ; drop whatever was already typed
fl_anykey:    lda     CH
              cmp     #0xff
              beq     fl_anykey
              lda     #0xff
              sta     CH
              lda     ICHID+IOCB1     ; $FF when the channel is not open
              cmp     #0xff
              beq     fl_gone
              lda     #CLOSE
              sta     ICCOM+IOCB1
              ldx     #IOCB1
              jsr     CIOV
fl_gone:      ldx     #0xff
              txs                     ; the DOS's own re-entry wants its stack
              jmp     (DOSVEC)

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

;;; The messages are `cdata`, which is bank $00 RAM: the bank number in the
;;; second one is filled in by fl_noram before it is printed.
              .section cdata, rodata
;;; Two lines, and the second one is the important one: a machine that
;;; cannot run gem4xe has not been damaged by finding out.  Forty columns.
msg_no816:    .byte   "gem4xe needs a 65C816: this is a 6502.", EOL
msg_no816_end:
msg_noram:    .byte   "gem4xe: no linear RAM in bank $"
msg_noram_bank:
              .byte   "xx", EOL
msg_noram_end:
;;; The second line, and the one that matters: the machine was not
;;; damaged by being asked.  One record a line -- CIO's PUT RECORD stops
;;; at the first EOL, whatever length it was given.
msg_key:      .byte   "Nothing was changed.  Press a key.", EOL
msg_key_end:
