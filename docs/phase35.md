# Phase 35 -- the model's side of the seam

Phase 34 gave the target one binary and two screens.  The MODELS still
had one screen: `tools/vdiref.py` -- the specification `src/vdi/vdi.c`
has to agree with -- was written to 640x240 at 4bpp, with the VBXE's
nibble packing and its `map_col` permutation inline in the rasteriser.

So the ANTIC gates could check the VDI and could not check the AES on top
of it, and `tests/emu/m25_antic_vdi.py` had sixty lines of
`src/aes/objc.c` transcribed by hand into `anticref`'s primitives to
cover the gap.  This phase closes it.

    make test-m25   53,760 / 53,760 -- against vdiref and aesref now
    make test-m26   the ANTIC desks against deskref, pixel for pixel

## The refactoring gate came first

The models are pure Python, so a refactor of them can be held to
byte-for-byte identical OUTPUT without booting anything.  Before touching
`vdiref` I built a snapshot: run all 86 VDI conformance scripts and all
13 AES cases through the models, hash every screen and every returned
record, print one digest per case and one for the lot.

    8a637b7a48672f31  == ALL 99 CASES ==     2.6 seconds

That digest did not move once, through every step below.  It is a much
stronger statement than "the emulator still agrees", and it is instant --
the difference between a refactor you can do and one you talk yourself
out of.

## `tools/devref.py`

The contract is the C's, name for name where the shapes allow: geometry
and the system font's metrics as fields, then `fill_rect`, `xor_rect`,
`plot`, `plot_xor`, `pixel`, `pen_of`, `pen_value`, `screen_form`,
`save_form`, `copy`, `read_pixel`, `write_pixel`, the cursor's saved
block, `colours`, `planes`, the palette, `to_rgb` and `key`.

**THE PEN IS THE VDI'S, not the hardware's** -- the same sentence
`src/vdi/vdidev.h` opens with.  `MAP_COL` and `HW_PAL` are gone from
`vdiref`; a caller passes the pen it was given.

`Vbxe` is the code that used to be inline, moved: the nibble edges, the
copy's alignment fast path (the blitter has no shifter), the nine-byte
cursor span at odd x, the palette in hardware order.  `Antic` is new and
mirrors `src/vdi/dev_antic.c` -- the form accessors, the copy direction
rule, and a cursor block that is **five** bytes at odd x where the other
is nine, which is the same arithmetic with eight pixels to a byte
instead of two.

Above the seam `vdiref` names no screen.  `SCR_W`, `FONT_H` and the rest
are `self.dev.w`, `self.dev.font_h`.

### And `aesref` needed one argument

    def run(script, tree, mem, ..., dev=None):
        v = vdiref.VDI(dev)

That is the entire diff, plus one line where `bb_save_restore` asked
`vdiref.VramForm` for the save buffer and now asks `self.v.dev`.  The
object library, the window manager, the control manager, the menus and
`form_do` are untouched, for the same reason the C's are: `gsx_start`
asks the VDI for the extent, the depth and the cell, and everything is
laid out on the answer.  That is what GEM's device-capability array is
FOR, and it is pleasing to watch it work thirty years late.

## What it proved, in three steps

Each of these is a comparison against something already known to match
the target, so each is a real check and not a tautology:

1. **The VDI model on ANTIC draws m25's three calls** bit for bit as
   m25's hand-written `anticref` model drew them -- and that model
   already matched the target.  53,760 of 53,760.
2. **The real `objc_draw`, unchanged, draws m25's four-object dialog**
   bit for bit as the hand transcription of `just_draw` drew it.  Same
   count.
3. **`deskref` runs**: the GEM Desktop model on the ANTIC device lays
   out on 320x168 with a 6x6 cell and a 10x9 box, reaches its first wait
   in the same 54 calls the VBXE run takes, and paints **25,076 ink
   pixels** -- which is the number `test-m26` had already measured on the
   target's own ANTIC desk, before there was a model to compare it with.

So m25's model is the models now, and the hand transcription is deleted
along with the class of bug it had already produced once: OUTLINED's
white ring is `w+4` by `h+4`, and writing 48 for a 46-tall box put its
bottom edge on the border it was supposed to sit outside.

## `test-m26` grew a picture

The ANTIC desks were checked structurally -- which device, what the AES
laid out for, what the config parsed to, two colours, some ink, and the
two routes to ANTIC identical.  They are now also compared **pixel for
pixel with `deskref` on the ANTIC device**, the same model `test-boot`
compares the VBXE desks against and `test-m17` drives through a whole
session.

Everything the model needs that is not on the disk is read off the
machine, the way `product_boot.py` reads it: the pool base, the far
heap's cursor, the pointer, the drive map.  The two colours come off the
SHOT, not from the model -- what a mode F luminance comes out as is
between ANTIC and the monitor, so the model's picture is bits and the
gate supplies the palette.

## Three smaller things that had to move

  * **`vram_symbol` to `vbxeref`**, which owns the VRAM map.  Two copies
    of a lookup into `vbxe.h` is exactly how a model drifts from a
    driver.
  * **The font strips to `tools/fontref.py`**, below the VDI model and
    both surface models.  A face is the device's, and leaving the loader
    in `vdiref` made the import graph a cycle the moment `devref` grew a
    second device: `vdiref -> devref -> anticref -> vdiref`.  It failed
    loudly and immediately, which is the good kind of design feedback.
  * **`GEM_PAL` to `devref`**, because what a device does with sixteen
    colours -- permute them, reduce them to two luminances, ignore them
    -- is the device's business.  The VDI keeps only what was *asked*
    for, in `pal_req`, which is what `vq_color` answers from.

## What is still not modelled on ANTIC

A whole `test-m17`-style session -- thirteen stops, the desktop's globals
byte for byte, the calls counted on both sides.  That needs the
conformance runner linked for ANTIC and a disk to boot it from, and it is
a bigger job than this one; what `test-m26` now holds is the desk at its
first wait, which is where the boot gates hold the VBXE desks too.
