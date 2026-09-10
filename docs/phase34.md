# Phase 34 -- one binary, two screens, and a file to argue with it

Phase 32 brought up the ANTIC surface and phase 33 put the VDI on it, but
the two devices were chosen at **link time**: `build/m3.xex` drew on a
VBXE and `build/m25.xex` on ANTIC, and `GEM.COM` refused a machine
without a VBXE with one line of text.

This phase makes the choice a runtime one, and gives the user a way to
overrule it that works on a machine whose screen is the thing that is
broken.

    make test-m26     the shipped GEM.COM with a VBXE, without one,
                      and in safe mode -- three boots, nothing typed
    make test-m3      86/86, still, through the vtable
    make test-m25     53,760 / 53,760, still

## The seam becomes a table

`src/vdi/vdidev.h` already named the 26 calls a device has to answer and
the geometry it has to declare.  Making the choice a runtime one is
therefore not a redesign; it is turning that list into a `struct` of
pointers and the names above it into macros onto a `vdev` that points at
one of them:

    #define SCR_W          (vdev->w)
    #define FONT_H         (vdev->font_h)
    #define dev_fill_rect  (vdev->fill_rect)

**Nothing above the seam changed.**  `src/vdi/vdi.c` still says `SCR_W`
and `dev_fill_rect` and reads exactly as it did when there was one device
compiled in; the diff is in the header, the two device files' last twenty
lines, and one array dimension.  That is the point of having drawn the
seam in phase 32 before needing it.

A device's OWN file does not see those macros.  It defines
`GEM4XE_DEV_IMPL`, which gives it the geometry back as compile-time
constants -- for it, they are constants -- and `GEM4XE_DEV_PREFIX`, which
stamps `vbd_` or `and_` on its functions so both can be linked at once
without either being renamed by hand.  The table at the end of each file
is where the two meet.

`vbxe.h`'s `SCR_W`/`SCR_H`/`SCR_STRIDE` became `VB_W`/`VB_H`/`VB_STRIDE`
on the way, matching `antic.h`'s `AN_`.  A device's numbers are the
device's; "the screen" is `vdev`'s.

### Where the tables live

`__far`, in `cfar` with the far code.  Each is about 130 bytes -- 26
24-bit function pointers and a dozen words -- and bank $00 has 2,430
bytes for every constant the system owns.  Two of them there is a tenth
of all near memory spent on a table that is read once per primitive, so
they are read through 24-bit pointers instead.  At 20 MHz that is a few
cycles against a VRAM write at 1.79.  It bought back 260 bytes: `Near`
went from 97.4% full to 86.7%.

### What it costs

An indirect call per primitive, which in the large code model is a `jsl`
through a pointer with no trampoline, and a long read for each `SCR_W`.
Both gates that measure the result -- 86 VDI conformance cases and 53,760
pixels -- are unchanged.

### What it bought besides the fallback

`build/vdi_a.o`, `build/pointer_a.o` and `build/font_a.o` are gone: the
device-independent halves were being compiled TWICE, once for each side
of the seam, and are now compiled once and linked into both.  test-m3 and
test-m25 now link the same `build/vdi.o`, which is a stronger statement
of the claim than the prose was.

## GEM4XE.CFG

    # VIDEO -- which screen.
    #
    #   AUTO    the VBXE if the machine has one, ANTIC if not.  The default.
    #   VBXE    640x240, sixteen colours.  Refuses to start without one.
    #   ANTIC   320x168, two colours.  The safe mode.
    #   SAFE    the same as ANTIC.
    #
    # VIDEO=AUTO

Everything else gem4xe can be told is in `DESKTOP.INF`, which the desktop
writes and reads with a GEM already on the screen.  This is the one thing
that cannot be, because it decides **what the screen is**.  A machine
whose monitor will not lock to the VBXE's 640x240 shows nothing at all,
and there is no dialog to be had on a display that is not there.

So the recovery path is: **reboot to the DOS prompt and edit a text
file**.  That decides the whole format.  It is `KEY=VALUE`, one per line,
`#` or `;` starts a comment, case does not matter, and **an unknown key
or value is skipped rather than refused** -- somebody typing into `ED` on
a 40-column screen must not be able to make the machine unbootable by
misspelling something, and a file written for a later gem4xe should still
boot this one.  No file at all is the same as the shipped one, which has
every setting commented out: finding the file is finding its
documentation.

`MOUSE=` is there too -- `ST`, `AMIGA`, `TRAKBALL`, `TABLET`, `XEM1`,
`NONE` -- because the pointing device is the other thing a user can be
stuck behind with no way to say so.

### Read as bytes, not as records

`cio_getrec` is the obvious call and is the wrong one.  It ends a record
at the Atari's own EOL (`$9B`) and nothing else, so a file that came off
a PC -- CR LF, or bare LF -- arrives as **one record too long for any
buffer** and every setting in it is lost, silently.  This is a file
people will edit on whatever they have, so `config_read()` reads bytes
and ends a line on any of the three, or on the end of the file with no
ending at all.  What ships is `$9B`, because that is what a DOS editor
writes.

### The tables are far, for the same reason as the device tables

The value names -- `TRAKBALL`, `MOUSTER` and the rest -- are `char[9]`
arrays inside a `__far` table rather than pointers to string literals, so
the whole thing lands in `cfar`.  A boot-time word list has no claim on
bank $00.  The `PTR_*` numbers come from the enum in
`src/vdi/pointer.h`, not from a comment: a number here that had drifted
from the one there would pick the wrong device and say nothing about it.

## What the gate establishes

Three boots of the SAME `build/gem.xex`, nothing typed in any of them --
the disks start GEM themselves, the way a user's would:

| | disk | machine | `GEM4XE.CFG` | comes up |
|---|---|---|---|---|
| `auto-vbxe` | shipped | has a VBXE | commented out | 640x240, 4 planes, 8x8 |
| `auto-antic` | shipped | has none | commented out | 320x168, 1 plane, 6x6 |
| `safe-mode` | shipped + one line | has a VBXE | `VIDEO=ANTIC` | 320x168, 1 plane, 6x6 |

and it checks, in the order it is worth checking:

  * **which device** -- `vdev` against the two tables' own addresses, read
    from the linker's symbols.  This is the claim itself;
  * **what the AES made of it** -- `gl_width`, `gl_height`, `gl_nplanes`,
    `gl_wchar`, `gl_hchar`, which `gsx_start` asks the VDI for and every
    dialog, window and menu is laid out from.  A GEM that reached the
    right device and then laid out for the other one would draw off the
    edge of the screen, and this is what would say so;
  * **what the file said** -- `config` itself, so a safe-mode boot that
    came up on ANTIC because the parser silently failed cannot pass as a
    safe-mode boot that worked;
  * the screen: two colours on ANTIC, and ink on both -- a desk that drew
    nothing is not a desk;
  * and **the two ANTIC desks against each other, pixel for pixel**.
    Same binary, same disk, same device, reached two different ways: if
    the override is really just "pick the other table" they are
    identical, and any difference is the override doing something else as
    well.

One thing that cost a round of the gate and is worth writing down: **a
VBXE in the machine changes the SCREENSHOT, not the screen.**  Altirra
emits a 672-pixel-wide frame whenever the card is fitted -- the overlay's
widest HR mode -- whether or not the overlay is on, so an ANTIC playfield
pixel is two screenshot pixels wide on the safe-mode machine and one on
the auto-antic machine.  The first comparison said 25,380 of 53,760
pixels differed; sampled correctly the answer is **0**.  The two desks
are the same picture to the pixel, and the gate now derives its sampling
from the frame's width rather than assuming it.

There is no pixel model of the ANTIC DESKTOP here, and that is deliberate
rather than an omission: `tools/deskref.py` draws through
`tools/vdiref.py`, which is written to a 4bpp VBXE surface, so a modelled
ANTIC desk is the whole VDI model ported and belongs in its own phase.
test-m25 already holds the ANTIC VDI, and the AES's object library on it,
to the pixel.

## ⚠ The lesson of this phase is a build one

Ten of the 86 VDI conformance cases went red the moment `vdev` became a
far pointer.  All ten were `v_locator`, all returned `(-1, -1)`, and the
generated code for the function that produced them --

    `?L38`:
                jsl     long:`?L319`      ; _Dp = vdev
                lda     [.tiny _Dp]       ; vdev->w
                dec     a
                sec
                sbc     ptr_state

-- was **perfectly correct**.  So was the memory it read: a probe on the
machine showed `vdev` holding the linker's own address for the table, and
the table holding `w = 640`.

The object beside it was not.  `build/pointer.o`'s rule in the Makefile
listed `pointer.h`, `vdi.h` and `irq.h` and **did not list `vdidev.h`**,
so make kept an object compiled when `vdev` was a NEAR pointer.  That
object took the low half of a bank-$02 address for a bank-$00 one, read
the application pool, got zero, and clamped every pointer position to
`SCR_W - 1 == -1`.

Phase 6's lesson was "a flag that changes the symptom is not a flag that
explains it".  This one is narrower and more embarrassing: **a
hand-written prerequisite list is a list that can be wrong, and when it
is wrong the compiler is not.**  The fix is not to add one header to one
rule.  `tools/ccdep.sh` now runs the compiler's own `--dependencies`
beside every compile and the Makefile includes `build/*.d`, so after the
first build the prerequisites are the compiler's and not anyone's memory.
The dependency pass is a preprocess and costs 17 ms against the compile's
1.2 seconds on the largest file in the tree.  The hand-written lists
stay: they are what makes the first build after a clean correct, before
any `.d` exists.
