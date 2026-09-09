# Phase 26 — more than one thing at a time

Every file operation the desktop has could already work on a set: the
delete, the copy and the move all walk the window's FNODEs and act on
each one flagged `F_SELECTED`.  What could not produce a set was the
*selecting*.  A click chose exactly one thing, and that was the whole
of it.

    make test-m19   a rubber band over two cells, and File -> Delete
                    taking both -- the folder tree and a file

## Two gestures, one of them ungateable

**SHIFT-click** adds an item to the selection or takes it out again --
`act_bsclick`, the donor's name and the donor's rule.  SHIFT is the only
modifier this machine can be asked about while the button is down
(`docs/phase18.md`), so it is the one.

**A rubber band** is the other: a press on a window's background, held,
draws a box until the button comes up, and everything the box touches
becomes the selection.  It grows right and down from the anchor -- the
origin stays put and only the size changes (`src/aes/grlib.c`,
`gr_rubwind`) -- which is the ST's behaviour and the donor's.

The band is the one the gate drives, and not by preference: **no harness
on this machine can hold SHIFT down through a mouse click.**  The
bridge's `KEY` sends a keystroke and `KEYRAW` holds one, but holding a
bare modifier across a button event needs a verb our AltirraSDL patch
does not have -- the same block that has kept the SHIFT-drag *move*
ungated since phase 18.  So shift-click is written, and the band is what
`test-m19` proves.

## The order a press is read in

    a double-click     opens what is under it, and only that
    a press held       drags the selection -- and an item pressed on
                       that was not part of it becomes the whole of it
    a press on nothing, held    draws a rubber band
    anything else      is a click, and act_bsclick decides

The order matters and it is not the order the code had.  A press used to
select first and ask about dragging afterwards; now the drag is
recognised first, because **SHIFT means "move" to a drag and "add to the
selection" to a click**, and the same press cannot be both.  `hndl_drag`
returns whether it really was a drag, and the click semantics only run
when it says no.

## What the gate does

The window is fulled first -- at its opening size only two cells are
inside the work area -- and then a band is drawn over two of them, one
above the other.  Its anchor is in the four pixels of gap to the LEFT of
the cells, and inside the work area rather than above it: a press on the
frame belongs to the control manager and never reaches the desktop.

**The nudge is the part that took a run to get right.**  A press is held
through the double-click interval and a move of more than two pixels is
what flushes the wait -- and the AES reports the pointer where it is
*then*, not where it went down.  A nudge of +8 across, which is what the
drag steps use, put the anchor on the very cell the band was meant to
catch, and the desktop dragged instead.  The nudge travels *down* the
gap.

Then File -> Delete, whose dialog counts four files and two folders --
the folder tree and the file beside it -- and the image afterwards has
neither.

## The stack that was never the stack: an emulator bug, found

The desktop's stack was 640 bytes and had to become 896.  At 640 the run
died at the *click* that precedes the band, with `irq_fault 4`, the
program counter in DOS's memory, and G and `op_path` smeared with a
repeating `02 04`.  At 896 every gate passed.  Both directions
reproduced, and the change was one line of the Makefile.  The low-water
mark at 896 said 302 bytes, so "the stack was too small" explained
nothing, and the note here said so: *896 is a measurement, not an
understanding*.

It is understood now, and it was never gem4xe's.

**What the wreckage actually was.**  The `02 04` is not a smeared buffer.
Dumping the whole address space at the stall -- rather than the region
the guess was about -- finds it *everywhere*: `$0400-$1FFF`,
`$2042-$CFFF`, `$E000-$FF97`, and, decisively, **the hardware**.
`HWSTATE` reports `DMACTL $02`, `HSCROL $02`, `VSCROL $04`, every GTIA
colour register, POKEY's `AUDF`/`AUDC`, PIA's `PORTB`: all `$02`/`$04`.
No C buffer overrun writes ANTIC's registers.  A **stack** does, when it
descends through the whole of bank `$00` -- and four bytes at a time is
what a 65C816 interrupt pushes.

That is the signature of the second bug in
`tools/altirra/altirra-65c816-native-mode.patch`, in the patch's own
words: *"SEI with a POKEY IRQ pending in native mode pushed four bytes
per fetch until the stack had wrapped through bank 0."*  The handler is
re-entered at every opcode fetch because the emulator's native-mode
vector states never clear the "one more IRQ" shadow flag.

**The controls.**  With `DESK_BSS = 2944`, `DESK_STACK = 640` -- the
build that fails -- and the gate unmodified:

    /usr/bin/AltirraSDL (altirrasdl-git r535, 3e85661e)   FAILS
    b3061c7 + the three patches in tools/altirra/          PASSES
    ...with only the SEI hunk reverted                     dies sooner
                                                           still (the
                                                           runner never
                                                           comes up)

The installed build is an *ancestor* of b3061c7 and nothing between them
touches `cpumachine.inl`, so the CPU core is the same code in both: the
difference is the patch, and reverting one hunk of it puts the failure
back.  `make test-m12`, which `tools/altirra/README.md` says fails
deterministically on an unpatched emulator, happens to pass on it today
-- which is why the emulator was not the first suspect.  A timing bug
that a couple of hundred bytes of layout will trip or untrip does not
stay reproduced by the case that first found it.

**What it cost, and the lesson.**  Three phases of the desktop's memory
budget were tuned around it, and `docs/phase24.md`'s layout-lottery
reasoning -- correct for the far allocator's bank straddle -- was the
wrong frame here, because it kept the search inside gem4xe.  The thing
that broke the deadlock was cheap and should have come first: **dump the
whole machine, not the part the hypothesis is about.**  One `HWSTATE`
said what a week of canaries could not, because a corrupted `DMACTL`
has exactly one explanation and it is not a C program's array.

**Where the numbers stand.**  `DESK_STACK` is 640, its measured
low-water use ~300, and `DESK_BSS` is the 2944 the desktop actually
needs.  They were briefly 896 and 3200 -- 256 bytes of slack that kept
an *unpatched* emulator green -- and phase 27 took that back, because
**a dodge is not a fix and does not keep**: adding the text view and the
sorts moved the code, and the storm moved with it, out of m19 and into
m18.  There is no layout that is safe; there is a patched emulator.

    ALTIRRASDL=/path/to/patched/AltirraSDL make test

What is left behind instead is a *diagnosis*.  Every desktop gate now
calls `storm_check` (`tests/emu/m7_form.py`) when it stalls: if the RAM
it samples is one alternating pair of bytes **and** ANTIC's, GTIA's and
POKEY's registers hold the same pair, it says so, names the patch, and
says the run tells you nothing about the desktop.  Nothing gem4xe can do
writes ANTIC's registers, so the two conditions together cannot mean
anything else.  `tests/host/test_storm.py` gates the rule itself,
against a faked bridge -- a storm is the emulator's to produce and no
target run can be asked for one on demand.
