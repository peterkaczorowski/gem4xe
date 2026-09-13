# Phase 6 — far code: lifting the bank-$00 ceiling

**Gate:** `make test-m6` — the far image arrives intact, executes in bank `$01`,
does not overlap the far heap, stages for free, and refuses a 6502 cleanly.

    far image  : $010000-$0135E5  (13798 bytes, 2 chunks of 7936)
    copy-up    : 521/521 probed bytes match
    execution  : far code reports bank $01 (linked for $01), CPU 65C816
    far heap   : starts at bank $02 (code reaches bank $01)
    staging    : $8000-$9F13 (MEMAC A window)
    on a 6502  : CPU 6502, bank $01 $00->$00, runner did not start,
                 message on screen

## The problem

Bank `$00` gave gem4xe about 12 KB of code space once the OS, DOS, the U1MB
banking window and the MEMAC window were subtracted. The VDI alone is 15 KB, so
Phase 4 borrowed `$4000-$73FF` — U1MB's banking window — as an explicit
scaffold, taking the ceiling to ~28 KB and making the test binary's map
different from the shipping driver's.

`--code-model=large` puts every C function in section `farcode`, calls it with
`jsl` and returns with `rtl`, so code can live anywhere in the 24-bit space.
The obstacle is the file format, not the CPU: **a `.xex` segment header is two
16-bit addresses**, so a DOS loader cannot place a single byte above `$FFFF`.

## The mechanism

The far image travels as chunks aimed at a staging buffer in bank `$00`, and
DOS itself does the copying, through `INITAD`:

    seg  $8000-$8003   00 00 00 00        header zeroed before INITAD exists
    seg  $8000-$9F03   dst24 pages payload    chunk 1
    seg  $02E2-$02E3   -> _fl_copy        ...which makes DOS call the copier
    seg  $8000-$9703   dst24 pages payload    chunk 2
    seg  $02E2-$02E3   -> _fl_copy
    seg  $02E0-$02E1   -> _atari_entry    run vector

`tools/mkxex.py` builds those; `src/farload.s` is the copier. (Since phase
38 the payload is LZ-packed and the copier is an unpacker -- the chunk
page count became a 16-bit output count, and nothing else here changed; see
`phase38.md`.) Four details are load-bearing:

- **The header and its payload are one segment.** Putting the destination and
  length immediately in front of the bytes is what lets a whole chunk — where
  it goes, how long it is, and the data — be described by a single `.xex`
  segment, so the copier needs no separate parameter pass.
- **`INITAD` is rewritten after every chunk, not once.** DOSes disagree about
  whether `INITAD` is called after *every* segment or only after one that
  writes to it. Rewriting it per chunk is correct under both readings, and the
  copier zeroes its own length field so a spurious extra call — the one after
  the run-vector segment, for instance — does nothing.
- **Every chunk is a whole number of 256-byte pages**, which is what lets the
  copier be a flat page loop. The tail of a segment is made whole by sliding
  the last chunk *backwards* to a page multiple, recopying a few bytes already
  placed, rather than padding forwards into whatever follows.
- **The staging buffer lives at `$8000`, inside the MEMAC A window.** That
  region is already reserved — no code or data may be placed there, because the
  driver maps VBXE VRAM over it at run time — and it is plain motherboard RAM
  until `vbxe_init()` opens the window. Staging there costs nothing at all.

The layout is not written down twice: `mkxex.py` reads `_fl_hdr`, `_fl_buf` and
`_fl_end` out of the ELF symbol table, so `src/farload.s` and `src/gem4xe.scm`
remain the only places that decide where the buffer is and how big it is.

## What moves, and what must not

Only `farcode` and `switch` go to bank `$01`. Everything a far function reaches
for its **data** stays in bank `$00`, because the small data model addresses
globals and constants *absolute*, through the data bank register:

    lda     tab,x          ; DB-relative -- tab MUST be in bank $00
    sta     gvar

So `cdata`, `idata`, `data_init_table`, `data` and `zdata` are bank `$00` by
requirement, not by preference. A `switch` table is read with `lda long:`, so it
could sit anywhere; it travels with the code it belongs to.

The result, against Phase 4:

| | before | after |
|---|---|---|
| bank `$00` code + rodata | ~15 KB, over two regions plus a scaffold | 2.8 KB |
| `$4000-$7FFF` (U1MB window) | borrowed by the test runner | **untouched** |
| bank `$00` headroom | ~0 | 4 KB at `$A000`, plus what `$3000` has left |
| code ceiling | ~28 KB with the scaffold | 64 KB per far bank |

The scaffold is retired: the conformance runner and the shipping driver now
have the same map everywhere except `teststage`, which has a region of its own.

## The bug that looked like a compiler bug

Three VDI conformance cases failed after the switch — and **a different three at
each optimisation level.** At `-O2` the diagonal-line cases drew correct
geometry in wrong colours; at `-O2 --no-cross-call` the rectangle cases filled
to the bottom of the screen; at `-O1` the text cases were wrong.

That pattern points hard at the compiler, and I spent a long time there: reading
generated assembly, bisecting against `--no-cross-call` and
`--no-interprocedural-cross-jump`, and building a reduced case for the `db65816`
simulator (which did not reproduce). The bisection was even self-consistent —
`-O2` failed, `-O2 --no-cross-call` passed the case I was testing — and I got as
far as writing the finding down as a Calypsi bug.

**It was mine.** `farmem_probe()` writes each bank's own number to
`bank<<16 | $0100` and `far_alloc()` handed out the bottom of the first bank it
found — and the first bank it found was `$01`, which is now where the code
lives. The program was shooting three bytes out of its own text: `$010000`,
`$010100` and `$010FFF`. Which three functions those bytes belonged to depended
on the code layout, which is why the failures moved with the optimisation level
and why each new build produced a fresh, plausible-looking "miscompilation".

What finally settled it was reading the target's bank `$01` back and comparing
it against the linker's own output, byte for byte — one mismatch, at `$010000`,
holding `$C3`, which is the literal `far_write8(a, 0xC3)` in the runner's
allocator round-trip. That check is now `make test-m6`.

Two things worth keeping from this:

- **Suspect your own code longer than feels reasonable.** A symptom that moves
  with the optimisation level is not evidence of a compiler bug; it is evidence
  that something depends on code layout, and self-corruption depends on code
  layout too.
- **The bisection was measuring the wrong thing.** `--no-cross-call` really did
  make case 12 pass — by moving a different function onto the corrupted
  addresses. A flag that changes the symptom is not a flag that explains it, and
  I should have run the *whole* suite before believing the result.

The fix is `_fl_heap_bank`, exported from `src/farload.s` as
`.byte2 (.sectionEnd farcode + 0x10000)` — the first bank above wherever the
linker actually put the code. `farmem_probe()` starts probing there and
`far_alloc()` starts allocating there, so the far heap can never be handed the
memory the far code is running from, and it follows the map if the code grows
into another bank. `make test-m5` now asserts the relationship instead of
expecting bank `$01`.

(Calypsi 5.18 at `-O2` with `--code-model=large` is exonerated: 49/49 VDI cases,
4/4 AES cases and pixel-exact VBXE output, with no flags.)

## Refusing the wrong machine

Writing bank `$01` needs a 65816: on an NMOS 6502 the long store `$9F` is an
unstable undocumented opcode, so running this .xex on the wrong machine would
*corrupt memory* rather than fail. The loader therefore identifies the CPU
before its first store, using only 6502 instructions to do it:

1. **NMOS or CMOS?** Decimal-mode `ADC` sets N and Z from the *binary* result on
   an NMOS 6502 and from the decimal result on everything later, so
   `$99 + $01 = $00` is reported as non-zero by a 6502 alone. This has to be the
   test that goes first — `$FB` (`XCE`) is an unstable read-modify-write on
   NMOS, so it cannot be.
2. **65C02 or 65816?** `clc xce` returns the old `E` flag in carry on a 65816;
   on a 65C02 `$FB` is a one-byte `NOP` and carry stays clear. The machine is in
   native mode for three instructions in between, where the interrupt vectors
   move to `$FFEA`/`$FFEE` and the Atari OS has never filled them, so ANTIC's
   NMI is switched off across the window rather than gambled on.
3. **Is there RAM where the image is going?** Probed at the destination itself,
   which the copy is about to overwrite — so the test costs nothing and asks
   exactly the right question, instead of trusting a documented memory map.

Any failure prints one line through CIO — DOS is still resident and IOCB #0 is
open on E:, which is why the diagnostic can be a message rather than a wedged
machine — clears `_fl_ok`, and writes nothing. `_atari_entry` reads `_fl_ok`
*before* it disables NMI and IRQ, and returns to DOS with the machine intact.

`make test-m6` boots the same disk without switching the CPU and requires all
of it: the CPU still a 6502, bank `$01` unchanged, the runner not started, and
the message on screen.

## What this does not do yet

- **Only `farcode` is far.** `cdata` is 2.5 KB of bank `$00` and most of it is
  the 8×8 font. Moving bulk constants out needs `--data-model=medium` or `__far`
  on the arrays, and it is not urgent while `$A000-$AFFF` is empty.
- ~~**One far bank.** `FarCode` is bank `$01` only.~~ Done, see the follow-up
  below: one memory per bank, `$01-$0F`, and `_fl_heap_bank` replaced by the
  loader's `_fl_top`.
- **The chunk loop is byte-at-a-time**, ~10 cycles per byte through
  `lda [dp],y` / `sta [dp],y`. At 14 KB that is invisible; `MVN` would need
  native mode and the interrupt-vector work, and this runs before the program
  starts, when a saved NMIEN is the only thing standing between us and the
  vectors the OS never filled.

## Follow-up (2026-09-02): spilling into the next bank

Bank `$01` was 89.6% full — 58,715 bytes of far code — once the benchmark
runner was in, and the next AES pieces (`form_alert`, icons, the desktop)
would not have fitted. The far code is now allowed to spread over banks
`$01-$0F`: the first megabyte, which on a Rapidus is the SRAM, and the budget
the project has set itself.

**Why one linker memory per bank and not one memory spanning them.** The
obvious edit — `(address (#x010000 . #x0fffff))` — links, and is wrong: the
linker treats the range as flat and placed `style_line` at `$01FFCE`, running
into `$020000`. The 65816's program counter wraps within its bank, so that
function would execute as two unrelated halves. Given fifteen memories that
each list the same sections, the linker fills them in the order they are
defined and never splits a fragment: `gr_inside` (50 bytes) was packed into
the gap at `$01FFCE-$01FFFF` and `style_line` moved whole to `$020000`. A
fragment is a function, a factored `?L` piece, a `switch` table or `cfar`,
none of which may straddle a bank — the tables are read with long addressing
and could sit anywhere, but they travel with the code.

`src/gem4xe.scm` generates the fifteen memories in Scheme, which is what the
linker's script language is:

    (define (far-bank b first)
      (list 'memory (string->symbol (string-append "FarCode" (number->string b 16)))
            (list 'address (cons first (+ (* b #x10000) #xffff)))
            '(section cfar farcode switch)))

with `(layout far-start)` building the whole list and
`(define memories (layout #x010000))` the default.

**What it cost: `.sectionEnd farcode`.** The far heap's first bank had been a
linker-derived byte, `.byte2 (.sectionEnd farcode + 0x10000)`, and the linker
refuses that operator once the section is in more than one memory:

    section farcode is placed in multiple memories (FarCode1 and FarCode2),
    cannot apply .sectionEnd operator

There is no memory-end operator either. So the number now comes from the
loader instead of the linker: `_fl_top`, three bytes in the `code` section
next to `_fl_ok` (so DOS loads them as zeros with every run and cstartup never
touches them), which `_fl_copy` raises to one past the end of every chunk it
copies — a maximum, because the tail of a segment is slid *backwards* to a
page boundary and the last chunk is not always the highest. `farmem_probe()`
and `far_alloc()` start at `(_fl_top + $FFFF) >> 16`, the bank above whatever
actually arrived. That is the more honest number anyway.

**The RAM probe moved from once to per chunk.** The first-call check probed
the first chunk's destination and reported "no linear RAM in bank $01"; a bank
that exists says nothing about the next one, so `_fl_copy` now writes `$A5`
and `$5A` to each chunk's own destination before copying it, and the message
carries the bank it stopped at. That path was exercised once by hand: a
hand-built `.xex` with a chunk aimed at `$FF0000`, the Rapidus register page,
after a real chunk in bank `$01` — the first copied, the second was refused
with `gem4xe: no linear RAM in bank $FF`, `_fl_ok` was cleared, `_fl_top` said
`$010400`, and the runner did not start. It is not a gate, because Altirra's
Rapidus has no configuration without RAM in the far banks, and it has not
been seen on hardware.

**Proving the spill before it happens.** The real build still fits in bank
`$01`, so a gate on it alone would leave the second bank untested until the
day the code grows into it. `make test-m6` therefore builds a second image,
`build/m6split`, from the same objects with
`--memories-expression "(layout #x01c000)"` — bank `$01` cut to its top 16 KB
— and boots both:

    == m3: the real build
    far image  : $010000-$01E5A8  (58793 bytes, 8 chunks of 7936)
    copy-up    : 1166/1166 probed bytes match
    execution  : far code reports bank $01 (_fl_running_bank linked at $01E205)
    far heap   : loader wrote up to $01E5A9 (image ends $01E5A9);
                 heap starts at bank $02, code reaches $01

    == m6split: bank $01 cut to 16 KB, forcing the spill
    far image  : $01C000-$01FFFF  (16384 bytes, 3 chunks of 7936)
    far image  : $020000-$02A5A8  (42409 bytes, 6 chunks of 7936)
    copy-up    : 1264/1264 probed bytes match
    execution  : far code reports bank $02 (_fl_running_bank linked at $02A205)
    far heap   : loader wrote up to $02A5A9 (image ends $02A5A9);
                 heap starts at bank $03, code reaches $02

The expected running bank is where the linker put `_fl_running_bank`, read
from the symbol table, not "the first far segment": in the split build that
routine is in bank `$02`. Every far segment is probed at its chunk seams, and
`_fl_top` is read back and required to match the ELF's image end (a padded
sub-page tail may put it up to 255 bytes high).

**What the profiler can no longer be sure of.** The bridge masks the
profiler's addresses to 16 bits, so `make bench --profile`'s attribution of an
address to a function goes through the map's far sections; with code in two
banks an address whose low 16 bits fall inside sections in both is ambiguous,
and `tests/emu/bench_vdi.py` now names every candidate joined with `|` rather
than picking one. In the real build there is one bank and no ambiguity.

Gates: `make test` green, `make movie` unchanged. Everything here ran in
Altirra; nothing has been tried on a Rapidus.
