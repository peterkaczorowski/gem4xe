# The benchmark — GEMBench's tests, on this machine

Status: **running**, not a gate. `make bench` prints the table below in
about four minutes; `python3 tests/emu/bench_gem.py --profile dialog text`
adds Altirra's instruction profiler per row. The first two runs found the
two hottest functions in the AES and cut every drawing row by a third to
two fifths, and pinned a seventh compiler defect on the way.

Everything here is measured in Altirra — a Rapidus at 11×, VBXE FX 1.26,
PAL — and not on hardware. The emulator's bus model is the emulator's;
where a number below says more about Altirra than about the boards, it
says so.

## What it is, and what it is not

GEMBench (Exxos) times a GEM dialog box, VDI text (plain, with effects,
small), VDI graphics, a GEM window, integer division, float maths, RAM
and ROM access and blitting, and prints each as a percentage of a stock
ST. Its source is not public and no ST is measured here, so the numbers
cannot be relative: `tests/emu/bench_gem.py` keeps GEMBench's headings
and reports **milliseconds per unit of work and units per second**. Two
of the headings do not apply — `vst_effects` is a no-op in this driver,
as in DRI's own, and there is one font — and are printed as such rather
than dropped.

The VDI and AES rows are the harness's own scripts (`tests/emu/m3..m8`)
run for time instead of for pixels: the same `objc_draw` of the same
five-object dialog that `make test-m4` compares against `tools/aesref.py`,
the same window that `make test-m8` opens with every gadget. The CPU and
memory rows are ops in the runner (`src/m3_vdi.c`, `bench_op`, opcodes
`2000+n`): two divides, a float expression, and read/write/copy loops over
256 bytes through a far pointer whatever the space, so the rows differ
only in the bus behind them.

## Timing

A frame is too coarse — the smallest rows take a fifth of one — and a
watchpoint is too fine: a halt inside a bridge `FRAME` leaves the gate
closed and the session wedged. So the clock is the VCOUNT tick, two
scanlines, 0.13 ms:

- the runner stamps `VCOUNT` into `STATUS[5]` when it sees `GO` and into
  `STATUS[6]` just before it sets `DONE`, and reports how many values a
  frame has in `STATUS[7]` (156 on PAL — measured, not assumed);
- the host reads the emulator's cycle counter at the frame boundary
  before `GO` and at the one where `DONE` is seen, and takes the idle
  tail between `DONE` and that boundary off using the two stamps;
- an empty script costs 203 cycles (125 µs) and comes off every row.

One correction that cost a run to find: the cycle counter Altirra exposes
leaves out the cycles ANTIC halts the CPU for — 32,520 a PAL frame, not
35,568 — so converting cycles to milliseconds through the clock rate
under-reports by 8.6%. Cycles become milliseconds through the frame
instead: a frame is `period` ticks of two 114-cycle lines at the machine
clock, 20.06 ms, and `ms = cycles / cycles_per_frame × frame_ms`.

Each row is repeated until its measured total reaches 300 ms or 40
runs, so the tick's resolution does not show in the ms column.

## The first run

The memory rows came out identical everywhere — 1,952 cycles per 256
bytes in fast SRAM, in SDRAM, in the OS ROM — which meant the loop, not
the bus, was being measured: a byte loop with stack locals is 24
instructions a byte on this compiler. The loops now move words, counting
down, with the counter and the sum in the direct page (`do ... while
(--n)`, unsigned: a signed compare costs six instructions), and the bus
shows. That is the baseline:

| Row | ms | rate | cycles/unit |
|---|---:|---:|---:|
| form_dial START, objc_draw of 5 objects, form_dial FINISH | 77.7 | 12.9 /s | 126,034 |
| objc_draw of the dialog alone | 66.9 | 15.0 /s | 108,425 |
| v_gtext, 40 characters, even x | 26.6 | 37.6 /s | 43,120 |
| v_gtext, 40 characters, odd x | 27.0 | 37.0 /s | 43,819 |
| vr_recfl 100×50, solid | 0.85 | 1,178 /s | 1,376 |
| vr_recfl 100×50, pattern 4 | 4.34 | 231 /s | 7,036 |
| box 100×50 as a 5-point v_pline | 2.49 | 402 /s | 4,034 |
| diagonal 100×100 v_pline | 2.31 | 432 /s | 3,753 |
| wind_create, open (400×160, every gadget), close, delete | 119.3 | 8.4 /s | 193,418 |
| 16-bit signed divide (`_Div16`) | 0.040 | 25,091 /s | 65 |
| 32-bit signed divide (`_Div32`) | 0.133 | 7,512 /s | 216 |
| float32: multiply, add, divide | 0.260 | 3,839 /s | 422 |
| read, bank $00 / far bank $02 / far bank $EF | 0.376 | 665 KB/s | 610 |
| read, bank $00 $A000 (the slow block) | 0.537 | 465 KB/s | 871 |
| read, VRAM through the MEMAC window | 0.554 | 451 KB/s | 899 |
| write, bank $00 / far bank $02 | 0.469 | 533 KB/s | 760 |
| write, bank $00 $A000 (the slow block) | 0.671 | 373 KB/s | 1,088 |
| write, VRAM through the MEMAC window | 0.687 | 364 KB/s | 1,113 |
| copy, bank $00 ↔ bank $00 / far bank $02 | 0.658 | 380 KB/s | 1,066 |
| copy, bank $00 to VRAM through the window | 0.858 | 292 KB/s | 1,390 |
| vram_write, the driver's upload, 256 bytes | 1.404 | 178 KB/s | 2,276 |
| read, the OS ROM at $E000 | 0.377 | 664 KB/s | 611 |
| vro_cpyfm 320×100 screen to screen, aligned | 2.28 | 439 /s | 3,690 |
| vro_cpyfm 320×100 screen to screen, odd x (the pixel path) | 3,486 | 0.3 /s | 5,652,125 |
| vrt_cpyfm 32×24 icon, transparent | 1.59 | 630 /s | 2,576 |
| vrt_cpyfm 32×24 icon, replace | 1.56 | 640 /s | 2,534 |

Two things the memory rows say, both about Altirra rather than the
boards. The OS ROM reads as fast as the accelerator's SRAM, and SRAM bank
`$02` reads exactly as SDRAM bank `$EF` does: the emulator's fast-bus
layers have one speed. Hardware will not agree on the first and may not
on the second. The slow block at `$A000` — the 16 KB window `rapidus.c`
leaves on the motherboard bus because the MEMAC window lives there — costs
about one machine cycle per bus byte over the fast bus (871 against 610
for a 256-byte read), and the MEMAC window itself costs the same as the
slow RAM around it. So the CPU reads VRAM at 450 KB/s and writes it at
360 KB/s, which is what the driver's whole architecture assumes; the
driver's own upload managed half that, and that is the first find.

## What the profile named

`--profile` runs Altirra's instruction profiler across one script of a
row and sums it per function, the attribution `tests/emu/bench_vdi.py`
already had. Over the dialog, the text and the window rows the top three
were the same three functions:

    form_dial START, objc_draw, FINISH      364,927 insns/unit
      bcb_common       102,030 insns/unit    28%
      vram_write        68,446               19%
      _Mul16            33,164                9%
    v_gtext, 40 characters                  124,644 insns/unit
      bcb_common        42,960               34%
      vram_write        34,280               28%
      _Mul16            13,600               11%

`bcb_common` builds a blitter control block. It was 537 instructions a
block: it started by zeroing the 21 bytes in a loop through a stack
pointer, which this compiler renders at 20 instructions a byte, then
stored the fields through the same pointer. A glyph is two blocks (the
AND strip and the OR strip), so 1,074 instructions a character went into
laying out 42 bytes — a third of the text row.

`vram_write` copies a block into VRAM through the MEMAC window; every
block goes through it, 21 bytes at a time, and it was a byte loop at 20
instructions a byte.

`_Mul16` is the compiler's 16-bit multiply, 42 instructions; eight of
them a glyph, from the row-base arithmetic in `v_gtext` and `blit_mask`.

## Two rewrites

**`bcb_common`** (`src/vbxe/vbxe.c`) now overlays the 21 bytes with a
struct of the fields they are and stores each once, no zero loop: 40
instructions a block, from 537. Nothing changes on the wire — the
callers still set the mask, pattern and mode bytes after it, as before.

**`vram_write`** moves words, counting down, with the pointers and the
count in the direct page — 15 instructions a word, from 40 — and the
window pointer is deliberately *not* `volatile`: the compiler will not
store a word through a volatile pointer without a stack temporary, and a
store through a pointer it cannot see past is not one it can drop. The
driver's upload row is the check: 1,117 cycles per 256 bytes, from 2,276,
which is now the benchmark's own write loop (1,113) — the bus, not the
code.

After both:

| Row | before | after | |
|---|---:|---:|---|
| form_dial START, objc_draw, FINISH | 77.7 ms | **50.7 ms** | −35% |
| objc_draw of the dialog alone | 66.9 | **41.0** | −39% |
| v_gtext, 40 characters, even x | 26.6 | **15.4** | −42% |
| v_gtext, 40 characters, odd x | 27.0 | **15.8** | −42% |
| vr_recfl 100×50, solid | 0.85 | **0.70** | −18% |
| vr_recfl 100×50, pattern 4 | 4.34 | **3.18** | −27% |
| box 100×50 as a 5-point v_pline | 2.49 | **1.63** | −34% |
| diagonal 100×100 v_pline | 2.31 | 2.31 | the line stepper, unchanged |
| wind_create, open, close, delete | 119.3 | **80.8** | −32% |
| vram_write, 256 bytes | 1.40 | **0.69** | 178 → 363 KB/s |
| vro_cpyfm 320×100 aligned | 2.28 | **2.15** | |
| vrt_cpyfm 32×24 icon | 1.59 / 1.56 | **1.49 / 1.47** | |

The memory and arithmetic rows did not move, as they should not have.
`make test-m3` is 68/68 and `make check-cc` passes with the change, so
the seam under every gate is the same seam, faster.

A 40-character line is now 0.38 ms — 623 cycles — a character. The
profile after the rewrite puts the rest where it was expected:
`vram_write` at 450 instructions a character (two blocks, 15 a word plus
the mapping), `_Mul16` at 340, `vdi_v_gtext` itself at 238.

## Compiler bug B7, found by the rewrite

The struct overlay was guarded with
`_Static_assert(sizeof(BCB) == 21, ...)`, and the assertion failed —
while the generated code laid the struct out in 21 bytes and every gate
passed. `sizeof` of a struct with 16-bit members after a byte member
evaluates to the padded size (26 for the BCB, 6 for a `{u16, u8, u16}`) *where an
integer constant expression is required* — an enum, an array bound, a
static assertion — and to the unpadded size everywhere else, and the
code generator uses the unpadded layout throughout. So an array of such
structs indexed through an enum stride reads the wrong element.

That is the seventh defect this tree has met and the first in the front
end's constant arithmetic rather than in code generation;
`tools/ccbug/bugs.c` reproduces it in the vendor's simulator (bug 17411,
fix 801) and `make check-cc` reports "9 of 9 bug shapes still present".
Rule 7 in `tools/ccbug/README.md`: never `sizeof` a struct where a
constant expression is required; write the byte count out. `BCB_SIZE`
is 21 by name and the assertion is gone.

## What is left, in order

- **`_Mul16`, eight a glyph.** The row base (`y × stride`) is computed
  per glyph in `v_gtext` and again in `blit_mask`; hoisting it to once a
  string and stepping by the stride would take ~300 instructions off each
  character, a fifth of the row.
- **The upload loop in assembly.** 15 instructions a word is the
  compiler's; `lda [s],y / sta [d],y` with a DP count is 5, and every
  block passes through it. Bounded, ~30 lines, and the second fifth.
- **`vro_cpyfm` at odd x.** 3.4 *seconds* for 320×100 — 98 µs a pixel
  through the window. The AES never takes this path (window x snaps to
  even, Phase 8) but an application can, and it should be a nibble-mode
  blit with a pre-shifted copy, like the odd-x glyph strips, not a pixel
  loop.
- The dialog and window rows are dominated by what they draw, not by the
  AES: `objc_draw` alone is 41 of the 51 ms. After the two items above
  the next profile decides, not this list.

## Running it

    make bench                                     # every row, ~4 min
    python3 tests/emu/bench_gem.py text blit       # groups by substring
    python3 tests/emu/bench_gem.py --profile --top 12 dialog
    python3 tests/emu/bench_gem.py --json out.json # for diffing runs
    python3 tests/emu/bench_gem.py --list

It uses the m3 runner's disk (`build/m3-boot.atr`) and cannot share the
emulator with another gate; `make bench` waits for the build, not for a
running `AltirraSDL`.
