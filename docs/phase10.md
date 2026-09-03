# Phase 10 — the application ABI

Status: **complete, in Altirra.** `make test-m11` PASS — the loader's
record, the application's eighteen, and the screen, against the reference
— and the rest of the suite green after it.
Nothing here has run on a Rapidus or a VBXE; the emulator's device models
are the hardware every number below was checked against.

Until this phase every line drawn by gem4xe was drawn by gem4xe: the test
runner (`src/m3_vdi.c`) is linked into the image and calls `vdi()` and the
AES's C entry points directly. An application is something else — a
program linked on its own rules, put somewhere at load time, with its own
stack, direct page and data, calling into a system that has all three of
those too. Phase 9's `## Next` named the parts: a dispatcher for callers
with another DP and DB, `contrl`/`intin` copied in and out, `appl_init`
and `appl_exit`, a loadable format. This phase built them, and a gate
application that uses them the way a small GEM program does.

## The call: `COP` (`src/sys/abi.s`, `src/app/gemabi.s`)

On the ST an application reaches GEM through `trap #2`; on the 65C816 the
equivalent is `COP`, a software interrupt native mode vectors through
`$FFE4` — one of the six native vectors Phase 9 installed, whose stub in
`src/sys/irq.s` now jumps to `gem_cop` instead of parking with fault code
1. A VDI call is `COP #$73`, an AES call `COP #$C8` — the ST's own
distinguishing values — with the parameter block's address in X:C. The
signature byte is read back from the caller's program bank through the
frame's PC, so the same handler serves both, and any other byte is refused
and counted in `gem_bad`.

`gem_cop` is the `saveds` entry point the plan asked for, written by hand
rather than with the attribute: it saves A, X, Y, D and DB, forces DB to
`$00` and D to gem4xe's direct page, notes the block and the signature,
gives interrupts back if the caller had them (the CPU sets I on a `COP`;
`P` is in the frame, so the caller's I is known), calls `gem_entry()`,
undoes all of it and `RTI`s. A, X and Y come back as they were; results
travel through the block.

**The stack switch** is what makes an application's stack its own
business. The application runs on 256 bytes of stack in its pool, and the
AES wants more than that below it (`objc_draw` recurses; `form_do` nests
the control manager). So the outermost `COP` from a running application is
served on gem4xe's stack, at the depth `app_run` left it. That works
because of an ordering both sides keep: `app_run` records S *inside* the
trampoline, after its own `jsl` has pushed the return address, and the
application's crt (`src/app/crt_gemapp.s`) takes its own stack before it
pushes anything — so everything below `gem_api_sp` is free, and the two
agree on where gem4xe's stack resumes. `gem_depth` counts nesting so only
the outermost call switches; `gem_api_sp` is cleared when `app_run`
returns so a `COP` with no application running stays where it is.

## The shim: copy in, copy out (`src/sys/abi.c`)

`gem_entry()` is DRI's entry shim, kept for the reason it was invented.
The 1984 screen driver's handlers are argument-free and read fixed
`CONTRL`/`INTIN`/`PTSIN` arrays; `vdi/entry.a86` in the GEM/3 tree copies
the caller's arrays in before dispatch and the results back after, and
EmuTOS's `xif()` does the same for the AES. So the caller's arrays can be
anywhere in the 16 MB — the block holds 32-bit addresses, the ST's layout
— and the handlers never learn. For the VDI: `contrl[0..11]`,
`intin[0..contrl[3]-1]`, `ptsin[0..2*contrl[1]-1]` in; `contrl`,
`intout[0..contrl[4]-1]`, `ptsout[0..2*contrl[2]-1]` out. The counts are
capped at gem4xe's array sizes, never at the caller's — a caller declares
the sizes it built, as on the ST.

The AES side is EmuTOS's `crysbind()` shape with its `int_in`/`addr_in`
packing (`include/aesdefs.h`, verified in `~/dev/emutos`, not remembered):
`control[0]` the opcode, `[1]` the `int_in` count, `[2]` the `int_out`
count, `[3]` the `addr_in` count; `int_out[0]` is the return, `TRUE` for a
call that has none, and −1 for an opcode gem4xe does not have. That
packing *is* the AES binding contract, the one every program built for
GEM expects, which is why it is not gem4xe's to redesign. Forty-two
opcodes are bound: `appl_init/write/exit`, the `evnt_*` seven, `menu_*`
six, `objc_*` five, `form_*` five, `graf_*` seven, `wind_*` nine.
`appl_init` fills `global[]` — version `$0140`, one application, ap_id 0,
the plane count — and returns 0, the pid.

Trees, strings and forms an application passes by address must be in bank
`$00`: the AES addresses them near, as the small data model does for
everything. An address with a bank byte is refused and counted, not
truncated to something that would draw garbage. Message buffers and the
block's own arrays are written through far pointers and can be anywhere.

## The format: `.g4a` (`tools/mkg4a.py`, `src/app/gemapp.scm`)

A `.PRG` carries relocations because the ST's linker emits them. Calypsi's
does not, and neither compiler nor linker will say which bytes of a
65C816 program are the high byte of a near address or the bank byte of a
far one. So the tool **derives** them: the same objects are linked three
times — at the placeholders, with the near region moved up one page, with
the far region moved up one bank — and the bytes that changed are the
fixups. A near address moved a page changes exactly its high byte by +1;
a far address moved a bank changes exactly its bank byte by +1. Any byte
that changed by anything else, or under both shifts, is address
arithmetic the loader could not relocate — a shifted or divided address,
a bank in a low byte — and the tool refuses rather than emit a program
that works at the placeholders and nowhere else. Nothing about the layout
is assumed: shifts, bases and the entry point are read from the ELFs.

The layout the application is linked to (`src/app/gemapp.scm`) is two
regions, both position-independent by fixup: a **near** region — a page
of direct page, then stack, data, bss, then the `cdata`/`idata` bits —
2 KB in the gate application, and a **far** region of code up to 64 KB.
The 32-byte header gives both regions' link addresses and sizes, the
entry, and four fixup counts; the body is the near bytes, the far bytes,
and the four offset lists. The gate application is 4,611 bytes: 14 page
fixups in the near part (the crt's stack, direct page and init-table
immediates, the data-init table itself), 82 page and 72 bank fixups in
the far.

The crt (`src/app/crt_gemapp.s`) replaces the library's `cstartup`, whose
`clc xce` and reset vector have no place inside a running program: it
keeps gem4xe's S, D and DB **on the application's own stack**, sets up its
direct page, DB `$00`, runs the section initialisation, calls `main`, puts
the three back and `rtl`s with `main`'s value in A. The first version kept
them in `zdata` — and the section initialisation it runs next zeroed them,
so the exit `tcs`'d a zero and `rtl`'d into nowhere. `irq_fault` said BRK
and the bridge's `REGS` said `S = $FF`; the fix is one line of comment and
five instructions.

## The loader (`src/sys/app.c`, `src/sys/apppool.s`)

`app_load()` checks the magic and that the blob is long enough for what
the header claims, takes the next page-aligned slot in the bank-`$00` pool
for the near part and `far_alloc_banks()` for the far, copies both, and
applies the four lists by adding the page delta and the bank delta —
bounds-checked; an offset outside its region is `APP_E_FIXUP`. The pool's
bounds are the linker's to decide and `apppool.s`'s to report — two words
in `cdata`, `.sectionStart`/`.sectionEnd apppool` of a block in
`src/gem4xe.scm` — the way `src/farload.s` reports `_fl_top`; `app.c`
never restates the map. (`.sectionEnd` is the *last* address, and the
first link of this had the pool one byte short: `APP_E_POOL` for a 2 KB
program in a 2 KB pool. `app_pool_hi` is one past now, like `_fl_top`.)
Memory is mark/release: `app_free()` puts both bump pointers back where
`app_load()` found them, so applications are released in reverse order or
together — the far heap has no free, and gained none.

`app_exec()` is `app_run(entry)`: a `jsl` through a bank-`$00` long
pointer, the application's `main` return in A.

## The gate: `make test-m11` (`src/m11_app.c`, `tests/emu/m11_abi.py`)

The gate application is linked against nothing of gem4xe's — its own crt,
its own `gem.h` and bindings (`src/app/gemlib.c`: `contrl`/`intin`/... as
a GEM program declares them, `vdi()` and `aes()` filling the block and
`COP`ing) — and packed by `mkg4a` into a C array (`build/app_blob.c`)
linked into the image. The runner's sys op 3004 loads it, runs it, frees
it and records the loader's status, the near base, the far bank, `main`'s
return, and the `COP`s taken and refused. The application makes eighteen
calls — `appl_init`, `graf_handle`, `v_opnvwk`, a fill, text, a polyline,
`objc_draw` of a three-object tree, `wind_create`/`open`/`get`,
`evnt_timer`, `wind_close`/`delete`, `appl_exit` — and records what each
returned, through the ABI's copy-out, in its own memory.

The harness reads the records out of the pool by symbol —
`build/m11_app.sym`, translated by where the loader put the near part —
and compares them with the reference's for the same calls, the reference
running the runner's prelude and then the application's sequence with the
tree decoded from the application's memory as the loader and the
application left it. The two values the application got at run time and
used later — the character-cell height from `graf_handle`, the window
handle from `wind_create` — are taken from its own records, so the
reference is given the calls the application actually made. Then the
runner's record: `APP_OK`; a page-aligned near base inside the bounds
`app_pool_lo`/`hi` report; a far bank above the image's top (`_fl_top`)
and inside the first megabyte; and three counts that must agree — what
`main()` returned, the records the application wrote, the `COP`s the ABI
took — with none refused. Then the screen against the reference's.

The three reference models grew what the gate needed: `vdiref` opens its
one workstation as handle 1, as `vdi.c` does; `aesref` binds `appl_init`,
`appl_exit` and `graf_handle`.

## Debts

- **The pool is 2 KB, in the slow window.** `$A000-$A7FF` is on the
  1.79 MHz bus — the 16 KB window MEMAC shares must be slow (Phase 7), and
  the pool sits in it — so an application's stack, direct page and data
  run at motherboard speed. `$4000-$7FFF` is 16 KB of fast SRAM, unused,
  and reserved by `src/gem4xe.scm`'s explicit rule for U1MB's PORTB
  window. Moving the pool there — or dropping the rule, on a machine
  where U1MB's banking is not in use — is a decision, not a fix, and it
  is the user's; it is written here so it is not made by accident.
- **`v_opnvwk` is `v_opnwk`.** Opcode 100 resets the one physical
  workstation — clip, attributes, cursor — and hands back handle 1, so an
  application's open clobbers the AES's state and `v_clsvwk` is a no-op.
  Model and target agree, and the gate passes through it, but real virtual
  workstations are what GEM has and gem4xe does not.
- **`gemapp.scm` fixes the near split** — a page of direct page, 1.5 KB
  without the bits, 256 bytes with — for the gate application; another
  application moves the boundary, and the loader never sees it, but the
  budget is the pool's 2 KB and every application shares it.
- **Nothing returns memory but in reverse order**, and the far allocator
  still has no free.
- Everything is Altirra.

## Lessons

- **A saved value has to survive what runs next.** The crt saved S and D
  in a bss word and then initialised bss. The compiler did nothing wrong;
  the order of two lines did.
- **A bound from the linker is still a bound to read carefully.**
  `.sectionEnd` is inclusive; the loader compared one-past against it.
  Deriving the pool from the map was right, and the off-by-one was in the
  derivation, not the map — which the gate said in one line.
- **Give the reference the calls that were made, not the calls that were
  meant.** The application's later calls depend on what its earlier ones
  returned; the harness reads those from the application's records and
  builds the reference script from them, then checks the records too.

## Next

The file layer — `rsrc_load`/`rsrc_obfix`, `fsel_input`, `shel_*` — which
needs CIO through the OS and so emulation-mode trampolines from native
code; the emulation vectors at `$FFF0-$FFFF` were left intact for that.
Then `form_alert`, icons, `graf_mouse`, and the desktop.
