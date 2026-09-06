# Phase 17 — the opcodes an application uses

The VDI shipped with the 37 opcodes the AES and the GEM Desktop call.
DRI's own screen driver `v_nop`'d ten of the rest and so did this one --
which is fine until an application arrives, because an application is
exactly what the other fifty-five are for.  A word processor measures
text with `vqt_extent`.  A drawing program wants circles, arcs, rounded
boxes, a paint bucket and arrowheads.  Anything that draws a chart wants
`v_fillarea` and markers.

    make test-m3   86/86   VDI conformance, pixels AND returned values

Everything below is in `src/vdi/vdi.c`, mirrored in `tools/vdiref.py` --
the model is the specification, and every one of these was written on
both sides before it was believed.

## What went in

| | |
|---|---|
| 7, 18, 19, 20 | `v_pmarker`, `vsm_type`, `vsm_height`, `vsm_color` |
| 9, 104 | `v_fillarea`, `vsf_perimeter` |
| 11 | `v_gdp`: bar, arc, pieslice, circle, ellipse, elliptical arc and pie, rounded box, filled rounded box, justified text |
| 13, 14, 26 | `vst_rotation`, `vs_color`, `vq_color` |
| 35, 36, 37 | `vql_attributes`, `vqm_attributes`, `vqf_attributes` |
| 39, 106, 107 | `vst_alignment`, `vst_effects`, `vst_point` |
| 103, 105, 108 | `v_contourfill`, `v_get_pixel`, `vsl_ends` |
| 116, 117 | `vqt_extent`, `vqt_width` |

What is still `v_nop`, on purpose: `v_updwk` (4), the cell array pair (10
and 27), `v_valuator` (29) and `vst_load_fonts`' friends beyond the four
Phase 15 filled in.  DRI nopped every one of those in production.

`v_opnwk`'s work_out now says so: ten GDPs at intout[14], their numbers
at 15..24 and the attribute each draws with at 25..34, and the marker
sizes in ptsout[8..11].  `vq_extnd(1)` answers with the text effects
that are real here, the writing modes, the input modes, the vertex limit
and the size of intin.  A program reads those rather than assuming, and
until now they were zeros.

## Three decisions

**An effect answers with what was APPLIED.** `vst_effects` takes six
bits and this device can do two of them -- thickened is the glyph blitted
a second time one pixel right, underlined is a solid row in the text
colour.  The other four come back cleared.  That is the contract, and it
is why a GEM application asks.

**A perimeter is drawn in the FILL colour, solid**, whatever the line
attributes say -- the donor sets `LN_MASK` to $FFFF and passes
`fill_color`, which here is a save and a restore around the polyline.
The interior includes its own boundary pixels (that is what the `+1`
before the shift in `clc_flit` is for), so the perimeter lands on top of
pixels the fill already painted.

**A polygon does not paint its own topmost row.** `clc_flit` runs
`for (y = maxy; y > miny; y--)`, so the scan line at the minimum y is
left alone.  It looks like an off-by-one and it is the donor's rule,
kept because two polygons that share an edge must not paint it twice.

## The paint bucket, and what a 14 MB heap is for

`v_contourfill` is a seed fill, and the donor's version leans on the
pixels it has already painted to stop the search coming back.  That only
works for a SOLID fill: a patterned one leaves interior-coloured pixels
behind and the search walks straight back into them.

So the region is discovered first, into a bitmap in far memory -- one bit
a pixel, 19,200 bytes, plus 32 KB for the stack of runs waiting -- and
painted afterwards, run by run, through the fill pattern like any other
filled area.  51 KB of a fourteen-megabyte heap for one call is the sort
of thing this machine makes free, and it buys two things: patterns work,
and the ANSWER DOES NOT DEPEND ON THE ORDER OF THE WALK.  The model can
use a Python list where the driver uses a stack in bank $04 and the two
still agree pixel for pixel, because they only have to agree about the
region.

It is done a ROW at a time.  The first version asked the hardware for one
pixel and the bitmap for one bit at a time and a screen-sized bucket took
**44 seconds**: a 4K page mapped and a call made per pixel.  Reading a
row of the screen through one mapping and fetching a row of the bitmap in
one go is the same algorithm without any of that -- **12 seconds**.  It
is still the slowest call in the VDI; the next step, if it matters, is to
turn each row into a mask of interior bits once and do the run-finding on
bytes instead of pixels.

## ⚠ B12: a signed 16-bit `>>` is not an arithmetic shift

This one cost the afternoon twice, so it is written down in full in
`../tools/ccbug/README.md` and checked by `make check-cc`.

    UWORD i = angle >> 3;               /* Isin() */

cc65816 5.18 emits three logical shifts and then a sign extension **from
the wrong bit** -- `eor ##4 / and ##7 / sec / sbc ##4`, which keeps three
bits and throws away the rest.  `900 >> 3` is 0, not 112.  `900 >> 4` is
-8, not 56.  A shift by ONE is a plain `lsr`, right only for a
non-negative value; 32-bit shifts are right; and the same compiler emits
the correct `cmp ##-32768 / ror a` for the same source shape elsewhere in
the same file, so reading one listing proves nothing.

It showed up twice in one day:

* every GDP curve drew nothing at all -- `Isin` returned `sin_tbl[0]` for
  every angle, so every point of every circle landed on its centre;
* the contour fill hung the machine -- `sn[i >> 3]` with `i` a WORD
  indexed **outside** the buffer, which is on the stack, so the fill
  walked over its own return address.  Adding a debug store moved the
  symptom, which is Phase 6's lesson arriving on schedule: a bug that
  moves with the layout is self-corruption until proven otherwise.

The fix is `(UWORD)v >> n` where the value cannot be negative and `asr()`
where it can.  A sweep of every object's generated assembly for the
broken idiom (`eor ##k / and ##2k-1 / sec / sbc ##k`) found no other site
in the tree.

## The runner's disk had to grow

The suite's runner outgrew its floppy: 117 KB of program on a
DOS 2.5 enhanced-density disk that holds 128 KB, with the DOS on it.
Adding an opcode ran out of sectors.

The single-density fixture's DOS boots to a command prompt, which is why
the gates could type `M3` and RETURN.  Its boot code cannot read a
double-density disk at all -- written onto one it stops at BOOT ERROR --
and no amount of rewriting the file system around it changes that
(`tools/atr.py` grew a `densify()` for the attempt and lost it again).

So the runner's disk is now built from the double-density fixture, whose
DOS boots its own disk and gives the DOS 2 menu.  `L` is that menu's
BINARY LOAD, and the file is called `M3` with no extension so the three
keys after it are the ones every gate already typed.  190 sectors free,
48 KB to grow into, and the gates that read the disk (`test-m12`) now
exercise 256-byte sectors as well.
