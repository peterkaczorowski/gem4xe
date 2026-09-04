# Phase 12 — alerts, icons and the pointer's shape

Status: **complete, in Altirra.** `make test-m13` PASS — nineteen cases:
every pointer form the AES owns, the caller's own, the commands that hide,
show, save and restore it, and six alerts compared in the button they
return, the pixels they draw, and the pool they give back. `make test-m4`
gained an icon case and is 13/13. Nothing here has run on a Rapidus or a
VBXE.

Three things an application asks the AES for that come out of the AES's
own pocket: a message box, an icon, and a pointer that says "wait".

## The artwork, in one place (`tools/gemdata.py`)

Eight mouse forms and three 32x32 alert icons are not anything a caller
supplies -- they are the same shapes on every GEM machine. They are
transcribed from the donor (EmuTOS `aes/mforms.c` and `aes/gem_rsc.c`,
generated there from `mform.rsc` and `gem.rsc`) into one script that
emits C for the target and is imported by `tools/aesref.py` for the
model. Neither side can drift from the other, because there is only one
copy.

A mouse form is what `vsc_form` takes and what the ST's `MFORM` holds: a
hot spot, one plane, the mask's colour and the data's, then sixteen words
of mask and sixteen of data -- 37 words, 74 bytes. Eight of them is 592
bytes, which bank `$00` has not got, so they live in far memory
(`gem_mforms[]`) and `gsx_mfform()` copies one down at a time.

## `graf_mouse` (`src/aes/grlib.c`, `src/aes/graf.c`)

The donor's `gr_mouse`, with its `M_SAVE`/`M_RESTORE`/`M_PREVIOUS`
extension. A mode of 0..7 is one of the AES's own; `USER_DEF` is the
caller's 37 words; `M_OFF` and `M_ON` are `gsx_moff`/`gsx_mon`; anything
else is the arrow, as the donor fails safe.

**The three forms the AES has to remember** -- the one set, the one
before it, and the one an application saved -- are another 222 bytes, and
they live in far memory too, taken once and travelling through a local on
the way in and out. Bank `$00` is for what is read every frame; this is
not.

## `G_ICON` (`src/aes/objc.c`)

The donor's `gr_gicon`: the mask blitted under the image, both
transparent, then the character and the label. `ib_char` packs the
foreground in bits 15-12, the background in 11-8 and the character in
7-0; `SELECTED` swaps the two colours rather than XORing the object,
which is why the state is cleared before `just_draw` reaches its XOR.
`WHITEBAK` over a white ground leaves what is there.

## `form_alert` (`src/aes/alert.c`)

    [1][The disk is full.|Delete something?][Ok|Cancel]

becomes a ten-object tree: an icon, up to five message lines, up to three
buttons. `fm_strbrk` breaks the string at `|` and `]`, a doubled one
being a literal, as Atari TOS and PC GEM both have it. `fm_build` lays
the tree out in **character cells** and `rs_obfix` converts to pixels --
the same fixup an application's resource goes through, which is why the
alert needs no resource at all.

The donor keeps that tree in its resident resource with the substring
buffers beside it. gem4xe has neither, so both come from the application
pool for the length of the call and go back at the end: 240 bytes of
objects, 268 of strings, 14 for a BITBLK and 128 for the icon.

**The icon comes down with them.** The three icons live in far memory,
but the VDI blits a one-plane form through a *near* pointer
(`raster_1bpp`), so a form must be in bank `$00`; a far address is read
with its bank byte dropped, which draws whatever happens to lie at that
offset. That is a rule about the VDI worth stating once: **a form the VDI
blits lives in bank `$00`.**

## Three bugs, and what caught them

**The clip made a restore 168 times slower.** The alert came up, took the
key, and then sat there for three seconds. It was not hung: the trace
said `bb_restore`, and a longer wait proved it finished. `vro_cpyfm`
clips its destination to the VDI clip when that destination is the screen
-- gem4xe's departure; the donor's raster ops are not clipped at all --
and the alert had set the clip to its own rectangle, at odd x. The clip
cut the byte-aligned copy back to an odd start, the fast path's test
failed, and 200x70 pixels went one at a time through the MEMAC window:
**168 frames against one blit.** `bb_save_restore` now takes the clip off
for the copy and puts it back; `menu_sr` had been doing that by hand, and
now no caller has to know.

**A form in the wrong bank.** With the copy fast, the alert drew -- with
noise where the icon belongs. See above: near pointer, far form. The fix
is 128 bytes of pool and one `far_get`.

**A race in the rig, exposed by the size of the image.** `make test-m4`
started failing on its first case only, in one byte, at the very start of
the scratch area: `ob_next` read `$FF00` where the harness had staged
`$FFFF`. The runner's start-up touched `vdi_scratch[0]` to keep the
linker from dropping the section -- *after* it had raised the `VD`
signature the harness waits for. Everything between the two (`farmem_probe`
alone is thousands of frames' worth of bus cycles) was a window in which
the host could stage a case and have its first byte overwritten. The
window had always been there; the image grew by 5 KB and the timing moved
into it. The touch now happens before the signature, and six gates wait
for `STATUS[2] == 1` -- "ready for scripts" -- rather than for "alive".

## Debts

- **The VDI cannot blit a far form.** `raster_1bpp` and the MFDB path
  read through a near pointer, so every image an object draws must be in
  bank `$00`. An application with many icons will feel that; teaching the
  raster loops long addressing is the fix, and it is not done.
- **No colour icons** (`G_CICON`), and `rsrc_load` does not load them.
- **`G_USERDEF` is still not drawn** -- the one object type left.
- The alert's tree is built and thrown away on every call, which is a few
  hundred cycles of layout each time; the donor keeps one. With the pool
  as small as it is, this is the right trade for now.
- Everything is Altirra.

## Lessons

- **"Stuck" and "slow" look the same through a gate.** The harness gave
  the op sixteen frames to finish and called it blocked; it wanted a
  hundred and seventy. The thing that told them apart was a trace
  variable in the target and a longer wait -- and the answer was a
  performance bug worth having found.
- **A clip is not free.** On this machine an odd pixel of clipping is the
  difference between the blitter and the CPU. The rule that fell out:
  code that copies a rectangle it has already measured should copy it
  with the clip off.
- **The rig is part of the program.** A start-up race that had been
  latent for six phases surfaced because an unrelated feature made the
  image bigger. Timing that depends on the size of the image is timing
  that will move again.

## Next

`G_USERDEF`, and then the desktop: `wind_*` and the file selector and the
alerts all exist now, and the desktop is the program that uses them
together.
