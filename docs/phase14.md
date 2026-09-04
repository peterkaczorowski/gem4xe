# Phase 14 — GEMDOS, and the road to the Desktop

Status: **in progress, in Altirra.**  Milestone 1, the GEMDOS layer:
`make test-m15` PASS on SpartaDOS 3.2g -- 0 problems across the
directory walk, the file calls, memory, the write path, attributes,
Dcreate/Ddelete and Dfree, every answer compared with the image on the
host; DOS 2 and SpartaDOS X are recorded below as they are measured.
Nothing here has run on a Rapidus, a VBXE or a real SpartaDOS machine.

The GEM Desktop is a GEM application: it draws through the VDI, runs
under the AES, and asks **GEMDOS** for everything else -- which files
are in a folder, their sizes and dates, a file's bytes, a folder made,
a program's memory.  gem4xe had the first two managers and no third.
This phase builds it, as the ST's trap #1 on CIO, so that the donor's
desktop code can read unchanged above it.

## The third face of the ABI (`src/sys/gemdos.h`, `src/sys/gemdos.c`)

    COP #$73   VDI     X:C = a VDIPB
    COP #$C8   AES     X:C = an AESPB
    COP #$01   GEMDOS  X:C = a call block:  +0 LONG result  +4 WORD fn  +6.. args

The block is the ST's trap #1 stack frame with the result put in front
of it: the function number, then the arguments in the ST's order and
sizes.  An ST binding's stack picture is the block's picture four bytes
along, which is why the layout is that and not something tidier.  The
handler (`src/sys/abi.s`) treats it exactly as the other two: the block's
address noted, gem4xe's stack and direct page, `gem_entry()` dispatches
on the signature byte.  On the application's side `src/app/gemabi.s`
gained `dos_call` and `src/app/gemlib.c` the ST's osbind names --
`Fsfirst`, `Fopen`, `Fread`, `Dsetpath`, `Malloc` and the rest --
pointers far, so a buffer Malloc gave out can be read into directly.

**What it answers.**  Handles, paths, the DTA, the attribute bits and
the error numbers are the ST's.  A drive is a `D:` unit (`A` = `D1:`);
a path is `A:\DIR\NAME.EXT` or relative to the per-drive current
directory GEMDOS keeps, composed and then given to the DOS seam
(`src/sys/dos.c`) to turn into what CIO opens.  What CIO cannot do says
so: `Fseek`, `Fdatime`, `Tgetdate` and `Tgettime` are `EINVFN` today and
are counted in `gemdos_bad`, rather than answering something plausible.
On a flat DOS a path with a directory in it is `EPTHNF` and `Dcreate` is
`EACCDN`.

**Searches.**  `Fsfirst` opens the directory once and reads as much of it
ahead as the pool allows -- 1 KB -- into a slot in far memory keyed by
the DTA's address; `Fsnext` walks the slot and never touches the DOS
until the cache is dry.  Seven slots, the least recently used taken for
an eighth.  The pattern is matched here (`dos_wildcmp`), the DOS asked
for `*.*`, so the ST's `*.T*` shapes work on a DOS that has no idea of
them.  Measured on 3.2, the six-entry root: `Fsfirst` 3 CIO round trips
and 15 frames, every `Fsnext` 0 trips.

**Raw or lines.**  Where the DOS can hand out the directory itself --
`DOS_CAP_RAWDIR`, SpartaDOS 3.2's `OPEN` with aux1 `$14`, 23-byte SDFS
entries -- the slot holds entries: status byte, size in bytes, name,
date and time, so the DTA carries the true length and a real stamp.
Where it cannot (DOS 2), the slot holds the 17-character lines, the
size is sectors x 125, and the stamp is `DATE0` -- 1 January 1980 --
which is what a desktop shows for a file the DOS knows nothing about.

**Memory.**  `Malloc` is the far heap's bump allocator, wound back when
the application exits (`app_free`), so `Mfree` is a no-op and a leak
lasts one run.  `Malloc(-1)` says how much is left: 15.5 MB on the
1088K emulated Rapidus.  `Fread` into a far buffer is one CIO call --
7 frames for the fixture -- because CIO reads into a bank-$00 bounce
buffer from the pool and the bytes are copied up.

## Two bugs that were not where they looked

**The runner died at `Dsetpath("deep")`, reporting 10798 records.**
10798 is `0x2A2E`, `".*"` in little-endian ASCII.  The stack is 1 KB at
`$3201-$3600` and `vdi_result_count` sits at `$31FD`, directly beneath
it; the first thing a stack overflow overwrites is the word the harness
polls.  The GEMDOS calls held two 128-byte paths and a 64-byte CIO name
in locals, `gd_full` under `gd_fsfirst` under `gemdos_call` under the
runner's op, and a COP from an application is served on gem4xe's stack
*under* the launcher's frames as well.  The path work is now a
`gd_work_t` taken from the pool per call (`pool_alloc`, released on
return, `ENSMEM` if there is no room), and GEMDOS's own frames are a few
dozen bytes.  Lesson recorded: a result count that reads as text is a
stack that reached it.

**`Fwrite` returned 770, then 7586, for a 149-byte write.**  The write
loop was `got = ok ? m : 0` beside a `cio_read(..., &got)` on another
path; cc65816 5.18 at -O2 loads `got` from an unrelated stack slot for
that shape.  Reproduced in six lines as B9 in `tools/ccbug` (`make
check-cc` shows the bug present and the workaround sound), the rule
written down: test the condition, break, then assign plainly.  The
compiler's ninth defect in this port; all of them are in that directory
with a reproduction each.

## What a SpartaDOS actually says (measured, 3.2g)

The GEMDOS error mapping is from CIO statuses, and the statuses had to
be measured rather than read, because the manual's are not always the
DOS's:

    XIO 35/36 (lock/unlock)              1
    XIO 33 on a protected file           $AA "not found" -- ERASE skips it
    rename of a protected file           1, allowed
    open for write of a protected file   1, allowed
    delete of a missing file             $AA
    MKDIR of an existing name            $97
    RMDIR of a non-empty directory       $A7
    RMDIR of a missing directory         $AA

So `Fdelete` of a locked file would have answered `EFILNF`, which a
desktop would show as "file not found" for a file it is looking at.
`gd_xio` re-opens the name for read on `$AA` and answers `EACCDN` if
it is there.  The raw directory open takes a pattern on 3.2
(`D1:>SUB>*.*`); the bare directory name (`D1:>SUB>`) is `$A5`.  The
`FREE SECTORS` trailer of the line listing has no leading spaces and
ends in an EOL, which is why `Dfree` keeps the last *complete* line.

## Two emulator bugs, and what they cost

Everything above runs in Altirra, and this phase found two places where
Altirra's 65C816 is not a 65C816.  Both are in native mode only, both
are in the CPU core (`src/Altirra/h/cpumachine.inl`), and both were
first seen as gem4xe crashes that no amount of reading gem4xe explained.
The fix for both is `tools/altirra/altirra-65c816-native-mode.patch`,
ten lines against upstream `b3061c7` (2026-09-01); the gates run
against a build carrying it with

    ALTIRRASDL=/path/to/patched/AltirraSDL make test

(`tools/a8test/launcher.py` takes the binary from that variable).
The installed `altirrasdl-git` package (r535, built 2026-06-24) has
both bugs, and so did upstream HEAD when they were found.  Both fixes,
with the U1MB switches below, are a pull request against
ilmenit/AltirraSDL (`tools/altirra/README.md` has the link); the CPU
core is shared with Windows Altirra, which is not on GitHub, so the
hunks carry AltirraSDL's fork marker to survive a mainline re-sync.

### 1. A taken branch reads the wrong page -- in bank $00

The SpartaDOS X gate died in the DOS: after gem4xe's first CIO call
into the cartridge, the cartridge answered garbage.  `CART_INFO` over
the bridge showed the MaxFlash bank had changed from 1 to 4 during
gem4xe's *initialisation*, before any CIO call, and nothing in gem4xe
writes to `$D5xx`.  Reading back through Altirra's own bus trace found
the write's twin: a **read** of `$D504`, and the instruction on the bus
at that moment was a `bne` at `$01D59A`, taken, to `$01D604`.

The 6502 spends an extra cycle when a taken branch crosses a page, and
in that cycle it reads the wrong-page address (`$D504` here: the old
page with the new low byte).  Altirra models that cycle as
`kStateJccFalseRead`, and emits it for the 65C816 in native mode too --
where the W65C816S data sheet (instruction table, note 7) says the
penalty exists in emulation mode only.  Worse, the read is
`AT_CPU_READ_BYTE(mAddr)`, a bank-$00 read, not a read in bank K: code
in bank $01 put `$D504` on the *motherboard* bus, and on a MaxFlash
cartridge any access to `$D500-$D50F` selects that bank.  A 21-byte
program -- a native-mode `bne` placed at `$01D5FE`, nothing else --
reproduces it: `CART_INFO` before and after differ.

Until the emulator is fixed the linker map leaves the `$D5` page of
every far bank empty (`src/gem4xe.scm`, THE HOLE): 256 bytes a bank,
and the failure becomes impossible instead of layout-dependent.  With
the patch applied the hole can go; it is kept for now because the
installed emulator is the unpatched one.

### 2. `SEI` with an IRQ pending, in native mode, never stops interrupting

With the first bug patched, `make test-m12` -- the file layer on DOS
II+/D, which had passed for three phases -- began failing on the same
hole build, at the sixth directory operation, every time.  The crash
shape was a stack that had wrapped through all of bank $00, a BRK storm
on top of the wreckage, and `REGS` sampled from the bridge showing the
program counter inside the IRQ stubs.  The false-read fix had not
caused it; it had shifted the timing by a few cycles, so that POKEY's
timer-1 IRQ (the 4 kHz pointer sampler) now arrived on the same cycle
as the `sei` at the top of `cio_call` (`src/sys/cio.s`).

The 6502 lets one interrupt through after `SEI`: an IRQ asserted during
the instruction is taken before the next one, I flag or no.  Altirra
models the shadow with `kIntFlag_IRQSetPending`, set by `kStateSEI`
when I goes 0 -> 1 and consulted by `ProcessInterrupts()` (`cpu.cpp`),
which takes the interrupt if the flag is set even though I is set.  The
emulation-mode vector states (`kStateIRQVecToPC`, `kStateNMIVecToPC`
and their `NMIOrIRQ` variants) clear the flag when the vector is
fetched.  The native-mode states `kState816_NatIRQVecToPC` and
`kState816_NatNMIVecToPC` set PC and K and nothing else.  So in native
mode the flag survives the interrupt, the handler's first opcode fetch
finds the IRQ still asserted (the handler has not run, so nothing has
acknowledged it) and the flag still set, and takes it again; and again
at every fetch, four bytes pushed each time, until S has wrapped
through bank $00 and something in the wreckage clears the flag by
accident.  On silicon `SEI` is atomic and the handler runs.

The proof is a per-state trace from an instrumented build of the
emulator (the instrumentation is not in the patch): after the `sei` at
`$388A` the IRQ sequence starts at `$388B`, then at `$3E40`, the IRQ
stub, then at `$3E40` again, and again, with S dropping by four each
time and the stub's first instruction never executed.  With the two
lines added -- clear the flag in both native vector states, as the
emulation states already do -- the same build passes; with *only* that
change and the false read left as upstream has it, it passes too,
which is how the two were told apart.

gem4xe could hide this (write `IRQEN = 0` before every `sei` that
follows a `cli`, and re-arm after), and does not: it is an emulator
defect that affects every native-mode program that uses interrupts, the
patch is ten lines, and a workaround in `cio.s` would have to be
explained to every reader who wonders why real hardware would need it.
It would not.

### What finding them taught about the rig

- **Instrument the emulator, not the program.**  Two days of poking the
  guest from the bridge -- watches, breakpoints, frame stepping, REGS
  sampled on the wall clock -- produced a dozen different crash
  shapes and no cause.  Twenty lines of `fprintf` in `cpumachine.inl`
  produced the cause in one run.  The AltirraSDL clone builds in about
  a minute incrementally (`./build.sh --release --system-sdl3`, with
  the two `-DALTIRRA_*FFMPEG*=OFF` options).
- **The bridge's watches are chip-bus layers and shift timing.**  A
  `WATCH_SET` on `$D504` made the crash move; it did not make it
  visible.  `BP_SET` does nothing in the accelerated (Rapidus) CPU
  path.  `HISTORY 4096` hangs the bridge.  Frame stepping cannot
  perturb sub-frame phase: every poke through the bridge is
  frame-aligned, so a crash that depends on which cycle an IRQ lands
  on is exactly reproducible whatever the harness does between frames.
- **A bug that moves with a fix elsewhere is usually timing.**  The
  false-read patch "caused" the m12 failure only in the sense that it
  changed which cycle a `sei` executed on.  The memory note from Phase
  6 (a bug that moves with the layout is layout-dependent) has a
  sibling now: a bug that moves with a cycle-count change is
  cycle-dependent, and the thing to trace is the interrupt.

### Status

Under the patched emulator the whole suite passes; under the installed
one `make test-m12` fails, deterministically, by the second bug, and
every other gate passes because none of their `sei`s happen to meet an
IRQ.  That "happen to" is the reason to fix the emulator rather than
the program: which gate fails next is a function of code size.

## The Ultimate 1MB, switched on at last

Phase 0 recorded that U1MB "cannot be enabled in AltirraSDL at all" and
parked it: on a Rapidus machine the U1MB is flash, SDX, PBI and a clock,
not memory, so nothing depended on it.  Phase 14 wanted the clock (for
`Tgetdate`/`Tgettime`) and, more than that, the machine as it is
actually built: SpartaDOS X booting from the U1MB flash, not from a
cartridge image the vendor made for emulators.  So the question was
asked properly.

**The core has emulated it all along.**  `ATUltimate1MBEmulator` is in
Altirra proper and AltirraSDL links it; what the SDL front end lacked was
any way to reach it from a command line or the bridge -- an ImGui
checkbox and `System.ToggleUltimate1MB` were the only paths -- and it
embeds no U1MB recovery BIOS (Windows Altirra builds one from
`src/Kernel/source/Ultimate/main.s`; the SDL resource table has no such
entry), so even switched on, an emulated U1MB had a blank flash and
nothing to boot.  Our clone of upstream (b3061c7, 2026-09-01, newer than
the installed `altirrasdl-git r535`) now carries `--ultimate1mb`,
`--u1mbrom <file>`, the bridge's `CONFIG u1mb`/`u1mbrom`, and `KEYRAW`
-- `tools/altirra/altirra-sdl-u1mb-keyraw.patch`, `tools/altirra/README.md`.

### Driving the BIOS without a keyboard interrupt

The U1MB BIOS 1.25 in the user's own flash image boots into its setup
screen on a fresh NVRAM, and it reads the keyboard the way firmware does:
polling `SKSTAT` bit 2 and `KBCODE` at `$C430`, with the I flag set and
no IRQ.  The bridge's `KEY` verb is the OS's cooked path -- it queues a
keystroke until the keyboard interrupt is enabled and acknowledged -- so
the BIOS never saw one.  `KEYRAW` holds the key in POKEY's matrix
instead (`PushRawKey`/`ReleaseRawKey`; the cursor keys are CONTROL plus
the arrow keys, and the verb splits the modifier bits into the real
SHIFT/CONTROL keys).  Per-frame readouts of `SKSTAT`, `SKCTL`, `IRQEN`
and the BIOS's own variables at `$D7xx` showed the key landing.

The first key tried, ESC, "seemed ignored": the screen was unchanged
after it.  It had been accepted.  ESC is *Abandon changes and exit*, and
with the fresh profile unsaved the BIOS exits, finds nothing saved, and
restarts straight back into setup -- `SKCTL=0`, `IRQEN=0`, loader code
at `$556B`, the same screen.  The way out is page 7, *Save and Exit*:
`RIGHT` x7 (the pages wrap), then `B` for *Save changes and boot*.
From then on the NVRAM lives in `~/.config/altirra/settings.ini`
(`"Ultimate1MB clock"`, with the firmware registration
`Firmware\Available\...` and `Firmware\Default\u1mb`), and the next
launch boots SDX directly: prompt after 100 frames, no setup screen.

### SpartaDOS X 4.49b, from the flash

    SpartaDOS X 4.49b, "Ultimate clock installed"
    PORTB $FF   MEMLO $1D99   MEMTOP $9C1F   RAMTOP $A0
    VBXE at $D640, core $10 (the BIOS shows "VBXE base: 0xD640")
    memory 1088K, cartridge slot empty (CART_INFO present: false)

The same shape as SDX 4.50 from the cartridge (docs/spike-spartados.md
§4): the DOS at `$A000-$BFFF` from the flash instead of a cartridge,
everything the Phase 13 layout assumed still true.

### `$D190-$D193` read `$FF` -- and that is correct

The Rapidus registers read `$00 $40 ..` while the BIOS ran and `$FF`
under SDX, which looked like the U1MB's `$D1xx` handling hiding them.
It is the PBI select register.  `rapidus.cpp` creates the register
layer at `kATMemoryPri_PBI` over page `$D1` and enables it only from
`SelectPBIDevice(true)` -- the OS writing the Rapidus's ID (bit 0) to
`$D1FF`.  The BIOS had it selected; SDX left `$D1FF` clear.  Every gate
in this suite already writes `$D1FF = $01` before `$D191`, from the day
the switch was first made to work, and it works under SDX-from-flash
for the same reason:

    $D1FF as SDX left it   D190-3: ff ff ff ff
    $D1FF = $01            D190-3: 00 40 ff ff      bit 6: a 6502
    $D191 = $00            reset; SDX boots again, mode 65C816
    D1:M3                  the runner up after 4250 frames (91 KB at SIO speed)

One thing to know for a real machine: the U1MB's own PBI ID is
`1 << (2 * n)` for its BIOS setting n = 0..3, so setting 0 is bit 0 --
the Rapidus's.  Altirra's PBI manager resolves the collision by whichever
device registered last; hardware would not.  Pick 1, 2 or 3.

`make test-m14u` and `make test-m15u` run the two SpartaDOS gates on
this configuration (`[u1mb].flash` in `fixtures.toml`, the patched
emulator via `ALTIRRASDL=`).  They are outside `make test` because they
need both.

One trap the U1MB gates set for every other gate: the emulator saves its
profile to `~/.config/altirra/settings.ini` on exit, U1MB state included,
so the first `--u1mbrom` run left U1MB switched on for every plain run
after it -- `make test-m15x` (SDX 4.50 from the cartridge) then booted a
U1MB machine with the flash's SDX and the cartridge together, and the
runner never came up.  The launcher now pins `--noultimate1mb` in its
base arguments (the installed emulator logs the unknown switch and goes
on); `--u1mbrom` later on the line switches it back on for the run that
wants it.  A harness must state every machine option it depends on,
because the emulator remembers the last one.

And a DOS finding: the SDX in this flash is 4.49b (December 2016), and
it answers XIO 35 (protect, GEMDOS `Fattrib` read-only) with OK and does
nothing -- `whatsnew-450.txt`, 4.49f: "XIO 35 worked by accident,
fixed".  `Fdelete` then removes the file it should have refused.  The
gate reports that as a note and skips the locked-file checks on such a
DOS; the port's side of it is the same code that passes on 4.50.
