# Phase 41 -- COP #$56, #$41, #$44: the call gates move, and a COP that is not ours goes to its owner

drac030, who wrote Rapidus OS, read `src/sys/abi.c` and pointed out on
AtariAge that gem4xe had picked the wrong signatures for its three call
gates.  `COP #$01` is taken, by Rapidus OS and by the 65C816 modules of
SpartaDOS X.  `COP #$C8` is in `$80-$FF`, which WDC reserves for new
instructions.  Both were chosen for being the ST's trap numbers -- `$73`
and `$C8` are trap #2's VDI and AES function codes, `$01` is trap #1 --
which is a fine mnemonic on a 68000 and says nothing about who else uses
the byte on this machine.

He was right, and the source showed it was worse than the numbers.

## What the handler did with somebody else's COP

`gem_cop` (`src/sys/abi.s`) took every COP.  It saved the caller's
registers, switched stacks and called `gem_entry()`, whose switch
refused any signature it did not know and counted it in `gem_bad`.  So a
`COP #$01` from anybody -- an SDX driver, an OS extension -- was served as
a GEMDOS call on whatever happened to be in X:C.  And `irq.c` writes all
twelve native vector bytes at `$FFE4`, so while gem4xe runs, the OS's own
COP handler is never reached at all.

Rapidus OS's specification (drac030.krap.pl, *The 65C816 XL/XE OS
revision*) is explicit about how that is supposed to work:

- `COP #$00` is the system emulation call (push a bank-0 OS address, the
  OS JSRs it); `COP #$01` is kmem (kmalloc/kfree, status in Y).  `$80-$FF`
  are WDC's.  A program uses `$02-$7F`.
- The ROM's native vectors are `JML [abs]` stubs through RAM vectors in
  page 2 -- confirmed in the 2.48 ROM: COP `$C9C1: DC 56 02`, through
  VCOPN at `$0256`; ABORT, NMI, IRQ and BRK the same way at `$0253`,
  `$0259`, `$025C`, `$025F`.  A program that takes the COP vector "must end
  your code with a JMP to the old location, or RTI if you bypass the ROM
  completely".
- `@:SYSDEF` says what the OS is: byte 10 the CPU (2 = 65C816), byte 13
  bit 0 the native interrupt services.

On the stock XL OS the same twelve bytes are not vectors at all (the COP
"vector" reads `$F8E0`): there is no handler to pass anything to.

## What changed

**The signatures are letters in `$02-$7F`:** `COP #$56` 'V' the VDI,
`COP #$41` 'A' the AES, `COP #$44` 'D' GEMDOS (`src/sys/abi.h`,
`src/app/gemabi.s`).  `$42` 'B' and `$58` 'X' stay free for a BIOS and an
XBIOS if they ever come.  The handler checks the byte against the same
three, from its own equates, and `tests/host/test_bind.py` now fails if
`abi.h`, `abi.s` and `gemabi.s` disagree or if any of them leaves
`$02-$7F`.

**A foreign COP goes to the OS when there is one.**  `abi_probe_os()`
opens `@:SYSDEF` once at start and sets `gem_cop_pass`.  In `gem_cop`, a
signature that is not one of the three, with `gem_cop_pass` set, leaves
before anything but the five register saves has touched the stack: the
caller's M and X are read from the stacked P, one of four ways out pulls
the saves in 16 bits and puts those widths back, and the jump is the one
the OS's own `$FFE4` stub makes, `JML [$0256]`.  The OS handler sees the
CPU's frame and the caller's registers exactly, restores, and RTIs.  With
no such OS the COP is refused: counted in `gem_bad`, and -- a change --
not in `gem_calls` or `app_calls`.  It is nobody's call, and counting it
would number an application's calls one way on the stock OS and another
under Rapidus OS, where it never reaches `gem_entry` at all.

This is the smaller of the two fixes the specification allows.  The
larger -- hook the RAM vectors (VCOPU, and NMI and IRQ too) instead of
writing `$FFE4` -- would also let an SDX driver that hooks IRQ or NMI see
its interrupts while GEM runs.  It rewrites the interrupt layer that
Phase 40 made work on a real card, and is not done.

**Old programs are refused by name.**  A `.g4a` is now format 3 (far
fixups u16) or 4 (three bytes) -- the same layouts as 1 and 2, renumbered
with the signatures.  The loader answers a 1 or a 2 with `APP_E_OLDSDK`,
and the shell puts up *This application was built for an older gem4xe.
Rebuild it with the current SDK.*  The number moving does the other
direction's work too: gem4xe 0.1.x refuses a new program as not a G4A,
instead of starting it and letting its first call vanish.  Every program
built with the kit -- the desktop and accessories here, RetroWP, GACS's
shell -- has to be rebuilt; nothing in its source changes.

**The kit can link assembly.**  `make APP=mine.c ASM="a.s b.s"`.  It was
needed here because the gate application gained an assembly file, and
`test_sdk` insists the kit rebuild that application byte for byte.

## The gate

`src/m11_cop.s` gives the m11 application one COP that is not gem4xe's:
Rapidus OS's `COP #$01` with function `$7FFF`, which the OS does not have
and answers with -110 in Y.  Y goes in as `$1234`.

    test-m11     stock OS:        Y $1234, gem_cop_pass 0, 1 refused, 31 calls
    test-m11-os  Rapidus OS 2.48: Y -110,  gem_cop_pass 1, 0 refused, 31 calls

`test-m11-os` runs the same gate with `[rapidus].os` from `fixtures.toml`
as the machine's XL kernel, from a private emulator profile
(`build/altirra-m11os`) whose firmware entry for the XL ROM points at it.

## What went wrong on the way

- `bit long:cop_mx`.  The first version carried the caller's M and X past
  the register pulls in the high byte of a word, to be read back with a
  16-bit BIT as N and V.  The 65C816 has no long BIT, and the pulls set N
  anyway.  Four short ways out, chosen before the pulls, replaced it.
- The probe ran before `irq_install()`.  It is a CIO call, and gem4xe's
  first CIO call has always come after the native vectors are in
  (`config_read`).  Under Rapidus OS, whose native vectors are real, the
  gate passed; on the stock OS the runner never came up.  As first written
  it would have stopped GEM.COM on every machine without Rapidus OS.  The
  probe now follows `irq_install()` in `src/gem.c` and in the runner.
- A byte search of `build/gem.xex` for the four `DC 56 02` tails found
  two.  The linked image has all four; the other two straddle a chunk
  boundary in the packed file.  Search the ELF, not the container.
- Three places pinned the G4A version where no grep for `mkg4a` would
  find them: `tools/memreport.py`, `tests/emu/m17_desktop.py` and
  `tests/emu/m31_huge.py`, the last as a plain `ver == 2`.
- The first full suite after the change failed three gates and one link,
  none of them the handler.  `M11.G4A` is also the program `test-m16` and
  `test-m18` run from the shell and the desktop, so its deliberate foreign
  COP turned up as a refusal neither expected -- both now expect exactly
  one -- and, because the refusal was still counted as a call, as one call
  too many before the desktop's second rsrc_load in m18, which is what
  moved the counting (above).  `test-m27` links `abi.s` against a stub
  engine that had no `gem_cop_pass`.  And `tests/host/test_sdk.py`'s
  byte-for-byte rebuild of the gate application needed the kit to link
  assembly.

## Not yet known

Whether any of this is behind the report that gem4xe does not work under
Rapidus OS (Phase 40's aftermath): every release 2.36-2.48 reaches the
desktop in the emulator, but none of those runs loaded SpartaDOS X's
65C816 drivers (65816.SYS, SIO816.SYS, CON816.SYS), which are the software
that would make an OS COP while GEM runs.  They are not in the SDX 4.50
cartridge image and have not been tried.
