# Phase 8 — windows, the control manager, menus

Status: **complete.** `make test-m8` 12/12 and `make test-m9` 4/4, with
the rest of the suite unchanged after them — m3 61/61 (six cases added for
the raster copy into and out of a VRAM form), m4 12/12, m7 10/10,
`make demo` pixel-exact, m5, m6, `check-cc`, and 29 host tests.

Phase 7 ended with "next is the window manager, where the two standing
warnings come due". This phase built it in three steps — the window
manager itself, the control manager that turns a press on a frame into a
message, and the menu library — with the save buffer and a VRAM-form
raster copy underneath the third. Along the way a mouSTer back end was
added to the pointer layer, from a sample driver rather than hardware.

Everything here is compared, call for call and pixel for pixel, against
`tools/aesref.py`, which now models the window tree, the rectangle lists,
the message queue, the control manager's ownership of the mouse and the
menu state machine; and the harness gained the ability to screenshot the
target *inside* a wait, which is the only place a drop-down can be seen.

## Step 1 — the window manager (`src/aes/wind.c`)

The donor's `gemwmlib.c` and `gemwrect.c`, single-tasking: eight window
slots, the window tree `W_TREE` whose root is the desktop and whose
children are the open windows in stacking order, and one 19-object
`W_ACTIVE` frame that `w_bldactive` lays out afresh for whichever window
is being drawn. The ROM's sizes — `NUM_WIN 8`, `NUM_ORECT 80`, a
16-message queue — and its rules where EmuTOS differs from it: the
coalescing of queued messages (a `WM_REDRAW` for a window that already
has one is unioned into it; a `WM_ARROWED` replaces the one waiting),
and no `WM_UNTOPPED`.

**Dirty rectangles are the whole design, not a refinement.** Every window
keeps the list of the rectangles of it that are visible — `newrect`
splits its area around every window above it — and both sides draw only
inside the pieces: the AES through `do_walk`, which draws the frame once
per piece with the clip set to it, and the application by walking
`wind_get(WF_FIRSTXYWH/NEXTXYWH)` in its `WM_REDRAW` handler. The Phase 1
measurement said the blitter moves about one screen a frame; a change to
one window costs its area, never the screen's.

**A move is a blit, and window x snaps to even.** `w_move` copies the
window across the screen with `vro_cpyfm` when it stays inside it, and
the blitter has no shifter, so that copy is one control block only when
source and destination x share parity. `w_snap` rounds a window's x down
and its width up to even in `wm_open` and `wind_set(WF_CXYWH)`; the
application reads the snapped rectangle back and draws to it, and a move
by an odd amount lands a pixel to the left. The first version snapped x
and compensated the width, which made every odd move look like a resize
to `draw_change` — redrawn, never blitted — and the case that checks the
old frame is gone caught it, not the eye.

**The reference and the target shared a bug.** The blitter copy of an
overlapping rectangle towards higher addresses read rows it had already
written, and the m3 overlap case *passed*, because `vbxeref.Surface`
copied the same way. Found by reading, while designing the move; fixed on
both sides as a backwards copy (`blit_move` / `Surface.move`), and the
case now pins a correct copy. A model that was written from the same
understanding as the code will agree with the code's mistakes — which is
why the model is written from the donor and the hardware manual, and
why "the gate is green" is where a reading starts rather than ends.

Two donor defects that a single-tasking port could hit: `ob_order` on an
only child leaves the tree headless (guarded with `ob_add`), and the
ORECT pool can run out under enough overlapping windows (`mkpiece` fails
rather than dereferencing null).

## Step 2 — the control manager (`src/aes/ctrl.c`, `src/aes/event.c`)

GEM's control manager is a second process that owns the mouse whenever a
press lands outside the application's control rectangle — the top
window's work area, or the whole screen while `form_do` runs — and the
application's waits see nothing of the button, the keyboard or the
pointer until the button is up and the mouse is handed back. gem4xe runs
one process and polls, so the control manager is a call, `ct_run`, made
from the poll that saw the press: it runs the gadget to completion,
nesting its own waits inside the application's, and any message it sends
is in the queue when the application's wait looks again. The ownership
rules are the donor's exactly — the press decides the owner, the
application's checks are gated while it is not the owner, the hand-back
posts the button state to a waiter as `set_mown` does — and where EmuTOS
and the ROM differ, the ROM is followed: the mouse is held until the
button is up, and no `WM_UNTOPPED` is sent.

`hctl_window` is the donor's non-3D path: the closer and the fuller
tracked with `graf_watchbox`, the mover's outline dragged with
`gr_dragbox`, the sizer's rubbered with `gr_rubwind`, an elevator dragged
with `gr_slidebox`, then one message — `WM_MOVED` carries the position,
the application's `wind_set` does the moving. The arrows repeat: one
`WM_ARROWED` at the press and, once the double-click time has passed with
the button still down, one more with every poll, the queue holding one
at a time so a slow application is not buried.

**Styled lines became a blit path.** The outlines the control manager
drags are `vsl_udsty` dots, and plotting a 300×150 box pixel by pixel
through the MEMAC window — a read and a write on the 1.79 MHz bus for
each of 900 dots — took milliseconds to show and again to erase, enough
to push a `WM_MOVED` past the frame it was due in and fail the case. A
styled horizontal or vertical line is now a pattern blit: the style
anchored to the line's first point, as the Bresenham path and the
reference define it, is a screen-anchored word rotated by the start
position, so a horizontal line is one expanded row and a vertical line a
column of bytes replicated by the blitter — and clipping afterwards moves
nothing. Four m3 cases pin the anchoring in every direction and mode.

**The pointer boots at (0, 0), which is in the menu bar.** The
ROM-faithful `chk_ctrl` gives a press there to the control manager
whether or not a menu is installed, and two Phase 7 cases stopped
completing the moment the ownership rules went in — the application's
`evnt_button` never saw the press. The cases now move the pointer first,
with a comment saying why.

**The runner's script buffer.** One m8 case grew past `SCRIPT_WORDS`,
and the target appeared to block at its last op: the runner stops at the
end of its buffer mid-script, and the words past it land on whatever
follows. The harnesses now assert a case fits before loading it, from
the linker's placement of the buffer and the symbol after it.

## Step 3 — the save buffer, the VRAM-form copy, menus

**`vro_cpyfm` between forms.** The AES's `bb_save`/`bb_restore` copy a
rectangle of the screen to a save buffer and back, and until now the VDI's
raster copy knew only the screen. An MFDB with `fd_addr` 0 is the screen
(the VDI's own convention); any other names a VRAM form whose rows are
`fd_wdwidth` words × `fd_nplanes` apart, and the save form at `VR_SAVE` is
laid out like the screen, so a save is a same-coordinates copy — one blit
at even x, the pixel path at odd. The destination is clipped to the
workstation rectangle and the screen when it is the screen, to the form's
bounds otherwise, and the source loses the same span, as the donor's
`do_clip` has it. Then **a deliberate departure**: the source is clipped
to *its* form's bounds and the destination loses that span. The donor
never clips a source and reads whatever lies past the edge — on the ST
the next row; here, past the end of a form, someone else's VRAM. This is
what makes the Atari corpus's `menu_sr` landmine (a drop-down off the
screen edge saves and restores memory outside the framebuffer) cost a
stripe left unrestored instead of a crash.

**The save buffer is a screen, not 25 columns.** The ROM's
`gsx_malloc` saves 25 character columns by the full screen height, and a
drop-down wider than that overflows it; VRAM has six screens spare, so
`vdi_save_form` hands the AES a whole one. The width limit is not
inherited.

**The menu library (`src/aes/menu.c`)** is the donor's `gemmnlib.c`
without the submenu extension: the RCS-shaped tree the AES trusts — the
bar, `THEACTIVE` with the titles in order, the drop-down box with the
drop-downs in the same order — `menu_sub` walking as many siblings as the
title is titles in (titles must be contiguous; items are found by link
and may be anywhere), `menu_fixup` rebuilding the Desk menu's children
for the accessories (none: the one "About" item stays, the separator
goes), `mn_bar` stretching the bar to the screen and drawing the line
under it with the clip off, and `mn_do`, the state machine that pulls
each drop-down as the pointer crosses its title, saving what is under it
and putting it back, and leaves on a button transition off a title. A
disabled title does not drop and is not remembered as the current one;
going down from it to where no drop-down is ends the menu with nothing.
`menu_icheck`, `menu_ienable`, `menu_tnormal` and `menu_text` are
`do_chg` on the tree; `menu_register` returns −1.

**How the menu is entered, and one decision against the donor.** In the
ROM the menu is the control manager's other entry: the pointer arriving
in the active bar with the buttons up runs it (the tail of `mchange`).
Here `ct_poll` does the same, taking ownership and running `mn_do`
nested in the application's wait, with `gl_ctmown` set so that presses
during it do not change the owner. `mn_do` returns with the button as
the transition that ended it left it — on an item, *down*: `MN_SELECTED`
is sent at the press, as GEM does, and the title stays selected until the
application's `menu_tnormal`. The donor's control manager then waits
again at once and, the button being down, dispatches it as a fresh press
— `WM_TOPPED` to a window under the item, or a gadget's drag started with
the button already held. gem4xe instead holds the mouse until the button
is up and hands it back, which completes an application's wait for the
release exactly as the hand-back after a gadget does. Documented in
`ct_poll` on both sides; the re-dispatch is not done.

## The harness: screenshots inside a wait

A drop-down is up only while `mn_do` runs, inside the application's
`evnt_multi`, and the final screen of a case shows it restored. So the
input plan gained a step: `("shot", fn)` runs no frame, the harness
screenshots the target where the reference keeps a copy of its own screen
(`AES.shots`), and the pairs are compared after the case like the final
screen. The four m9 cases take ten of them: File down with its disabled
separator, View across with the check mark, View still down with the
pointer off the bar, the Desk menu with its one item, an item selected,
`menu_text`'s new string in place, a disabled title with the last
drop-down put back.

One more timing rule joined Phase 7's list. A wait that begins in the
frame that ended the wait before it — the release completing one
`evnt_multi` and the next entered before the frame is out — counts the
frame's end as a tick on the target, where the reference's entry poll
does not. The case that failed had a move planned between the timer's
tick and the end of its plan; the rule is that a plan ends with more
frames than the timer needs, so the tick lands in that last step on
either count and the frames after it are dropped on both sides.

## The mouSTer XEM1 back end (`src/vdi/pointer.c`)

A mouSTer in XEM1 mode presents a 7-bit position counter per axis on the
port's two POT lines, sent as 64..191 so that exactly one of bits 6 and 7
is set — an empty port or a paddle at either end fails that, which is how
the device is found, both ports tried until a pair validates. The left
button is the trigger, right and middle are the port's upper two
direction lines, and its lower two carry the wheel as plain quadrature.
The decode follows the sample driver: a reading counts only if both axes
validate, the difference from the last *used* reading is taken modulo 128
and halved, and a reading that halves to nothing does not become the new
reference, so a slow drag of one count a frame is not lost but arrives
every other frame. Altirra does not emulate the device, so this is
verified in `tests/host/test_pointer.py` against a simulated counter —
including a run that was checked to fail on a deliberately wrong decode
before the right one went in — and awaits hardware. The wheel is read
and not yet delivered as `WM_ARROWED`.

## What the gate says

    make test-m3    61/61   VDI, now with the VRAM-form copy and clip both ways
    make test-m4    12/12   AES: draw, find, change, edit, centre
    make test-m7    10/10   evnt_*, form_do, form_dial, graf_watchbox
    make test-m8    12/12   windows: lists, moves, WM_REDRAW, the gadgets, the arrows
    make test-m9     4/4    menus: the bar, the calls, hover, select, disabled
    make check-cc   PASSED  every workaround shape is right; 7 of 7 bug shapes still present
    make demo       pixel-exact
    make test-host  29      the pointer layer with XEM1, the .xex staging, the patterns

## Lessons

- **A model written from the code agrees with the code.** The overlap
  copy was wrong on both sides and green. The reference is written from
  the donor and the manuals for that reason, and a green gate is where a
  reading of the code starts.
- **The frame budget is a correctness gate, not a performance note.** The
  dotted outline was right and slow, and slow moved a message past the
  frame the case expected it in. Every path the AES draws in a loop —
  text, fills, now lines — has had to become a blit before it passed.
- **Follow the ROM where EmuTOS improves on it, and say so.** The
  `WM_UNTOPPED`, the held mouse, the queue's union rule, the pending-clicks
  semaphore: each is a place the two differ, and an application written
  for TOS expects the ROM. The one departure taken on purpose — no
  re-dispatch of the press that chose a menu item — is in the code's
  comment on both sides so it is not mistaken for an oversight.
- **Name the timing difference before working around it.** The extra
  tick at a wait's entry is now a rule the plans follow, rather than a
  margin someone added to one case.

## Next

`form_alert`, and the bell on a click outside a form; `graf_mouse`, after
which `ct_mouse` should swap the arrow form as the donor does; the wheel
as `WM_ARROWED`; `G_ICON`, `G_USERDEF` and `G_CICON`. Still overdue: the
native-mode interrupt vectors, on which a quadrature mouse, the blitter
IRQ, a VBI that stops the input loop losing a tick, and returning to DOS
(with the `$0000-$3FFF` write-back) all wait. `wm_set(WF_VSLIDE/HSLIDE)`
on a top window that lacks the bar still indexes `tree[NIL]`. With the
menu bar in, the desktop can begin.
