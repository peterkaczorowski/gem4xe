# Phase 2 — the VDI

Status: **gate green.** `make test-m3` — 22/22 conformance cases.

## The shape of it

`src/vdi/vdi.c` follows the Digital Research / EmuTOS screen driver exactly: a
fixed parameter block of five arrays, two flat jump tables split 1..39 and
100..137 as DRI split them, and handlers that take no arguments and read the
globals.

That 1984 ABI is a gift on this machine. Every handler is `void f(void)`;
nothing needs argument passing and nothing needs reentrancy. DRI's own
`entry.a86` ran the whole VDI on a private 256-byte stack, and the same shape
drops straight onto a 65816 with no adaptation at all.

## Implemented

| | |
|---|---|
| Workstation | `v_opnwk` (1), `v_opnvwk` (100), `vq_extnd` (102), `v_clrwk` (3) |
| Attributes | `vsl_type/width/color` (15-17), `vsf_interior/style/color` (23-25), `vst_color` (22), `vswr_mode` (32) |
| Output | `v_pline` (6), `vr_recfl` (114) |
| Raster | `vro_cpyfm` (109), `vr_trnfm` (110) |
| Clipping | `vs_clip` (129) |

`v_clswk` (2) and `v_clsvwk` (101) are no-ops, as they are in DRI's own
shipping driver. The remaining opcodes are `v_nop` and can stay that way: DRI
shipped ten of them nopped.

Still to come, and they are the rest of the "~8 real primitives": **`v_gtext`
(8)** with the font pipeline, and **`vrt_cpyfm` (121)** for glyph and icon
masks. Both want the 1bpp→4bpp font expansion, so they belong together.

## The 4bpp edge problem, solved and tested

A pixel `x` lives in byte `x>>1` — high nibble when `x` is even, low nibble when
odd. The blitter is byte-granular, so an edge byte only half inside a rectangle
must keep its other nibble. `fill_rect_dev()` does that with a pair of
constant-source read-modify-write blits: **AND away the nibble being replaced,
then OR the colour in.**

Mode 6's nibble stencil would be one blit instead of two, but it **cannot write
colour 0** — a zero nibble means transparent. The AND/OR pair works for every
colour, so it is what the driver uses.

One useful detail found in Altirra's `BlitRow`: modes 1-5 skip a byte whose
`c == 0`, **except mode 4 (AND), which writes 0**. So `x & 0` still clears, as
it should. `tools/vbxeref.py` models this exactly.

Six of the 22 cases exist solely to pin this down: odd left edge, odd right
edge, both odd, one-pixel columns at even and odd x, a rectangle living inside
a single byte, and adjacent rectangles that must not bleed into each other.

## ⚠ The blitter has no shifter — and it shapes the window manager

`vro_cpyfm` can be a single blit **only when source and destination x share
parity and both are even**. The VBXE blitter moves bytes; it cannot shift 4bpp
pixels half a byte. Every other case falls back to CPU pixel copying through
the MEMAC window, which is roughly **twenty times slower**.

> **Design consequence: snap window x to even pixels.** At 640 wide it costs
> nothing visually, and it keeps every window move, uncover and scroll on the
> pure-blit path. A window manager that lets windows sit at odd x will be
> mysteriously slow half the time.

Five conformance cases cover both paths — aligned, odd source x, differing
parities, odd width, and an overlapping move — and require them to produce
identical pixels.

## MFDB: kept exactly, reinterpreted

The struct keeps the VDI's layout byte for byte, `fd_wdwidth` in 16-bit WORDS
included, because every GEM application builds one by hand and a changed layout
breaks all of them. On this device it is only a hint: the driver computes the
real byte stride itself (`width / 2`).

`fd_stand` distinguishes VDI-standard from device-specific form. There is
exactly one form here, so **`vr_trnfm` is a genuine no-op** rather than a stub.

## The conformance harness is the deliverable

`tools/vdiref.py` is a host implementation of the same VDI, written against
`tools/vbxeref.py`'s model of the blitter. **It is the specification.** When it
and the target disagree, it is right until proven otherwise, and the
disagreement gets shrunk into a new case.

Scripts are **poked into the target over the bridge**, not compiled in
(`src/m3_vdi.c` runs whatever lands in `vdi_script[]`). A new case costs a line
of Python, not a rebuild. The harness finds `vdi_script` by name from
`build/m3.sym`, which `tools/mkxex.py --syms` emits, so addresses can move
freely.

This is what Phase 3's AES work will be built on, and it is the reason the
plan called this pattern the most valuable thing in the vbxetxtadv rig.

## Notes

- Line styles 1-7 are implemented as the standard 16-bit VDI patterns.
  Horizontal and vertical **solid** lines take the blitter fast path as 1-row
  and 1-column rectangles; styled or diagonal lines go pixel by pixel. That is
  the right trade: the AES never calls the GDPs or `v_fillarea`, and every box
  it draws is an axis-aligned polyline.
- `vq_extnd(1)` returns `intout[4] = 4` planes, which is the value the AES
  reads to learn the screen depth (`gsx_nplanes()`).
- The palette is GEM's standard order — **pen 0 is WHITE, pen 1 is BLACK** —
  which is what every GEM application is written against, and the opposite of
  what a framebuffer usually assumes.
