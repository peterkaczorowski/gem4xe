# Phase 21 — the kit

`docs/phase20.md` gave an application every call by name.  It did not
give anybody a way to *build* one: a `.G4A` was made by a macro inside
this tree's Makefile, and an outside author would have had to read that
Makefile, find `src/app/gemapp.scm`, discover that three links and a
diff are how the fixups are derived, and know that the near budget
comes out of a 2 KB pool.  All of that is written down now, and
packaged.

    make sdk          build/gem4xe-sdk/ and build/gem4xe-sdk.tar.gz
    make test-host    97 tests, eight of them the kit's

## What is in it

Eleven files, 110 KB, and no part of gem4xe itself — an application
links against nothing of the system's:

    README.md           how to build one, what you can call, what your
                        memory is, and how to get it onto a disk
    Makefile            make -> hello.g4a; make APP=mine.c -> mine.g4a
    include/gem.h       every call the system serves
    lib/gemlib.c        the bindings
    lib/gemabi.s        the three call gates
    lib/crt_gemapp.s    the start-up
    lib/gemapp.scm      the linker's rules, and the budget
    tools/mkg4a.py      ELF x3 -> .g4a
    tools/mkxex.py      (mkg4a reads ELFs with it)
    example/hello.c     a whole program, commented
    COPYING

The library goes as **source, not an object**: built by the author's
compiler with the author's flags, and readable — a binding is four
lines, and reading one is the fastest way to understand what a call
does.

The `Makefile` exposes the three numbers that are actually a decision —
`BSS`, `BITS`, `STACK` — with the README saying where they come from
(gem4xe's 2 KB pool in bank `$00`) and which one runs out first.  The
part an author cannot be expected to infer is the part that got the
most prose: **the code is in a far bank and not in that budget**, and
the AES calls back *into* the program, so the deepest stack is not the
one the program's own code makes.

## The gate, and why it is worth having

A kit is complete or it is not, and the only way to know is to use it
the way somebody else would.  `tests/host/test_sdk.py` assembles it,
copies it to a directory with no relation to this tree, and builds from
there — so a file left out of `tools/mksdk.py`'s manifest is a failure
here rather than a discovery somebody else makes.  It also reads the
`Makefile` and the linker script for a path that climbs out or an
absolute one back into this tree, since either would work here and
nowhere else.

Then the assertion the phase is really for:

> **The kit rebuilds the Phase 10 gate application byte for byte.**

`src/m11_app.c` goes into the kit the way an author's source would, is
built with the kit's own rules, and the `.g4a` that comes out is
compared with the one this tree builds.  They are identical — and
`make test-m11` says that exact binary loads, relocates into the pool
and a far bank, and runs on the emulated machine, making eighteen VDI
and AES calls whose every return value is checked.

So the kit is not merely self-contained.  What it produces is a program
already proven to work, and a change to the tree that would break an
outside author's build breaks this instead.

The whole thing costs 0.6 seconds: Calypsi is fast, and the kit is
four compiles and three links.

## The example's own gate

The example is the file an author copies first, so the **order** of
what it does matters more than most code: announce yourself, ask the
AES for the character cell, open a workstation, draw, close, leave.
Drawing before `v_opnvwk` or forgetting `appl_exit` is exactly the
mistake an example teaches.

So it is run — in the compiler's simulator, against a recorder that
answers plausibly (`tests/host/hello_sim.c` hands back a workstation
handle and a screen size), with the order of the calls read back out.
Three assertions: `appl_init` is first and `appl_exit` last;
`graf_handle` comes before `v_opnvwk`; and every drawing call falls
between `v_opnvwk` and `v_clsvwk`.  That proves the shape, not the
pixels.

## What it still does not do

- **The pixels the example draws are not compared with anything.**  Its
  shape is checked, and it is deliberately straight-line — no event
  loop — so it is short enough to read; but an emulator gate that drew
  it would have to transcribe its calls the way `tests/emu/m11_abi.py`
  transcribes the gate application's, which is a second copy of that
  work for a program whose purpose is to be copied and changed.
- **The kit does not make a disk.**  The README says to use the tree's
  `tools/mkspdisk.py --add`, which needs a DOS image to start from.  A
  kit that could produce a bootable gem4xe disk on its own would need
  the system itself in it, and that is a different artefact — the
  product disk, which `make` already builds.
- **There is no `make dist`** that puts the product disks, the kit and
  a page of "how to try this" in one place.  The kit is the half of it
  that a person who wants to *write* something needs.
