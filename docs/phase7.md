# Phase 7 — the AES form and event layer

Status: **complete.** `make test-m7` 10/10, and the rest of the suite
unchanged after it — m3 55/55, m4 12/12, `make demo` pixel-exact, m5 and
m6, `check-cc`. Step 4 records a hardware finding that had been costing
every phase before it.

Phase 6 ended with "next is `form_do` and the window manager". Getting there
turned out to need three things first: the VDI fill patterns the AES draws
dialogs with, the rest of the object library — `objc_change`, `objc_edit`,
`form_center` — and, unexpectedly, a catalogue of the compiler. Then the
event layer and `form_do` themselves (Step 3), and — while timing them — the
discovery that the program had been running its data at 1.79 MHz since
Phase 0 (Step 4).

## Step 1 — pattern fill (VDI)

`vsf_interior`, `vsf_style` and `vsf_udpat` had been accepted and ignored;
every `vr_recfl` was solid. The AES fills a dialog background, a disabled
item, a slider track and a menu's shadow with the standard patterns, so this
was the first thing `objc_draw` was going to expose.

The tables — 8 dithers, 16 OEM patterns, 6 coarse and 6 fine hatches — are
extracted from the donor's `vdi_fill.c` by `tools/patconv.py` into
`src/vdi/fillpat.c`, checked in like the font so that the host reference and
the target link the same bytes (`tests/host/test_fillpat.py` holds the two
together). The workstation keeps the donor's `fill_style` / `fill_index` /
`patptr` / `patmsk` quartet and the same off-by-one (`vsf_style`'s index
minus one), so the AES code that will sit on top reads unchanged.

On the blitter a pattern fill is **one control block**: the 16-row pattern is
expanded to 4bpp once, into a small VRAM tile behind the cursor save area,
and blitted with the **pattern counter** (BCB byte 19) wrapping the source
every eight bytes. Altirra's `BlitRow` resets the source pointer to the *start
of the row*, not to where the row's read began, so the tile holds each row's
16-pixel repeat **twice**, letting a blit that starts on any byte of the
repeat read a full eight before the wrap. Seven m3 cases (six net: the old
"hollow draws nothing" became "white in replace, pen in erase, else nothing",
which is what the donor does) pin the four writing modes, the odd edges, the
16-row band boundaries and the user pattern's XOR-twice identity.

## Step 2 — the object library, at donor fidelity

`src/aes/objc.c` was rewritten against EmuTOS `gemoblib.c` / `gemobed.c`
function for function — `everyobj`, `ob_sst`, `ob_format`, `just_draw`,
`ob_find`, `ob_change`, `ob_center`, `ob_edit` and its helpers — and the
drawing waist moved into `src/aes/graf.c` (`gemgraf.c` + `gemgsxif.c`:
`gsx_start`, `gsx_attr`, `gsx_sclip`, `gr_box`, `gr_rect`, `gr_just`,
`gr_gtext`, `gsx_blt`, `gr_crack`). The AES reads `gl_wchar`, `gl_hchar` and
the screen rectangle **from the workstation** at `gsx_start`; nothing about
640×240 is written into it.

`tools/aesref.py` grew the same way and remains the specification. Twelve
m4 cases, eight of them new: a dialog with default and disabled buttons, a menu bar with a
dropped and selected item, INDIRECT specs, subtree draws, the TEDINFO family
with templates and justification, `objc_change` with and without a clip,
`objc_edit` typing / backspace / arrows / delete / escape and filling a field
to its end, and `form_center` of a plain and of an outlined-and-shadowed root.
The runner gained AES ops 44, 46, 47 and 54 alongside the existing draw and
find.

## Step 2b — five text failures and what was under them

With everything else pixel-exact, **no TEDINFO object ever drew its text**,
and the direct-text types (G_STRING, G_BUTTON, G_TITLE) were fine. The
difference is the path: direct text goes `expand_string` → `gsx_tblt`, while
the TED types go through `gr_gtext` → `gr_just` → `gsx_tcalc` to fit the text
to its field. `gsx_tcalc` returned rubbish, and reading its assembly found
**two** compiler bugs at once:

- `numchs = (n < m) ? n : m` through an out-pointer, in a `static` function
  the optimiser had inlined, **stored into a dead stack slot** and never
  wrote the caller's variable (B3).
- `if (*ph / hc)` — does one line of text fit? — compiles to `jsl _Div16;
  beq`, and the library's `_Div16` returns with the **flags of the sign
  word**, not of the quotient. A field exactly one line high divides to 1
  and tests as zero (B2).

B3 is fixed by returning the value; B2 by `src/sys/div16.s`, a replacement
`_Div16`/`_Mod16` that loads its result last, linked with
`--override _Div16 --override _Mod16`. An assembly scan of every unit found
the `gsx_tcalc` site to be the only place branching on the flags after a
divide — but the override is global, so no future one can bite.

Then a new case for **outward borders** — a negative thickness byte in
`ob_spec`, which is what makes GEM's default button wear a thicker ring —
painted a black band the full height of the object: `(WORD)(int8_t)(spec >>
16)` had its sign extension **dropped**, in every spelling that derives the
byte from the 32-bit value in the same expression (B4). Through a `WORD`
local it is right.

That made four, counting B1 from earlier in the phase (`x[d] = x[d-1] + ob_x`
on a stack array compiling to a *load*). Rather than keep them as folklore
in comments, each is reproduced from its gem4xe shape in
`tools/ccbug/bugs.c`, run in the vendor's simulator by `tools/ccbug/check.py`,
and gated by **`make check-cc`**, which fails only if a *workaround* shape
stops compiling right and reports when a bug is fixed upstream so its
workaround can go. `tools/ccbug/README.md` has the five rules and the
characterisation of each.

**Five**, because writing that up meant looking again at Phase 2b's
"unexplained codegen difference" in `vrt_cpyfm`. Restoring the original
`bits + row * stride` form failed three m3 cases on target, and its assembly
showed the fault was in the *previous* line: `stride = src->fd_wdwidth * 2u`,
with `src` spilled to the stack by the `order()` calls and dead afterwards,
compiles to an in-place `asl` of `src`'s own stack slot — the field is never
read and `stride` is the pointer doubled. Row 0 was right only because
`0 * garbage` is 0. Reproduced in the simulator in a dozen variants (every
shift-shaped operator; not `* 3`, not a byte field, not a global pointer, not
a pointer still live), fixed by reading the field through a scalar, pinned
as B5. `docs/phase2b.md` now says so.

## Step 3 — events and `form_do`, driven from the host

`src/aes/event.c` is EmuTOS's `gemevlib.c` + `geminput.c` with the kernel
taken out. Under GEM's AES the interrupt handlers post to a fork queue, a
dispatcher runs the posts, and a process in `evnt_multi` sleeps until an
event completes. gem4xe runs one application and polls, so the same state
machine is driven from `vdi_input_poll()` through the VDI's three input
vectors — motion every pass, button on a change, timer once per frame — and
`ev_multi` loops on the poll until something it asked for has happened. What
is kept exactly is the button logic: the transition pair
(`button`/`pr_button`, so a press-and-release that arrived before anyone
asked still reports the press), the double-click delay that turns rapid
presses into a click count, and `downorup()`'s mask/state/flag test that
every waiter is phrased in. `form_do` and the window manager are written
against precisely that behaviour, so it was not worth simplifying.

One departure, taken from the ROM AES rather than the donor: cancelling a
pending multi-click button wait releases the pending-clicks semaphore.
EmuTOS leaks it, so after the first keystroke in a form every later click is
reported through the double-click delay.

`src/aes/form.c` is `gemfmlib.c`: `form_do` owns the screen for the life of
the dialog, moves the edit cursor between EDITABLE fields, feeds keys to
`objc_edit`, tracks the button over SELECTABLE objects through
`graf_watchbox`, keeps radio buttons exclusive and returns the
EXIT/TOUCHEXIT object (bit 15 set for a double click on a TOUCHEXIT).
`form_dial`'s grow and shrink are `graf_growbox`/`graf_shrinkbox`, XOR
outlines that leave the screen as they found it; FMD_FINISH redraws the
desktop where the dialog was through `w_drawdesk`, which today draws only an
installed WF_NEWDESK tree and moves to the window manager with the rest of
it. `fm_own`'s menu hold and mouse-rectangle grab reduce to the nesting
count until there is a menu bar to hold.

### The gate: behaviour under input

`make test-m7` compares *what the target does while blocked in a call*, not
just what it draws. The harness pokes the pointer record, holds keys through
POKEY and runs frames while the target is inside an `evnt_*` or `form_do`
call; `tools/aesref.py` walks the same input plan with the same frame
semantics; then every returned word, the screen and the tree's memory are
compared, as in m4 — `form_do`'s edits land in `te_ptext` and `ob_state`,
and a wrong write there is invisible on screen.

A plan is a list of steps applied while the target is inside one op:
`("frames", n)`, `("move", x, y)`, `("button", s)`, `("key", name, code)`.
The rules that make the two sides agree were each paid for by a case that
disagreed:

- **A plan starts with a settle.** The harness sees an op's index the moment
  the previous op completes, and a step applied then can land in the op's
  entry poll (the quick checks) or its loop (the button FIFO), which count
  clicks differently. A leading `("frames", n)` puts the target in the loop
  before the first real step, and the model walks the same frames. The
  reference refuses a plan without one.
- **One step is one poll with the tick.** The emulator is paused at a frame
  boundary between bridge commands, so the target's first poll of a frame
  sees the new input and the tick together, and its wait loop tests after
  every poll. The model does the same: apply the input, poll once with a
  tick, test.
- **Frames left after satisfaction are dropped on both sides.** A
  `("frames", n)` step runs one frame at a time and stops when the op
  completes; the target does not poll between ops and sees at most one tick
  when it next does. So a timer's plan need not name the exact tick, and
  plans that count a click through the double-click delay stay well clear
  of its edge — the target sees one tick more or fewer than the harness ran
  when the first or last poll straddles a boundary.
- **The held-button spin collapses.** `form_do` over a disabled object, or
  outside the dialog where GEM rings its bell, returns from `ev_multi` at
  once with the button still down and comes straight back — thousands of
  times a frame until the input changes. The model walks the plan to the
  change instead of modelling each return.
- **GEM leaves `prets[5]` alone for a non-button event.** The first
  reference wrote 0 there; the target, like the ROM, leaves whatever the
  caller's stack held. The runner zero-initialises its `out[]` so the value
  is defined, and the reference matches — the harness must not be stricter
  than the specification, the Phase 3b lesson again.
- **A click on an EXIT button that is still SELECTED does not exit.**
  `form_do` leaves the chosen button SELECTED when it returns, and
  `fm_button` toggles through `graf_watchbox`; so the next dialog's click on
  the same button clears it and the dialog continues, and the click after
  that ends it. The dialog case runs `form_do` three times over one tree to
  pin both directions.
- **The pointer record can tear.** The harness rewrites `x` and `y` between
  frames — later a VBI will — and a reader that fetches `x` before the
  change and `y` after sees a position that never existed, then answers a
  rectangle it was never in. `ptr_sample()` copies the device record to
  `ptr_seen` in one piece, retrying if it changed underneath, and everything
  above the seam reads the copy. Found by the rectangle-crossing case
  failing intermittently.
- **The result count carries a sentinel.** The harness leaves `$FFFF` in
  `vdi_result_count` and the runner zeroes it when it picks up ST_GO, so
  "the previous case's total" and "this case has started" cannot be
  confused, and `n <= k` after a plan — not just `n == k` — is the failure
  test: an op satisfied at entry lets the count run past.
- **LASTOB goes on the last object in the array**, as RCS puts it.
  `objc_draw` and `objc_find` walk the links and never look at it, so m4
  never noticed a tree without one; `form_do`'s field search walks the array
  and stops there, and without it walked off the end.

The ten cases: `evnt_keybd`/`evnt_multi(MU_KEYBD)` through POKEY;
`evnt_button` satisfied at entry, with `graf_mkstate`;
`evnt_multi(MU_BUTTON)` counting clicks through the delay; `evnt_mouse` and
`MU_M1|MU_M2` rectangle crossings; `evnt_timer`/`MU_TIMER`; `form_keybd` and
`form_button`; `form_do` on a dialog (RETURN, a click, a disabled button,
re-select); `form_do` on a panel (fields, radios, a toggle, slide-off,
TOUCHEXIT); `graf_watchbox` in/out/in/release and with the button up;
`form_dial` grow, shrink and FINISH.

## Step 4 — six phases at 1.79 MHz

The panel case took **25 frames** to draw and half a second to react, and
the first instinct — blame the blitter, or the MEMAC window — was wrong.
The arithmetic did not fit: ~700 cycles per pixel at 1.79 MHz is one
`plot()` through the window, and a 4bpp `objc_draw` of a panel is mostly
blits. The slow part was the *C*.

The Rapidus can serve bank `$00` from its own SRAM in four 16 KB windows,
each independently *slow* (every access goes to the motherboard's 1.79 MHz
bus) or *fast* (reads come from the SRAM copy). `MCR` at `$FF0080`: bits
0–3 set a window slow, bit 5 write-through, bit 6 I/O, bit 7 the base OS.
Write-through keeps the copy coherent — a write lands in both, at bus
speed — so a fast window has fast reads and slow writes; `CMCR` at
`$FF0081` bit 6 drops write-through for `$0000-$3FFF` alone, and only there
can writes be fast. (Read out of Altirra's `rapidus.cpp`: the shadow layers
that carry write-through are the one set of Rapidus layers never marked
fast-bus.) **Reset leaves `MCR = $FF`: every window slow.** gem4xe's direct
page, stack, globals and every byte of data had been on the motherboard bus
since Phase 0, and until Phase 6 moved it to bank `$01` so had the code.
Nothing in the suite could see it — the gates measure correctness, and the
plan's "code in fast RAM is immune to ANTIC DMA stealing" was true only of
the code, only since Phase 6.

That is also why the linker map matters more than it looked: `stack`,
`data`, `zdata` and the direct page all sit in `$2000-$2FFF`, inside the one
window whose writes can go fast; `$A000-$A7FF` carries only code and
constants, and `$A800-$BFFB` only the test runner's host-poked buffers
(the split was `$B000` until Phase 8b grew the runner's buffers).

`src/sys/rapidus.c` fixes it by derivation, not by writing a constant:

- the board is identified by the `"6S"` signature at `$FF0000`, and a
  machine without it is left alone;
- the MEMAC window's 16 KB block(s) — from `MEMAC_WIN_ADDR`/`MEMAC_WIN_SIZE`,
  the same macros the driver opens the window with — **stay slow**. A fast
  window is served from SRAM and the motherboard bus is never reached, so
  EXTSEL never fires and MEMAC is invisible (the plan's Correction 2, now
  confirmed: Altirra's MEMAC layer sits at priority 5, the fast SRAM window
  at 58; on hardware the bus is not driven at all);
- `$C000-$FFFF` is left as found, since the OS ROM and hardware live there
  (Phase 9 changed this: `src/sys/irq.c` fills that SRAM window with a copy
  of the OS and switches it fast, see `docs/phase9.md`);
- the other windows go fast. If write-through was off the SRAM copy may be
  stale, so it is turned on and each window is re-synced by copying itself
  onto itself (read from the motherboard, written to both);
- write-through for `$0000-$3FFF` is dropped only after the linker's
  placement of the direct page, the stack and the data is checked to be
  there.

Measured: `MCR $FF → $FC`, `CMCR $40`. The panel draws in under a frame; so
does the test prelude; every `v_gtext` case that had cost 2–3 frames is
under one. The runner publishes what it found in
`STATUS+25..29` and **`make test-m6` now reads the registers back** and fails
if the windows holding the program are slow, the MEMAC window is fast, or
the direct page is still writing through — checked by building without the
call and watching it fail. Not done, and noted in `rapidus.h`: a return to
DOS must write `$0000-$3FFF` back first (clear CMCR bit 6, copy the window
onto itself), because with write-through off the motherboard copy is stale.
*(Done in Phase 9: `rapidus_restore()`, on the way out to DOS.)*

### The odd-parity glyph strip

Text at odd x was still 3 frames per ten glyphs after the speed map. The
even-x path is two blits (AND the mask, OR the ink); at odd x the glyph
straddles nibbles and went pixel by pixel through the CPU path — 32-bit
address arithmetic and two MEMAC accesses per pixel, ~94 µs each. The fix is
a second mask strip in VRAM: every glyph shifted one pixel right, five bytes
wide, with paper nibbles at both ends so the neighbours are untouched
(`VR_FONT_ODD`, 10 KB after the 8 KB even strip). `draw_glyph` picks the
strip by `cx & 1` and the same two blits do the work; replace mode clears
the cell with a fill first and skips the AND. The CPU path remains only for
a glyph the clip rectangle cuts and for XOR/erase. Ten glyphs at odd x: under
a frame. m3's 55 cases, including "text at odd x must match", stayed
pixel-exact, which is the point of having them.

## What the gate says

    make test-m3    55/55   VDI conformance, pixels and returned values
    make test-m4    12/12   AES: draw, find, change, edit, centre, outward borders
    make test-m7    10/10   evnt_*, form_do, form_dial, graf_watchbox under host input
    make check-cc   PASSED  every workaround shape is right; 7 of 7 bug shapes still present
    make demo       pixel-exact
    make test-m5    PASS
    make test-m6    PASS    now including the Rapidus speed map

## Lessons, in the order they cost something

- **Read the assembly of the function that fails, not the line.** Four of
  the five bugs were misattributed at first — to the ternary, to the divide,
  to the pointer arithmetic — and in each case the emitted code for the
  *statement before* or the *callee* was the culprit.
- **A workaround is a hypothesis until it is reproduced outside the
  program.** Phase 2b's incrementing pointer worked, so the note said
  "not root-caused" and moved on; the real defect sat one line above it for
  five phases. The simulator recipe in `tools/ccbug/README.md` makes a
  reproducer an hour's work, which is cheap enough to be the rule.
- **Every bug here was invisible except through the reference model.**
  Nothing drew wrong in a way the eye would have called wrong; text was
  simply absent, or a border was a band. The pixel diff and the returned-value
  diff found all of them.
- **Do the arithmetic before blaming the hardware you understand least.**
  The blitter and MEMAC were the suspects because they were the exotic
  parts; 700 cycles a pixel said the ordinary C was slow, and the ordinary
  C was on a 1.79 MHz bus. Six phases of correct-but-slow passed every gate
  because no gate measured speed. The m6 check exists so that the next
  regression of this kind fails loudly.
- **The harness must model the target's timing, not idealise it.** Every
  plan rule above — the settle, one poll per step, dropped frames, the spin
  collapse — is a place where "the reference does the obvious thing" and
  "the target does what a polled loop does" disagreed by one poll or one
  tick, and the case failed intermittently until the difference was named.

## Next

The window manager: `wind_create/open/close/set/get`, `wind_update`, the
window tree and `w_drawdesk` moved out of `form.c`, WM_REDRAW through a
message pipe (which also lights MU_MESAG), and `menu_bar`. The two standing
warnings come due there: **dirty rectangles are mandatory** (the Phase 1
measurement: one full-screen copy per frame), and **window x snaps to even
pixels** (the blitter has no shifter; an odd-x window would put every move,
uncover and scroll on the CPU path that Step 4 just spent a strip of VRAM
avoiding for text). Native-mode interrupt vectors remain overdue — a VBI
would also fix the input loop's lost tick when a pass exceeds a frame.
