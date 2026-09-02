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
| `make test-host` | 29/29 | pointer device layer, .xex far-code staging |
| `make test-emu` | 5/5 | VBXE FX 1.26 / Rapidus / MEMAC A / CPU switch |
| `make test-m1` | 5/5 | Calypsi C on the 65C816 |
| `make test-m2` | PASS | 640×240×4bpp HR overlay, 153,600/153,600 pixels |
| `make test-m3` | 68/68 | VDI conformance — pixels *and* return values |
| `make test-m4` | 12/12 | AES object library: draw, find, change, edit, centre |
| `make test-m5` | PASS | linear RAM probed: banks `$02-$EF`, 14.9 MB |
| `make test-m6` | PASS | far code copied up and running in bank `$01`; bank `$00` on the fast bus |
| `make test-m7` | 10/10 | `evnt_*`, `form_do`, `form_dial`, `graf_watchbox` under host-driven input |
| `make test-m8` | 12/12 | the window manager and the control manager: rectangle lists, moves, gadgets, `WM_*` |
| `make test-m9` | 4/4 | menus: the bar, drop-downs, `MN_SELECTED`, screenshotted inside the wait |
| `make check-cc` | PASS | the six compiler bugs worked around, in the vendor's simulator |
| `make movie` | PASS | a session with the AES itself, filmed frame by frame and checked as a gate: `build/movie/gem4xe.mp4` |

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
Phase 8, 2 ms now (`docs/phase8c.md`). Next is `form_alert` and the
desktop.

Phase 7 also found that the Rapidus resets with all of bank `$00` on the
1.79 MHz bus, and that gem4xe had run its data, stack and direct page there
for six phases without a gate noticing (`docs/phase7.md`, Step 4).
`src/sys/rapidus.c` derives the speed map from the linker's placement and
the MEMAC window rather than restating either, and `make test-m6` reads the
registers back.

Code lives in **bank `$01`**, not bank `$00`. A `.xex` segment header is two
16-bit addresses, so a DOS loader cannot place anything above `$FFFF`; the far
image therefore travels as chunks aimed at a staging buffer and DOS copies it
up through `INITAD` as it reads the file. That took the code ceiling from ~28 KB
to a bank at a time, and freed the `$4000-$7FFF` scaffold the test runner had
been borrowing from U1MB.

Running this on a machine without a 65C816 would corrupt memory rather than
fail — the long store the copier needs is an unstable undocumented opcode on an
NMOS 6502 — so the loader identifies the CPU and probes for linear RAM before
its first store, and prints a line and returns to DOS if either is missing.

## Verification

Every gate compares the target against a **host reference model** —
`tools/vbxeref.py`, `tools/vdiref.py`, `tools/aesref.py` — which is treated as
the specification. Both pixels and returned values are compared, byte for byte,
running on emulated hardware with all three boards fitted.

Nothing here is asserted by eye. Two of the bugs found so far were invisible on
screen and only a pixel diff caught them.

Calypsi cc65816 5.18 has six defects this tree has met — five in code
generation and one crash — each reproduced in the vendor's own simulator (the
crash, in the compiler itself) and worked around at the source (or, for the
divide flags, with a linker override). `tools/ccbug/README.md` lists
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
