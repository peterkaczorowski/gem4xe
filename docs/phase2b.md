# Phase 2b — text and the raster masks

Status: **gate green.** `make test-m3` — 35/35 conformance cases (was 22).

Completes the "~8 real primitives" the AES needs: **`v_gtext`** and
**`vrt_cpyfm`** join `v_pline`, `vr_recfl`, `vro_cpyfm`, `vr_trnfm` and
`vs_clip`.

## The font

`tools/fontconv.py` extracts the **GEM 8x8 system font** from EmuTOS
(`bios/fnt_st_8x8.c`, GPL v2+) into `src/vdi/font8x8.c`. It is a horizontal
strip: `form_width` 256 bytes, `form_height` 8, so character N's byte on row r
is at `r*256 + N`, stored big-endian per `F_STDFORM`.

The generated file is **checked in**, and `tools/vdiref.py` reads that same
file — so the host reference cannot drift from what the target links.

Shipping 1bpp and expanding on target keeps **2 KB linked instead of 8 KB**,
which matters when the whole program lives in bank $00.

## One glyph mask serves every ink colour

`vdi_font_expand()` builds 4bpp glyph **masks** in VRAM — `$F` where ink, `$0`
where paper — laid out so a glyph is a single blitter rectangle. Drawing is
then two blits:

    AND   src=mask, and=$FF, xor=$FF, mode 4   -> clears exactly the ink pixels
    OR    src=mask, and=ink*$11, xor=$00, mode 3 -> paints them

The AND step works because **mode 4 is the one mode that writes 0 when `c == 0`
instead of skipping** — the quirk found in Altirra's `BlitRow` back in Phase 2.

**Ink 0 needs no OR at all**: the AND has already left colour 0 there. That is
why no inverted mask is kept, and why mode 6's nibble stencil is *not* used —
it cannot write colour 0, and colour 0 is white in GEM's palette, i.e. every
inverted menu item.

Text at odd x, or a glyph the clipping rectangle cuts, falls back to CPU
plotting. Both paths are tested against each other.

Alignment is left/baseline (`vst_alignment` is still `v_nop`, so only the
default applies): the given y is the **baseline** and the cell top is
`y - FONT_TOP` (6, from the Fonthead).

## vrt_cpyfm

A one-plane source expanded into device colours — how the AES draws icons.
The source form lives in RAM, out of the blitter's reach, so this is honest CPU
work; acceptable because the AES only ever uses it for small forms. All four
writing modes are implemented (replace, transparent, XOR, reverse-transparent).

## ⚠ Two real bugs found, both by the conformance suite

**1. MFDB `fd_addr` must be `uint32_t`, never a native pointer.** The MFDB is a
GEM/68000 structure and applications build one by hand at that layout. Calypsi's
small data model makes `void *` **16 bits**, which silently shifted every field
after `fd_addr` by two bytes — `vrt_cpyfm` read `fd_w` out of the address's
upper half. Declaring it `uint32_t` (and taking the low 16 bits, since forms
live in bank $00) keeps the struct at its specified 20 bytes.

**2. A codegen difference, worked around here and root-caused in Phase 7.**
Inside `vrt_cpyfm`,

    r = bits + (uint16_t)((sy1 + row) * stride)

computed per iteration read the **wrong address** under Calypsi 5.18: row 0
was correct and every later row read unrelated memory. Rewriting the walk as
an **incrementing pointer** (`bits += stride` in the loop header) was correct,
and at the time I wrote that it was not root-caused and not asserted to be a
compiler bug.

It was one, and it was not in the line I was looking at. The line before it,

    stride = (uint16_t)((uint16_t)src->fd_wdwidth * 2u);

compiles — when `src` has been spilled to the stack by the `order()` calls and
is dead after this line — to an **in-place shift of `src`'s own stack slot**:
`tsc; clc; adc ##9; tax; asl 0,x`. The field is never read; `stride` is the
pointer doubled. With `sy1 == 0` row 0 is `0 * garbage`, hence correct, and
every later row is garbage. The incrementing form changed the slot allocation
so `stride` no longer shared `src`'s slot, which is why it worked and why the
symptom looked like the pointer arithmetic. The fix is to read the field
through a scalar; it is B5 in `tools/ccbug/README.md`, reproduced in the
vendor's simulator and pinned by `make check-cc`.

Both bugs were invisible to the eye and caught immediately by pixel comparison.
That is the argument for the whole harness.

## Performance note

`vdi_font_expand()` first ran as 2,048 `vram_write()` calls of 4 bytes each and
took **several emulated seconds** — the 32-bit address arithmetic per call
dominated completely. The expanded font is exactly two 4 KB MEMAC pages and is
contiguous within each, so streaming through a window pointer (`vram_win()`)
with 16-bit arithmetic brought it under a frame.

Worth remembering generally: `vram_write()` is fine for the XDL and blit lists,
which are tens of bytes. **For anything kilobyte-sized, map the page once and
stream.**

## Harness additions

- The script format now carries `contrl[7..10]`, so any MFDB opcode can be
  driven from a case.
- `vdi_scratch[]` stages forms and their MFDBs, poked by the host.
- Both staging buffers live in a dedicated **`teststage`** section, which
  `src/gem4xe.scm` maps to `$4000-$7FFF`. That region is off limits to the
  *driver* — U1MB banks extended memory there — but the conformance runner
  never enables banking, and a named section means nothing else can drift into
  the region by accident.
- `tools/mkxex.py --syms` emits a symbol file so the harness finds
  `vdi_script` and `vdi_scratch` by name rather than by a hard-coded address.

## What is left before the AES

The mouse: `vsc_form` (111), `v_show_c` (122), `v_hide_c` (123) and the
save/restore under the pointer. Then the input opcodes (`v_locator`,
`v_string`, `vsin_mode`, the `vex_*` vector swaps), which are plumbing rather
than rendering.
