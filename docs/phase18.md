# Phase 18 — files, moved by hand

The desktop could make a folder and delete a tree.  It could not copy
anything, which is the first thing anyone tries.

    make test-m19   PASS   New folder, a tree DRAGGED into it, and Delete

## The drag

A press on an item in a window, with the button still down, is a drag.
`graf_dragbox` -- the call the control manager already moves a window
with -- drags the outline and answers where the button came up; where
the POINTER came up is what says where it went, so the state is asked
for again afterwards (the box is snapped to the grid the item came from
and is not where the hand is).

Where it lands decides what happens.  Another window, or a folder in
one, is a **copy**; the same drag with SHIFT held is a **move**; the
trash is a delete, which is the delete that was already there.  Dropped
in its own window on nothing, or on itself, it is the click it started
as.

**SHIFT, and not CONTROL, and here is why.**  Newer GEM moves with
CONTROL held (EmuTOS's `deskfun.c`: `op = (keystate & MODE_CTRL) ?
OP_MOVE : OP_COPY`), and this machine cannot answer the question.
POKEY reports the SHIFT key on a line of its own -- `SKSTAT` bit 3,
live, whether or not another key is down -- and reports CONTROL only in
bit 7 of the code of a key that is *being held*.  A hand holding CONTROL
and nothing else is invisible to the hardware.  So the modifier is the
one the machine can be asked about while the mouse button is down, and
the Atari key is no better off than CONTROL.

## The operation

`fun_file2any` is `fun_del`'s shape, because it is the same three
passes: count what is selected, ask in a dialog, then do it with the
counters ticking down.  The walk carries an operation now --
`OP_COUNT`, `OP_DELETE`, `OP_COPY`, `OP_MOVE` -- and a second path
beside `op_path`, going down and coming back up in step with it.  A
folder is created BEFORE its contents are copied and deleted AFTER
they are (a move), which is the only asymmetry.

A move is a copy and a delete.  Neither DOS here renames a file into
another directory -- SpartaDOS's RENAME changes a name in place, and
DOS 2 has no directories to move between -- and a drag between two
floppies is not one drive anyway.

A file goes through a 1 KB buffer in the arena.  GEMDOS takes a FAR
buffer and shuttles it through a slice of the pool (`gd_xfer`), so the
desktop's near memory, which is its scarcest, pays nothing for it.  A
copy that stops half way deletes what it wrote: a truncated file looks
like a whole one to everything that reads it afterwards.

## One dialog, three titles

The donor has a dialog per operation.  This has one, and which operation
it is comes from a free string of `DESKTOP.RSC` -- `DELETE FILE(S)`,
`COPY FILE(S)`, `MOVE FILE(S)` -- set into the tree's title before the
dialog goes up and put back afterwards.  The rule that no string a
person reads may be in the C (`docs/shipping.md`) is what shapes that:
a title written in the C could not be translated.

## What the gate does

`make test-m19` drags the `SUB` tree onto the `NEWDIR` folder it made a
moment earlier, says OK to the copy, and then deletes the original --
so one run covers a folder copied recursively (a folder inside a folder,
three files), the dialog wearing its copy title, and the delete that was
already there.  414 ABI calls on both sides, eleven screens, `G` at six
probes, and the image read back afterwards: `NEWDIR\SUB\` holds
`ONE.TXT` and `TWO.DAT` at four bytes each and `DEEP\THREE.TXT` at six.

Two things the gate taught, both about the machine rather than the code:

* **A press is delivered when the pointer moves**, not when the button
  is released.  The AES holds a press for the double-click interval to
  see whether a second one is coming, and a movement of more than two
  pixels flushes that wait at once (`mchange`).  That is what makes a
  drag possible at all, and the gate's press-and-nudge is what a hand
  does.
* **The second row of a window's icons hangs below the work area.**  A
  press there belongs to the control manager, not to the desktop, and
  never arrives.  The gate drags an item from the first row.

## Not gated yet: the move

The move is written on both sides and takes the same path as the copy;
what is missing is a way to hold SHIFT in the harness.  `KEY` taps a key
for a frame and `KEYRAW` (`tools/altirra/`) holds one, but neither holds
a MODIFIER on its own -- `shift` is not a key name in either.  Holding
some other key with it would put a keystroke in the AES's queue and end
the drag.  The next step is a standalone `shift`/`ctrl` name in that
patch, and then the gate is the copy's with one step changed.
