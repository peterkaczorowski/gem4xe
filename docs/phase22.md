# Phase 22 — what a tester is handed

`make dist` builds the artefact: the bootable disks, the system's files
loose for a disk of somebody else's making, the application kit, and
one page that says how to try it.

    make dist         build/gem4xe-<date>-<commit>/ and .tar.gz
    make test-boot    both disks, one of them switched by typing 816
    make test-host    105 tests, eight of them the distribution's

> **Superseded in part by `phase23.md`.**  Writing the page made it
> obvious that the loader should do this itself, and it now does: a disk
> boots into the desktop with nothing typed.  `816.COM` stays on the
> disks as the escape hatch, and everything below is why it exists and
> what it writes.

## The thing that was actually missing

Not the packaging.  **A machine that can run gem4xe cannot start it.**

The Rapidus always cold-boots as a 6502 — Altirra's own device does it
in `ColdReset()` ("reset FPGA, force boot on 6502") and the card does
the same — so a tester's first screen is the loader's refusal, and the
switch to 65C816 mode has to come from somewhere.  Until now it came
from the test harness, which pokes `$D1FF` and `$D191` over the bridge.
A person has no bridge.

So `tools/mk816.py` writes **`816.COM`**, fourteen bytes at `$0600` —
page 6, which no DOS allocates, so it loads under any of them:

    COLDST ($0244) = 1     the switch resets the CPU, and the OS treats
                           that reset as a WARM start, which is exactly
                           when a DOS does not run its start-up file.
                           Without this the machine comes back to a
                           prompt and sits there.
    $D1FF = $01            the PBI slot the Rapidus answers on: its
                           registers are only mapped while its PBI
                           device is selected (Altirra's rapidus.cpp,
                           SelectPBIDevice -> EnableLayer)
    $D191 = $00            bit 6 clear: the 65C816.  The CPU resets
                           here and the RTS below is never reached.

It is the sequence `tests/emu/product_boot.py` already proved, moved
out of the harness and onto the disk.  Both product disks carry it, and
the gate now **types `816` at the SpartaDOS prompt** instead of poking
the registers — so what the page tells a person to do is what the gate
does.  The DOS 2 disk keeps the poke: that DOS's command processor is a
menu rather than a prompt, and driving it is not what that gate is for;
the file is the same on both disks.

## The page

`tools/dist/README.md` is a template and `tools/mkdist.py` fills it in,
which is the reason this is a tool and not a directory of `cp` rules.
Two of its sections are read **out of the program**:

- **what works** and **what is not there yet** are the desktop's own
  menu, item by item, with the split being exactly the `NOT_YET` tuple
  the desktop disables at start-up.  The page cannot claim a menu item
  the resource greys out, and `tests/host/test_dist.py` asserts that
  every item lands in exactly one of the two lists.
- **what is on each disk** is read back out of the images with
  `tools/atr.py`, so the file list is the disk's and not a memory of
  it.  The gate reads them again, independently, and checks the page
  against them.

The rest is prose that had to be written once: the machine (VBXE FX
1.26, a 65C816 with linear RAM, U1MB optional and what it buys), the
emulator command line the suite itself uses, the three-step boot, the
limits that are not menu items — one item at a time, no `DESKTOP.INF`
yet, a folder that cannot be renamed and why — and where the kit is.

It also says, because it is true and because somebody will redistribute
this: **the DOS on each disk image is not gem4xe's.**  The loose files
are there so that a person can build a disk from a DOS of their own.

## What the gate does

`tests/host/test_dist.py`, eight assertions.  The interesting ones:

- **no placeholder survives into the page.**  A `{stamp}` left unfilled
  is a hole nobody notices until a tester reads it.
- **a disk this tree cannot build is said to be missing.**  The
  fixtures are somebody's own disks and are not always here; the
  artefact must say which ones it does not have rather than quietly
  omit them.  The test adds a disk that does not exist and looks for
  the sentence.
- **every menu item is in exactly one list**, and the disabled set *is*
  `deskrsc.NOT_YET`.

## Debts

- **`make dist` does not run anything.**  It packs what `make` built;
  the disks are proven by `test-boot` and the kit by the host tests,
  and nothing checks that the artefact as a whole boots.  A gate that
  booted the dist's own copy of a disk would be a better one.
- **816.COM does not check that a Rapidus is there.**  It writes the
  registers and, if nothing resets, returns to the DOS having done
  nothing visible.  Identifying the card first is possible — its
  config register reads back — but only against Altirra's model of it,
  and selecting a PBI slot that belongs to somebody else's hardware is
  the kind of guess this project does not make without measuring.
- ~~**The refusal is still the first thing a tester sees.**~~  Paid, in
  `phase23.md`: the loader probes the PBI slots, identifies the card on
  two registers measured at a DOS prompt, and switches it.  A disk boots
  into the desktop with nothing typed, and the refusal is left for the
  machine it was written for -- one with no accelerator at all.
