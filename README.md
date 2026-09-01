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
| `make test-host` | 9/9 | pointer device layer |
| `make test-emu` | 5/5 | VBXE FX 1.26 / Rapidus / MEMAC A / CPU switch |
| `make test-m1` | 5/5 | Calypsi C on the 65C816 |
| `make test-m2` | PASS | 640×240×4bpp HR overlay, 153,600/153,600 pixels |
| `make test-m3` | 49/49 | VDI conformance — pixels *and* return values |
| `make test-m4` | 4/4 | AES `objc_draw` / `objc_find` |
| `make test-m5` | PASS | linear RAM probed: banks `$01-$EF`, 14.9 MB |

`make test` runs them all. Per-phase notes, including the bugs and what caught
them, are in `docs/`.

The 37 VDI opcodes the AES and the GEM Desktop actually use are complete. The
AES object library draws and hit-tests. Next is `form_do` and the window
manager.

## Verification

Every gate compares the target against a **host reference model** —
`tools/vbxeref.py`, `tools/vdiref.py`, `tools/aesref.py` — which is treated as
the specification. Both pixels and returned values are compared, byte for byte,
running on emulated hardware with all three boards fitted.

Nothing here is asserted by eye. Two of the bugs found so far were invisible on
screen and only a pixel diff caught them.

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
