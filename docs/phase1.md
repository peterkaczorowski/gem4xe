# Phase 1 — the VBXE surface

Status: **gate green.** `make test-m2`.

    640x240 4bpp HR overlay, drawn entirely by the blitter,
    153,600 / 153,600 pixels match the host reference model.

## What exists

| | |
|---|---|
| `src/vbxe/vbxe.h` | FX 1.2x register map, XDL bits, BCB layout, VRAM map |
| `src/vbxe/vbxe.c` | detection, MEMAC A windowing, palette, XDL, blitter |
| `src/m2_vbxe.c` | the milestone program: bring-up + test pattern + timing |
| `tools/vbxeref.py` | **host reference model** of the HR surface and blitter |
| `tests/emu/m2_vbxe.py` | the pixel-exact gate |

`tools/vbxeref.py` is the important one. It is the specification the on-target
code must match, and it is what Phase 2's VDI opcode conformance tests get
built on. A test that says "153,600 of 153,600 pixels match" is worth
immeasurably more than one that says the screenshot looked right.

## It is written in C

Deliberately. Everything in the VBXE seam is either a handful of register
pokes or a copy through a window; the hot path in a VDI is the **blitter**,
not the CPU. Nothing here needed hand-written assembly, and the measurement
below says nothing here is the bottleneck either. Measure before moving any of
it to `as65816`.

That also avoids the two-assembler build the plan flagged as a risk: the only
assembly in the project is `src/crt_atari.s`, in Calypsi's own `as65816`.

## Measured blitter throughput — the architecture is validated

Timed on target with `blit_time()`, counting ANTIC's VCOUNT (1 tick = 2
scanlines; a PAL frame is 156 ticks):

| Operation | 76,800 bytes | frames | rate |
|---|---|---|---|
| Full-screen **fill** (`and_mask == 0`, no source fetch) | 79 ticks | **0.51** | ~7.5 MB/s |
| Full-screen **copy** (`and_mask == $FF`) | 137 ticks | **0.88** | ~4.4 MB/s |

The plan predicted "about one full-screen copy, or ~1.8 full-screen fills, per
frame" from Altirra's cycle model. Measured: **1.14 copies and 1.97 fills per
frame.** The estimate was right, and the conclusion it carries stands:

> **Dirty rectangles are mandatory, not an optimisation.** A window manager
> that repaints the screen cannot run at frame rate on this hardware.

The ~55-65% of the vendor's headline figures (13.5 MB/s fill, 6.75 MB/s copy)
is the display eating DMA, exactly as documented: the blitter only gets the
cycles the overlay and XDL fetches leave behind.

It also tempers the VRAM window-backing-store idea from the plan. Backing
stores are cheap in *memory* (~300 KB spare) but every restore still costs its
own area in blitter bandwidth, and a full-screen restore is most of a frame.

## Things that bite, recorded

- **Overlay priority must be `$FF`.** Bits 6/7 changed meaning between FX 1.24
  and 1.26; a priority of `$00` renders normally on 1.24 and makes the overlay
  **vanish** on 1.26, where bit 7 became COLBAK.
- **Detect the core as `(CORE_REVISION & $0F) == 0`**, never a whole-byte
  compare. Altirra's manual says FX 1.26 reads `$11`; hardware and the emulator
  both read `$10`. The manual contradicts its own bit table.
- **Never hard-code the register base.** U1MB selects `$D640`/`$D740`/disabled
  through UAUX (`$D381` D5:D4). `vbxe_detect()` probes both.
- **Clear the XDL/BCB region before first use.** A blit list started in
  uninitialised VRAM (`$FF`) runs forever, because the `Next` bit is always set.
- **The blit list must be contiguous** — the `Next` bit only advances to the
  physically adjacent BCB; there is no jump. `blit_run()` therefore stages a
  contiguous array in RAM and uploads it in one go.
- **The 9-bit width field is real.** A 320-byte row needs `320-1 = 319`, which
  does not fit in one byte: it is `p[12]` plus bit 0 of `p[13]`. The
  full-stride band in the test pattern exists specifically to exercise it.
- **`vram_map()` shadows the bank** because `MEMAC_BANK_SEL` should not be
  re-poked per byte, and is invalidated after a blit, since the blitter owns
  VRAM while it runs.
- **The DAC model is confirmed exactly**: `out = (v & 0xFE) | (v >> 7)`.
  Verified against all 16 test colours — `$96`→`$97`, `$7F`→`$7E`, `$C0`→`$C1`.
- **The overlay lands at screenshot column 16, 1:1** in a 672x240 shot.
  Measured, not assumed (`tools/vbxeref.py` records it as `SHOT_X0`).

## Deferred, deliberately

**The blitter-complete IRQ.** `blit_run()` currently polls `BLITTER_BUSY`, and
every read costs a resync to 1.79 MHz on Rapidus. The plan is right that the
driver should use the IRQ instead — but the win only materialises once there is
other work to overlap with a blit, and there is not yet.

It also needs something Phase 0 deliberately skipped: `src/crt_atari.s` switches
interrupts *off*, because the 65816 moves its vectors in native mode (NMI
`$FFEA`, IRQ `$FFEE`) and the Atari OS ROM only fills the emulation-mode ones.
Using any interrupt means installing **native-mode vector stubs** first. That is
a real piece of work and it belongs with the first VDI code that has layout to
do while the blitter runs.
