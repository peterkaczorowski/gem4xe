# Phase 8b — a session with the AES on film, and the cost of a pixel

Status: **complete.** `make movie` runs a scripted session of the AES on
the emulated Rapidus + VBXE machine — menu, dialog, a window opened,
dragged, sized, covered, fulled and closed — checked as a gate is, and
writes `build/movie/gem4xe.mp4` and `.gif`. Making it pass found the
project's second large performance bug and one behavioural one, in the
VDI this time: a pixel plotted through the MEMAC window costs 60–80 µs,
which made the pointer 12 ms a draw and an icon three frames; and the
pointer was clipped by `vs_clip`, on the target and in the model alike.
After it: m3 65/65 (four cases added), m4 12/12, m7 10/10, m8 12/12,
m9 4/4, `make demo` pixel-exact, and the movie's 135 records, 8 shots
and last screen all match the reference.

The question that started it was whether there was more to show than a
mock-up screenshot. `make demo` paints a desktop through the VDI; it is
pretty and proves nothing about the AES. This is the AES itself, driven
by the pointer and button the harness feeds the emulator, the way the
gates drive it, with every returned word compared against
`tools/aesref.py` and the screen compared at each "shot" step and at
the end. A frame that looks right but is not what the model draws fails
the run. It runs in Altirra — no real hardware was involved.

## The harness (`tests/emu/demo_aes.py`)

The session is a script in the gates' own record format, written the
way an application would write it: a desktop tree with two icons is
handed to the AES (`wind_set(WF_NEWDESK)`) and drawn, the menu bar
shown, the pointer form set and shown; then

1. the pointer walks to Desk and pulls About, answered with
   `form_dial(FMD_START/GROW)`, `objc_draw`, `form_do`, `FMD_SHRINK/FINISH`;
2. a double-click on the floppy icon opens a window, whose contents the
   application draws through the shared VDI in reply to `WM_REDRAW`;
3. the window is dragged by its name bar and sized by its sizer;
4. About is opened over it — `form_dial(FMD_FINISH)` now sends the
   window a `WM_REDRAW` for the area the dialog covered, the donor's
   shape, which had been a comment until a window existed to receive it;
5. the fuller, then the closer.

Three things the gates did not need:

**Chapters.** The runner's buffers hold 48 result records and 1,024
script words; the session is 135 records. `chapters()` splits it where
the buffers require, the first chapter carrying the prelude, and the
model runs it whole through a new `aesref.resume()` — the target's state
carries from one chapter to the next unless a script starts it over with
`GSX_START`, so the model's must too. The buffers were doubled
(`SCRIPT_WORDS` 512 → 1024, `SCRATCH_BYTES` 1024 → 2048) by giving the
runner 6 KB of the `$A000-$BFFB` region: `Near2` had never held a byte.

**The camera.** `Camera` wraps the bridge's `frames()` so each frame the
harness runs is one `FRAME` and one `SCREENSHOT`; ffmpeg assembles the
PNGs. Nothing in the drive loop changes, so what is filmed is exactly
what a gate would have run.

**The plan-tick rule.** An `evnt_multi` with a timer that a plan is
meant to end early must be given a timeout longer than the plan: the
AES's tick is a frame (20 ms PAL), a step is a frame, so
`wait(ticks * 20)` where `ticks` counts the plan's steps. A shorter
timer returns on its own, the plan's remaining steps land on the next
record, and the harness reports "op k did not complete on its plan" —
which is also what a genuinely late op reports, and the ambiguity cost
an hour.

One obligation the movie made explicit: `form_do` returns the exit
object *selected*, and the application must `objc_change` it back
before the dialog is drawn again — the gates' dialogs were drawn once.

## The finding: 60–80 µs a pixel

Chapter 1 failed on the sizer: the `WM_SIZED` wait's result count came
one frame after the plan's last step, every time, while the mover in the
same chapter passed. Both waits end the same way — the control manager
erases its rubber box and shows the pointer inside the release — so the
sizer's two boxes against the mover's one pointed at the erase, and a
benchmark settled it. A script fed the runner one primitive four times
and counted the frames between result records (it grew into
`tests/emu/bench_vdi.py`, which is kept):

| op, on the Phase 8 driver | frames |
|---|---|
| `vrt_cpyfm` 32×24, every bit set, transparent | **3 each** |
| the same form, no bit set (768 tests, 0 plots) | 0.7 each |
| `vrt_cpyfm` replace (768 plots) | 3–4 each |
| pointer show + hide (≈200 plots) | 0.75 a pair |
| `v_pline` diagonal, 100 px | 0.75 each |
| `v_pline` horizontal, 600 px (one blit) | < 0.25 |
| `vro_cpyfm` 64×24 on screen (one blit) | < 0.25 |

So the loop is fast — the CPU runs at 20 MHz from SRAM, as Phase 7
arranged — and `plot()` is the cost: 768 plots in three frames is
~75 µs a pixel, ~1,500 cycles. Each plot is a 32-bit multiply for the
row, a read and a write through the MEMAC window at 1.79 MHz with a bank
compare each, and the clip test. A pointer show was 12–15 ms, most of a
frame; every `v_hide_c`/`v_show_c` pair the AES wraps around a
primitive cost that twice; the window's four icons appeared one per
60 ms, and the sizer's release — erase two dotted boxes, show the
pointer — ran past the frame it was due in. The mover passed by phase.

The fix is the one the text path has used since Phase 2b: expand once
into 4bpp strips in VRAM and let the blitter write.

**The pointer** (`cursor_expand`, `cursor_paint` in `src/vdi/vdi.c`).
`vsc_form` expands the form into four 16×16-byte strips at `VR_CURSOR`:
for each parity of x an AND strip ($0 under the mask or the data, $F
elsewhere) and an OR strip (the data colour under the data, the mask
colour under the rest of the mask, $0 elsewhere). A show is the save
copy and the two blits, one chain, wherever the pointer is; a hide is
the restore copy. The strips are clipped by the screen the way the save
already was — the same byte span, read from the same offset into the
strip. `VR_CURSOR` follows the line strips at the next 256-byte boundary
and a typedef-array check refuses a layout in which the four strips
straddle a MEMAC page, since they are written through one mapping.

**Icons** (`vdi_vrt_cpyfm`). The source form is in RAM, out of the
blitter's reach, but the blitter can do the writing. The destination
rectangle is clipped to the pixel — `vs_clip`, then the screen — and
expanded through one mapping into an AND strip and an OR strip at
`VR_STRIP`, half a page each; a band is as many rows as half a page
holds at the span's width, so a 32-wide icon is one band and a
full-width form is forty. Each mode is a pair of nibbles per source bit
— transparent `(0, fg)/(F, 0)`, erase `(F, 0)/(0, bg)`, replace
`(0, fg)/(0, bg)`, XOR a single strip of `F/0` under one XOR blit — and
a pixel outside the clip is `(F, 0)`, so the strips carry the clipping
and the blit is rectangular. A strip that would change nothing (all-$F
AND, all-$0 OR) is not blitted, which makes transparent in pen 0 one
blit. `VR_STRIP` is the first page boundary after the save buffer,
derived from `VR_SAVE + SCR_BYTES` rather than written down.

| op, after (`tests/emu/bench_vdi.py`, 16 of each) | ms | frames |
|---|---|---|
| `vrt_cpyfm` 32×24, every bit set, transparent | 18.8 | 0.94 |
| the same form, no bit set (no blit at all) | 18.8 | 0.94 |
| `vrt_cpyfm` replace | 18.8 | 0.94 |
| `vrt_cpyfm` XOR | 17.5 | 0.88 |
| `vsc_form` (576 strip bytes through the window) | 12.5 | 0.63 |
| pointer show + hide | 1.8 | 0.09 |
| `v_pline` diagonal, 100 px (100 plots) | 10.3 | 0.52 |
| `v_pline` horizontal, 600 px (one blit) | 2.5 | 0.13 |

The pointer is eight times cheaper and the sizer's release fits its
frame, which is what the chapter needed. The icon is three times
cheaper and still most of a frame — and the no-bit-set row says why:
the blits are not the cost, the C loop that builds the strips is, at
~24 µs a source pixel.

### Is the CPU fast?

That number, and 100 clipped-out plots costing 5 ms, looked like a CPU
on the 1.79 MHz bus, which Phase 7 only ever verified by reading the
Rapidus registers back. Altirra's bridge has a profiler
(`PROFILE_START mode=insns`, `PROFILE_DUMP`; `bench_vdi.py --profile`
runs it across an op and names the hot addresses), and it settles it: eight
fully clipped 600-pixel diagonals ran **968,420 instructions in 355,680
machine cycles — 0.37 cycles an instruction**, 2.7 instructions per
1.79 MHz cycle, which is a 65C816 at 11× with almost every access in
SRAM. The runner reads back `MCR $FC`, `CMCR $40`: windows 0 and 1
fast, the MEMAC window's block slow by design, fast writes on
`$0000-$3FFF`. The clock is fine. The cost is the instruction count:
**a Bresenham step that plots nothing is ~200 instructions.** The
profile's hottest address is a three-instruction loop run 7.5 times a
pixel — `1u << (15 - (bit & 15))`, the line-style mask, which the
compiler shifts in a loop — and after it come `paint_pixel`, `plot`
and `plot_visible` as separate far calls, each entered through a stack
frame, with 16-bit signed comparisons at four instructions apiece and
the row address as a 32-bit multiply. `--speed` changes nothing
measurable against the default `--space` (19 bytes in `vdi.c`);
`--no-cross-call` removes 202 `jsl`/`rtl` pairs for 5% more code and
would buy a few percent. The real gains are algorithmic and on the
list below: a rotating mask instead of the shift, a row-base table
instead of the multiply, trivial rejection of a line against the clip
before its loop, and for `vrt_cpyfm` a nibble-pair table that turns
the per-pixel expansion into per-byte lookups.

`plot()` itself is unchanged and still serves diagonal lines and the
per-pixel fallbacks; the AES draws none.

## The pointer and the clip rectangle

Found on the way: `cursor_paint` plotted through `plot()`, which honours
`vs_clip`, so the pointer was clipped by whatever rectangle the
application had set — and the model's `_cursor_paint` plotted through
`_plot`, which honours it too. The two agreed, the gate was green, and
both were wrong: GEM's pointer is drawn wherever it is on the screen.
In the movie the application shows the pointer while its clip is the
window's work area, and the arrow would have been cut at the frame. The
model now paints the pointer through `_plot_raw` (screen bounds only)
and the blitted pointer ignores the clip by construction;
`cursor ignores vs_clip` is an m3 case. It is the second time the model
and the target have shared a bug (Phase 8's overlap copy was the first),
and the same lesson: the model is written from the donor and the manuals
so that its agreement means something, and a rule the manuals state —
here, that the pointer is not clipped — has to be looked for on purpose.

## What the gate says

`make test-m3` 65/65: added `vrt_cpyfm clipped by vs_clip at odd edges,
every mode` (a clip edge inside a byte, each mode's "leave alone" pair),
`vrt_cpyfm transparent in pen 0` (the AND strip alone), `vrt_cpyfm at
the screen corners` (a negative destination, a form off the right and
bottom edges), and `cursor ignores vs_clip`. The pointer cases from
Phase 3a — odd x, hot spot, screen edges, hide nesting — pass against
the unchanged model pixel for pixel, which is the check that the strip
expansion and the plotted form agree.

`make movie`: 3 chapters, 135 records, 12 planned waits, 8 shots, 950
frames (19 s at 50 Hz), every record and every shot matching the
reference, the last screen 153,600/153,600, and the three object trees
read back as the model left them — that last check compares against
the model's copies, not the layouts as loaded, because `menu_bar`
rebuilds the Desk box's chain (one child, one line high) on both sides.

## Lessons

- **Measure the primitive, not the symptom.** The sizer failing where
  the mover passed looked like a control-manager bug; the benchmark took
  ten minutes and said "every pixel costs 75 µs", which explained the
  sizer, the icons' one-per-60 ms appearance, and why the pointer
  flickered on every move. A harness grace period would have hidden all
  three.
- **The bus, again.** Phase 7's lesson was that data on the motherboard
  bus is invisible to a correctness gate. So is a pixel path through
  the MEMAC window: correct, green, and thirty times slower than the
  blitter next to it. Anything per-pixel through the window is a
  cold path by the plan's own architecture, and the plan was right.
- **Agreement is not correctness.** Model and target clipped the
  pointer the same way. The gate can only say they agree; what they
  should agree *on* has to come from the specification.
- **Profile before blaming the clock.** "Still slow after the fix"
  read as a CPU on the slow bus, and Phase 7 had never timed it. The
  profiler took one script and said 0.37 cycles an instruction; the
  instruction count was the story, and it is in the compiler's
  listing (`--assembly-source`) for anyone to read. A cycle budget
  wants a profile, not a register read-back.

## Next

The desktop; `form_alert`; `graf_mouse` and the control manager's arrow;
the wheel as `WM_ARROWED`; `G_ICON`, `G_USERDEF`, `G_CICON`; the
native-mode vectors, overdue since Phase 3a; the return to DOS
write-back; the per-pixel loops (`vrt_cpyfm`'s strip builder by
nibble-pair table, `plot()` by row table and a rotating style mask, a
line rejected against the clip before its loop) — the desktop draws
icons by the dozen.
