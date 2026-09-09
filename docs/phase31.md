# Phase 31 -- size to fit

The View menu's last item, and the first thing in the desktop that
scrolls sideways.

## What it is

With **size to fit** on -- the default, and everything the desktop did
until now -- a window's columns are worked out from its own width, so
the items reflow every time it is resized and nothing ever needs to
scroll horizontally.  With it off, they are laid out for **the widest
window this screen could show** and the window scrolls over that grid.

The difference matters when the window is narrower than the desk: an
item then keeps its place while the window is resized, instead of
jumping to a different row and column each time.

    G.g_ifit     on or off
    G.g_icols    the columns a full-width window would hold, which is
                 the grid a window that does not size to fit is laid on
    w_vncol      the columns the LISTING occupies
    w_cvcol      the first of them the window shows
    w_pncol      the columns the WINDOW shows (unchanged)

With size to fit, `w_vncol == w_pncol` and `w_cvcol` is 0, so the whole
thing reduces to what was there before -- which is why the icon and text
views, the sorts and every existing gate were untouched by it.

`g_icols` is settled in `win_view()`, beside the item metrics, because it
is a property of the view and the screen and of nothing else: one
`wind_calc(WC_WORK, ...)` on the desk's rectangle per view change,
rather than one per window.

## What came with it

The window frame already carried `LFARROW | RTARROW | HSLIDE` and the
gadgets had never done anything.  They do now: `WA_LFLINE`, `WA_RTLINE`,
`WA_LFPAGE`, `WA_RTPAGE` and `WM_HSLID` all scroll the view sideways
through `win_hscroll`, which is `win_scroll` with the columns in place of
the rows.  `WF_HSLSIZ` and `WF_HSLIDE` are set from the same three
numbers the vertical pair use.

The setting outlives the machine: `DESKTOP.INF`'s `#E` line carries it in
the fifth byte as **`INF_E5_NOSIZE 0x10`** -- the donor's bit, and the
donor's sense, which is "do NOT size to fit", so a `#E` written before
this phase reads back as size to fit ON.

## The gate

`test-m17` grew three stops, placed where the window is **not** full,
because a full-width window is the one place the setting cannot show:

    with-fit         the icons as they were, the horizontal slider full
    no-fit           the same listing on the screen's grid instead of the
                     window's -- the slider is suddenly a short thumb,
                     because there is somewhere to go for the first time
    scrolled-right   the right arrow held: a column further along, SUB
                     gone off the left and DESKTOP.RSC arrived at the
                     right

525 pixels differ between the first two and 1,026 between the second and
third, so the gate is not passing because nothing changed.  G is compared
byte for byte at each of them, and G now carries `g_ifit`, `g_icols` and
the two new WNODE words -- 2,046 bytes against 2,026.

**One thing the gate taught, again:** an arrow gadget sends its
`WM_ARROWED` at the PRESS, once the double-click time has passed, so a
step that clicks one must press and HOLD and let the next step release
it.  Driving it as a complete click leaves the release unconsumed and the
model says so -- `record 0 (op 1025) returned with 2 plan step(s)
unused`, which is a better error than a picture that differs.

## Still greyed out

`Set preferences`, `Install icon` and `Install application` remain in
`NOT_YET`.  A resolution picker belongs in the first of them, and belongs
after the ANTIC driver, which is what will give it a second resolution to
pick.
