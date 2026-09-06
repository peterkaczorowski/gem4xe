# Phase 15 — what the system says, and what it says it in

Two files, and the rule they finish.

**No string a person reads is in the C.** The desktop's eleven went into
`DESKTOP.RSC` in Phase 14; the *system's* were still literals — the five
alerts `form_error` puts up, the one that carries a DOS error number, and
the shell's two failures. They are now free strings of `LANG.RSC`, which
a translator replaces. And a translation of what the system says is no
use in a character set that cannot spell it, so the 8x8 font is loadable
too: `SYSTEM.FNT` beside `GEM.COM`, read at the same moment.

    make test-m20   PASS   form_error on three disks: the product's
                           LANG.RSC, a German one, and no file at all
    make test-m21   PASS   a loadable font: the system font inverted,
                           loaded at start-up off one disk and absent
                           from another, with vqt_name / vst_font /
                           vst_load_fonts / vst_unload_fonts either way

Both are compared with the host models — every returned word and every
screen — given the strings and the strip that disk carries. Neither gate
asserts a text or a glyph by eye.

## `LANG.RSC`: far, and one string at a time

The pool that holds a resident resource is 14 KB under `GEM.COM` and the
desktop with its own resource already takes 8.7 KB of it, so a kilobyte
of alert text cannot live there. `LANG.RSC` is a real `.RSC` — free
strings and nothing else, which `tools/rsc.py` writes and an RCS could
open — kept in **far memory as the file's own bytes**, unfixed, there
being no trees in it to fix. `lang_str()` copies the one string being
used into a single near buffer, which is what `fm_alert` wants. One
buffer is enough because an alert is modal; a caller that wants to keep a
string past the next call must copy it, and the header says so.

`tools/langrsc.py` builds **three things from one description**, because
the three must not disagree: the file, the same bytes as a `__far` array,
and the indices the C uses. The array is the fallback: a disk without
`LANG.RSC` gets the identical English, because a system that cannot say
*this application cannot be found* **because its language file is
missing** is worse than one that says it in English. The file overrides;
it is not required.

Two rules a translation must keep, and `tests/host/test_lang.py` holds a
translation to both: `form_alert`'s grammar (`[icon][text|lines][buttons]`),
and one `#` with two characters after it in the string that carries an
error number. The number is written over those two characters, **found by
searching for the `#` rather than by counting to it**, so a translation
may move the phrase — and the German fixture in both gates does, which is
what proves the search.

### What it cost

    the resource            668 bytes, 8 strings, longest 130
    bank $00                2300 -> 2333 bytes of near code
    far memory              668 bytes, when the file is read
    GEM.COM                 92,230 -> 92,888 bytes

## The font: the strip is the loadable part

`src/vdi/font.c` reads a GEM `.FNT` — DRI's format, 88-byte big-endian
header, offset table, then the strip — and for a monospaced 8-wide font
of 256 glyphs **that strip is already the layout `vdi_font_expand()`
reads**, so loading one is a read into far memory and an expansion rather
than a conversion. CIO has no seek, so the strip is reached by reading
the file to it and dropping the 514 bytes of offset table on the way.

EmuTOS ships exactly the sets a translation wants, all GPL: `make fonts`
writes Latin-2, Cyrillic, Greek and Turkish out of a checkout
(`tools/mkfnt.py`). The gate uses none of them, because a gate should not
need a checkout — it uses the system font **inverted**, built from the
strip that is committed, so every glyph differs and "the file is what is
being drawn from" is a screenshot rather than a matter of trust.

**The cell stays 8x8, and that is a decision rather than an oversight.**
The AES asks for the character cell once, at start-up, and lays the
desktop out with the answer; the blitter has no shifter, so every glyph
is also a pre-shifted second copy in VRAM; and the host reference draws
the same cells. A face of another *size* is all of that again. A face of
another *alphabet* is 2 KB and a file. So the loader refuses — and keeps
the face it had — a form that is not 256x8, a range that is not 0..255, a
`top` that is not the linked font's, and the colour or word-swapped
variants of the format.

**GDOS's own calls do the work**, since they are what an application
would use: `vst_load_fonts` (119), `vst_unload_fonts` (120), `vst_font`
(21) and `vqt_name` (130). That is the font half of GDOS and not the rest
of it — no `ASSIGN.SYS`, no NDC, no Bezier, no metafile. With 14 MB of
RAM the memory was never the constraint here; the geometry is.

## What was wrong on the way

**A gate's disk is a fixture, and adding a file to it is a change to two
measurements.** Putting `LANG.RSC` on the runner's disks — so that every
gate would read what the product reads — broke `test-m12` twice at once:
the file selector's cases want a directory that fits its nine lines and
D1: now listed ten names, and the far-heap accounting moved by exactly
668 bytes, the size of the resource `lang_init` had just allocated. Both
failures were correct. The runner's disks stayed as they were and
`test-m20` makes its own copies with the file added, removed and
translated, which is the point there anyway.

**A pure VDI script never reaches `gsx_start`.** The font loaded under
`GEM.COM` and not under the runner, and `test-m21` said so precisely:
`vqt_name(2)` answered with the *system* font's name and id. `lang_init`
was being called from the runner's `gsx_start` op, which a script of
`v_gtext` and `vqt_name` never invokes. It is called from the runner's
bring-up now, as `GEM.COM` calls it, and is idempotent so the op may
still call it.

## What a translator ships, and what is still English

Two files: `LANG.RSC` and `SYSTEM.FNT`, beside `GEM.COM`. Their
application's own `.RSC` is a third, and that is where a *dialog's*
geometry travels, which is why the split is drawn there
(`docs/shipping.md`, section 5).

Still English, and each for a reason worth writing down: the **file
selector's tree**, which is in the far image rather than in `LANG.RSC`
because the selector copies its whole resource into the application pool
every time it opens; the **desktop's missing-resource alert**, which
cannot come from a resource; the **keyboard**, which is a table of US
scan codes and wants another table, not new code; and the **date format**
in the window information line and the selector.
