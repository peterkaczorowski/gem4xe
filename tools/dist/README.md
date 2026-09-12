# gem4xe — how to try it

**{stamp}**

GEM on an Atari XL/XE: the VDI, the AES and a desktop, at 640 x 240 in
16 colours on VBXE's HR overlay, running native on a 65C816.  It is a
port of the Caldera-GPL Digital Research sources by way of EmuTOS, and
it is early — this page says what works and what does not, and both
halves are generated from the program itself.

## The machine

| | |
|---|---|
| **VBXE** | wanted, not required; an **FX core, version 1.26**.  gem4xe detects the core by the low nibble of `CORE_REVISION` and writes an overlay priority of `$FF`, because bits 6/7 changed meaning at 1.26 and a priority of `$00` makes the overlay vanish there.  **Without a VBXE you get 320 x 168 on ANTIC mode F instead** — one `GEM.COM` carries both drivers and picks at start-up, and `VIDEO=ANTIC` in `GEM4XE.CFG` forces the small screen on a machine that has a VBXE its monitor will not show |
| **a 65C816 with linear RAM** | required. **Rapidus** is the one this is tested on; Antonia should qualify and is not emulated, so it is untested |
| **Ultimate 1MB** | optional. Its DS1305 is where file timestamps come from, and its flash can hold SpartaDOS X and the PBI BIOS that mounts a CF card |

gem4xe **refuses a plain 6502** rather than corrupting it: writing the
bank its code lives in needs a 65C816, and on an NMOS 6502 the long
store is an unstable undocumented opcode.  A machine that *has* an
accelerator sitting in 6502 mode is a different case, and the loader
switches that one itself — see *Booting*.

## In the emulator

[AltirraSDL](https://github.com/ilmenit/AltirraSDL), or Altirra with the
same devices.  This is the command line the test suite itself uses:

    AltirraSDL --pal --hardware 800xl --kernel xl --nobasic \
        --memsize 1088K --cleardevices \
        --adddevice "vbxe,version=126,alt_page=false,shared_mem=false" \
        --adddevice rapidus \
        --disk disks/gem-sp.atr

In desktop Altirra the two devices go in **System > Configure System >
Devices**; the machine is an 800XL, PAL, BASIC off.

**One warning about emulation, and it is not gem4xe's bug.**  Altirra's
65C816 core has two faults in native mode, which is the only mode gem4xe
runs in: a taken branch does the 6502's page-crossing dummy read (so code
in a high bank can put `$D5xx` on the bus and switch a cartridge's bank),
and `SEI` with an interrupt pending leaves a shadow flag the native-mode
vectors never clear, which re-enters the handler at every opcode fetch
until the stack has walked through all of bank `$00`.  Both are fixed by
the patch in the source tree's `tools/altirra/`, filed upstream as
[pull request #88](https://github.com/ilmenit/AltirraSDL/pull/88).  A
stall with the screen frozen, or a machine that reboots itself, is more
likely to be one of those than anything here — **on real hardware neither
exists**.

## Booting

**Put the disk in and wait.**  You should see the machine start, stop
and start again, and then the desktop.

The restart is not a fault.  A Rapidus **always cold-boots as a 6502** —
Altirra's own device does it in `ColdReset()` ("reset FPGA, force boot
on 6502"), and the card does the same — so gem4xe begins loading on a
CPU it cannot run on.  Rather than refuse, the loader looks for the card
behind that 6502, and when it finds one it sets `COLDST` (so that the
restart is a *cold* one: a DOS does not run its start-up file after a
warm start) and switches the CPU.  That reset is the stop you see.  The
DOS then starts GEM again, this time on a 65C816, and it stays.

On a machine with an **Ultimate 1MB** the question does not arise: its
Rapidus plugin sets the CPU over the M1 signal before the OS runs, so
there is only one boot.

If you have put the files on a disk of your own and start GEM **by
typing its name**, the same thing happens — but the DOS has no start-up
file to run afterwards, so you come back to a prompt on a machine that
is now a 65C816.  Type it once more and it stays.  Giving the disk an
`AUTORUN.SYS` (DOS 2) or a `STARTUP.BAT`/`AUTOEXEC.BAT` holding `GEM`
(SpartaDOS) is what makes that second one unnecessary.

If the machine does **not** switch itself, you will see this instead —
and nothing will have been written:

    gem4xe needs a 65C816: this is a 6502.
    Nothing was changed.  Press a key.

That means no accelerator answered.  On a machine that has one, the
escape hatch is on the disk: press a key to get the DOS back and run
**816** — type `816` at the SpartaDOS prompt, or give `816.COM` to the
DOS 2 disk's binary-load option.  It makes the same three writes by
hand.  **If you have to do that, it is worth reporting**, because the
loader should have.

## On real storage

The card image is for an **APT** interface -- Ultimate 1MB or Incognito,
SIDE/SIDE2/SIDE3, IDE Plus 2.0, MyIDE-II.  They all read the same table,
because the driver that reads it lives in your machine's flash or
cartridge rather than on the card, so there is no per-interface build
and no driver file to copy.

`gem-cf.img` is a whole-card image: writing it **replaces everything on
the card**, and only its first 16 MB are in the partition table.  Give
it a card of its own.

    dd if=disks/gem-cf.img of=/dev/sdX bs=1M conv=fsync    # sdX, not sdX1

**If you already have an APT drive**, do not write the card image over
it.  `gem-sp.atr` is an install disk: it holds the same `\GEM\` and
`\APPS\` the card does, so putting gem4xe on your own drive is a
directory copy --

    COPY D1:>GEM>*.* D2:>GEM>*.*
    COPY D1:>APPS>*.* D2:>APPS>*.*

-- plus an `AUTOEXEC.BAT` holding the two lines the floppy's holds,
`CD >GEM` and `GEM`.

If what you have is a **loader that reads FAT** -- a SIDE3, an AVGCART
-- or an SDrive-MAX, a FujiNet or a real drive, then the floppies are
what you want: copy `gem-sp.atr` onto the card you already have and
load it like anything else.  `docs/media.md` in the source tree has the
whole matrix and the reasoning.

## What is on the disks

{disks}

And loose, for putting on a disk of your own — any DOS gem4xe supports
(SpartaDOS 3.2, SpartaDOS X, DOS 2) will do, and the program keeps
whatever name it is given:

{system}

## What works

The desktop:

{works}

and, with the mouse: an item dragged into another window or onto a
folder is **copied** there, the same drag with **SHIFT** held **moves**
it, and a drag onto the trash deletes it.  Show info is also the
rename — what you leave in its name field is what a file is called
afterwards.  Under all of it: windows that open, scroll, size, full and
close, menus, dialogs, alerts, the file selector, and a program run
from its icon that comes back to the desk where it left it.

Translations are read from `LANG.RSC` and an alphabet from a `.FNT`
beside it, so what the system says is on the disk rather than in the
program.

## What is not there yet

These are in the menu and **disabled** — the desktop puts them up
greyed rather than pretending:

{notyet}

Besides those:

- **one item at a time.**  There is no rubber band and no shift-click,
  so every operation works on the single selected item.
- **the desktop remembers its layout only when you ask it to.**
  *Options -> Save desktop* writes a `DESKTOP.INF` and the desktop reads
  it at start-up; nothing is saved automatically, so a power cycle loses
  whatever was not saved.
- **a folder cannot be renamed.**  `XIO 32` renames a file, and both
  SpartaDOS 3.2 and SpartaDOS X answer "file not found" for a
  directory, so Show info shows a folder's name greyed rather than
  offering something the DOS will refuse.
- **nothing prints yet, though the printer driver is in there.**  The VDI
  has a printer device — 640 x 800 dots, which `v_updwk` writes out as
  PCL 5 or PostScript to wherever `PRINTTO=` names — and `PRINTER=` in
  `GEM4XE.CFG` turns it on.  What is missing is a *Print* item: no
  program opens the workstation yet, so there is nothing to click.  An
  application you write yourself can use it today.
- **no clipboard.**  `scrp_read`/`scrp_write` are not implemented.

## Writing a program for it

`sdk/gem4xe-sdk.tar.gz` is everything needed to build one, and nothing
of gem4xe itself — an application links against none of it.  Unpack it,
read its `README.md`, and `make` turns its commented example into a
`.g4a` you can put on the disk beside the others and double-click.

## If something is wrong

Worth saying, with the report: **the line in `VERSION`** (it is also this
page's subtitle and the name of the folder this came in), which disk,
what the machine is (real or emulated, and with what), what was on the
screen, and what you did.  A screenshot settles most of it.  The refusal
in step 1 is not a fault; anything after step 3 probably is.

`VERSION` holds two numbers and they answer different questions: the
release, which is what **Desk -> About gem4xe** shows and what to say out
loud, and the date and commit, which identify the build exactly.  The
About box's *other* number, the AES version, is 1.40 for every build --
it is the AES gem4xe claims to be, not gem4xe's own.

## Licence

gem4xe is **GPLv2 or later** — `COPYING`, and the source carries the
lineage: EmuTOS, which is the Caldera-GPL'd Digital Research GEM.
`src/gem4xe-src.tar.gz` is the tree these binaries were built from,
exactly as committed, because that is what the licence asks for.

One caveat, stated because it is true rather than because anyone will
ask: the **compiler's own runtime** is linked into these binaries and is
not ours to give -- Calypsi's library says "permission to use", not
permission to redistribute.  The source is free to pass on; these
`.COM` and `.G4A` files are for trying this out, and a proper release
waits on that grant.  `docs/licence.md` in the source has the detail.

**The DOS on each disk image is not gem4xe's**, and is there so that the
disk boots.  Whoever owns it owns it; the images are for trying this
out, not for redistribution.
