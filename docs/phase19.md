# Phase 19 — Show info, which is also the rename

The desktop's File menu had `Show info...` in it from the first
milestone, disabled with the rest of `NOT_YET_ITEMS` so the menu would
keep its shape.  This is the milestone that enables it, and with it the
desktop's **rename** — because in GEM a rename is not a menu item.  It
is the name field of the information dialog, and what you leave in it is
what the item is called afterwards.

    make test-m19    PASS   15 screens, G byte for byte at 8 of them,
                            549 calls on both sides, and the disk read
                            back afterwards: OUT.TXT is OUT.DAT

## The dialog

`ADFINFO` in `tools/deskrsc.py`, the donor's `ADFFINFO` (`desk_rsc.c`,
and `deskinf.c`'s `inf_file_folder` for what fills it) with two
departures:

- **The date and the time share one field.**  The donor has `FFDATE`
  and `FFTIME`; ours has `Date: __-__-__  __:__` and ten digits, which
  the AES scatters into the places for us.  A field costs an object and
  an object costs the application pool.
- **There is no Skip button.**  The donor shows the dialog once per
  item of a multiple selection and offers Skip for the ones you did not
  mean; this desktop selects one item at a time, so there is nothing to
  skip.

The title is a free string — `STFIINFO` or `STFOINFO` — and is centred
when it is set rather than placed in the file, which is the donor's
`align_title` (`deskmain.c`): only `ob_x` moves, and the width the
resource gave the object is the longer title's, so nothing clips.  The
operation dialog's title now goes through the same function, which
incidentally centres `COPY FILE(S)` and `MOVE FILE(S)`, one character
left of where they used to sit.

Everything a person reads is still in `DESKTOP.RSC`, including the
alert for a rename the DOS refuses (`STRENAME`).

## What the fields say

For a **file**: the size out of the listing, the stamp out of the
listing, the name editable, and the read-only bit as a pair of radio
buttons — the AES turns the other one off by walking the parent box's
children (`src/aes/form.c`, `fm_button`), which is what the `G_IBOX`
around them is for.  `Fattrib(path, 1, attr)` is the DOS's lock, and
like the ST we do not ask what it thought of it.

For a **folder**: the counts and the size come from the same walk a
delete runs first — `walk(0, OP_COUNT)`, a DTA per level — with one
line added to it, `G.g_opsize += dta->d_length`, so a folder is as big
as what it holds.  The file counts are disabled for a file and the
attribute buttons for a folder, each blanking what the other uses.

## ⚠ A folder cannot be renamed, and the DOS is why

The first version let the name be edited whichever kind of item it was.
The gate stalled in `form_alert`: `Frename` had failed and the desktop
was showing `STRENAME`.

Measured rather than guessed — a case added to `tests/emu/m15_gdos.py`,
which drives GEMDOS calls directly:

    Frename of a folder: -33; the root now has NEWDIR   (SpartaDOS 3.2)
    Frename of a folder: -33; the root now has NEWDIR   (SpartaDOS X)

`-33` is `EFILNF`.  Our `Frename` is CIO's `XIO 32` (`src/sys/gemdos.c`,
`gd_rename`), and **XIO 32 renames a file**; asked for a directory
entry it answers "file not found".  Both SpartaDOS variants agree, and
DOS 2 has no folders at all — so on every DOS gem4xe supports today, a
folder's name is not the desktop's to change.

So the field is **shown and not editable** for a folder, the way the
attribute buttons are, rather than editable and always refused.  One
flag, cleared in `fun_info`; the day a DOS can do it, it goes back.
A dialog that lets you type a name it will never use is a worse lie
than one that shows you the name greyed.

## The comparison a rename is made against

Not the entry's name: **what the field started as**.  `fmt_name` puts
"ONE.TXT" into the eleven places `________.___` scatters, and eleven
places cannot hold every name a file system can — the donor's own
examples include `TESTWINDOW.C` becoming `TESTWINDC`.  Comparing the
field against the FNODE would then see a difference nobody typed and
rename the file to the truncation.  So `fun_info` unformats the places
once, before the dialog, and compares what comes back with that.

`fmt_name`/`unfmt_name` are the donor's `fmt_str`/`unfmt_str`
(`util/optimize.c`), checked against its own documented examples, with
one difference: ours drops blanks in the *extension* as well as the
name, and an extension of nothing but blanks takes its dot with it.
`fun_mkdir` had its own copy of the unformatting inline; it uses this
one now, so there is one statement of the rule.

A name with no extension is deliberately **not** padded out — "SUB"
stays three characters — because the places left over are where the
edit cursor sits when the dialog opens, ready to be typed into rather
than backspaced through.

## What the gate does

`tests/emu/m19_files.py` grew from thirteen screens to fifteen.  After
the copy it **fulls the window**, and that is not decoration: at the
window's opening size only two cells are inside the work area and both
of them hold folders.  A press on the second row belongs to the control
manager, not the desktop (the trap milestone 7 already recorded), so
until the window is the desk there is no file to ask about.

Then: `SUB` selected and `File -> Show info` — the folder form, the
counts the walk made, the name and the attributes both greyed, Cancel.
Then `OUT.TXT` — the file form, three BACKSPACEs over the extension and
`DAT` typed in its place, OK.  `Frename`, the window listed again, and
`OUT.DAT` in it.  The disk image afterwards is read with `tools/atr.py`
and asked the same question.

The model side is `tools/deskref.py`'s `fun_info`, and the two GEMDOS
calls it needed — `Fattrib` and `Frename` — went into `tools/aesref.py`'s
modelled DOS.  Its `Frename` refuses a name already taken and moves a
directory's listing with it; what a DOS does with a **locked** file it
does not model, so nothing may rename one until it does.

## Lessons

- **A stall in `form_alert` is an answer, not a mystery.**  The gate's
  post-mortem said which AES call the target was inside (52) and what
  its path buffer held (`A:\SUB`), which named the failing call before
  anything was instrumented.
- **When the DOS refuses, ask the DOS directly.**  The GEMDOS gate can
  make one call and print its result; two lines there settled in one
  run what would have been an afternoon of reasoning about SpartaDOS.
- **Do not compare against the truth when the user edited a copy of
  it.**  The rename comparison is between two unformattings, not
  between a field and a filename.
