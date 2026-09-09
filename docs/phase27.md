# Phase 27 — the View menu: as text, and in an order

The View menu has had two items since the resource was written and only
one of them did anything.  `Show as text` now does the other: the same
FNODEs the icon grid draws, one line each, with what a directory entry
knows about a file laid out in columns.

    make test-m17   View -> Show as text, the lines drawn and compared
                    pixel for pixel, then View -> Show as icons back

## The line is the resource's, not the C's

The donor builds its line in C (`deskinf.c format_fnode`) with `sprintf`
and a screen-width test, and picks the date order from the country the
ROM was built for.  gem4xe cannot: `docs/shipping.md` says no string a
reader sees may live in the C, because a translation is a resource and
nothing else.

So `STFLINE` is a **template**, and `win_line` fills it:

    fnnnnnnnnneee sssssss dd/mm/yy HH:MM

Each run of a placeholder letter takes one field, in the template's own
order and at the template's own width -- `f` the folder/read-only mark
(one of three characters, `STFMARK`), `n` and `e` the name and its
extension, `s` the size right-aligned, then `d m y H M`.  Anything else
is copied as it stands.  A run shorter than its field truncates it, a
longer one pads it, and **the C is never told what the layout is**: a
translation may move the columns, change `/` to `.`, put the year first
or leave the time out, and nothing recompiles.  It is the same
principle as `inf_dttm`'s ten places (`docs/phase19.md`), one step
further along: there the order was in the C and the separators in the
resource; here both are the resource's.

The layout it ships with is the donor's NARROW one -- 36 places -- and
not the wide one the donor would choose for a 640-pixel screen.  The
donor asks how wide the SCREEN is; what a line has to fit is a WINDOW.
A window opens 38 characters wide (304 pixels), less 1 for the border
and 12 for the slider's, less the view's own 16-pixel margin: 275
pixels, thirty-four characters.  So a window as it opens shows all but
the last two places, and the minute's second digit arrives when the
window is fulled.  That is the donor's behaviour too, exactly -- its
default window is the same 38 characters (`deskapp.c desk_inf_data1`)
and its narrow line the same 36 -- so an ST clips it in the same place.

## One departure, and it is the hardware's

The donor sets a text line's left margin to `2*gl_wchar - 1`, an odd
number, and says why: it "aligns the text on a byte boundary, allowing
the fast text output routine to be used".  On a 4bpp chunky surface that
arithmetic inverts -- two pixels to a byte, so an ODD x is the
pre-shifted strip and the SLOW path (`docs/phase2b.md`).  The margin
here is `2*gl_wchar`, even, for the donor's own reason applied to this
machine's pixels.

## Where a line lives

`ob_spec` is a **near** pointer -- `src/aes/objc.c` truncates it to 16
bits -- so a line cannot live out in the far bank beside the FNODE it is
made from.  It goes in the item's `SCREENINFO`, which the icon view uses
for an `ICONBLK` and a label: 34 bytes and 13, and a line is 48, so the
two became a union and the store grew by one byte an item.

That union brought a trap worth naming.  The two halves are different
lengths and a slot is reused in both views, so whatever a slot holds
past its own text is what the LAST item to use it left there -- and G is
compared with the model byte for byte.  Residue would have had to be
*modelled*.  Both builders empty the slot first (`obj_clear`), which
costs sixteen 48-byte clears a redraw and makes the question disappear.

## What a change of view does

`desk_view` moves the menu's checkmark and works out the item metrics
again (`win_view`); then every open window is built again and only then
are any of them drawn -- the donor's `win_bdall` before `win_shwall`, so
that no window stands in the new view beside one still in the old.

The view is saved: the INF gains the donor's environment line,

    #E 80 00

whose first byte is `INF_E1_VIEWTEXT` in bit 7 (`deskapp.c`).  The
second is the donor's date and clock formats, which are the resource's
here, so it goes out as zero and is not read back.  A desktop that was
in text view when it was saved comes back in text view -- through
`Save desktop`, through `Read .INF`, and through the shell buffer when
a program is run and the desktop returns.

## ...and in an order

The other five items of the menu are the order a listing is in:
`S_NAME`, `S_TYPE`, `S_SIZE`, `S_DATE`, `S_NSRT`, which are the donor's
values because they are the items' own numbers less `NAMEITEM`
(`deskfpd.h`).  `pn_fcomp` is the donor's, rule for rule:

- **date** and **size** run the other way round -- newest and biggest
  first -- because that is what someone sorting by either wants at the
  top;
- **type** compares from the dot;
- every order falls back to the **name**, which is the whole of
  `S_NAME` and the tie-break for the rest, so a listing never depends on
  the order the directory happened to be in;
- **folders come first in every order but `No sort`**, where the
  directory's own order is the whole of it.

`No sort` is why an FNODE gained `f_seq`.  The listing is built sorted,
by insertion as it is read, so the directory's order is gone by the
time anyone asks for it back -- unless each entry remembers where it
was.  Two bytes in far memory buys that, and a change of order then
costs no directory read at all: `win_srtall` runs the same insertion
sort over the FNODEs where they already stand (`pn_sort`, the donor's).
`do_viewmenu` sorts every window, then builds every one, then draws
any -- `win_srtall`, `win_bdall`, `win_shwall`, in the donor's order.

The INF's environment line grew to the donor's five bytes for this.  The
sort is two bits of the first (`INF_E1_SORTMASK`), which holds four
values and there are five; the donor puts the fifth -- "no sort" -- in
bit 7 of the FIFTH byte (`INF_E5_NOSORT`), so that is where it goes
here, with the three bytes between written as zero and read past:

    #E 20 00 00 00 00      sort by size, icons
    #E 80 00 00 00 80      text view, no sort

Every bit is where the donor puts it, which is worth the nine extra
characters: an INF written by a real desktop sets gem4xe's order
correctly, and the other way round.

## The storm again, and the last of the dodging

Adding all this moved the desktop's code by about a kilobyte, and
`make test` on the stock emulator went red in **m18** -- a gate that had
been green through the whole of phase 26, while m19, which had been the
red one, went green.  Nothing about the desktop's memory changed
between those two runs except where the code sat.

That is the phase-26 bug (`docs/phase26.md`), and it is the end of the
argument for keeping 256 bytes of slack to avoid it: the slack bought
one build's luck and this phase spent it.  `DESK_BSS` goes back to the
measured 2944 and `DESK_STACK` stays 640, the gates run against a
patched emulator, and what the tree keeps instead is a **diagnosis** --
`storm_check` in `tests/emu/m7_form.py`, called by every desktop gate
when it stalls, gated by `tests/host/test_storm.py`.  A future stall
that is the emulator's now says so in one line.

At the numbers as they now stand the suite happens to be green on the
unpatched emulator as well -- 49 gates, both ways round.  **That is
luck and not a property**, and saying so is the whole point of the
paragraph above: the next thing that moves the desktop's code re-rolls
it, and the gate will name what happened instead of leaving it to be
found again.

## Standing

`ICONITEM`, `TEXTITEM` and the five orders are out of `NOT_YET`.  What
is left in it from this menu is `Size to fit`, which is not a view or an
order but a column width, and belongs with whatever does window sizing
next.
