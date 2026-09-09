# Phase 30 -- the clock was stopping the mouse

A question about the clock -- *"does it use one of the RTCs? I think the
SIDE 2 has one"* -- turned up the cause of a complaint from two sessions
earlier: **the mouse is slow as snot**, and then **I can't click on
anything**.

## What was wrong

`clock_read()` bit-banged the DS1305 through `$D3E2` **unconditionally**,
on every `Tgetdate` and `Tgettime`.  On a machine WITH an Ultimate 1MB
that is the RTC.  On a machine without one it is not dead space:

> The PIA is mirrored every four bytes across `$D300-$D3FF`, and
> `$D3E2 & 3 == 2` is **PACTL**.

`PACTL` bit 2 is what makes `$D300` read the joystick port rather than
its data DIRECTION register, and the clock routine finishes with
`out(0)` -- so it left `PACTL = $00`.  From that moment `PORTA & 0x0F`,
which is where `poll_relative()` and the timer interrupt take the ST
mouse's quadrature, read a flat zero.  **The pointer stops moving.**  The
buttons go on working, because those are GTIA's `TRIG0`/`TRIG1` and
nothing had touched them.

The timing fits the report exactly: nothing asks GEMDOS for a date until
a directory is listed, so the pointer moves until the first window or
folder is opened and is dead afterwards.  Opening `APPS` is what killed
it, and the calculator that came up next could not be clicked because the
pointer was no longer where the hand was.

Measured, not inferred.  On the shipping build with no U1MB:

    HWPOKE $d3e2 $00   ->  PACTL=$00 (DDRA),  $D300=$00
    HWPOKE $d3e2 $03   ->  PACTL=$03 (DDRA),  $D300=$00

`$03` is `RTC_CE | RTC_LOW`, which is exactly what the clock writes.

## The fix: look before touching

Neither card has to be detected the hard way, because **both decode a
READ of that register as the RTC's one output line and nothing else** --
`ReadState() ? 0x08 : 0x00` in Altirra's `ultimate1mb.cpp` and
`side.cpp` alike.  So a register that reads with any other bit set is not
an RTC, and that is settled without a single write:

  * a PIA's `PACTL` reads `$3C` once the OS has set it -- rejected;
  * floating cartridge space reads `$FF` -- rejected;
  * `$D3E2` gets one more check for free: on a machine with no U1MB it
    *is* `PACTL`, so it reads the same as `$D302`, and on a machine with
    one it does not.

`clock_probe()` runs once, tries the U1MB's register and then the SIDE's,
and only writes to a candidate that passed the read-only test and then
answers a plausible BCD time.  A machine with neither keeps the ST's
epoch, as before -- and keeps its mouse.

## And the SIDE 2's clock now works

The SIDE and the SIDE 2 carry **the same DS1305, wired the same way**, at
`$D5E2` instead of `$D3E2` (`side.cpp`: `case 0xD5E2: // DS1305 RTC`,
with `WriteState((value & 1) != 0, !(value & 2), (value & 4) != 0)` --
CE, an inverted clock, and data, bit for bit what the U1MB does).  So
supporting it cost one address in a probe list, and a machine with a
SIDE 2 and no U1MB now tells the desktop the real date.

`$D5xx` is the cartridge control area, which is why the read-only test
matters more there than anywhere: nothing is written to it unless it has
already answered like an RTC.

## The gate that was missing

`test-m15` asked the clock for the time and checked the answer.  It never
asked what the asking had cost.  It does now, and it is not a comparison
against a model -- it reads the machine:

    PACTL after the clock: $3C (the joystick port)

Run against the previous `clock.c` on the same machine, the same line
reads

    FAIL: after Tgetdate/Tgettime PACTL is $00: bit 2 is clear, so $D300
    now reads DDRA and not the joystick port -- the clock was driven
    through the PIA
    PACTL after the clock: $00 (DDRA -- BROKEN)

which is the whole bug in one line, and the reason it survived so long is
that **every gate drives the pointer by poking `ptr_state`**
(`src/m3_vdi.c` opens with `ptr_init(PTR_NONE, ...)`).  Nothing that runs
under a gate has ever read `PORTA` for a pointer position, so nothing
could notice that `PORTA` had stopped being `PORTA`.

## Worth remembering

**`$D3xx` and `$D5xx` are not free space on this machine.**  A register
that belongs to an expansion card belongs to the Atari when the card is
absent, and the Atari's version of it may be something the machine cannot
do without.  Probe by reading, and only ever write to hardware that has
already identified itself.
