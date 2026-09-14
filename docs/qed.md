# QED on gem4xe — what it would take

`qed` is a GEM text editor for the Atari ST, maintained in the FreeMiNT
tree (<https://github.com/freemint/qed>).  It is the first candidate for
something gem4xe has never had: **a real application somebody else wrote.**
Everything in `\APPS\` so far is ours -- a calculator, a clock, a
hello-world -- and a port of qed would say the platform is a platform.

This is a scoping note, not a plan of record.  Nothing is ported yet.

## The licence is Public Domain, and that took checking

**qed is PUBLIC DOMAIN, not GPL.**  GitHub reports no licence for the
repository at all: there is no `LICENSE` file, no SPDX line and no GPL
header in any source file, and a web search that says "GPL-2.0" is wrong.
The manual says it outright, in all three languages it ships
(`doc/qed-{en,de,nl}.stg`):

> qed ist ab Version 3.09 inklusive aller Quelltexte Public Domain!

-- public domain, *including all sources*, from version 3.09 on.  That is
compatible with gem4xe's GPLv2+, and more permissive than it.

One thing to know rather than discover: `LIESMICH` records that Tom
Quellenberg built qed out of the demo programs of the book *"Vom Anfänger
zum GEM-Profi"*, and that the program's structure came from the book too.
The author's public-domain declaration covers what he distributes.

## The shape of it

**~800 KB of C in about 35 files, and not one line of assembly** -- which
is the thing that makes this tractable at all, because the ST-specific
assembly is what usually has to be rewritten.  It is a full editor:
syntax highlighting (`highlite.c`, 64 KB), the editor core (`edit.c`,
53 KB), projects, search and replace, macros, word wrap, printing, a
clipboard and multiple windows.

It binds through `cflib.h`, gemlib and `mintbind.h`, with its MiNT-only
code behind `#ifdef __MINT__` and the inter-application AV protocol in a
module of its own (`av.c`).  Both are separable, which matters: gem4xe is
one application under SpartaDOS X, not a multitasking desktop.

## What gem4xe already answers

Counted rather than guessed, by matching every AES/VDI call in `src/*.c`
against `src/app/gem.h`: **qed makes 74 distinct GEM calls and gem4xe has
59 of them.**  The fifteen that are missing are not fifteen pieces of work:

| | calls | what it is |
|---|---|---|
| cflib helpers, not AES | `menu_help`, `menu_key`, `v_slider` | comes with the cflib subset |
| GRECT wrappers | `wind_create_grect`, `wind_open_grect` | trivial; the calls under them exist |
| AV / MultiTOS | `appl_find`, `appl_search`, `appl_control`, `appl_xgetinfo` | trim, or stub as "no extensions" |
| **the clipboard** | `scrp_read`, `scrp_write` | **the one real gap: gem4xe has no GEM scrap** |
| GDOS printing | `v_opnprn`, `vq_devinfo`, `vs_document_info`, `vqt_ext_name` | trim, or map onto our own printer VDI (test-m30) |

So with AV and printing trimmed, **the only substantive AES work is the
scrap manager.**  The editor-critical surface is already there and gated:
`objc_edit` for editable dialog fields, `form_center`/`form_keybd`,
`menu_icheck`/`tnormal`/`ienable`, `wind_calc`, the scroll messages
(`WM_VSLID`, `WM_ARROWED`), styled text, and the file selector.

## The prerequisite that is already cleared

A text editor holds its buffers in far memory, so qed would be compiled
`--data-model=large` -- and until 2026-09-14 that was a wall rather than a
preference: every string literal in such a program is far, and the COP
shim nulled a far string, so `form_alert` drew nothing and `rsrc_load`
opened no file.  That is fixed (`src/sys/abi.c`, `near_str`), and `fsel`'s
dialog title bounces too, so the file dialog works.  The limit that
remains is written down in `src/app/gem.h`: a far string is capped at 63
bytes, so a longer alert -- or a second string in one call -- still wants
`__near`.

## If it were done, roughly in this order

1. **The scrap manager** (`scrp_read`/`scrp_write`) -- the one real gap,
   and useful to gem4xe with or without qed.
2. **A cflib subset** -- only the functions qed calls, not the library.
3. **Trim** `av.c`, the `__MINT__` paths and the GDOS printing, so what is
   left is the editor.
4. **Build it far** -- ~800 KB of C is several far banks; the loader
   already carries multi-bank images (test-m6, test-m31).
5. **Then the long tail**: the ST character set, key codes, and whatever
   the editor assumes about a screen that is 640x240 rather than 640x400.

The honest size is "a project, not an evening" -- but it is bounded, it is
mostly mechanical, and the licence and the ABI are both out of the way.
