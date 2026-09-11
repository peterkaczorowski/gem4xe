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

## What is still open

  * **The emitters.**  `PRINTER=PCL | PS | EPSON | FILE` in GEM4XE.CFG,
    beside VIDEO and MOUSE.  PCL 5 and PostScript are both row-oriented
    and take the page's rows as they are; ESC/P is column-oriented and
    needs the page transposed, which is why the dot-matrix format is the
    awkward one here and not the modern ones.  ESC/P earns its place by
    being what FujiNet's printer emulation renders, which is how a
    machine with no printer at all gets a PDF.
  * **PCL 6 is not on that list** and should not be: it is PCL XL, a
    binary protocol and a different thing entirely, and every device that
    speaks it also speaks PCL 5.
  * **The page buffer's lifetime.**  64,000 bytes of far memory taken at
    `v_opnwk` and given back at `v_clswk`, which puts it above the
    allocator's floor (src/sys/app.h) and inside the program's own
    extent.
  * **`v_updwk`** is what emits a page -- opcode 4, which DRI's own
    screen driver `v_nop`s and which a printer driver is the first thing
    here to implement.
