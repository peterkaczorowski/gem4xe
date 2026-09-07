# Phase 23 — the loader switches the machine

Put the disk in.  That is the whole change.

    make test-boot   both product disks, into the desktop, with nothing
                     typed and nothing poked
    make test-m6     the refusal, on a machine with no accelerator at all

## The problem the distribution exposed

A Rapidus **always cold-boots as a 6502**.  Altirra's device does it in
`ColdReset()` — "reset FPGA, force boot on 6502" — and the card does the
same, so the first thing that happens on a machine that can run gem4xe
is that gem4xe cannot run.  The loader identified the CPU, printed

    gem4xe needs a 65C816: this is a 6502.
    Nothing was changed.  Press a key.

and gave the machine back.  Correct, and useless: the machine in
question is one that *could* have run it.  Every gate got past this by
poking `$D1FF` and `$D191` over the test bridge, and phase 22 shipped
`816.COM` so that a person could make the same writes by hand.  Both are
workarounds for the loader not doing it itself.

## What it does now

`fl_no816` in `src/farload.s`, thirty-five bytes, reached only when the
CPU test says this is not a 65816:

    fl_no816:     lda     #1              ; PBI device 1, then 2, 4, ...
    fl_slot:      sta     PDVS            ; select it; the registers appear
                  pha
                  lda     RAPBANK
                  bne     fl_next         ; open bus, or not in 6502 mode
                  lda     RAPCFG
                  and     #RAPCFG_6502
                  beq     fl_next         ; a 65816 already: not our business
                  lda     #1
                  sta     COLDST
                  lda     #0
                  sta     RAPCFG          ; ...and the CPU resets here
    fl_next:      pla
                  asl     a
                  bcc     fl_slot
                  sta     PDVS            ; A is $00: nothing selected

Three things are worth pointing at.

**`COLDST` before the switch.**  The switch resets the CPU, and the OS treats that
reset as a *warm* start — which is exactly when a DOS does not run its
start-up file.  Without `$0244`, the machine comes back to a prompt and
sits there instead of loading GEM again.  This was already measured for
the harness's own switch (`docs/phase22.md`); it is the same fact.

**The slot is not assumed.**  The mask is shifted left, so the eighth
shift leaves `$00` in A — which is *also* "nothing selected", so the
loop's exit value is the deselect and the tidy-up costs one instruction.
Selecting for the length of a call and deselecting after is the PBI
convention, and this code follows it on a machine where it finds
nothing.

**The card has to answer twice.**  Measured at a DOS prompt on a booted
machine, with each of the eight slots selected in turn:

    rapidus present:   PDVS=$01  ->  $D190 $00   $D191 $40   $D192 $FF   $D193 $FF
                       every other slot          $FF $FF $FF $FF
    no rapidus:        every slot                $FF $FF $FF $FF

`$D190` is the FPGA bank register, which answers only in 6502 mode;
`$D191` is the config register, bit 6 set for the 6502.  Open bus reads
`$FF`, so `$00` at `$D190` is the strong half and the mode bit is the
confirmation.  Nothing is written to a slot that fails either test.

## What it cost

**Bank $00 had 47 bytes free and now has 4** (the runner's link; the
product's has 12).  `Near` — the entry stub,
the loader, the C start-up, the CIO trampoline and every constant — is
`$3580-$3FFD`, 2,686 bytes, and it is 99.6% full.  The first version of
this shim used a table of the eight masks and a counter and did not fit
at all; the shift loop is what made it thirty-five bytes instead of
fifty-seven.  The next thing that needs near code will have to find room
somewhere, and there is none in `LoRAM` either (7 bytes).

Those thirty-five bytes also turned `test-m14x` red, which is
`phase24.md`: not because of anything here, but because they moved the
far image and the far allocator would hand out a block straddling a
bank.  The shim waited on that fix and went in with it.

## What the gates say now

- **`test-boot`** does nothing to the machine after power: no key, no
  poke.  It watches the CPU rather than the screen — both boot passes
  look alike until the desk appears — and requires it to start as a 6502
  and to become a 65C816 by itself.  It did, 800 frames in on the
  SpartaDOS disk and 100 on the DOS 2 one, and the desk that came up
  matched the model pixel for pixel.
- **`test-m6`** now boots its disk with the **Rapidus taken out of the
  machine**, which is the only place the refusal is still the right
  answer.  That also gates the probe's negative half: on a machine with
  nothing to find, all eight slots are looked at, no far RAM is written
  (`$FF -> $FF` at all three probed banks), the runner does not start,
  and the message is on the screen.
- **`cf_boot`** got the same rewrite and **is not verified here**: it
  needs the U1MB flash fixture and the patched emulator, and this
  machine has only the stock `AltirraSDL`.  The change is the same shape
  as `test-boot`'s.

## Debts

- **Verified against Altirra's model of the card, not against a card.**
  The register values above are the emulator's.  The fallback if a real
  Rapidus answers differently is exactly the old behaviour — the refusal
  — and `816.COM` is still on the disk for that case, which is why the
  distribution's page now says that having to run it is worth reporting.
- **A card that answers the probe and then ignores the switch leaves
  `COLDST` set.**  The next RESET on that machine is a cold one instead
  of a warm one.  It is five bytes to put back and there are twelve
  left; it is written down instead.
- **Probing reads somebody else's registers.**  There is no way to ask
  "is a Rapidus on this slot" without selecting the slot and reading
  two bytes of it, and a device that treats a read as a strobe would
  see one.  `$D100-$D1FF` is the PBI convention's register window and
  devices are expected to tolerate reads there, but this is an
  assumption about other people's hardware rather than a measurement of
  it.  It happens only on a machine that has already failed the CPU
  test, and it stops at the first slot that answers -- which on every
  machine seen so far is the first slot tried.
- **The 65C02 goes down the same path.**  It is not a 65816, so it
  probes and then refuses, which is right — but a 65C02 with a Rapidus
  is not a machine anybody has, and the code does not distinguish.
