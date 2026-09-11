# Phase 37 -- the printer, and the page off the machine

The VDI's device seam was drawn in phase 34 and the model's side of it in
phase 35, and both were argued for on the strength of a claim that could
not yet be tested: that a device behind it need not be a screen.  This is
the phase that tests it.  `src/vdi/dev_print.c` is 640 x 800 dots at one
bit in far memory; `src/vdi/emit.c` writes them out; and the same
`src/vdi/vdi.c` that draws on the VBXE and on ANTIC draws on paper with
nothing above the seam changed.

    make test-m30

    the model's page: 10,624 dots of 512,000
    the VDI opened a workstation on the printer, 65C816, no VBXE in the machine
    the Atari wrote 14,074 bytes of PCL and 129,049 of PostScript
    the PCL decodes to the model's page, all 512,000 dots
    ghostscript renders the PostScript back to the same 512,000 dots
    gem4xe-m30: PASS -- the VDI on the printer, 0 problem(s)

The geometry and the argument for it are in [printing.md](printing.md).
The short version: 80 bytes a row by 800 rows is 64,000 against a far
bank's 65,536, so the whole page is addressable with plain 16-bit
offsets, and at a declared 100 dpi that is 6.4 x 8.0 inches -- Letter and
A4 with better than three-quarters of an inch of margin.  It is the only
size that fits one bank, which is why it is the size.

## What the seam had to absorb

Three things, and none of them above it.

**A device id.**  `v_opnvwk` takes one in `intin[0]`; 21 to 30 is the
printer in every GEM there has ever been, and it is what picks
`vdev_print` and takes the page.  A program asks for a printer the way
the manual says to, and gets one.

**Opcode 4.**  `v_updwk` is the call DRI's own screen driver `v_nop`s,
because updating a screen is what drawing on it already did.  On a
printer it is the only call that does anything at all: it is the page,
leaving.  It is the first thing in this project to implement it.

**A workstation that carries its device.**  `vdev` was one global until
this phase; now a `Vwk` names its own and `vwk_select` binds it, so a
program can hold a screen workstation and a printer workstation at once
and the VDI does not have to be told twice which it is talking to.

Everything else -- the clipping, the writing modes, the attributes, the
text placement, `vr_recfl`, `v_pline`, `v_gtext` -- is the code that was
already there.

## The emitters, and the one asymmetry

`GEM4XE.CFG` gained two keys beside VIDEO and MOUSE:

    PRINTER = NONE | PCL | PS
    PRINTTO = P:

NONE is the default, and does nothing.  A machine with no printer must
not stop in `v_updwk` waiting for one, and the honest way to say "there
is no printer here" is to say nothing.

PCL 5 and PostScript are both **row-oriented**, which is the whole reason
these two.  The page's rows go out as they lie.  ESC/P -- the dot-matrix
language, and the one FujiNet renders into a PDF -- is column-oriented,
each byte eight vertical dots, so it wants the page transposed: the
oldest format is the awkward one here and the two modern ones are nearly
free.  PCL 6 is not on the list and should not be; it is PCL XL, a binary
protocol and a different thing, and every device that speaks it also
speaks PCL 5.

The asymmetry is worth writing down because it is invisible until it is
on paper.  **A 1 bit in this page means ink.**  PCL agrees -- a 1 raster
bit prints a dot -- so its rows are copied out unchanged.  PostScript's
`image` in DeviceGray reads **0** as black, so the bytes are INVERTED on
the way out.  That is done rather than argued about with `imagemask`'s
polarity, which is exactly the sort of thing that is only ever discovered
by printing it.

The blank-row skip is not an optimisation to be proud of; it is the
difference between a usable printer and an unusable one.  Most of a page
of GEM is paper.  The gate's page is 14,074 bytes of PCL where the raster
is 64,000, and on a serial line that is a minute the user does not spend.

## The VDI does not read the configuration file

`src/gem.c` copies `config.printer` and `config.printto` into `pr_kind`
and `pr_dest` at start-up, the way it already hands the pointer kind to
`ptr_init`.  This is not tidiness.  `emit.c` reached into `config`
directly at first, and the VDI conformance runner and the ANTIC runner --
neither of which has a configuration file, or wants one -- stopped
linking.  The layering was wrong and the linker said so before anything
else did.

## The model's third device

`tools/devref.py` grew a `Printer` beside `Vbxe` and `Antic`, and
`tools/anticref.py`'s surface -- which was a 320 x 168 framebuffer with
its geometry in module constants -- became a 1bpp surface of any size.
`tools/vdiref.py` runs on it with one argument changed, exactly as it
does for the second screen, and neither it nor anything above it was
edited.

`tools/emitref.py` is new: the model of `emit.c`, plus a `from_pcl()`
decoder deliberately strict enough to refuse anything but what `pcl()`
emits.  A decoder that shrugs at an unexpected escape is a decoder that
would have passed the bug.

## Ghostscript is the oracle

PostScript is a programming language, and a model that says "these are
the right bytes" only checks that two programs agree about what to write,
not that what they wrote means anything.  So the gate hands the Atari's
own PostScript to **Ghostscript**, tells it to put the BoundingBox corner
on the origin and render 640 x 800 at 100 dpi -- one device pixel on each
of the page's dots -- and compares the bitmap that comes back with the
model's page.  That is what checks the y-flip, the image matrix and the
inversion, none of which fails visibly.

`tests/host/test_print.py` does the same on a synthetic page, and also
pins the constants: `print.h`, `config.h`, `emitref` and `devref` are one
set of numbers, checked by parsing the header rather than by copying it.
Its header parser evaluates expressions, because `PR_STRIDE` is
`(PR_W / 8)` and a test that could only read literals would be a test of
how the header is punctuated.

## Four bugs, and where each came from

**Nine bytes of `cdata`.**  The emitters' string literals -- the
PostScript preamble above all -- went into bank $00's 2,430-byte `Near`
region, which had 314 bytes spare before this phase and nine too few
after.  They belong in `cfar` beside the system font, and now live there,
reaching CIO through an 80-byte buffer because CIO writes from bank $00
and no further.  **The link is the thing that noticed**, which is the
argument for a memory map with named regions rather than one big pool.

**The runner would not link.**  Above: the VDI reaching into `config`.

**The milestone hung in its first `cio_write`.**  It drew the page,
reached the emitter, and stopped -- and the screen said nothing, because
there is no screen.  `src/gem.c` opens with `dos_ident()`,
`rapidus_speedup()` and `irq_install()`, and the milestone had none of
them.  In native mode the 65816 fetches its vectors from `$FFEA`/`$FFEE`,
which the Atari's ROM does not fill, so the first VBI inside the first
SIO transfer derailed the machine.  **The other two VDI milestones get
away without it because they never call the OS** -- this is the first one
that writes, and writing is SIO, and SIO is interrupts.

The progress byte the gate reads at `$0604` exists because of this: with
nothing to photograph, a milestone that stops has to be able to say
where, and "it reached `l` and not `P`" was the whole diagnosis.

**`dev_glyph` read the face as cells.**  The system font is a **strip**:
row *r* of all 256 characters lies together, so a row is at
`row * 256 + ch` and not at `ch * height + row`.  `dev_print.c` had it
the second way, and "GEM4XE -- 640 x 800 DOTS AT 100 DPI" came out as
thirty-five other letters' middles.  Nothing about the page looked wrong
in outline -- the rules, the block, the XORed hole, the diagonal and the
last row all matched -- and it took 512,000 dots compared against a model
to find eight rows that did not.

That one is the gate's whole justification.  It was written for a device
whose output nobody can look at, and on its first complete run it found a
bug in the device it was written for.

## What is not done

  * **ESC/P**, and with it FujiNet's print-to-PDF.  It needs the page
    transposed -- eight rows at a time into a column of bytes -- which is
    a buffer and a loop and no new argument.
  * **A `NET` destination** through FujiNet's `N:`, so a page can go
    straight at a CUPS queue rather than to a file somebody carries.
  * **No program prints yet.**  The device, the page and the emitters
    exist and are checked; what is missing is a `File -> Print` that
    opens the workstation, draws into it and calls `v_updwk`.  The
    desktop is the obvious first one.
