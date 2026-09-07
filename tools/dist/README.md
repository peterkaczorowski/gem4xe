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
| **VBXE** | required; an **FX core, version 1.26**. gem4xe detects the core by the low nibble of `CORE_REVISION` and writes an overlay priority of `$FF`, because bits 6/7 changed meaning at 1.26 and a priority of `$00` makes the overlay vanish there |
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
- **the desktop does not remember its layout across a power cycle.**
  It keeps window places while it runs a program and gets them back
  afterwards, but nothing is written to a `DESKTOP.INF` yet — which is
  what *Save desktop* above would do.
- **a folder cannot be renamed.**  `XIO 32` renames a file, and both
  SpartaDOS 3.2 and SpartaDOS X answer "file not found" for a
  directory, so Show info shows a folder's name greyed rather than
  offering something the DOS will refuse.
- **no clipboard and no printer.**  `scrp_read`/`scrp_write` are not
  implemented, and there is one VDI driver, for the screen.

## Writing a program for it

`sdk/gem4xe-sdk.tar.gz` is everything needed to build one, and nothing
of gem4xe itself — an application links against none of it.  Unpack it,
read its `README.md`, and `make` turns its commented example into a
`.g4a` you can put on the disk beside the others and double-click.

## If something is wrong

Worth saying, with the report: which disk, what the machine is (real or
emulated, and with what), what was on the screen, and what you did.  A
screenshot settles most of it.  The refusal in step 1 is not a fault;
anything after step 3 probably is.

## Licence

gem4xe is **GPLv2 or later** — `COPYING`, and the source carries the
lineage: EmuTOS, which is the Caldera-GPL'd Digital Research GEM.

**The DOS on each disk image is not gem4xe's**, and is there so that the
disk boots.  Whoever owns it owns it; the images are for trying this
out, not for redistribution.
