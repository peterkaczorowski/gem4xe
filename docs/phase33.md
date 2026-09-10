# Phase 33 -- the VDI on the ANTIC surface

Phase 32 built the surface.  This one puts **the same `src/vdi/vdi.c`**
on it -- the dispatcher, the workstation state, the clipping, the
attributes and the text -- and then the AES on top of that, on a screen
none of it was written for.

    make test-m25   53,760 / 53,760 pixels, against tools/anticref.py

## The seam, extracted one call at a time

`src/vdi/vdi.c` was 3,676 lines and knew what a pixel was made of:
`blit_`, `vram_`, `VR_` and `MEMAC_` ran through it.  Phase 33 took that
out into `src/vdi/vdidev.h` -- 26 calls and the geometry -- with
`dev_vbxe.c` and `dev_antic.c` on the far side.

It was done in **seven increments, with the 86-case conformance gate run
after each one**, because the alternative -- move it all and then find
out -- is a bisection over a 1,100-line move.  Nothing was ever red for
more than one increment.  vdi.c came out at about 2,530 lines with no
reference to the VBXE left in it.

Three bugs the increments caught, each of which would have been much
harder to find at the end:

  * **`gl_nplanes` was 4 on a one-plane device.**  The AES sizes its menu
    save buffer from it (`gsx_malloc`: `gl_wchar * 25 * gl_height *
    gl_nplanes / 8`), so a device that lies here wastes memory or loses
    part of a menu.  `dev_planes()` exists because of this.
  * **ccbug rule 3 + 5 together**: `*p++ = set ? 0xFF : 0x00;` ran its
    loop once for any count above two and then left the function.  The
    constant had to be hoisted and the increment separated.
  * **ccbug rule 12**: `x >> 3` on an `int16_t` in `an_at()` -- a signed
    right shift is not an arithmetic one -- so a diagonal was correct to
    x=31 and eight bytes short after it.  `(uint16_t)x >> 3`.

## The font, and why 8x8 was not going to do

320 pixels and an 8-wide cell is **forty columns**.  A GEM lays its menu
bar, its dialogs and its file listings out in character cells, and forty
of them is not a desktop; it is a screen with one window on it.

The trick every 8-bit program of the period used -- draw a narrower font
and get 80 columns out of a 40-column screen -- is the right answer here
too, and GEM already had one: **`bios/fnt_st_6x6.c`, Atari's own
condensed face**, which the ST uses for icon labels in low resolution.
It is a face designed to be read at that size rather than one squeezed
into it, and it is GPL v2 in EmuTOS like the 8x8.

    8 x 8   40 columns, 21 rows
    6 x 6   53 columns, 28 rows

`tools/fontconv6.py` repacks it: the donor stores six bits per character
and gem4xe's strip is a byte per character, 256 bytes a row, glyph
left-aligned -- so the device can blit every face with the same code.

The cell is the DEVICE's, in `vdidev.h`, beside the geometry.  Nothing
above the seam has an opinion about it: `gsx_start` asks the VDI at
start-up and everything the AES lays out afterwards comes off the answer,
which is exactly the mechanism GEM has for coping with ST low, medium and
high resolution.

A 4x8 face was drawn by hand as well (`tools/font4x8.py`, with a
validating generator) and rendered; at four wide it is too cramped to
read and it is not wired into the build.  The 40-column classics -- The
Last Word's among them -- are in closed-source programs and would need a
licence grant, so if a narrower face is ever wanted, it is a conversation
and not a transcription.

### The claim that was wrong

I said a 4-wide face was the only way the menu bar would fit, because
`ADMENU` is 80 cells wide in the resource.  It is not: `mn_bar` does

    tree[THEBAR].ob_width = gl_width - tree[THEBAR].ob_x;

so **the AES stretches the bar itself** and the resource's width never
binds.

## The font trilogy

Three bugs in a row, each hiding the next, all in getting the condensed
face onto the screen:

  1. `antic_glyph()` looped eight rows over a six-row strip;
  2. `antic.c`'s `an_font_row()` read `font8x8` directly instead of the
     face it was handed, so it kept drawing the old one;
  3. the milestone never called `vdi_font_default()`, so `vdi_font` was
     zero and the strip was read from address 0.

And underneath them, the far-pointer idiom that survives cc65816: do the
arithmetic on the `uint32_t` **before** the cast --

    (const uint8_t __far *)(addr + row * FONT_STRIDE)

-- because `face[row * FONT_STRIDE + ch]` after the cast is miscompiled.

## What the gate holds

`tests/emu/m25_antic_vdi.py` drives the VDI through its own interface --
`contrl`, `intin`, `ptsin` and a call to `vdi()` -- so what is tested is
the dispatcher and the state and the clipping, not the device: a filled
rectangle in pen 1, the same rectangle again in XOR so a hole appears in
it, a line of text, and then an **AES object tree** handed to `ob_draw`:
an OUTLINED box with a 2px border, a title, an edit field and a button
carrying the DEFAULT ring.  All 53,760 pixels are compared against
`tools/anticref.py`.

It also checks what `gsx_start` made of the device -- the extent, the
depth, the cell, the box, the menu bar's height, the desk's -- by doing
the AES's own arithmetic rather than by comparing numbers somebody wrote
down.  A constant would not be evidence that a GEM adapts to a second
screen.

**The pens are worth reading.**  GEM numbers its pens white 0, black 1.
The device has two colours and no palette, so pen 0 is the background and
anything else is ink -- and mode F gives the background its hue and set
pixels COLPF1's luminance, so a GEM screen comes out white with black ink
without anything having to arrange it.  `map_col` exists on the VBXE
because XOR complements bits and the AES XORs a selected button; on one
plane the two are already complements and there is nothing to map.

Modelling the object tree found one thing, in the model rather than the
target: OUTLINED's white ring is `w+4` by `h+4`, and writing 48 for a
46-tall box put its bottom edge on the object's own border and erased it.
320 pixels, all in two rows.  The model is built from the object's rect
now rather than from arithmetic done by hand.

## What is not gated here

The AES's *layout* on ANTIC is checked; the DESKTOP's pixels are not.
`tools/deskref.py` draws through `tools/vdiref.py`, which is written to a
4bpp VBXE surface, so a modelled ANTIC desktop is the whole VDI model
ported.  That is its own phase.  Phase 34 checks what can be established
without it.
