# Phase 40 -- the boot screen, read off a real screen: "not kept"

Phase 39 ended with a promise: it would end when the `Vectors` line had
been read off a real board.  It has.  A 130XE with a Rapidus (a 6S9054E
core, BIOS and menu 1.2), an Ultimate 1MB 4.2 with SpartaDOS X 4.49b in
its flash, a VBXE and a SIDE 2 holding the card `tools/mkcf.py` writes
boots SDX, runs `GEM` from `AUTOEXEC.BAT`, and puts up the boot screen
with every line as the emulator draws it -- version, processor, config,
language, video, DOS, memory -- except one:

    Vectors   vectors not kept ($74/$81)

and then goes back to the prompt with the sentence `main()` gained in
Phase 39.  That is the machine saying exactly which step failed, which
is what the line was for.  (Before that there had been a freeze and a
run of attract-mode colour cycling with nothing on the screen at all,
until a change in the Rapidus setup menu -- which change is not yet
known -- let it boot.  That story is not finished either.)

## What the line says

`$74/$81` are the MCR and CMCR as the firmware left them: windows 0, 1
and 3 fast, window 2 (`$8000-$BFFF`, where the MEMAC window goes) slow,
write-through on, I/O passed through, *System ROM: Rapidus*; the 1 MB
SRAM.  `vectors not kept` is `IRQ_FAIL_VEC`: `copy_run()` reported the
OS ROM copied under itself into both runs, and then the twelve bytes of
native vectors written at `$FFE4` read back as something else.

The copy's verification is blind here, and that is worth writing down.
`copy_run()` writes each byte and reads it back, but what it writes is
the ROM's own byte, so if the write goes nowhere and the read still
comes from the ROM, every byte compares equal and the copy "succeeds".
Only `write_vectors()` writes something the ROM does not contain, and
it is the only step that could have noticed.  So the finding is narrower
than "the copy failed": with PORTB bit 0 clear, window 3 fast and
write-through off -- the Phase 13 way, chosen so that a DOS living
under the ROM survives -- a write to `$FFE4` on this card does not land
where the next read comes from.  The emulator's Rapidus takes that
write; the real one, in this configuration, does not.

Two things about the card make it plausible without settling it.  The
firmware's own native NMI vector goes in with write-through *on*
(read out of MODULE.ROM 1.2 for what it does, not how) -- the card's
maker writes it the one way gem4xe did not.  And *System ROM: Rapidus*
means the OS the CPU runs comes out of the card's own copy, so whether
PORTB bit 0 alone takes window 3 from ROM to RAM on the SRAM side is a
question the emulator does not ask.

## What changed

**`irq_install()` finds the way in rather than assuming it.**  There
are three ways to write the RAM under the OS ROM on a Rapidus, and
`find_via()` in `src/sys/irq.c` tries them in order until the twelve
vector bytes read back:

    S   IRQ_VIA_SRAM   window 3 fast, write-through off -- the SRAM
                       alone, the motherboard's copy (and any DOS in
                       it) untouched.  First, because it spares the DOS.
    W   IRQ_VIA_BOTH   window 3 fast, write-through on -- the firmware's
                       way; the motherboard's copy is overwritten.
    M   IRQ_VIA_BUS    window 3 slow -- the motherboard's RAM, as a
                       machine without a Rapidus has it.  The OS then
                       runs from it at bus speed, which costs next to
                       nothing: while GEM runs the vectors are the only
                       thing fetched there.  `irq.fast` is 0 and
                       `cio.s` is told not to re-fast the window.

The twelve bytes are saved from the motherboard first and put back, in
every RAM, if all three refuse, so a failed probe leaves the machine as
it was.  Whichever way took is the way the ROM copy then goes, and the
boot screen says which:

    Vectors   OS copied ($74/$81/S)
    Vectors   not kept ($74/$81/M:E2)

The letter is the way; after a failure a colon and the first byte that
read back wrong, as it read (`irq.bad_byte`, set by the copy and by the
vector write alike), so the next photograph says whether the write
went nowhere (the ROM's byte) or somewhere strange.  A machine without
a Rapidus prints `not kept E2`.  `copy failed` replaces `no RAM under
ROM`, in place, so LANG.RSC's indices stand.  Twenty-six columns, and
the longest case uses all of them.

`m6` reads `irq.via` out of STATUS and requires the SRAM way (the
emulator's model always takes it, and `m14`'s SpartaDOS 3.2 run would
be the next to fail if that changed); the other two were exercised by
forcing the probe's start for a run each -- `m6` and `m14x` pass all
three ways, with MCR reading `$FC` in the bus way -- and the forcing
was removed.  `product_boot.py` expects the `/S`.

## What to do on the hardware

The same card image, rebuilt.  Film the `Vectors` line.  `OS copied`
with `W` or `M` is the answer and the desktop should follow; `not kept`
with three letters' worth of trying behind it and a byte after the
colon is the next conversation, and a much shorter one.

Two settings on the card are worth a look at the same time.  The
6S9054E's known-issues list says to leave the SDRAM 4K cache *off* on
this core, and it is on; and *System ROM: Default* takes the card's own
OS copy out of the question for the length of one experiment.

## And then it did

The rebuilt card booted to the desktop -- the first GEM desktop on a
real Atari 8-bit, 2026-09-13, on a Commodore 1084S: the green field,
DISK A and B, TRASH, and the File menu dropped down under a mouse that
moves.  The boot screen, photographed on the next boot:

    Version    0.1.1
    Processor  65C816, Rapidus
    Vectors    OS copied ($74/$81/W)
    Memory     14.8 MB, banks $04-$EF
    DOS        SpartaDOS X
    Settings   GEM4XE.CFG
    Language   LANG.RSC
    Screen     VBXE 1.26 ($D640)
    Clock      U1MB, 2026-09-13 23:22:55
    Pointer    ST mouse
    Printer    none

**`W`.**  The card refused the SRAM way and took the firmware's: window
3 fast, write-through on.  So on a real 6S9054E a write with
write-through off does not reach the SRAM under the OS ROM at all --
or reaches an SRAM the next read is not served from -- and the
emulator's `S` is the emulator's.  The order stays SRAM first: `S` is
what spares a DOS under the ROM when a card allows it, and the probe
costs twelve bytes written three times at most.  What it means for the
card's owner is that the motherboard's copy of the OS window is
overwritten by the ROM's own bytes, which SpartaDOS X (in flash, not
under the ROM) does not mind.  Everything else on the line matches the
emulator's screen to the character, the clock included: the DS1305
bit-bang read at 20 MHz held its timing.

**And the freeze before that** -- the attract-mode colours, the blue
screen three times, the click and the ticking -- was the Rapidus
menu's first option, *Preference*, set to *Sweet16*.  On *Rapidus* it
boots.  *Preference* is a preset for the rest of the menu -- the
firmware's strings for it are *Classic*, *Sweet16*, *Warp XE*, *Warp
II*, *Rapidus*, *Custom* -- each the speed-up profile of an earlier
accelerator: which windows are fast, waitstates, the caches, the 64K
address-wrap option.  Which of those the Sweet16 profile sets that the
program cannot live with is not known; the boot screen never appeared
under it, so the MCR and CMCR it would have printed were never read.
The wrap option is the suspect, since it folds bank `$01`, where the
code is, onto bank `$00`.  A setup note for docs/shipping.md, and a
probe for later: `rapidus_speedup()` reads the MCR and CMCR and could
refuse a profile it does not understand instead of running into it.

The camera's moire over the desktop's dither is the camera's: the
1084S shows a steady 50 % checker.
