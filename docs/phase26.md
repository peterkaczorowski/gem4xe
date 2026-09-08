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

## ⚠ An anomaly, written down rather than explained

The desktop's stack was 640 bytes and had to become 896.  At 640 the
run dies at the *click* that precedes the band, with `irq_fault 4`, the
program counter in DOS's memory, and G and `op_path` smeared with a
repeating `02 04`.  At 896 every gate passes.  Both directions
reproduce, and the change is one line of the Makefile.

**And the low-water mark at 896 says 302 bytes.**  Nothing in the run
ever went deeper than 302, so "the stack was too small" does not explain
a failure at 640.  What else moves with `DESK_STACK` is where `data`
lands after it -- the sections go `zdata`, `stack`, `data` -- so this
has the shape of the layout lottery `docs/phase24.md` describes, where
a wild write's victim depends on the layout and a size change moves it.

The far allocator's straddle was exactly that, and was found by pushing
on it until it moved somewhere legible.  This one has not been pushed
on yet.  **896 is a measurement, not an understanding**, and the next
person to touch the desktop's memory should know it.
