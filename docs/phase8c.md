# Phase 8c — the per-pixel loops, read from the listing

Status: **complete.** The two loops Phase 8b left on the list — the strip
builder under `vrt_cpyfm` and the diagonal under `v_pline` — were rewritten
against the compiler's own listing and the emulator's profiler rather than
against a hunch, and the raster then lost its second strip to a blitter
mode the plan had written off. An icon that cost 60 ms in Phase 8 and
18.8 ms after Phase 8b is 3.8 ms by frame count and about 2 ms by cycle
count; a 100-pixel diagonal went from 10.3 ms to 3.8; a diagonal wholly
outside the clip from 22.5 ms to 2.5. m3 is 68/68 (three cases added),
every other gate is unchanged, `make movie` still matches its reference,
and a sixth compiler defect — a crash, not a wrong pixel — is in
`tools/ccbug`. Everything here ran in Altirra; no hardware was involved.

The Phase 8b page ends with the diagnosis: the CPU is at 20 MHz in SRAM
(0.37 cycles an instruction), so the cost of a pixel is the number of
instructions the compiler emits for it, and the listing
(`--assembly-source`) says which. This page is what the listing said and
what was done about it.

## What was measured

`tests/emu/bench_vdi.py`, sixteen of each op, frames between the first
and the last result record. The frame count is quantised — sixteen ops
in three frames says "3.75 ms" whether the ops took 40 ms or 59 — and
the scripts each open the workstation and set the pointer form first,
which is ~17,500 cycles (10 ms) inside the same count; so the second
table, from the profiler, is the one to compare a rewrite by.

| op (16 of each) | Phase 8 | Phase 8b | **8c** |
|---|---|---|---|
| `vrt_cpyfm` 32×24, every bit set, transparent | 60 ms | 18.8 | **3.8** |
| the same, replace | 60–80 | 18.8 | **3.8** |
| the same, XOR | 60–80 | 17.5 | **3.8** |
| `v_pline` diagonal, 100 px | 15 | 10.3 | **3.8** |
| `v_pline` diagonal, 600 px, entirely outside the clip | 15 | 22.5 | **2.5** |
| pointer show + hide | 15 | 1.8 | 1.9 |
| `v_pline` horizontal, 600 px (one blit) | 2.5 | 2.5 | 2.5 |

Per function, from `bench_vdi.py --by-function` (Altirra's instruction
profiler across the op; a machine cycle is one of the 1.79 MHz bus,
35,568 to a PAL frame):

| | instructions per op | cycles per op |
|---|---|---|
| `raster_1bpp` + its store fragment, Phase 8b | ~20,500 | — |
| … nibble-pair tables, direct-page state, two strips | 6,546 | 3,380 |
| … **one strip, one blit (mode 6)** | **5,203** | **2,455** |
| the icon's control block (`bcb_common` + `vram_write`), two blits → one | 1,994 → 1,058 | 396 |
| waiting for the blitter (`blit_run`) | ~1,325 | ~690 |
| `line_diag`, 101 pixels, Phase 8b | 15,000 (149 a pixel) | — |
| `line_diag`, 101 pixels, now | 9,558 (95 a pixel) | 3,517 |
| `line_diag`, the clipped-out line (rejected by its box) | 87 | 31 |

So an icon is about 4,000 cycles — 2.2 ms — of which the CPU's strip
building is 2,455, the blitter's control block 400, and the wait for
the blitter 700. The frame count's 3.8 ms is that plus the pointer form
and the idle to the frame's end.

## `vrt_cpyfm`: a source byte at a time

Phase 8b's strip builder did a pixel at a time: a bit test, a table
look-up for the AND nibble and the OR nibble, a shift into place, a
store — some 120 instructions a pixel. The rewrite in `raster_1bpp`
(`src/vdi/vdi.c`) does a **source byte** at a time: each nibble of it
indexes a 16-entry table of strip *words* — the two strip bytes that
four pixels become — so one source byte is two indexed loads and two
16-bit stores. A row is then: the first strip byte on its own if the
clip starts at an odd x (its even pixel is outside and must be left
alone), the whole source bytes through the expander, up to three
bytes of remainder (a pair from a nibble, a single from two pixel
tests), the last byte on its own if the clip ends at an even x. When
the source is not byte-aligned with the strip — the form's x and the
destination's x differ mod 8 — a second expander reads two source
bytes and shifts; the last read is one byte past the row, inside the
form or just after it, in bank $00, and only the bits above the
boundary are used. The tables are rebuilt only when the mode, a pen,
or the edge condition changes — a signature word — since the AES draws
the same icon in the same mode many times over.

The expander's state — source pointer, strip pointer, count, the byte,
the two table offsets — lives in the direct page,
`__attribute__((tiny))`. That is what turns "load, index, store" into
one instruction each: `lda (.tiny r1_s)`, `ldx dp:.tiny r1_hi; lda
r1_v16,x`, `sta (.tiny r1_w)`, where a stack local costs a three-byte
stack-relative instruction and cannot be indexed through. The aligned
expander is 34 instructions a source byte, eleven in the loop and the
rest in a fragment the compiler factored out of both expanders (below).

### The second strip, and mode 6

That got an icon to ~6,500 instructions, and the profile then said
where the rest went: the four 16-bit stores per source byte into the
strips, at 3.1 cycles each — the MEMAC window is on the 1.79 MHz bus by
design (Phase 7), and a store there costs what it costs however fast
the CPU is. Two strips, an AND and an OR, was the text path's design
carried over: it handles every mode with one pair of tables and two
blits.

Phase 2b had written off the blitter's mode 6, the nibble stencil,
because it cannot write hardware nibble 0 — pen 0, white — and every
inverted menu item is white text. That is true and it is not the whole
story. Mode 6 writes each non-zero nibble of its source and leaves the
pixel under a zero one alone, so **one strip holding the pen wherever
a pixel is written and 0 elsewhere — clipped pixels included — is the
whole raster in one blit, as long as no pen written is white.** For
the common icon (black on transparent) that is every case. The kinds
`r1_tables` now chooses between:

| mode, pens | strip byte per pixel | blit |
|---|---|---|
| transparent, fg ≠ 0; erase, bg ≠ 0; replace, both ≠ 0 | the pen where written, 0 elsewhere | one, mode 6 |
| transparent in white; erase to white; replace both white | 0 where written, $F elsewhere | one, AND |
| XOR | $F where set | one, XOR |
| replace with one white pen, every strip byte inside the clip | fg / bg | one, COPY |
| replace with one white pen, a strip byte partly outside | fg / bg, 0 outside | an AND blit, then an OR blit |

The last row is the only one that still needs two blits, and its AND
source is not a strip: for a replace, the AND mask is $00 for every
pixel inside and $F for the one outside, which is the same on every
row — so it is **one row**, written once per call at the top of the
strip page, that the blitter reads with a source step of zero. The
second strip is gone from every path; the strip has most of a page to
itself instead of half, so a full-width form goes in eleven-row bands
instead of six; and the expander stores two words a source byte
instead of four. Icons: 6,546 → 5,203 instructions, 3,380 → 2,455
cycles, and one control block to upload instead of two.

The new m3 case (`vrt_cpyfm with white: the copy, the AND row, the AND
strip`) puts each of those kinds at even and odd x, from a shifted
source, and under a clip that ends inside a byte; `vrt_cpyfm at the
screen corners` already had the two-blit case. `tools/vbxeref.py` had
modelled mode 6 since Phase 2, and `patt_span` had used it for fills,
so the model needed nothing.

## `v_pline`: the diagonal

Phase 8b's diagonal was `plot()` a pixel: a 32-bit multiply for the row
address, `vram_map` with a bank compare, the clip test, a read and a
write through the window, and around it a Bresenham step of ~200
instructions — the listing showed the line-style mask `1u << (15 -
(bit & 15))` compiled to a shift *loop* run 7.5 times a pixel, and
`paint_pixel`, `plot`, `plot_visible` as three far calls each with a
stack frame.

`line_diag` now keeps the whole step in the direct page: x, y, the
deltas, the error term (and `-dy`, so the compare against it is a
16-bit compare and not a negation), the pixel count, the style word,
the clip box, the row's VRAM page and offset within it, and the page
mapped. The style rotates one bit a pixel instead of being shifted
into place from an index (a solid style skips even that); the row
address is kept as (page, offset) and stepped by ±320 with a carry
into the page, so there is no multiply and the page compare is one
instruction; a line whose bounding box lies wholly inside the clip
skips the per-pixel clip test; and a line whose box misses the clip is
rejected before the loop — 87 instructions for the 600-pixel clipped
case that cost 22 ms as 600 tested-and-skipped plots. The four byte
values a pixel can need — AND mask, XOR value, and whether to skip —
are looked up by `(style bit << 1) | (x & 1)` from three four-entry
tables. 95 instructions a pixel on a 45° line, where both axes step
every pixel; the read-modify-write through the window is 2 of the
loop's ~35 cycles a pixel.

The line's page/offset pair and `vram_map_page()` are the only new
seam: `vram_map(addr)` now calls it with `addr >> 12`, and the line
calls it directly with the page it is already holding.

Two m3 cases pin this down — `diagonals: styled, every mode, clipped,
off the screen`, and text in XOR and erase cut by the screen edge,
which goes through the same raster path as an icon.

## What the listing says about C on this compiler

None of this needed assembly; all of it needed the listing. What
cc65816 5.18 at `-O2` does with ordinary C, as read in `vdi.c`'s
output:

- **A local is a stack-relative access** — `lda 6,s` — three bytes and
  a cycle more than a direct-page one, and it cannot be an index
  register's base: `(6,s),y` is the only indexed form, and the
  compiler spills to a pseudo-register (`_Dp+n`) to get one. A loop
  variable that indexes a table belongs in the direct page.
- **A shift by a variable is a loop.** `x << n` or `x >> n` emits
  `dey; bne` around a one-bit shift, and a shift that sign-extends
  goes through a `jsl` helper. Shift by a constant or rotate a word.
- **A signed 16-bit compare is four to six instructions**; the
  unsigned `(UWORD)(x - x0) <= w` form of a range test is two, and
  tests both bounds at once.
- **32-bit arithmetic is a helper call** (`_Mul16`, and the
  pseudo-register shuffles around it). Keep row addresses in 16 bits
  as (page, offset) and step them.
- **A static function called once is inlined; called from two places
  it is a `jsl`** with a frame. The expanders are inlined into
  `raster_1bpp`; `edge_byte`, called four times, is not — fine for
  the edge bytes, wrong for anything per pixel.
- **Common code is factored into shared fragments** — the `?L` labels
  — and called with `jsl`/`rtl`. The expanders' store sequence is one:
  21 instructions, called once a source byte from both loops, placed
  by the linker among the other small sections and nowhere near either
  caller. That is also why a profile by nearest symbol attributed a
  quarter of the icon's instructions to `ev_wait_ticks` in one build
  and to a pointer-driver function in the next.
- `--speed` and `--no-cross-call` change none of this by more than a
  few percent (Phase 8b measured them); the gains were in the C.

### The direct page, and bug B6

`__attribute__((tiny))` on a static puts it in the `ztiny` section,
which the linker places in the direct page at `$2000`; the map shows
83 of the 256 bytes used, 60 by these variables and 23 by the
compiler's pseudo-registers. Scalars and pointers work as the guide
says. **An array does not**: any variable index into a direct-page
array is an internal compiler error (`Non-exhaustive patterns in
function mem8Reg`), at every optimisation level. It is `B6` in
`tools/ccbug` — a compile-only probe, since the shape cannot be run —
and the rule is the one the sources already wanted: tables stay in
ordinary RAM, the *index* lives in the direct page, and
`ldx dp:.tiny i; lda tbl,x` is one instruction either way. The
`__tiny` keyword form on a pointer declarator is rejected by the
parser; the attribute form is what works.

## Profiling by function

`bench_vdi.py --by-function` sums the profile per function rather than
listing addresses, which is the figure a rewrite is judged by — with
the runner's own polling and the idle to the frame's end shown beside
it, so they are not mistaken for the primitive. Getting the sums right
took the linker map, not just the symbol file: a `static` function's
section is placed under a `?L` label in the map, with the function's
own symbol at its start, and a factored fragment is a `?L` section
with no function symbol in it at all. So an address is attributed to
the function whose section holds it, and an address in a fragment to
the fragment's *module* — `vdi.o ?L` — which is what the map records
of it; the raster's store fragment is 2,112 of the icon's instructions
and shows up there. The profile's addresses come back without a bank
(the bridge masks the profiler's 24-bit record to 16), and one inside a
far section of the map is taken as that section's bank — every
candidate, joined with `|`, if the code has spilled into a second bank
and the low 16 bits fall inside sections in both.

The harness has one artefact worth knowing: the m3 runner's scratch,
where the bench's icon form lives, is at `$AF82`, inside the 16 KB
block that holds the MEMAC window and must therefore stay on the slow
bus. The expander's source reads cost 2 cycles each there instead of
0.4; an icon read from the AES's own data, in `$2100-$37FF`, is
~150 cycles cheaper than the table says.

## Lessons

- **Read the listing before rewriting.** Every one of the changes
  above is small in C and was chosen because the assembly for the old
  form was long: the shift loop, the stack-relative index, the signed
  compare, the helper call. Guessing what a compiler does with a
  construct is how Phase 8b's version was written; reading is how this
  one was.
- **The bus cost moves, it does not go away.** Phase 7 found the data
  on the slow bus; Phase 8b found the pixels going through the window;
  this phase found the strip *stores* going through the window at
  3 cycles each after the instruction count had been cut by two thirds.
  Each time the fix was to hand more of the byte traffic to the
  blitter. The next place it will surface is the control block upload,
  ~21 instructions a byte through the same window.
- **A hardware mode written off in one context is worth a second
  reading in another.** "Mode 6 cannot write white" was true and it
  closed the door on the one strip layout the raster wanted. The
  finding is not that Phase 2b was wrong; it is that the constraint
  was recorded without the case it did not apply to.
- **Attribute the profile from the map.** A nearest-symbol profile is
  right for functions and wrong for fragments, and the fragment was
  the hottest thing in the icon. The map has the section for every
  address; the symbol file does not.
- **A compiler crash is a bug like any other**: reproduced to a
  shape, added to `check-cc`, with the rule the sources follow so the
  next reader does not rediscover it.

## Next

The desktop; `form_alert`; `graf_mouse` and the control manager's arrow;
the wheel as `WM_ARROWED`; `G_ICON`, `G_USERDEF`, `G_CICON`; the
native-mode vectors, overdue since Phase 3a; the return to DOS
write-back. On speed, in the order the profile suggests: the pointer
form's expansion (`cursor_expand`, ~59,000 instructions a `vsc_form`,
which the AES calls on every shape change); the control block upload
(`vram_write`, ~21 instructions a byte, so a blit's 21-byte block is
~400 cycles before the blitter starts); text in transparent mode with
a non-white ink as one mode-6 blit of the glyph mask with the ink in
the AND mask, instead of the AND/OR pair; and, if any of the loops
still matters after that, an assembly expander — the aligned one would
be about half its 34 instructions a source byte.
