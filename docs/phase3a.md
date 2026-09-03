# Phase 3a — the pointer seam and the mouse cursor

Status: **gates green.** `make test` — 9 host tests, 43/43 VDI cases.

## The seam

`src/vdi/pointer.h` is the whole idea: everything above it consumes **one**
thing, an absolute screen position plus buttons. Below it sit devices that
disagree fundamentally about what position even means.

| Device | Model | Read through |
|---|---|---|
| Atari ST mouse (joystick-port adapter) | relative, quadrature | PORTA `$D300` |
| Amiga mouse | relative, quadrature, **H/V pins swapped** | PORTA |
| Atari CX80 trak-ball | relative | PORTA |
| Atari CX77 touch tablet / KoalaPad | **absolute** | POT0/POT1 `$D200/1` |

GEM's own design already assumes this: `vex_motv` hands the AES an absolute
position and `v_locator` is a device-independent locator, so nothing above the
seam ever learns which device is fitted. Supporting all four is a few lines of
bit-shuffling, not a fork.

All four are emulated by Altirra (`STMouse`, `AmigaMouse`, `Tablet`,
`KoalaPad`, `Trackball_CX80`), so all four are testable on real emulated
hardware when the time comes.

## ⚠ Only the absolute devices work today

*(True when written. Phase 9 built the native-mode vectors and a timer IRQ
that samples PORTA at ~4 kHz, so the ST mouse, the Amiga mouse and the CX80
trak-ball work as well -- in Altirra; `docs/phase9.md` says what was and was
not verified.)*

A quadrature device must be polled often or it silently loses counts — the
classic Atari 8-bit mouse complaint — which means an interrupt.

**gem4xe still runs with NMI and IRQ switched off.** `src/crt_atari.s` disables
them because the 65816 in native mode uses different interrupt vectors
(`$FFEA`/`$FFEE`) than the ones the Atari OS ROM fills. So:

> **The mouse is what finally forces the native-mode vector stubs.** It is now
> the third thing waiting on them, after the blitter-complete IRQ and returning
> to DOS — and the first that cannot be worked around.

An absolute device has no such problem: one POT read per frame from a polling
loop is enough, and `PTR_TABLET` is what `src/m3_vdi.c` initialises today. The
"for fun" option turned out to be the one that works first.

## Honest limits of the tablet

POKEY's pot counter tops out near **228** per axis, against a 640x240 screen:

- horizontally ~**2.8 screen pixels per step** — fine for hitting a menu item
  or a button, too coarse to draw with
- vertically ~**1:1** — genuinely good

`tests/host/test_pointer.py` asserts both, so they are recorded properties
rather than a surprise discovered later. Smoothing belongs above the seam if
it is wanted; scaling harder will not help.

## Quadrature decoding

The classic 2-bit Gray-code table, indexed by `(prev << 2) | now`. The four
two-step entries — a missed sample — are **0 on purpose**: on a dropped count
it is better to lose motion than to invent it in the wrong direction.

The table exists twice, in `src/vdi/pointer.c` and `tools/vdiref.py`, and
`test_c_and_reference_tables_agree` parses the C out of the source and compares
them so they cannot drift.

## The cursor

`vsc_form` (111), `v_show_c` (122), `v_hide_c` (123), `vq_mouse` (124) and
`v_locator` (28).

Form handling is GEM's: 37 words of `intin` carrying hotspot, planes, bg, fg,
a 16-word mask and a 16-word data plane. The **mask is painted in bg first,
then the data over it in fg**, so a mask bit with no data bit is the outline
and a clear mask bit leaves the screen alone.

Save/restore is a VRAM→VRAM blit of the byte span the cursor touches —
`x>>1` through `(x+15)>>1`, which is **9 bytes at odd x, not 8**. There is a
conformance case for exactly that.

`vdi_cursor_move()` is what an input poll calls after `ptr_poll()`: it skips
the work entirely when the pointer has not moved, because redrawing a
stationary cursor costs two blits and 256 plots for nothing.

**`v_locator` doubles as the test hook** — GEM passes the locator's initial
position in `ptsin`, which is how `gsx_setmousexy()` places the pointer — so
the conformance suite needs no test-only back door.

## Two real bugs found

**1. `v_clrwk` left a stale save block.** With the cursor up, the block saved
underneath it describes a screen that is about to be erased. Restoring it later
stamps stale pixels onto a cleared screen. Fixed: discard the save rather than
restore it, clear, then repaint the cursor so it survives the clear.

**2. `v_opnwk` did not reset cursor state.** Found because the conformance
suite constructs a fresh reference per case while the target's statics
persisted — a visible cursor from one case suppressed `v_show_c` in the next
(`cur_drawn` was already set). Opening a workstation resets the driver, and
that has to include the cursor. Both fixes are semantically right, not test
accommodations.

## What is left before the AES

The input plumbing: `vsin_mode` (33), `v_string` (31), and the `vex_*` vector
swaps (118, 125-127) that the AES installs to receive motion and button
events. Those are wiring, not rendering — but `vex_motv` in particular is
where the native-mode interrupt work will surface again.
