# Phase 4 — the AES object library

Status: **gate green.** `make test-m4` — 4/4, pixel-exact drawing *and*
hit-test agreement.

`objc_draw` is the centre of the AES: dialogs, menus, the desktop and window
contents are all object trees drawn by it, so it is the piece worth getting
exactly right before anything is built on top.

## What exists

`src/aes/aes.h` + `src/aes/objc.c`: the GEM `OBJECT` (24 bytes, byte-for-byte
as a `.RSC` contains it), `gr_crack`, `ob_offset`, `objc_draw` and `objc_find`.
Object types G_BOX, G_IBOX, G_BOXCHAR, G_BUTTON, G_STRING, G_TITLE and the
G_TEXT family; states SELECTED, DISABLED, OUTLINED, SHADOWED, CHECKED.

**All drawing goes through the VDI** by filling the parameter block and calling
`vdi()`, exactly as an application would. The AES has no private path to the
screen, which is what keeps the device seam honest.

## Two things I had wrong, both caught by the gate

**1. `ob_spec`'s byte order.** I assumed colour in the high word and thickness
in the low byte. It is the other way round, and the reason is that GEM reads
the field as *bytes of a 68000 LONG*
(EmuTOS `aes/gemobjop.c` `ob_sst`: `th = *(((char *)pspec)+1)` and
`return *(char *)pspec`, with `gr_crack` taking the low word):

    bits 31-24   character   (G_BOXCHAR only)
    bits 23-16   thickness   (signed byte)
    bits 15-0    colour word

Getting this backwards makes every box the wrong colour with the wrong border —
and it would have made every real `.RSC` unreadable.

**2. A button's thickness is COMPUTED, never stored.** `ob_sst` starts at 0,
then `th--`, one more for `EXIT`, one more for `DEFAULT`. So a plain button is
-1, an exit button -2, and a default exit button -3. Negative thickness grows
the border **outward**, which is precisely why GEM's default button wears a
visibly thicker ring than its neighbours. `G_TITLE` likewise gets a fixed
thickness of 1.

I would have shipped a plausible-looking dialog with subtly wrong geometry in
both cases. Checking EmuTOS instead of trusting memory is what caught them.

## The colour word

    bits 15-12  border colour     bits 11-8   text colour
    bit 7       writing mode      bits 6-4    fill pattern (3 bits)
    bits 3-0    inside colour

(EmuTOS `aes/gemgraf.c` `gr_crack`.) Fill patterns are decoded but not yet
honoured — the VDI has no `vsf_udpat` — so `DISABLED` currently stipples with a
dotted line grid rather than GEM's 50% hatch. It reads as "unavailable" and
does not pretend to be the real thing; replace it when pattern fill lands.

## Trees are right-threaded

There is no parent pointer: the last child's `ob_next` points back at the
parent. So `ob_offset` finds a parent by scanning, exactly as the AES does.
That is also why the plan's warning from flashjazzcat matters — he abandoned
this structure on a 6502 for code-size reasons. It is kept here because the
object tree **is** the resource-file format, and gem4xe's hardware is far less
constrained; but `objc_draw`/`objc_find` are the two routines to profile first
if the AES ever feels slow.

## ⚠ Bank $00 code space is now the binding constraint

The AES did not fit. Bank $00 offers ~12 KB of code between `$3000-$3FFF` and
`$A000-$BFFD`, and the object library alone overran it.

As a **scaffold**, the conformance runner now also uses `$4000-$73FF` for code,
which takes it to ~28 KB. That region belongs to U1MB's banking window, so the
*shipping driver* still may not use it — this is a test-harness expedient, and
`src/gem4xe.scm` says so at the point of use.

**The real answer is the one the plan named from the start:**
`--code-model=large`, code in Rapidus banks `$01+`, copied up at load time
because a `.xex` loader cannot place anything outside bank `$00`. There is
14.5 MB waiting there. This is now the next piece of infrastructure due, ahead
of the rest of the AES.

## Native-mode interrupts: answered

The question flagged since Phase 0 is settled. From `rapidus.cpp:955-983`:
with the OS ROM disabled (PORTB bit 0 = 0) and `MCR` bit 3 clear, **`$C000-$FFFF`
becomes writable Rapidus SRAM**, and `MCR` bit 6 fragments the window so
`$D000-$D7FF` stays hardware.

So the recipe is: with write-through on, copy ROM→SRAM through the still-mapped
ROM; switch the window in; patch native-mode vectors at `$FFEA`/`$FFEE`. That
unblocks the blitter IRQ, returning to DOS, and quadrature mice.

It is **not** needed for the AES itself — a polled event loop is fine — so it
stays deferred, but it is now a known procedure rather than an open risk.
*(Built in Phase 9, essentially as described here; `docs/phase9.md`.)*

## Next

`form_do` and the dialog event loop, then the window manager — where the two
measured constraints bind: dirty rectangles are mandatory (a full-screen copy
costs 0.88 of a frame), and window x should snap to even pixels (the blitter
has no shifter).
