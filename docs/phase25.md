# Phase 25 — the desktop remembers

`Save desktop...` and `Read .INF file...` were the last two items of the
Options menu still greyed out, and between them they are the difference
between a demo and a desktop: until now the layout survived running a
program and did not survive switching the machine off.

    make test-m19    seventeen screens; DESKTOP.INF read back off the
                     image, and read back by the desktop itself

## Most of it was already there

The desktop has carried its windows between programs since milestone 6:
it writes `#R 02` and a `#W` line per window slot -- the view, the place
in character cells, the path -- into the AES's **shell buffer** when it
exits, and reads them back when the shell loads it again.  The format,
the writer, the parser and the slots are the donor's and were all
working.

What was missing was a **file**.  So:

    inf_name()    "A:\DESKTOP.INF" with the boot drive's letter in it,
                  the donor's INF_FILE_NAME trick (deskapp.c), so that a
                  desktop which has walked off somewhere still writes
                  where the next boot will look
    inf_load()    the file into the shell buffer
    inf_store()   the buffer out to the file -- the text only, without
                  the NUL or the copy/paste bytes in front of it, so
                  what lands on the disk is what a person could read
    inf_parse()   the "#W" lines out of app_start(), so the menu can
                  use the same parser

and the three lines that matter, in `app_start`:

    if (G.g_shelbuf[CPDATA_LEN] != '#' && !inf_load())
        build_inf();

which is the donor's order: what the shell buffer holds -- how a
desktop that has just run a program gets its windows back -- then the
file, which is how it gets them back after the machine has been off,
and then the built-in default.

`Save desktop` is `cnx_put()` (the open windows into the slots) then
`inf_write()` then `inf_store()`.  `Read .INF file` is the reverse and
one thing more: what is open is **closed first**, so that what comes up
is what was saved rather than what was saved on top of what was there.

## What the gate does

`test-m19` grew two stops.  After the delete: `Options -> Save desktop`,
then `Options -> Read .INF file`.  The first writes the file and the
gate reads it back **off the disk image** with `tools/atr.py` -- 121
bytes, five lines, starting `#R 02`, four `#W` lines, and one of them
naming `A:\*.*`, the window that was open.  The second closes the
windows and opens them again from the file, and the screen after it is
compared with the model like every other.

That covers both directions.  The **failure** path of the start-up read
was already covered by every desktop gate the moment this went in: none
of their disks has a `DESKTOP.INF`, so each one now opens the file,
fails, and falls back to the default -- two more calls at start-up
(`Dgetdrv`, `Fopen`), matched on both sides.

## A limit of the model, written down

`tools/aesref.py`'s GEMDOS keeps a file's **length**, not its bytes.  So
when the model reads `DESKTOP.INF` back, what it parses is what is still
in its own buffer -- which is the same text, because the desktop wrote
it there moments before.  Model and target agree on every call and every
length, and the file's actual content is checked by the gate reading the
image.

The case that would catch a real divergence is a disk that already has a
`DESKTOP.INF` when the desktop starts -- the model would have nothing in
its buffer to parse.  No gate boots such a disk today.  Written here
rather than papered over.

## Debts

- **Nothing boots a disk that has a saved layout.**  The save and the
  read are both gated, and the start-up read's failure path is gated
  everywhere; the start-up read's SUCCESS path is not.  It is the same
  `inf_load()` the menu item uses, which is gated -- but "the machine
  came up with yesterday's windows" is not yet a thing a gate has seen.
- **Only the windows are saved.**  The donor's INF also carries the
  desk's icons, the view preferences and the application
  associations -- `#M`, `#T`, `#F`, `#G` lines -- and those are the
  menu items still greyed out.  The format already ignores lines it
  does not know, so they can be added without breaking a file written
  today.
