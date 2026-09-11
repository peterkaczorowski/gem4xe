# Printing — the geometry, pinned

Nothing is built yet.  This is the page the printer workstation will
rasterise, decided before the driver is written because every gate
compares against it and every emitter is parameterised by it.

    raster          640 x 800 dots, 1 bit each
    buffer          64,000 bytes, far
    cell            8 x 8 -- the same face the VBXE screen draws with
    grid            80 columns x 100 rows
    declared        100 dpi -> 6.4 x 8.0 inches
    work_out[3,4]   254 x 254 microns, which is what 100 dpi means to an
                    application that asks how big a pixel is

## Why 640 x 800, and not something rounder

**It is the only useful size that fits in one far bank.**  80 bytes a row
by 800 rows is 64,000, and a bank is 65,536.  Every larger page spans
banks, and two things in this tree make that expensive rather than
merely untidy: `far_alloc` refuses a block that straddles a bank at all
(it cannot be indexed past its own bank), and `__far` pointer arithmetic
is SIXTEEN BITS WITHIN one bank -- which is the bug docs/phase24.md is
about.  A page inside one bank is a page whose every inner loop can do
plain 16-bit arithmetic, exactly as `dev_antic.c` does on the
motherboard, and the device becomes that file with a different stride
rather than that file plus a class of pointer bug.

    640 x  800   64,000 bytes   one bank
    640 x 1000   80,000         spans
    768 x  960   92,160         spans
    800 x 1000  100,000         spans

**640 is also the screen's width.**  The VBXE surface is 640 x 240 with
the same 8 x 8 cell, so the printer reports the same 80 columns the
screen does, and a dialog or a form laid out for one lands on the other
at identical coordinates.  That is device independence paying off rather
than merely existing: the AES lays out on what `gsx_start` is told, and
being told the same thing twice is the cheapest possible port.

**800 rows is 100 lines**, and 3⅓ screens tall.

## Why 100 dpi

At 100 dpi the page is 6.4 x 8.0 inches, which fits US Letter and A4
with better than three-quarters of an inch of margin on every side.  It
is also one of the five resolutions PCL 5 will take directly (75, 100,
150, 300, 600), so the raster goes out with no scaling: `ESC *t100R` and
the printer replicates to its own dots.

An 8-dot character at 100 dpi is 0.08 inch, near enough 6 point.  That is
small, and it is also exactly what an 80-column page off an 8-pin printer
has always looked like, which is the size this platform's users have in
their hands already.

**Bigger text is the EMITTER's business, not the raster's.**  Nothing
above the emitter knows what an inch is: the page is 640 x 800 dots and
the declared resolution is a number in a header.  PostScript can scale
to anything; PCL is limited to its ladder, and 75 dpi gives 8.53 x 10.67
inches -- too wide for Letter by three hundredths of an inch, so it is
for tractor paper rather than a default.  That is why the resolution is
a config value and not a constant in the device.

## What the device reports

    work_out[0], [1]    639, 799        the extent, in pixels
    work_out[3], [4]    254, 254        a pixel, in microns
    colours             2
    planes              1
    cell                8 x 8

## The emitters

`GEM4XE.CFG` chooses, beside VIDEO and MOUSE:

    PRINTER = NONE | PCL | PS       what a page is written in
    PRINTTO = P:                    where it goes; a filename works

NONE is the default, and does nothing: a machine with no printer must not
stop in `v_updwk` waiting for one.  `src/gem.c` copies both into
`pr_kind` and `pr_dest` at start-up, the same way it hands the pointer
kind to `ptr_init` -- the VDI does not know what a configuration file is.

**Both formats are ROW-oriented**, which is the whole reason these two
and not the older one.  The page's rows go out as they lie; ESC/P, the
dot-matrix language, is column-oriented -- each byte eight vertical dots
-- so it wants the page transposed, and the oldest format is the awkward
one here while the two modern ones are nearly free.

    PCL 5       ESC E, ESC *t100R, ESC *r0A, then per row either
                ESC *b<n>Y  to skip n blank rows
                ESC *b80W   followed by the row's 80 bytes
                and ESC *rC, ESC E to eject.
                14,074 bytes for the gate's page: most of a page of GEM
                is paper, and the skip is the difference between that and
                64,000 down a serial line.

    PostScript  a DSC-conforming one-page program: 460.8 x 576 points
                (6.4 x 8.0 inches at 100 dpi) translated to 76, 108, and
                one `image` with the matrix [640 0 0 -800 0 800], which
                flips y because PostScript counts from the bottom left
                and a raster from the top left.  129,049 bytes, all of
                it ASCII hex.

**One asymmetry worth knowing.**  A 1 bit in this page means ink.  PCL
agrees -- a 1 raster bit prints a dot -- so its rows are copied out
unchanged.  PostScript's `image` in DeviceGray reads 0 as black, so the
bytes are INVERTED on the way out.  That is done rather than argued about
with `imagemask`'s polarity, which is the sort of thing that is only ever
discovered on paper.

**PCL 6 is not on the list and should not be**: it is PCL XL, a binary
protocol and a different thing entirely, and every device that speaks it
also speaks PCL 5.

Where they reach: PCL 5 to `P:` is a laser printer, a FujiNet passing the
bytes through, or Altirra's emulated 825.  PostScript to a FILE is every
modern printer there is, by way of the machine the user copies it to --
CUPS, Ghostscript, a mail client.  Neither needs gem4xe to know anything
about networks.

## How it is checked

`v_updwk` on a printer produces a file, and a wrong one does not fail:
it prints, and comes out blank, or shifted by a row, or in negative, and
the thing that tells you is a laser printer in another room.  So:

  * `tools/emitref.py` is the model of `src/vdi/emit.c` -- `pcl()`,
    `ps()`, and a `from_pcl()` decoder strict enough to refuse anything
    but what `pcl()` emits;
  * `tools/devref.py`'s `Printer` is the third device the models drive,
    beside `Vbxe` and `Antic`, and `tools/vdiref.py` runs on it with one
    argument changed;
  * `tests/host/test_print.py` pins the constants across `print.h`,
    `config.h`, `emitref` and `devref`, round-trips the PCL, and renders
    the PostScript with **Ghostscript** -- the only honest way to check a
    program written in a language that has an interpreter;
  * `tests/emu/m30_print.py` runs the real thing on the Atari, reads the
    two files back out of the disk image, and compares all three: the
    decoded PCL against the model's page, the PCL bytes against the
    model's bytes, and Ghostscript's rendering of the Atari's own
    PostScript against the same page.

It earned its keep on the first run.  `dev_glyph` read the system face as
`ch * height + row` when the face is a STRIP -- row r of all 256
characters together, `row * 256 + ch` -- so "GEM4XE" came out as six
other letters' middles.  Nothing about the page looked wrong in outline;
it took 512,000 dots compared against a model to say so.

## What is still open

  * **The page buffer's lifetime.**  64,000 bytes of far memory taken at
    `v_opnwk` and given back at `v_clswk`, which puts it above the
    allocator's floor (src/sys/app.h) and inside the program's own
    extent.
  * **ESC/P**, which is what FujiNet's printer emulation renders to a
    PDF -- and so how a machine with no printer at all gets one.  It
    needs the page transposed: eight rows at a time into a column of
    bytes, which is a buffer and a loop and no new argument.
  * **A `NET` destination** by way of FujiNet's `N:` handler, so a page
    can go straight at a CUPS queue rather than to a file somebody
    carries.
