# gem4xe — GEM for the Atari 8-bit

A port of GEM — the VDI graphics layer, then the AES — to an Atari XL/XE fitted
with **VBXE** (video), a **65C816 accelerator** with linear RAM (**Rapidus**, and
in principle Antonia), and **Ultimate 1MB**.

The target surface is **640 × 240, 16 colours** — VBXE's HR overlay, 4bpp chunky.
That is a better GEM surface than the Atari ST's medium resolution.

**A 65C816 with linear RAM is required.** The 6502 and ANTIC/GTIA paths are
deferred, not cancelled: the driver seam that would host an ANTIC back end
exists and is unused.

## Why it is shaped the way it is

VBXE sits on the 1.79 MHz chip bus no matter how fast the CPU runs. Measured on
target, a full-screen fill costs 0.51 of a frame through the blitter and a
full-screen copy 0.88; the same work through the MEMAC window is roughly twenty
times slower. So **the VDI emits blitter control blocks, it does not plot
pixels**, and the window manager will use dirty rectangles rather than
full-screen repaints.

## State

| Gate | | |
|---|---|---|
| `make test-host` | 41/41 | pointer device layer — the ST, Amiga and CX80 models walked through the target's C in the compiler's simulator — and .xex far-code staging |
| `make test-emu` | 5/5 | VBXE FX 1.26 / Rapidus / MEMAC A / CPU switch |
| `make test-m1` | 5/5 | Calypsi C on the 65C816 |
| `make test-m2` | PASS | 640×240×4bpp HR overlay, 153,600/153,600 pixels |
| `make test-m3` | 68/68 | VDI conformance — pixels *and* return values |
| `make test-m4` | 13/13 | AES object library: draw, find, change, edit, centre, icons |
| `make test-m5` | PASS | linear RAM probed: banks `$02-$EF`, 14.9 MB |
| `make test-m6` | PASS | far code copied up and running from the banks the linker chose — bank `$01`, and `$01`+`$02` in a forced-spill link; bank `$00` on the fast bus |
| `make test-m7` | 10/10 | `evnt_*`, `form_do`, `form_dial`, `graf_watchbox` under host-driven input |
| `make test-m8` | 12/12 | the window manager and the control manager: rectangle lists, moves, gadgets, `WM_*` |
| `make test-m9` | 4/4 | menus: the bar, drop-downs, `MN_SELECTED`, screenshotted inside the wait |
| `make test-m10` | 27/27 | native-mode interrupts: the OS shadowed into SRAM byte for byte, the VBI, a ~4 kHz timer, the keyboard, a trak-ball counted under interrupt, and a clean return to DOS |
| `make test-m11` | PASS | the application ABI: a separately linked program loaded, relocated and run, calling the VDI and the AES through `COP` — its records, the loader's, and the screen against the reference |
| `make test-m12` | PASS | the file layer: CIO through the OS in emulation mode, `rsrc_load`/`rsrc_obfix`, `shel_*`, and the file selector driven over two disks — its listings, its scrolling and its returned strings against the reference, pixel for pixel |
| `make test-m13` | 19/19 | alerts, icons and the pointer: `form_alert` parsed, laid out and drawn against the reference; every mouse form `graf_mouse` owns, and the caller's own |
| `make test-m14` | PASS | SpartaGEM on SpartaDOS 3.2: the DOS identified behind CIO, paths mapped into its `>` syntax, files read through subdirectories, and the file selector walked into a folder and back out -- listings and strings against the reference, pixel for pixel |
| `make test-m14x` | PASS | the same on SpartaDOS X 4.50, the cartridge -- with the application pool and the test stage moved out of its way, into the banked window it services calls from |
| `make test-m15` | PASS | GEMDOS: the ST's trap #1 as gem4xe's third `COP` face, answered from CIO and the DOS seam -- directory searches, paths, files, far memory, attributes and errors, every answer against the disk image; `test-m15x` on SpartaDOS X, `test-m15d` on DOS 2 |
| `make test-m16` | PASS | the shell loop: DESKTOP.G4A loaded and run, a program run from it and the desktop back, a missing program's alert, shutdown -- the screen against the reference at each stop, the pool and the far heap back where they were, the stack's low-water mark (1199 of 2048 bytes); the VDI's virtual workstations (one per program) under it. GEM.COM, the product, does the same from the DOS prompt and returns to it |
| `make test-m17` | PASS | the GEM Desktop: DESKTOP.G4A's menu bar, drive icons and trash, an icon clicked, Desk -> About and its dialog, a drive opened into a folder window, a folder opened in it and closed back out, the fuller, the arrows, the closer, File -> Quit -- driven at the mouse and checked against `tools/deskref.py`, the desktop itself transcribed against the AES model, its directory listings answered from the disk image: thirteen screens, the desktop's 1984 bytes of globals byte for byte at nine of them, 252 calls on both sides, the pool and far heap back, the runner's and the desktop's stack low-water marks (1223 of 2048, 292 of 640) |
| `make test-m18` | PASS | a program run from the desktop: drive A opened, the window full, M11.G4A double-clicked -- the desktop puts its window's place in the shell buffer as DESKTOP.INF text and exits, the shell runs the program, the desktop comes back and opens the window where it was, File -> Quit -- the desktop transcribed twice against one AES model with the program's calls counted between: six screens, `G` at four waits, 275 calls over the three programs, the pool and far heap back, each run's stack low-water mark (425 and 268 of 640) |
| `make test-m19` | PASS | the desktop's first writes to a disk: File -> New folder, the name typed into its dialog, `Dcreate`, the folder in the listing; the same name again, refused, and the alert -- text and all -- out of DESKTOP.RSC's free strings; the SUB folder selected and File -> Delete, counted first (three files, two folders, the walk a DTA deep per level), confirmed in a dialog whose counts tick down, and the tree gone -- eight screens, `G` at five waits, 289 calls on both sides, and the disk image itself read back afterwards |
| `make test-boot` | PASS | both product disks booting into the desktop with nothing typed: `build/gem-sp.atr` (SpartaDOS, `STARTUP.BAT` for 3.2 and `AUTOEXEC.BAT` for X) and `build/gem-boot.atr` (a double-density DOS 2, the system named `AUTORUN.SYS`, `DUP.SYS` still on it and 42 KB free); the 6502 boot runs GEM by itself and ends in the loader's refusal; `COLDST` and the Rapidus switch bring the machine up cold as a 65C816, the DOS starts GEM again, and the far image is spot-checked against the linker's output before the desk is compared pixel for pixel with the desktop model at its first wait |
| `make test-cf` | PASS | the product **CF card** booting into the desktop: `build/gem-cf.img`, an APT table and two SDFS partitions, on a SIDE 2's IDE bus, with SpartaDOS X *and* the PBI BIOS that mounts those partitions coming from a real Ultimate 1MB flash image. The gate walks the U1MB BIOS setup itself (PBI BIOS on, hard disk on, an ID that is not the Rapidus's) from a fresh profile of its own, keeps the SIDE's SDX bank unmapped so the PBI BIOS will touch the disk, and then runs the same boot as `test-boot` -- refusal, switch, desk against the model. Needs the U1MB fixture and the patched emulator, so not in `make test` |
| `make test-m14u` `test-m15u` | PASS | the same two on SpartaDOS X 4.49b booted from a real Ultimate 1MB flash image, U1MB switched on -- needs the patched emulator in `tools/altirra/`, so not in `make test` |
| `make check-cc` | PASS | the ten compiler bugs worked around, in the vendor's simulator |
| `make movie` | PASS | a session with the AES itself, filmed frame by frame and checked as a gate: `build/movie/gem4xe.mp4` |
| `make bench` | — | GEMBench's tests on this machine, in milliseconds, not a gate (`docs/bench.md`) |

`make test` runs them all. Per-phase notes, including the bugs and what caught
them, are in `docs/`.

The 37 VDI opcodes the AES and the GEM Desktop actually use are complete. The
AES object library draws, hit-tests and edits; `form_do` runs a dialog under
keyboard and pointer input; the window manager keeps dirty-rectangle lists
and blits a window across the screen (x snapped to even: the blitter has no
shifter); the control manager turns a press on a frame into `WM_*` messages
and holds the mouse until the button is up, as the ROM does; menus drop, are
saved and restored through a VRAM form, and report `MN_SELECTED` — all
compared call for call and pixel for pixel against the host model
(`docs/phase8.md`). `make movie` runs all of that as one session on the
emulated machine — About from the Desk menu, a window opened by
double-click, dragged, sized, covered by the dialog, fulled and closed —
screenshotting every frame, with every returned word and every shot
checked against the model. Making it pass found that a pixel plotted
through the MEMAC window costs 60–80 µs, so the pointer and `vrt_cpyfm`
now go through the blitter like text does (`docs/phase8b.md`); reading
the compiler's listing then cut the strip builder and the line stepper
to a third of their instructions, and a blitter mode written off in
Phase 2b turned out to do the whole icon in one blit — 60 ms an icon in
Phase 8, 2 ms now (`docs/phase8c.md`). `make bench` then put GEMBench's
headings on the machine — absolute milliseconds, since GEMBench's source
is private and there is no ST here — and its first profile named the
control-block builder and the VRAM upload: the dialog went from 78 to
51 ms and a 40-character line from 27 to 15 ms, and a seventh compiler
defect turned up under the rewrite (`docs/bench.md`). Next is
`form_alert` and the desktop, with the multiply-per-glyph and the upload
loop still on the benchmark's list.

Phase 7 also found that the Rapidus resets with all of bank `$00` on the
1.79 MHz bus, and that gem4xe had run its data, stack and direct page there
for six phases without a gate noticing (`docs/phase7.md`, Step 4).
`src/sys/rapidus.c` derives the speed map from the linker's placement and
the MEMAC window rather than restating either, and `make test-m6` reads the
registers back.

Code lives **above bank `$00`**, in the accelerator's first megabyte. A `.xex`
segment header is two 16-bit addresses, so a DOS loader cannot place anything
above `$FFFF`; the far image therefore travels as chunks aimed at a staging
buffer and DOS copies it up through `INITAD` as it reads the file. That took
the code ceiling from ~28 KB to a bank at a time, and freed the `$4000-$7FFF`
scaffold the test runner had been borrowing from U1MB.

Bank `$01` was 90% full by the time the benchmark landed, so the far code is
now linked into **one linker memory per bank, `$01` through `$0F`**, filled in
order: the image spills into the next bank only when the current one cannot
hold the next whole function, and no function ever straddles a bank boundary
(the 65816 program counter wraps within its bank, and a single memory spanning
banks let the linker place a function across the seam — tried, and it did).
The far heap starts above the highest address the loader actually wrote,
`_fl_top`, recorded chunk by chunk, since the linker has no operator for the
end of a section that lives in several memories. Because the real build still
fits in bank `$01`, `make test-m6` also links the same objects with bank `$01`
cut to 16 KB and boots that: the code runs from bank `$02`, the heap starts at
`$03`, and the mechanism is proved today rather than on the day the code
outgrows the bank (`docs/phase6.md`, the follow-up).

Running this on a machine without a 65C816 would corrupt memory rather than
fail — the long store the copier needs is an unstable undocumented opcode on an
NMOS 6502 — so the loader identifies the CPU and probes for linear RAM before
its first store, and prints a line and returns to DOS if either is missing.

**Interrupts run in native mode** (`docs/phase9.md`). The 65C816's native
vectors sit at `$FFE4-$FFEF`, inside the OS ROM, which the Atari OS never
fills — so from Phase 0 to Phase 8 gem4xe ran with NMI and IRQ off and
polled everything. `src/sys/irq.c` copies the OS ROM into the Rapidus's
SRAM under it, page by page through write-through, patches the six vectors
to point at bank-`$00` stubs, and switches the window in; the handlers
count frames, run a POKEY timer at ~4 kHz that samples the joystick port
and decodes a quadrature or trak-ball device into two counters, and put
keys into a ring. The pointer layer consumes the counters, so an ST mouse,
an Amiga mouse and a CX80 trak-ball now work as well as the tablet did —
in Altirra; no real hardware has been near this. On exit the ROM is
switched back, `$0000-$3FFF` is written back to the motherboard, and DOS's
own keyboard IRQ echoes the next key typed at its prompt, which is what
the gate checks. Getting the gate green also found that the emulator was
reading a phantom joystick: the host keyboard's "System Control" HID
interface, which SDL enumerates as a joystick with one out-of-range axis,
held PORTA's left line low through AltirraSDL's input maps. The rig now
keeps SDL's joystick subsystem off every host input device it can find in
sysfs.

**Applications call in through `COP`** (`docs/phase10.md`). `COP #$73` is
a VDI call and `COP #$C8` an AES call, the parameter block's address in
X:C — the ST's `trap #2` on a 65C816. The handler is the `saveds` entry
point of the plan: it takes gem4xe's direct page, data bank and stack for
the duration and gives the caller's back, and the shim behind it keeps
DRI's copy-in/copy-out discipline, so an application's arrays can be
anywhere in the 16 MB. A program is linked on its own rules
(`src/app/gemapp.scm`) and packed as a `.g4a` by `tools/mkg4a.py`, which
derives the fixups Calypsi's linker does not emit by linking the same
objects three times and diffing; the loader puts the near part in a
bank-`$00` pool and the code in a far bank. The gate application makes
eighteen VDI and AES calls and the harness checks what each returned,
from the application's own memory, against the reference.

**SpartaGEM: gem4xe runs on SpartaDOS** (`docs/phase13.md`), 3.2 from
disk and X 4.50 from its cartridge, as well as on DOS 2 -- the same
binary, which asks the DOS what it is at start-up (`src/sys/dos.h`) and
reads its answer for the shape of a path and the mark on a folder in a
listing; a GEM application sees `A:\DIR\NAME.EXT` on all three. Making
room for the cartridge moved the application pool and the test stage
into `$4000-$7FFF`, the banked window the port had kept out of for
twelve phases: a DOS banks there only inside its own call, and puts it
back. Two things bit on the way. SpartaDOS 3.2 keeps 7 KB of itself
under the OS ROM, where the interrupt layer's ROM copy had overwritten
it, so the copy now goes to the Rapidus's SRAM alone -- gem4xe on
SpartaDOS 3.2 needs the accelerator. And the bridge reads what the CPU
sees, SpartaDOS X's bank included, so the one word the harness polls
during a call moved out of the window. Every call costs more than on
DOS 2 (a directory read about twice as long a record), measured in
`docs/phase13.md`, and everything is Altirra: no real SpartaDOS machine
has been near this.

**GEMDOS, and two emulator bugs** (`docs/phase14.md`). The ST's trap #1
is gem4xe's third `COP` face: a call block laid out as the ST's stack
frame with the result in front, so an ST binding's picture of the
arguments is the block's picture four bytes along, and the donor's
desktop code can read unchanged above it. Building it found two places
where Altirra's 65C816 is not a 65C816 in native mode -- a taken branch
that does the 6502's page-crossing dummy read, in bank `$00`, which
puts `$D5xx` on the cartridge bus from code running at `$xxD5xx`; and
`SEI` with an IRQ pending, whose "one more interrupt" shadow the native
vector states never clear, an IRQ storm that wraps the stack. Both are
fixed in `tools/altirra/` and sent upstream; until then the linker map
leaves the `$D5` page of every far bank empty. The Ultimate 1MB, which
the SDL emulator could not switch on headlessly, now can be: the same
patches add the switches, and the two `*u` gates boot SpartaDOS X from
the machine's own flash.

**The desktop** (`docs/phase14.md`, milestones 3 to 7). The AES's shell
loop runs `DESKTOP.G4A`, then whatever it asks for, then the desktop
again; `GEM.COM` is that loop from the DOS prompt, back to it at
shutdown. The desktop is the donor's deskmain.c, deskobj.c and
deskwin.c cut to what shows so far -- the bar, an icon for each drive
GEMDOS reports and the trash, About, Quit, folder windows, and a
program run from its icon, and now a folder made and a folder tree
deleted: a drive or a folder double-clicked lists through
`Fsfirst`/`Fsnext` into a window, folders first, with an icon per
entry, the name and information lines, the fuller, the arrows and the
closer; a program double-clicked goes to `shel_write`, and the desktop
leaves its windows' places in the shell buffer as the text of
DESKTOP.INF and opens them again when the shell brings it back; File
-> New folder and File -> Delete (deskfun.c) put up the donor's
dialogs, and the delete counts what it will do before it does it,
walking folders inside folders with a DTA per level -- our GEMDOS
keeps a search by the DTA that owns it, as the ST does. It loads its
resource from the disk beside it, its icons EmuTOS's, checked in like
the font. Its gate is a new kind: the desktop is a program, not a
script, and which calls it makes depends on what the AES answers, so
`tools/deskref.py` is the desktop transcribed against the model, and
the harness syncs to the ABI's own call counter rather than a record
count. A tenth compiler defect (parameters clamped in place, then read
from a slot never written) put two of three icons off the screen on
the first run; an eleventh (a near-to-far struct copy over 8 bytes is
an internal error) is copied around byte by byte. The first window
open ran the application's 256-byte stack out, and a `.G4A`'s stack
is sized per link now; and the hourglass that stayed over an opened
folder was the donor's `gsx_mfset` hide-and-show, missing on both
sides of the gate. The transcription found the next one on the host
before the emulator ran: the desktop's loop ends on `do_open`'s
answer, and a drive's window opening had been answering TRUE -- as it
found, a milestone later, that a New folder was answering the same way
and quitting the desktop. The delete's dialog broke one of the
compiler's own rules (a 16-bit load through a local pointer with the
arithmetic in the same expression, `tools/ccbug` rule 5) and filled
25 KB with spaces, through GTIA space, which soft-resets VBXE, and
through POKEY's IRQEN, which froze the machine in an interrupt storm:
the gate's post-mortem -- which call the target is inside, what the
delete had counted, the CPU's last thirty-two instructions -- is how
that was read back.

**What is not built yet, and is written down so it shapes what is**
(`docs/shipping.md`). The system is 121 KB — more than a single-density
floppy holds, and an enhanced-density one leaves it three sectors of
room. **Double density** is where the DOS 2 product disk lives now, and
`tools/atr.py` writes it: 253-byte sectors leave 42 KB free with the
DOS, its shell and a demonstration program beside GEM — a few programs,
not a library. The volume gem4xe belongs on is a CF card or a hard disk with APT
partitions, and `make` writes one: `build/gem-cf.img` is a 16 MB image
with an APT table, two 8 MB SDFS partitions and the install layout on
the first — the system in `\GEM\`, an application in `\APPS\`, an
`AUTOEXEC.BAT` that runs it. **`make test-cf` boots that card into the
desktop**, on the machine the project is for: an Ultimate 1MB whose
flash holds SpartaDOS X *and* the PBI BIOS, a SIDE 2 with the card on
its IDE bus. Nothing on the card is a driver — the PBI BIOS reads the
APT table and mounts the partitions as `D1:` and `D2:` before any DOS
runs. Three things about that machine had to be read out of its own
firmware first, including the wait at `$D803` that stops the disk dead
while the SIDE still claims the cartridge window (`docs/shipping.md`
§3). The floppy is now the bootstrap, not the ceiling.
Both product disks now come up in the desktop rather than at a prompt,
and `make test-boot` boots them with nothing typed: the SpartaDOS one
from a `STARTUP.BAT` and an `AUTOEXEC.BAT` (3.2 runs the first, X the
second), the DOS 2 one from `AUTORUN.SYS` — which DOS II+/D, the disk's
old DOS, turns out not to have at all. **No string a person
reads belongs in the C**, and none does now: the desktop's eleven alerts are nine free strings of DESKTOP.RSC,
asked for by index (`fun_alert`, the donor's shape), and the gate puts
one on the screen and compares it. The one exception is the alert that
says the resource is missing, which cannot come from the resource. Next
is a `LANG.RSC` for what the *system* says — far-resident, copied a
string at a time into a near buffer, since bank $00 is the scarce thing
— beside a per-language resource for each application, because a GEM
dialog's geometry travels with its text.

## Verification

Every gate compares the target against a **host reference model** —
`tools/vbxeref.py`, `tools/vdiref.py`, `tools/aesref.py` — which is treated as
the specification. Both pixels and returned values are compared, byte for byte,
running on emulated hardware with all three boards fitted.

Nothing here is asserted by eye. Two of the bugs found so far were invisible on
screen and only a pixel diff caught them — and one went the other way: the file
selector listed a file the reference did not, every returned value agreed, and
only the screenshots disagreed (`docs/phase11.md`).

Calypsi cc65816 5.18 has ten defects this tree has met — eight in code
generation, one crash and one in the front end's constant arithmetic — each
reproduced in the vendor's own simulator (the crash, in the compiler itself)
and worked around at the source (or, for the divide flags, with a linker
override). `tools/ccbug/README.md` lists
them and the rules the sources follow; `make check-cc` reports when one is
fixed upstream so its workaround can go.

## Building

Needs [Calypsi](https://github.com/hth313/Calypsi-tool-chains) 5.18+ for the
65816 and Python 3. There is no Atari target in Calypsi, so this tree carries
its own board support: `src/crt_atari.s`, `src/gem4xe.scm` and
`tools/mkxex.py`. The GEM system font is extracted from an
[EmuTOS](https://emutos.sourceforge.io/) checkout at build time rather than
committed here, so point `EMUTOS=` at one; `CALYPSI=` finds the tool chain.
Both default to `~/dev/…`.

    make            # build
    make test-host  # the host tests: no emulator, no fixtures, no toolchain

The emulated gates need [AltirraSDL](https://github.com/ilmenit/AltirraSDL),
and the ones that put an Ultimate 1MB in the machine need the patches in
`tools/altirra/` as well — two are open pull requests upstream, so run those
with `ALTIRRASDL=/path/to/patched/AltirraSDL`.

They also need Atari disk images, and **none is distributed here**: a DOS 2
disk, a double-density DOS 2 disk, a SpartaDOS 3.2 disk, an SDX cartridge
image, a U1MB flash image. Copy `fixtures.toml.example` to `fixtures.toml`
(gitignored) and put your own paths in it; the harness copies an image into
`build/` before it touches it, and a gate whose fixture is missing says so and
stops.

    make test       # host tests + the emulated gates
    make test-cf    # the CF card on the U1MB machine (needs [u1mb].flash)

## Licence

GPLv2 or later — see `COPYING`. The lineage is EmuTOS, which *is* the
Caldera-GPL'd Digital Research GEM source carried forward in C, so the licence
position is inherited rather than chosen.

Where a file follows EmuTOS, its header names the donor file it follows, and
the two trees are read side by side deliberately — this is a port, not a clean
room. Two provenance rules hold everywhere else:

- **The Atari Corp VDI/AES corpus is a specification only.** It settles what a
  real ROM does; no line of it appears here.
- **Nothing that is not ours to give is in the tree.** No ROM images, no disk
  images, no fonts, no firmware: the GEM font comes out of an EmuTOS checkout
  at build time, the disk images are the user's own (`fixtures.toml`), and the
  Altirra patches in `tools/altirra/` are diffs against a GPLv2 project that
  are also filed upstream.
