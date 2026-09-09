# Phase 32 -- the ANTIC surface

The second display gem4xe can draw on: stock ANTIC and GTIA, no VBXE in
the machine at all.

The plan has said since phase 0 what this is for, and it is not comfort:

> keep the ANTIC driver in scope as **proof the seam is real** -- it is
> the thing that stops VBXE assumptions leaking into portable code -- but
> do not let it gate the VBXE work, and do not promise the full desktop
> on it.

So this phase brings up the surface and gates it, the way phase 1 did for
VBXE.  The VDI on top of it is the next one.

## The geometry, and why it is 320 x 168

ANTIC mode F: 320 pixels a line, one bit each, 40 bytes a line,
scanline-sequential with no character-cell indirection.  Where it can
live is decided entirely by who else wants the memory:

  * ANTIC fetches over the chip bus with 16-bit addresses, so the
    framebuffer must be in **bank $00** and in **motherboard RAM** -- not
    the accelerator's SRAM, which ANTIC cannot see;
  * not `$4000-$7FFF` either, the banked window, which a DOS switches out
    from under itself while it services a call and which ANTIC would
    then happily put on the screen;
  * which leaves `$8000-$9BFF`, the region `src/gem4xe.scm` reserves for
    the VBXE's MEMAC window and which on a machine with no VBXE is simply
    free.  7,168 bytes, bounded above by SpartaDOS X's screen at `$9C00`.

Take the display list off the front and **168 lines** is what fits.  The
ceiling is memory, not ANTIC: 192 would need 7,680 bytes and 240 needs
the DLI-gated DMA trick as well.  21 rows of an 8-pixel font, one of them
the menu bar.  It is a real GEM, not a comfortable one, exactly as the
plan said.

    $8000-$80AE   the display list, 175 bytes
    $8100-$9B3F   the framebuffer, 168 x 40 = 6,720 bytes

## The framebuffer is linear, which it has no right to be

ANTIC's memory counter wraps at a 4 KB boundary, so a screen that crosses
one normally needs its lines re-based and the rasteriser needs to know
where.  Here the base is chosen so that **the crossing falls exactly
between two lines**:

    $8100 + 96 * 40 = $9000, to the byte

so a second LMS at line 96 re-points ANTIC at the address the linear
formula already gives, and every line is `base + L * 40` after all.  The
rasteriser never learns that the boundary exists.

That is also what the gate's widest bar is for: it spans line 96, and if
that LMS is wrong the bottom of the bar is drawn from the top of the
screen while nothing else on the display is wrong enough to notice.

## The gate

`test-m24` boots the emulator with `vbxe=False` -- the machine really has
no VBXE -- and requires a picture: the border, a bar across the 4 KB
crossing with a hole punched in it, a diagonal one plot per line, thin
one-pixel bars, and a comb of runs that begin and end inside a single
byte at every one of the eight offsets.

The screen must hold **exactly two colours**, because mode F is a hires
mode and a third means the display list is not saying what the driver
thinks it says.  WHICH two is GTIA's business, so the gate does not
hardcode a palette: it takes the colour under a pixel the model calls
clear as the background and the one under a set pixel as the foreground,
requires them to differ, and then requires all **53,760** pixels to
agree with `tools/anticref.py`.  Exact about the bits, which the driver
owns; silent about the palette, which it does not.

## What it cost: two rules already written down

Neither defect was new.  Both are in `tools/ccbug/README.md` already, and
the code that broke them looks perfectly ordinary, which is the point.

**Rule 3 and rule 5, in one line.**  The obvious way to fill the middle
of a run is

    *p++ = set ? 0xFF : 0x00;

-- a conditional stored through a pointer, and a pointer incremented in
the expression that uses it.  cc65816 5.18 miscompiles it in a way that
one iteration will never show you: **the loop runs once for any count
above two and then leaves the function**, so the run's right-hand edge is
never drawn either.  Two middle bytes worked and eight did not.  Hoisting
the constant out of the loop and separating the increment from the store
fixes it, and is better code besides.

**Rule 12, again.**  `x >> 3` on an `int16_t` to find a pixel's byte.
The diagonal drew correctly to x=31 and then landed eight bytes short of
where it belonged for every pixel after it -- which is what a sign that
appears at 32 looks like from outside.  `(uint16_t)x >> 3`.

Both were found by the gate in one run each, from 9,515 wrong pixels to
235 to none.  A rule that is written down is not the same as a rule that
is followed, and the second-best time to find that out is a gate.

## The device layer: writing modes, text, v_get_pixel

The seam the VDI needs is not the opcodes -- those are device-independent
and already written -- but the handful of things underneath them.
`paint_rect` in the VBXE driver is the shape of it: a device-INdependent
decision (XOR goes one way, ERASE is a no-op, everything else fills) over
device-DEPENDENT primitives called `*_dev`.  That is where the cut goes,
and the ANTIC side of it now has:

  * **the four GEM writing modes** over a source, a pen and a
    destination.  On a one-bit device REPLACE writes `pen ? src : ~src`,
    TRANS puts the pen where the source is set, XOR ignores the pen
    entirely and ERASE puts the pen where the source is CLEAR;
  * **spans and rectangles** in all four, with the edge masks;
  * **text**, which is where this driver is SIMPLER than the other one.
    The system font is a 1bpp strip already, so a glyph goes into a 1bpp
    framebuffer as itself, shifted across at most two bytes -- no
    expansion, and none of the second pre-shifted copy the VBXE driver
    keeps in VRAM so that it can blit the same glyph at an odd x;
  * **v_get_pixel**.

A solid source makes REPLACE and TRANS the same thing and ERASE nothing
at all, and the gate shows all three: the band has two holes punched in
it and one rectangle that left no mark.  That is not a curiosity -- it is
what the VBXE driver's `paint_rect` decides for itself, and the two
drivers have to agree about it.

The gate draws glyphs at **every one of the eight shifts** (x = 20 + 9i
walks all of them), in REPLACE on a clear ground, TRANS with pen 0 on a
set one, and XOR.  And it checks `v_get_pixel` as a RETURNED VALUE, from
eight positions the program stores into page 6, because a picture cannot
check it -- the same reason the VDI conformance suite compares returned
values and not only pixels.

It passed first time, which is what the ccbug rules are for.

## Patterns, and the styled lines that are the same thing

A GEM fill pattern is one UWORD a row with bit 15 the leftmost pixel of a
**16-aligned screen word** (`src/vdi/fillpat.c`).  The alignment is the
SCREEN's, not the run's, which is the property that makes two window
backgrounds side by side look like one desk rather than two patterns that
happen to abut.  The gate draws two runs meeting at x=149 for exactly
that reason.

And a styled horizontal line is not a separate thing: the VDI anchors
`vsl_type`'s mask to the same screen grid with `style_anchor()` and then
draws the line as a patterned span.  So `antic_patt_span` serves both,
and **the two drivers cannot disagree about the phase** -- which they
could very easily have done if the ANTIC side had grown its own idea of
where a dash starts.  A vertical line indexes the same mask by y.

## Still to come

`vro_cpyfm` and the mouse cursor, and then
wiring the dispatcher so that `screen()` reaches these instead of the
blitter.  `v_opnwk` already answers a device capability array and the AES
already lays out to whatever it says -- that is what the mechanism is for
-- so nothing above the VDI needs to change.

The selection is a BUILD choice for now, not a runtime one: the geometry
constants are compile-time and making them variables would touch every
clip and every stride in a 3,676-line file that 49 conformance cases
stand on.  One binary that finds no VBXE and falls back is the better
product and it is the obvious next step after the driver is complete; it
is deliberately not being done first.
