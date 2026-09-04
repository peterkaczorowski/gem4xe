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
| `make test-host` | 38/38 | pointer device layer — the ST, Amiga and CX80 models walked through the target's C in the compiler's simulator — and .xex far-code staging |
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
| `make check-cc` | PASS | the eight compiler bugs worked around, in the vendor's simulator |
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

## Verification

Every gate compares the target against a **host reference model** —
`tools/vbxeref.py`, `tools/vdiref.py`, `tools/aesref.py` — which is treated as
the specification. Both pixels and returned values are compared, byte for byte,
running on emulated hardware with all three boards fitted.

Nothing here is asserted by eye. Two of the bugs found so far were invisible on
screen and only a pixel diff caught them — and one went the other way: the file
selector listed a file the reference did not, every returned value agreed, and
only the screenshots disagreed (`docs/phase11.md`).

Calypsi cc65816 5.18 has eight defects this tree has met — six in code
generation, one crash and one in the front end's constant arithmetic — each
reproduced in the vendor's own simulator (the crash, in the compiler itself)
and worked around at the source (or, for the divide flags, with a linker
override). `tools/ccbug/README.md` lists
them and the rules the sources follow; `make check-cc` reports when one is
fixed upstream so its workaround can go.

## Building

Needs [Calypsi](https://github.com/hth313/Calypsi-tool-chains) 5.18+ for the
65816, Python 3, and AltirraSDL for the emulated gates. There is no Atari target
in Calypsi, so this tree carries its own board support: `src/crt_atari.s`,
`src/gem4xe.scm` and `tools/mkxex.py`.

    make            # build
    make test       # host tests + every emulated gate

## Licence

GPLv2 or later — see `COPYING`. The lineage is EmuTOS, which *is* the
Caldera-GPL'd Digital Research GEM source carried forward in C, so the licence
position is inherited rather than chosen.

The Atari Corp VDI/AES corpus is used as a **specification only** and no line of
it appears here.
