# Storage: what an Atari can boot gem4xe from, and what to build for it

`make dist` produces three media today — two floppies and a 16 MB CF
card image — and the question this answers is whether that is the right
set, given what people actually have attached to an Atari in 2026.

The short of it: **it is, and one card image serves every modern
interface**, because the thing that differs between them is a driver
that lives in the machine, not on the card.

## The four classes of thing people have

**1. An APT interface.**  Ultimate 1MB and Incognito (their PBI BIOS),
SIDE and SIDE2 (a soft driver from the cartridge's own flash, loaded
through SDX's `CAR:` device), SIDE3, IDE Plus 2.0, MyIDE-II with the
APT drivers, and IDEa.  All of them read Konrad Kokoszkiewicz's
**Atari Partition Table**, which is what `build/gem-cf.img` carries.

This is the important part: **the card is the same for all of them.**
The driver that reads it is in the machine's flash or cartridge, so
gem4xe's card needs no `SIDE.SYS`, no driver file, no per-interface
build.  What a tester needs is an SDX (or PBI BIOS) that knows their own
interface, which is what their interface shipped with.

**2. A loader that reads FAT.**  The SIDE3 Loader has full read/write
FAT16/FAT32 on the SD card and runs `.ATR`, `.XEX`, `.CAR` and `.ROM`
from it; AVGCART and The!Cart are the same shape.  These people do not
want a card image at all -- they want the **`.ATR` floppies**, dropped
onto the FAT card they already have, next to their games.  We ship
those.

**3. A SIO device.**  SDrive-MAX, FujiNet, a real 1050.  Also `.ATR`,
also already shipped.  A FujiNet mounts ours over the network the same
way it mounts anything else.

**4. Something from the 1990s.**  MIO and BlackBox are SCSI with their
own partitioning and their own configuration tools; the original MyIDE
has its own scheme that predates APT.  Nothing here targets them, and
nothing needs to: they all also have a floppy drive, and the `.ATR`
disks work.

**Altirra emulates every one of these** -- `blackbox`, `kmkjzide`,
`kmkjzide2`, `mio`, `myide`, `myide2`, `side`, `side2`, `side3`, plus
the Ultimate 1MB -- so any of it can be gated here rather than argued
about.  `make test-cf` currently gates the one this project is for
(U1MB + SIDE), and the others are a device tag away.

## Why one card image is enough, read rather than assumed

Altirra's `ATDecodePartitionTable` (`src/ATIO/source/partitiontable.cpp`)
is the parser real APT disks are read by, and this is what it does
first:

    if (buf[510] == 0x55 && buf[511] == 0xAA) {
        for (int offset = 0x1BE; offset < 0x1FE; offset += 16)
            if (buf[offset + 4] == 0x7F) {
                aptLBA = LE32(&buf[offset + 8]);
                break;
            }
        ...
    }

Two things follow, and the second is the useful one.

- The APT table is found **through the MBR**, by an entry whose type
  byte is `$7F`.  Ours is entry 1, covering the card
  (`tools/apt.py`, `tests/host/test_apt.py` pins it).
- It scans **all four entries** and cares only about the `$7F` one's
  start block.  So **a FAT partition can sit on the same card as the
  APT system** -- one entry for FAT, one for `$7F`, and both drivers
  find what they are looking for.

That last is not built, and the decision is that it does not need to be.
Someone with an APT drive already has one, with their own partitions and
their own idea of where things go; what they want is not a card image
that would overwrite it but **the files, on a floppy they can copy
from**.  So that is what the SpartaDOS floppy is now: an install disk,
laid out exactly as the card is.

    D1:                  the floppy                 D2: (or wherever)
      GEM>GEM.COM        the system         ---->     GEM>GEM.COM
      GEM>DESKTOP.G4A                                 GEM>DESKTOP.G4A
      GEM>DESKTOP.RSC                                 GEM>DESKTOP.RSC
      GEM>LANG.RSC                                    GEM>LANG.RSC
      GEM>816.COM                                     GEM>816.COM
      GEM>GEM4XE.CFG     the screen and the mouse     GEM>GEM4XE.CFG
      APPS>M11.G4A       the applications             APPS>M11.G4A
      APPS>CALC.G4A                                   APPS>CALC.G4A
      APPS>CLOCK.G4A                                  APPS>CLOCK.G4A

Under SpartaDOS that is two commands and no decisions:

    COPY D1:>GEM>*.* D2:>GEM>*.*
    COPY D1:>APPS>*.* D2:>APPS>*.*

...and an `AUTOEXEC.BAT` on the drive holding the same two lines the
floppy's holds, `CD >GEM` and `GEM`.  The floppy keeps 989 sectors
free, so it is also somewhere to put a program of your own on the way
past.

The evidence for the FAT-beside-APT card stays written down because it
is the obvious thing to build if someone asks for a single card a PC can
also drop files onto, and because the evidence for it being possible is
here rather than in a forum thread.

## So: which medium for whom

| If you have | Use | Why |
|---|---|---|
| U1MB / Incognito, SIDE, SIDE2, SIDE3, IDE Plus 2.0, MyIDE-II | `disks/gem-cf.img` written to a card | APT; the system installs to `\GEM\`, applications to `\APPS\` |
| An APT drive you have already partitioned | `disks/gem-sp.atr`, and copy `GEM>` and `APPS>` off it | the floppy is laid out as the card is; nothing of yours is touched |
| SIDE3 / AVGCART / any FAT loader | `disks/gem-sp.atr` on the card you have | the loader mounts it; nothing to install |
| SDrive-MAX, FujiNet, a real drive | `disks/gem-sp.atr` (SpartaDOS) or `disks/gem-boot.atr` (DOS 2) | plain floppy images |
| A DOS you already like | `system/` -- the loose files | put them where you want; give the disk a start-up that runs `GEM` |
| MIO, BlackBox, original MyIDE | the floppies | their partitioning is their own; nothing here writes it |

## Writing the card image, and the warning that goes with it

`gem-cf.img` is a **whole-card image**, 16 MB of 512-byte blocks.
Writing it with `dd` or a disk imager **replaces everything on the
card**, and a card larger than 16 MB keeps only the first 16 MB in the
partition table -- the rest is unallocated until it is repartitioned
with the APT tools.

So it is for a card you are giving to gem4xe, not for the card with
your collection on it.  For that one, the floppies.

    dd if=disks/gem-cf.img of=/dev/sdX bs=1M conv=fsync    # sdX, not sdX1

## What would change if this grows

Two things are cheap and neither is needed yet:

- **A FAT partition beside the APT one**, as above: a card a PC can
  read and a SIDE3 Loader can browse, with the installed system behind
  it.  It wants a small FAT16 writer in `tools/`, in the shape of the
  SDFS and DOS 2 writers already in `tools/atr.py`.
- **A size other than 16 MB.**  `tools/mkcf.py` fixes the geometry; a
  knob is a few lines.  16 MB was chosen because it is the smallest
  thing that comfortably holds two 8 MB partitions, and because a
  16 MB file is a reasonable thing to put in a release.
