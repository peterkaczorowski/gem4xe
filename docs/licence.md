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

That is a grant to **use**, and it does not mention redistribution either
way.  Read carefully, the position is narrower than it first looks, and
worth stating in both directions.

**The tool chain's licence almost certainly permits this.**  Its clause 2
expressly grants "personal non-commercial use, including personal hobby
and education, **producing application software for vintage and retro
computing systems**", and the restriction that follows -- "you may not
... distribute Software" -- is about *Calypsi*, which clause 1 defines as
"The Software and its documentation".  Not its output.  A licence whose
stated purpose is producing retro software, read as forbidding anyone
from being given the retro software, would defeat its own grant.

**What is genuinely unresolved is on the GPL's side, not the vendor's.**
Distributing a GPL'd binary means being able to licence the WHOLE work
under the GPL, and 815 bytes of GEM.COM -- 0.6 per cent of it -- is under
"permission to use", which is not a GPL-compatible licence.  That is
precisely the gap GCC's Runtime Library Exception exists to close, and
GPLv2's "system library" carve-out does not cleanly cover a
cross-compiler's runtime.  No choice of GPL version fixes it, because it
is not a question of which GPL.

So this is a corner, not an obstacle: the risk of shipping is very low
and the intent of every party is obvious, but the paperwork does not
close.  Two ways to close it, in the order they should be tried:

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

Until one of those happens, the honest statement -- and the one the
release page makes -- is that **the binaries carry 815 bytes that are not
ours to relicense**, that nobody involved is likely to mind, and that a
release calling itself properly GPL'd wants the grant in writing first.
Not that the binaries cannot be handed to a tester.

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
