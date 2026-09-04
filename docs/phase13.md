# Phase 13 — SpartaGEM: gem4xe on SpartaDOS

Status: **complete, in Altirra.** `make test-m14` PASS on SpartaDOS 3.2g
(the user's Abbuc disk) and `make test-m14x` PASS on SpartaDOS X 4.50
(the vendor's cartridge for emulators) -- the DOS identified, the file
layer read through subdirectories, and the file selector walked into a
folder and back out, six cases pixel- and string-exact against the model
on each. `make test` runs the DOS 2 gates as before, then both. Nothing
here has run on a Rapidus, a VBXE or a real SpartaDOS machine.

GEM on the ST is VDI + AES on GEMDOS. gem4xe's file layer sat on Atari
DOS 2, the weakest file system the machine has, and the spike
(`docs/spike-spartados.md`) settled that SpartaDOS was viable on both of
its forms. This phase is the port's first change for a DOS: a seam that
says which one booted, a memory map that gives the cartridge its room,
a disk the DOS will load the program from, and the selector taught what
a folder looks like.

## The seam (`src/sys/dos.h`, `src/sys/dos.c`)

The DOSes differ, as far as the AES can see, in exactly two things: the
shape of a path and what a directory listing says. `dos_init()` decides
both once, at start-up, into one struct the selector and the path mapping
read afterwards -- neither asks the DOS again.

**Identification** is two bytes and a comparison, none of them a
version string. A SpartaDOS keeps an `'S'` at `$0700`, the first byte of
its resident part, and `DOSVEC` points at its command table whose fourth
byte is a `JMP` -- the ZCRNAME entry, "common to all SpartaDOS and DOS XL
versions" (SpartaDOS X Programming Guide 4.50, chapter 5). DOS 2's
`$0700` is the boot record's flag byte, zero. Between the two SpartaDOSes,
`MEMTOP` tells: below `$A000` means the cartridge is in. What answers
neither is taken for DOS 2, the DOS gem4xe grew up on -- the guess that
costs subdirectories, not correctness. Measured on the target:

    DOS 2            kind 0                              MEMTOP $BC1F
    SpartaDOS 3.2g   kind 1  caps DIRS      MEMLO $17A2  MEMTOP $BC1F
    SpartaDOS X 4.50 kind 2  caps DIRS      MEMLO $1E39  MEMTOP $9C1F  RAMTOP $A000

**The path.** `dos_cioname()` maps `A:\DIR\SUB\NAME.EXT` to `D1:NAME.EXT`
on DOS 2 (the directory part dropped, as before) and to
`D1:>DIR>SUB>NAME.EXT` on a SpartaDOS, uppercased, cut at
`CIO_NAME_MAX` -- now 63, up from 31: nine more per directory. `shel.c`'s
`sh_cioname` is the seam's rule and nothing else; an application never
sees a `>`.

**The listing** is the surprise the spike could not settle. Through CIO
(`OPEN` with aux1 = 6) both SpartaDOSes print **DOS 2's 17-character
line** -- lock mark, space, name in 8, extension in 3, space, sectors in
3 -- for DOS 2's sake. The DIR command's byte counts and dates are the
console's, not CIO's. Each adds one thing for what DOS 2 has not got, a
subdirectory, and each adds it differently:

    3.2g     "  SUB     DIR 002"    the extension field is DIR in inverse
                                    video ($C4 $C9 $D2); the sectors are
                                    the directory's own
    X 4.50   " :SUB         001"    a ':' in the flag column, the
                                    directory's own extension where a
                                    file's would be

The first was measured; the second is what the Programming Guide says
of the short format's mode `$08` ("a directory will get marked with a
':' character printed before the name", 10.2) and was then measured.
`dos_folder_line()` knows both, looks for both on either DOS, and
neither can occur in a DOS 2 line. The trailer is `173 FREE SECTORS` on
both. Sector counts differ too -- SDX counts `ceil(bytes / 128)`, 3.2
counts the map sectors in -- which is why the gate predicts the listing
from the image and compares names and flags, never sectors.

## The map moved (`src/gem4xe.scm`)

SpartaDOS X is a cartridge and `$A000-$BFFF` is it. The application pool
and the test stage lived there, and for twelve phases `$4000-$7FFF` was
reserved against the U1MB's PORTB banking. Both decisions are reversed:

    was                                  is
    $2100-$35FF  LoRAM                   $2100-$367F
    $3600-$3FFF  Near                    $3680-$3FFD, the reset word at $3FFE
    $4000-$7FFF  RESERVED                $4000-$5FFF  the application pool, 8 KB
                                         $6000-$7FFF  the test stage
    $8000-$9FFF  Stage (MEMAC A)         $8000-$9BFF  -- SDX's screen is at $9C20
    $A000-$A7FF  the application pool    NOT OURS: the cartridge
    $A800-$BFFB  the test stage          NOT OURS

The banked window is main RAM whenever nobody is in the middle of
banking, and the only thing that banks on a gem4xe machine is a DOS
servicing its own call: SDX with `USE BANKED` keeps its buffers and
drivers in a system bank here and switches it in while it works. It
puts it back before returning, and it addresses a caller's buffer in main
memory through its memory-index mechanism (main RAM is index `$00`; the
transfer loops live below `$4000` for exactly this reason -- Guide 3.7
and 22.4.3), so a CIO buffer in the pool is served correctly. On a
Rapidus, Altirra's emulation bypasses the accelerator's window 1 for as
long as PORTB has a bank in (`rapidus.cpp`, `UpdateSRAMWindows`:
`window1Enabled = !slow && !xramEnabled`, and the shadow write off with
it), so the SRAM copy of the pool is neither read nor written by the
DOS's bank work, and **the pool is fast** -- which `$A000`, sharing its
16 KB window with MEMAC, never was. What the window may not hold is
anything an interrupt handler needs, since a handler can run while a bank
is in; gem4xe's are all in `$2000-$3FFF`.

The pool is four times what one application is linked to fit
(`src/app/gemapp.scm`); the selector's tree and buffers are taken from
it too. The stage lost its last kilobyte to SDX's display list and
screen, so `farload`'s chunk is 27 pages rather than 31 (`src/farload.s`,
sized to the memory; a chunk too big fails the link).

## The disk (`tools/atr.py`, `tools/mkspdisk.py`)

SpartaDOS 3.2 reads a DOS 2 disk but would not load `M3.COM` from one:
94 KB living above sector 720 on an enhanced-density disk, marked
`<M3      COM>` and refused. So `tools/atr.py` gained an SDFS
implementation -- `Sdfs.format`, `mkdir`, `add_file`, `boot_from`, and
the reader the gate predicts listings with (`entries`, `read`,
`free_count`) -- and `tools/mkspdisk.py` builds `build/m14-boot.atr`: a
fresh 1040 x 128 volume carrying the boot sectors and DOS file copied off
the fixture (never the fixture itself), the program, the DOS 2 gate's
files, and a small tree for the selector:

    MAIN      X32G.DOS  M3.COM  TEST.TXT  TEST.RSC  OUT.TXT  SUB>
    SUB       ONE.TXT  TWO.DAT  DEEP>
    SUB>DEEP  THREE.TXT                              173 sectors free

Under SDX the cartridge boots instead and the disk is just `D1:`. The
disk is the one fixture both gates share; the DOS 2 gates' AUTORUN path
is untouched.

**Loading is slow, and it is the same on both.** `M3.COM` is ~94 KB in
`farload`'s chunks, and a SpartaDOS takes about 2,900 frames -- a minute
-- to load and run it after `RETURN`, on the 65816. The gate types `M3`
at the prompt, as a user would; `--run` does not survive the reset the
CPU switch causes (the spike), so the DOS has to do the loading.

## The RAM under the ROM is not ours (`src/sys/irq.c`, `src/sys/cio.s`)

The first run on 3.2 identified the DOS, mapped every path, and then
**every `OPEN` answered `$81`** -- not a CIO error code at all. The DOS
was reading its own entry point from somewhere gem4xe had written.

SpartaDOS 3.2's `X` builds load 7 KB of themselves under the OS ROM --
`$CC00-$CFF2` and `$E6ED-$FFF8` -- and switch the ROM out to reach it on
every call. Phase 9's native-mode interrupts copy the OS ROM into the
RAM under it, byte for byte, so the vectors at `$FFEA`/`$FFEE` can be
written. On a plain machine there is one RAM under the ROM; the copy
went into it, over the DOS.

On a Rapidus there are two: the accelerator's SRAM, which is what a fast
window reads, and the motherboard's, which the DOS reads with the ROM
out. The copy is now written with **write-through off** -- a write to a
fast window then lands in the SRAM alone (`UpdateSRAMWindows` again),
and the motherboard's RAM under the ROM keeps whatever the DOS put there,
untouched and invisible until a CIO call. `cio_call` does the swap for
the call: the ROM in first (with the ROM in, reads come from the
motherboard whatever the MCR says, so the vectors are the OS's at every
step), then window 3 slow, `cli`, `jsr CIOV`; and back in the reverse
order. `irq_cio_swap` says what to swap, set by `irq_install` from what it
found. **Without a Rapidus, SpartaDOS 3.2 does not survive gem4xe**, and
that is written on the function rather than hoped away: SpartaGEM needs
the accelerator's SRAM. SDX keeps nothing under the ROM and does not
care either way.

## The bridge reads what the CPU sees (`src/m3_vdi.c`)

SDX's first run was blocked on its first selector case with
`MEMDUMP $10000 65536 -> bad address`: the harness had read
`vdi_result_count` as `$FFFF`. The count lived in the test stage at
`$6000`, and the harness polls it *while* an op runs -- during the
directory read, that is, while SDX has its system bank switched in over
`$4000-$7FFF`. Altirra's bridge `PEEK`/`MEMDUMP` go through
`sim.DebugReadByte`, the CPU's view, PORTB banking included
(`bridge_commands_write.cpp`, `bridge_commands_state.cpp`), so the host
read SDX's bank, which is not where the count is.

The count is the one word polled during a call; it is ordinary data now,
at `$31E2`, and the gate asserts it is outside the window. The buffers
in the stage are touched only between calls and stay where they are.
Every gate that polls the count follows the symbol, and every one of them
is green on DOS 2 after the move.

## The listing on both sides (`src/aes/fsel.c`, `tools/aesref.py`)

`fs_entry()` returns the entry's flag now -- the donor's `' '` for a file
and `0x07` for a folder -- rather than TRUE, and `fs_add` writes it into
the slot the donor always had for it. A folder passes the wildcard
unfiltered, as the donor's does. Beyond that the selector is **the
donor's own code, which never knew the DOS underneath had changed**: a
folder clicked goes into the path, `FCLSBOX` comes back out of it, and
the path in the dialog reads `A:\SUB\DEEP\*.*` with backslashes an
application expects.

The model's `fs_entry` learned the same two marks from the same header
comment, and `aesref.run` takes `dirs=` keyed by CIO name with folders
flagged `FS_FOLDER`. The gate builds those dirs from the image with
`Sdfs.entries`, so the prediction is the disk's, not the script's.

## What it costs, measured

Frames per call from the target's own counter (`irq_frames`), the
calls the spike timed on DOS 2, all through the selector's own path:

    call                    DOS 2   SpartaDOS 3.2g   SpartaDOS X 4.50
    open a root file          14          18               22
    open in SUB                            22               34
    open in SUB>DEEP                       22               42
    read 128 bytes             6          10 / 6           10 / 6
    open the root listing      7          14               22
    read the root listing     48 (13)     52 (7)           52 (7)
    read SUB's listing                    30 (4)           30 (4)
    read DEEP's listing                   18 (2)           18 (2)

Both SpartaDOSes cost more per call than DOS 2, SDX the most. An open
walks the path: SDX pays ~10 frames a directory, 3.2 four for the first
and nothing for the second -- and a root open on 3.2 is 18 against DOS
2's 14 anyway. A listing costs about 7 frames a record on either
SpartaDOS against DOS 2's 4: the line is DOS 2's, but each one takes
twice as long to make. In the selector that is a second and a half for
the root, at 50 frames a second, where DOS 2 took one. All of it is
emulated: these are Altirra's disk timings with its default access
delays, not a drive's.

## Bugs, and what caught them

**The DOS overwritten under the ROM** -- above. Caught by the seam's
first `OPEN`; found by reading 3.2's load map and asking what gem4xe
had written there.

**The count in the DOS's bank** -- above. Caught by a `MEMDUMP` of
`$10000` words.

**The model descended twice.** On the target a click on a folder
(`M, B(1), F(14), B(0)`) lists the folder once; the model, given the
same script, went into `SUB` and on into `DEEP`, and the shots differed
by a thousand pixels. The model was right about GEM: `ev_wait` takes a
still-held button for another turn when the screen has changed under
it, which is what makes a held scroll arrow repeat. The target's listing
is a 30-50 frame CIO call, so the button was up by its next wait; the
model's listing is instantaneous, so the button was still down. Any
click that changes the listing is now a `TAP` -- press and release
before the double-click delay delivers the click -- which leaves nothing
held on either side. The rule is the model's, and the CLICK the DOS 2
gate uses on the scroll arrows is right for the reason the TAP is right
for folders.

**Two settle margins in the DOS 2 gate that were measurements.** The
pool's move to a fast window changed `make test-m12`'s timing: a
screenshot taken the frame `ST_DONE` came up caught a string half
drawn, and the release after a drag was shot before the page had
scrolled. Two frames after done and eight after the release are bounds
now, with the reason written beside each. And the refusal case -- the
selector saying no for want of pool -- stopped refusing, because 8 KB of
pool holds the test resource with room for the tree; the runner takes the
rest on the application's behalf through a new op (`ALLOC`, 3011), and
the case asserts what it leaves is less than the tree.

**Two of the rig's own.** `--cart` before `--cartmapper` is accepted and
ignored, and a relative `--disk` path resolves in the launcher's run
directory, not the repo's; either boots the machine to Self Test,
looking from a screenshot exactly like a DOS that did not come up. The
gate's disk path is absolute, with the reason beside it.

## Debts

- **A SpartaDOS 3.2 machine without a Rapidus loses its DOS** to the ROM
  copy. The port needs a second RAM under the ROM, and there is no
  fixing that from the port's side short of not copying the ROM there.
- **Timestamps.** SDFS has them; CIO's listing does not carry them, and
  a desktop will want them. That is a `SpartaDOS`-specific read of the
  directory file, not the DOS 2 line.
- **MyDOS** would be taken for DOS 2 and lose its subdirectories. It has
  neither mark and its own path syntax.
- **A DOS 2 disk under a SpartaDOS** is listed in yet another format
  (the spike saw `*<MENU    DEF>008`) and is not tested.
- **One unexplained crash.** Once, in a whole-suite run, the DOS 2
  selector's case [8] faulted (fault 52) and did not again in the runs
  since. It is not reproduced and not understood; the suite logs are
  watched for it.
- Everything is Altirra: its disk timings, its Rapidus, its bridge.

## Lessons

- **A DOS is a program with a memory map.** The port had a map of what
  was *its*; it had never had one of what was the DOS's. SpartaDOS 3.2's
  7 KB under the ROM was in its load map all along.
- **The harness is a CPU too.** A bridge that reads the CPU's view reads
  the bank the CPU sees; anything polled during a call has to live where
  no call banks. That is a rule about where a variable is linked, not
  about the harness.
- **The model being faster than the target is a difference the gate
  sees.** A held button is an input on both sides, and how long it is
  held relative to the work under it is a timing the script has to own.
  The fix was to the script, not to either side of the comparison.
- **Read the DOS's own guide before the emulator's.** Both marks, the
  memory-index rule that makes the pool safe, and the ZCRNAME entry the
  identification rests on are in the SDX Programming Guide; the spike's
  caveat about `$4000-$7FFF` was an inference the guide answers.
- **The spike's tentative results were all retracted or confirmed by
  measurement**, which is what a spike is for: the listing is DOS 2's
  line (not DIR's), a folder has two marks (not one format), and the
  window is safe (not a caveat).

## Next

The desktop, on a file system that has folders now: `wind_*`, the
selector, the alerts and a hierarchical disk exist; the desktop is the
program that uses them together. Timestamps for it are the first
SpartaDOS-only read.
