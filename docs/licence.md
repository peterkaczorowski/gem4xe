# The licence position, and what is still open

gem4xe is **GPLv2 or later**.  The lineage is EmuTOS, which *is* the
Caldera-GPL'd Digital Research GEM source carried forward in C, so the
licence is inherited rather than chosen.  What follows is the part that
is not obvious, written down because it was got wrong once in this
tree's own README and because it decides what may be linked.

## The donor's two wordings, and GPLv2 section 9

EmuTOS does not say the same thing in both halves:

    vdi/*.c   "This file is distributed under the GPL, version 2 or at
               your option any later version."
    aes/*.c   "This software is licenced under the GNU Public License.
               Please see LICENSE.TXT for further information."

The AES files name **no version**, and `doc/license.txt` -- what they
point at -- is the plain GPLv2 text.  The question that matters is
whether that is a v2-only grant.  It is not, and the answer is in GPLv2
itself, section 9:

> If the Program specifies a version number of this License which applies
> to it and "any later version", you have the option of following the
> terms and conditions either of that version or of any later version
> published by the Free Software Foundation.  **If the Program does not
> specify a version number of this License, you may choose any version
> ever published by the Free Software Foundation.**

So silence is not v2-only; silence is the *recipient's* choice of any
version, v3 included.  The only arguable point left is whether pointing
at a file that happens to contain the v2 text amounts to "specifying a
version number" -- and the wording is "for further information", which
reads as naming where the licence text is rather than electing a version.
EmuTOS's own practice treats the tree as v2-or-later throughout.

**This matters less than it looks, now, and that is deliberate.**  The
only reason the project ever needed v3 was to link an Apache-2.0 C
library, and it no longer links one (below).  The tree is v2-or-later and
nothing in it requires a reader to accept the paragraph above.

## What was removed, and why

Calypsi's C library carries the string and memory functions as
`libs/libc/string/lib_*.c` and `libs/libc/...`, taken from **Apache
NuttX** and licensed **Apache 2.0**.  Apache 2.0 is incompatible with
GPLv2 -- it is compatible with v3 and not with v2 -- so linking them put
Apache-2.0 object code inside a GPLv2 binary.

`src/sys/clib.c` replaces all eight with our own, written to the ISO C
standard's wording: `memcpy memset strlen strcpy strcat strcmp strncmp
strchr`.  They are cold -- six `strlen`, five `strcpy`, two `memset`, one
each of the rest across the whole engine, none in a drawing path -- so a
byte at a time is the right shape, and they compile into `farcode` with
the rest of the C and cost bank $00 nothing.  GEM.COM got 34 bytes
smaller.

The applications -- the desktop, the calculator, the clock, the
accessory -- never linked any of it.

## What is still open, and it is not a code problem

The **compiler's own runtime** is still the vendor's, in the engine and
in every application:

    pseudoRegisters.o   _Dp (738 references), _Vfp
    integer.o           _Mul16, _Mul32, _Div32, _UDivMod16, _UDivMod32
    controlFlow.o       _JmpIndLong
    vswitch16.o         _ValueSwitch16
    memory.o            _MoveLongNear
    memcpy_far.o        __memcpy_far
    memset_far.o        __memset_far
    spill.o             _FillDP2
    initialize.o        __initialize_sections
    cstartup.o          __program_start, __low_level_init
    simplified_exit.o   exit
    defaultExit.o       _Stub_exit

Its header reads, in full:

> Copyright Håkan Thörngren.  This file is part of the Calypsi C library.
> Permission to use with the Calypsi tool chain is hereby granted.

That is a grant to **use**, not to redistribute, and the tool chain's own
licence says "you may not ... distribute Software".  **No choice of GPL
version fixes this**: it is not a GPL-compatibility question but the
absence of any redistribution permission, which is what a compiler's
runtime exception normally supplies and what this one does not say.

Two ways forward, in the order they should be tried:

1. **Ask the author for a runtime exception.**  Calypsi has one
   developer, this is the request every compiler vendor fields, and it is
   why libgcc has the exception it has.  One message, and it settles the
   position for every program anyone builds with the tool chain, not just
   this one.
2. **Replace it.**  Tractable but not small: *none of those symbols is
   documented anywhere in the Calypsi guide*, so each register contract
   has to be discovered by experiment -- compile an expression, read the
   generated assembly, confirm in the vendor's simulator.
   `_ValueSwitch16` needs the *data format* of the table the compiler
   emits, not just a register contract, and `_Dp` needs the size the code
   generator assumes (`ctx_regs_ok()` already reads that from the linker).
   `src/sys/div16.s` is the precedent for the mechanism -- `--override`,
   and a reproducer in `tools/ccbug/` run under `make check-cc`.

Until one of those happens, **gem4xe's source is freely distributable and
its binaries are not**, which is the honest statement and the one the
release page should make.

## Keeping it true

`tests/host/test_licence.py` reads the linker maps and fails if an
Apache-2.0 unit comes back, or if the vendor-runtime set grows beyond
what is listed above.  It is the same discipline as
`tests/host/test_memory.py`: a number the machine checks rather than a
paragraph somebody remembers.

    python3 -m unittest tests.host.test_licence

## The rest of the provenance, which is unchanged

  * **The Atari Corp VDI/AES corpus is a specification only.**  It settles
    what a real ROM does; no line of it appears here.
  * **Nothing that is not ours to give is in the tree.**  No ROM images,
    no disk images, no firmware: the disk images are the user's own
    (`fixtures.toml`), and the Altirra patches in `tools/altirra/` are
    diffs against a GPLv2 project that are also filed upstream.
  * What *is* here from elsewhere is GPL'd and says so in its own header:
    the GEM 8x8 font and the standard fill patterns, extracted from
    EmuTOS by `tools/fontconv.py` and `tools/patconv.py`.
  * **The DOS on each disk image is not gem4xe's.**  It is there so the
    disk boots; whoever owns it owns it.
