# Phase 39 -- the first real Rapidus, and a white screen

The first report from hardware: two phone videos of a 1200XL with a
Rapidus, an Ultimate 1MB (PBI BIOS 1.84) and a VBXE on a Checkmate
monitor, SpartaDOS X 4.49e, `X GEM` typed at the prompt -- and a screen
that goes from SDX's blue text to a uniform pale field and stays there.
Nothing draws.  No emulator had ever been near the board, which is the
standing caveat on every line of `src/sys/irq.h`, so this phase is
about turning a picture of a screen into a line of text the machine
says about itself.

## What the videos say

Frame-exact, at the phone's 30 fps: SDX's text is on the screen until
one frame, then the field is dark for four frames (about 130 ms; the
second video has one blended frame), then pale for good.  The pale
settles to a light desaturated blue once the phone's exposure catches
up -- what a monitor's white looks like to a phone that has just
exposed for a blue screen, though a pale blue GTIA field (the ANTIC
path's cleared screen, if the VBXE was not taken) is not ruled out by
the pixels alone.  Either way it is a cleared screen with nothing
drawn on it, and both runs went the same way.

That is the order the code goes in.  The overlay is switched on
(`vbxe_xdl_hr`) with palette 1 still whatever the board powered up
with -- dark -- then `dev_clear_screen` fills the screen with index 0,
then `gsx_start` opens the workstation and `v_opnwk` loads the GEM
palette, whose index 0 is white.  Dark, then white, then nothing: the
machine got as far as the workstation and never drew the desktop.

## Reproduced in the emulator, by taking one call out

What could get that far and then stop?  The diagnostic build below
made it a one-line experiment: skip `irq_install()` and boot.  The
emulator's screen goes white and the desktop's call counter stays at
zero -- the same picture, from a machine that is otherwise fine.

The reason is one that was true since phase 27 and written down
nowhere: **every application calls in through COP** (`src/sys/abi.s`,
`COP #$73` for the VDI and `#$C8` for the AES), and the native-mode COP
vector at `$FFE4` is one of the vectors `irq_install()` places under
the OS ROM.  The polled regime -- `IRQ_OFF`, what `irq_install()`
leaves behind when the copy under the ROM reads back wrong or the
vectors do not stick -- was designed as a fallback that can draw and
read the mouse, and it still can.  It just cannot run a program: the
desktop's first call jumps through whatever the ROM has at `$FFE4`.
White screen.

So the strongest candidate for the real machine is that `irq_install()`
failed on it.  Everything it does under the ROM -- switch the Rapidus's
window 3 to its SRAM, copy the ROM into it with write-through off so
the DOS under the motherboard's ROM survives (`docs/phase13.md`), write
the vectors, read them back -- is modelled on Altirra's reading of a
board whose registers have no public documentation.  It is exactly the
piece of gem4xe that could be wrong on the hardware and right in every
gate.

## What changed

**The boot screen says what the vectors did.**  A line after
`Processor`:

    Vectors   OS copied ($EF/$81)

`OS copied` is `IRQ_ROM_COPIED`, `OS in RAM` is `IRQ_RAM_FOUND`, and the
two failures are `no RAM under ROM` (`IRQ_FAIL_COPY`) and `vectors not
kept` (`IRQ_FAIL_VEC`).  The two bytes are the Rapidus MCR and CMCR as
the firmware left them (`rapidus.mcr_before`, `rapidus.cmcr_before`,
the second new this phase): the board's setup -- which SRAM windows,
write-through, RapidOS or Base, the 64K-wrapping option that would
alias bank `$01`'s first page onto zero page -- in a form a phone can
film.  The emulator already gives two answers: `($FF/$00)` when the
product disk's loader switches the CPU itself, which resets the board
to its defaults, and `($EF/$81)` when the Rapidus PBI firmware has
booted it first, as it does on the machine in the videos.  `test-boot`
checks the line against the struct.

**No vectors, no desktop.**  When `irq.how` is `IRQ_OFF` after the boot
screen's hold, `main()` puts the accelerator back and returns to DOS
with one of three lines (`LANG.RSC`'s `EXIT_NOCOPY`, `EXIT_NOVEC`,
`EXIT_NOIRQ`) instead of clearing the screen and hanging at the
desktop's first call.  A white screen was the worst possible report;
this is the same failure as a sentence.

**GEMDIAG.COM** (`make diag`, `build/gemdiag.com`): `GEM.COM` with a
mark before every start-up step, for a machine that has no bridge.
`src/sys/diag.h` has the design; in short, two channels -- an inverse
digit at column *n* of the top text line, and a POKEY channel-4 tone
that rises a step each time -- and three console keys: **OPTION**
skips `rapidus_speedup()`, **SELECT** skips `irq_install()`, **START**
held makes it wait for a press before each step, so a person can count
where it stopped.  The digits are on the OS screen, so they are visible
until the overlay covers them at step 7; the tones go on.  It is the
same objects on the same layout with `src/gem.c` compiled once more
with `-DGEM_DIAG`; `GEM.COM` compiles the marks to nothing.

Under the emulator: all fifteen marks in order and the desktop up
(plain and with OPTION); with SELECT, marks 0-6, the boot screen with
`Vectors   none`, and `gem4xe: no interrupt vectors installed` at the
SDX prompt -- which is the whole point.

## What to do on the hardware

1. `VIDEO=ANTIC` in `GEM4XE.CFG` on the card as it is.  Takes the VBXE
   out of the question in one edit.
2. This build's `GEM.COM` and `LANG.RSC`.  Its boot screen is held for
   three seconds (SHIFT holds it longer); film the `Vectors` line.  If
   it says `no RAM under ROM` or `vectors not kept`, the machine will
   also say so on the way back to the DOS prompt, and the two bytes in
   parentheses are the next conversation.
3. If there is no boot screen at all, `GEMDIAG.COM` beside `GEM.COM`,
   `X GEMDIAG`, and the last digit or tone names the step.  Then again
   with OPTION held, and with SELECT held.

Nothing here is confirmed on the board yet.  The phase ends when the
`Vectors` line has been read off a real screen.
