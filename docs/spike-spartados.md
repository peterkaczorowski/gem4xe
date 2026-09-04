# Spike — SpartaGEM: GEM on SpartaDOS

What this is: an experiment, not a phase.  Three questions were asked of
the emulator before committing to a DOS change, and all three were
answered in one session.  Nothing was downloaded, no original image was
modified (every fixture is copied before use), and no code changed.

**Postscript, from Phase 13** (`docs/phase13.md`, which did the work):
every question left open below was answered by measurement, and one
caveat is retracted.  CIO's directory read on a SpartaDOS is DOS 2's
17-character line, not DIR's console format; a subdirectory is marked
in it by each DOS its own way (3.2: `DIR` in inverse video in the
extension field; X: a `:` in the flag column, per the SDX Programming
Guide 4.50 §10.2).  The `$4000-$7FFF` caveat in §4 is withdrawn: SDX
addresses a caller's buffer in main memory through its memory-index
mechanism (Guide 3.7, 22.4.3), and Altirra's Rapidus bypasses the SRAM
window for as long as PORTB has a bank in (`rapidus.cpp`,
`UpdateSRAMWindows`), so the pool and the test stage now live there, and
both SpartaDOS gates pass with CIO buffers in it.  The cross-DOS
comparison ran, on an SDFS boot disk `tools/mkspdisk.py` builds; the
numbers are in the phase document.  One thing the spike did not see at
all: SpartaDOS 3.2 keeps 7 KB of itself under the OS ROM, where the
port's ROM copy overwrote it.

## The question

gem4xe's file layer (docs/phase11.md) sits on Atari DOS 2 through CIO,
which is the weakest file system the machine has: 8.3 names, one flat
directory, no timestamps, no random access.  GEM on the ST is VDI + AES
on **GEMDOS**.  The 8-bit's nearest equivalent is SpartaDOS -- a
hierarchical file system with subdirectories, timestamps, NOTE/POINT and,
in SpartaDOS X, a driver subsystem.  Hence the name for the
configuration: **SpartaGEM**.

Before any of that, three facts had to be established.

## 1. Can the rig boot something other than DOS 2, headlessly?  YES

Two disks in the user's own collection boot SpartaDOS with no cartridge
and no U1MB:

    Abbuc SonderMag 16 Side B    SpartaDOS Ver 3.2g 04-Jun-94
    Abbuc SonderMag 15 Side A    SpartaDOS Ver 3.2f 25-Feb-94

Cartridges load too, which matters for SpartaDOS X later:

    AltirraSDL --cartmapper 11 --cart FILE      (11 = SpartaDOS X 64K,
                                                 43 = 128K)

**The order matters.**  `--cart` before `--cartmapper` is accepted, then
ignored: the loader logs "Unknown cartridge mapper" and the machine boots
to Self Test looking, from a screenshot, exactly like a DOS that did not
come up.  An hour went into that.

## 2. Is the SDX in the U1MB flash usable as a cartridge?  NO

`ultfull.rom` (the 512 KB U1MB flash image) does carry SpartaDOS X: the
`SDX` magic and `sta $D5E0` -- the cartridge's own bank register -- are at
offset 0, and SpartaDOS strings run through the first 256 KB.  But carved
out and mounted as a 64 KB SDX cartridge it dies immediately:

    [NETPLAY] emu error: PC=01F9 A=1F X=00 Y=00 S=FB P=32 illegal=1

PC in the stack page, an illegal opcode: U1MB presents that flash through
its own banking, so a straight carve is not a cartridge image.  Running
SDX proper needs a genuine cartridge image.  **SpartaDOS 3.2 needs
nothing that is not already here**, which is why it goes first.

## 3. Does gem4xe's memory map survive?  ALMOST EXACTLY

SpartaDOS 3.2g, asked directly:

    D1:MEM
    Memlo: $17A2  Memhi: $BC1F

Against src/gem4xe.scm:

    DirectPage  $2000-$20FF    inside
    LoRAM       $2100-$35FF    inside
    Near        $3600-$3FFF    inside
    (reserved)  $4000-$7FFF    free -- 3.2 is not a cartridge
    Stage       $8000-$9FFF    inside (MEMAC window)
    AppPool     $A000-$A7FF    inside
    TestStage   $A800-$BFFB    COLLIDES above $BC1F

One collision, about 1 KB at the top of the test stage.  Compare
SpartaDOS X, which as a cartridge takes `$A000-$BFFF` outright and would
move the application pool as well -- another reason 3.2 comes first.

## What the file layer would have to learn

SpartaDOS's own file system (SDFS), listed by its DIR command:

    Volume:    X32G_DOS
    Directory: MAIN
    X32G     DOS  12721  8-24-35  9:43a
    X32GX    DOS  12710  6-04-94  9:01p
    TDLINE   COM   1377  6-06-94 11:20p
       394 FREE SECTORS

Name, extension, **size in bytes**, date, time -- with header lines and a
free-sector trailer.  Nothing like DOS 2's 17-character sector-count line
that `fs_entry()` parses today (docs/phase11.md).  gem4xe would gain file
dates, which a desktop wants.

And SpartaDOS reads **DOS 2 disks too**, in a third format again:

    *<MENU    DEF>008
    * VORWORT TXT 008
    * ----------- 000
    000 FREE SECTORS

So the listing parser must be chosen by what the DOS emits, not by what
the port assumes -- and a SpartaGEM machine still reads ordinary DOS 2
floppies.

## 4. SpartaDOS X 4.50 itself -- it boots, and it costs more

The vendor publishes a variant for emulators, and it works:

    sdx.atari8.info -> Current release -> "Altirra and Atari800 emulators"
    SDX450_maxflash1.car   128 KB, CART type 41 (MaxFlash 128K)
    SDX450_maxflash8.car   1 MB,   CART type 42 (MaxFlash 1024K)

Self-describing `CART` headers, so Altirra picks the mapper itself -- no
`--cartmapper` needed:

        SpartaDOS X 4.50 23-12-2024
      Copyright (C) 2024 by FTe & DLT
    D1:MEM
    Main: $1002,$1002    Ext: $724A,$724A    Use: BANKED

It reads the SpartaDOS 3.2 disk in D1 and lists it in the same SDFS
format, with dates in a different order (`24-08-35` where 3.2 printed
`8-24-35`) -- a reminder that even the same DOS family formats its
listing differently between versions, and that the parser has to be told
which, not left to guess.

**The map, measured from the machine rather than read out of the DOS:**

    MEMLO $1E39   MEMTOP $9C1F   RAMTOP $A000   PORTB $FF

Against gem4xe's:

    DirectPage  $2000-$20FF    inside
    LoRAM       $2100-$35FF    inside
    Near        $3600-$3FFF    inside
    (reserved)  $4000-$7FFF    flat RAM here -- but see below
    Stage       $8000-$9FFF    $9C20 up is above MEMTOP (the OS screen)
    AppPool     $A000-$A7FF    THE CARTRIDGE.  Not RAM at all.
    TestStage   $A800-$BFFB    THE CARTRIDGE.

RAMTOP is `$A000`: under SDX the whole `$A000-$BFFF` window is the
cartridge, so **the application pool and the test stage have to move.**
That is the decision that has been deferred for three phases, and the
evidence now forces it rather than inviting it.

`$4000-$7FFF` reads as flat RAM in this configuration (written and read
back through a PORTB flip), which makes it the obvious new home -- with
one caveat that must not be forgotten: `Use: BANKED` means SDX switches
extended RAM in over that window when it touches its own buffers.  Main
RAM survives the switch, but **a buffer handed to CIO must not live at
`$4000-$7FFF` on an SDX machine**, because SDX may have banked something
else there while it services the call.

*Retracted in Phase 13* -- see the postscript at the top.  The caveat
was an inference; the guide says SDX reaches a caller's buffer in main
RAM from its bank, and the gate proves it.  What is true instead is
narrower: anything the **harness** polls during a call must be outside
the window, because the bridge reads the CPU's view, bank and all.

## The two DOSes, side by side

    SpartaDOS 3.2g (disk)     free $17A2-$BC1F     gem4xe loses ~1 KB
    SpartaDOS X 4.50 (cart)   free $1E39-$9C1F     gem4xe loses $A000-$BFFF

3.2 costs a trim; SDX costs a remap.  Both give the hierarchical file
system.  SDX gives the driver subsystem, timestamps a modern user
expects, and is what people actually run.

## What the file layer costs today (DOS 2, measured)

The same calls the file-layer gate makes, timed in frames from the
target's own counter (`irq_frames`), on the fixture disk:

    open a file                  14 frames    1 CIO round trip
    read 128 bytes                6 frames
    open the directory            7 frames
    read the whole directory     48 frames    13 lines

That is the baseline any other DOS has to be compared against.  It is
also the only leg of the comparison that ran: see below.

## Why the cross-DOS comparison did not run (here -- it did in Phase 13)

Three attempts, three findings, none of them about speed:

1. **The CPU switch must come after the DOS is up.**  Poking Rapidus's
   `$D191` resets the machine, and the DOS boots again on the 65816.  Do
   it while the DOS is still booting and the machine wedges -- which is
   what made SpartaDOS look, for one wrong hour, as though it could not
   run on a 65816.  It can: DOS 2, SpartaDOS 3.2g and SDX 4.50 all come
   back with a prompt after the switch.
2. **`--run` does not survive that reset.**  Altirra injects the program
   at boot; after the switch resets the machine it does not inject again.
   Proven under DOS 2 with the known-good binary: typed at the prompt it
   comes up, injected it never runs.  So gem4xe has to be loaded BY the
   DOS, not around it.
3. **SpartaDOS will not load gem4xe's `M3.COM`.**  It reads the disk
   perfectly -- it lists all twelve files, `M3.COM` among them at 734
   sectors -- but marks that one `<M3      COM>` and does not run it: 94
   KB living above sector 720 on a DOS 2.5 enhanced-density disk.

A build trimmed to fit under SpartaDOS 3.2 (test stage capped at `$BC1F`,
script buffer halved) links and is in the scratchpad; it could not be
tested for want of a way to load it.

**So the comparison needs the port's first real change, not more harness
work**: gem4xe on a SpartaDOS-format disk, which means an SDFS writer in
`tools/atr.py` -- which the gate needs anyway, since it predicts what the
selector lists from the image.

## Not settled here

*Both settled in Phase 13; see the postscript.*

- **Whether CIO's directory read matches the DIR command.**  Everything
  above is the console output of DIR.  What `OPEN` with aux1 = 6 returns
  is what `fs_entry()` actually parses, and reading that needs a program
  on a SpartaDOS disk -- the first task of the phase, not of the spike.
- **How a subdirectory appears in a listing.**  The boot disk has none and
  `MKDIR` is an external command that is not on it.

## Conclusion

SpartaGEM is viable on both, and the order is a choice, not a constraint:

* **SpartaDOS 3.2 first** costs a 1 KB trim and runs on media already
  here.  Good for building the DOS seam cheaply.
* **SDX 4.50** costs relocating the application pool and the test stage
  out of `$A000-$BFFF`, and is the DOS that people actually run, with
  drivers and timestamps.  If the pool has to move anyway -- and the
  desktop will want more than 2 KB of it -- then moving it once, for SDX,
  is the better trade.

Either way the shape is the same: a small `src/sys/dos.h` carrying the
listing rule, the path syntax and capability bits, with DOS 2 kept
working behind it.

## Reproducing it

    tools/a8test/launcher.py, --disk <a copy of the SpartaDOS disk>
    then type at the D1: prompt through the bridge and read SAVMSC back.

The scripts are in the session scratchpad rather than the tree: they are
one-shot probes, and what they found is written down here instead.
