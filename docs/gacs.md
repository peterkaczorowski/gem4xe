# The applications this is for — and what a port actually needs

gem4xe was not started to have a GEM.  It was started because two
programs wanted one: **GACS**, the GURPS Autoduel Construction Set, and
**RetroWP**, a word processor — both built the same way, a portable
engine in strict C89 with no I/O beside a shell per platform, both
aimed first at the Atari ST.  An Atari 8-bit with GEM on it is another
shell.

So the honest question is not "does gem4xe work" but "would GACS run on
it".  `make gacs-check` asks, and keeps asking.

    engine, --data-model=small 7/7 files
    engine, --data-model=large 7/7 files
    footprint: 24819 bytes of code, 56253 of data and 18661 of
               constants, all far; 84 bytes in bank $00
    tables: 27 chassis, 13 engines, 10 tires, 20 weapons
    design: subcompact / small / std
    ad_compute -> 0
    GEM4XE -- Subcompact, Std. chassis, Light suspension, Small power
    plant, 4 Standard tires, driver only. Armor: F 0/1, ... Accel. 5,
    Top speed 80, Driving skill modifier

That is GACS's own engine, compiled by Calypsi for the 65C816, running
in Calypsi's simulator, parsing the six tables GACS ships and computing
a vehicle.  Not a mock-up of one: `ad_compute` answers `AD_OK` and
`ad_1e_format_line` writes the line GACS's own CLI writes.

## What it took, and what it says

**Nothing, in the engine.**  All seven files compile clean at
`--data-model=small` and at `--data-model=large`, with no warnings and
no changes to GACS.  Strict C89, no floating point, no OS calls: the
discipline that makes it portable to a 68000 makes it portable to a
65816, which is the whole argument for writing an engine that way.

**The memory model is the decision.**  `ad_tables` is 22 KB and
`ad_sheet` is another 33; gem4xe gives an application 2 KB of bank $00
and 14 MB above it, reached through GEMDOS's `Malloc`.  So an
application compiles `--data-model=large`, where a pointer is 24 bits,
and works out of far memory -- which is exactly the shape GACS already
has, because its first prime directive is that a shell hands the engine
a buffer.  Compiled that way the engine wants **84 bytes of bank $00**.

**What is missing is a shell, and rather more than one linker line.**
That estimate stood here until somebody tried it, which is the honest
argument for trying things.  What a `--data-model=large` program actually
needs, found by building one (`src/m29_big.c`, `make test-m29`):

  * **Six sections, not three.**  `src/app/gemapp.scm` mapped `farcode`,
    `switch`, `cfar`, `libcode` and `code`.  A large-data program also
    has `far` (initialised variables), `zfar` (uninitialised) and `ifar`
    (the initialiser `far` is copied from) -- and, in bank $00, `near`,
    `znear` and `inear`, because a global goes FAR there unless it is
    declared `__near`, and anything handed to the AES must be near
    (`src/sys/abi.c`, `near_of`).  It also needs `_NearBaseAddress`
    declared, which a small-data program never refers to.
  * **Bits and bss cannot share a memory.**  `far`/`zfar` carry no bytes
    and the linker refuses to place them beside `farcode`, so they go in
    a memory of their own -- the bank above the code.
  * **A runtime library of the same model.**  The linker will not mix
    runtime models, so `crt_gemapp`, `gemabi` and `gemlib` are built
    twice and a large-data program links `clib-lc-ld.a`.
  * **And the .G4A header had to learn to count banks.**  `tools/mkg4a.py`
    sized the far region from the image, and a far bss carries no bytes:
    a program whose variables are in the bank above its code would have
    been given one bank and left them in memory the far heap goes on to
    hand somebody else, with nothing failing at the time.  The count
    comes from the linker's map now, and `m29_big.g4a` asks for two banks
    where every other program in the tree asks for one.

All of that is done.  **What is not done is running it**: M29.G4A links,
the loader gives it its two banks, and it does not reach its first
statement.  `make test-m29` is written and red, and out of `make test`
until it is not.  The next thing to look at is the crt's
`data_init_table` walk over `far` and `zfar` -- the one part of start-up
no program in this tree had ever exercised.  After it, the port
is: GACS's `shells/gem/main.c` against gem4xe's `COP` ABI instead of the
ST's trap, and `fopen`/`fread`/`fwrite` onto GEMDOS -- which is now a
seam with `Fseek` under it (`docs/phase16.md`).

## What the shell will find waiting for it

Measured against `shells/gem/main.c`, not assumed:

- **Every AES call it makes, gem4xe has.**  `appl_init`/`appl_exit`,
  `evnt_multi`, `menu_bar`/`menu_ienable`/`menu_tnormal`, `objc_draw`,
  `form_do`/`form_dial`/`form_alert`/`form_center`, `graf_handle`,
  `graf_mouse`, `fsel_exinput`, the nine `wind_*`, and
  `rsrc_load`/`rsrc_free`/`rsrc_gaddr`.  The `_grect` and `_str`
  spellings are gemlib's wrappers over those; `menu_sync` and
  `menu_action` are GACS's own functions, not AES calls.
- **Its VDI use is three calls** -- `v_opnvwk`, `v_clsvwk`, `vs_clip` --
  all of them in the 37.
- **From GEMDOS it calls exactly one thing**: `Dgetpath`.
- **`GACS.RSC` is the format we read**: version 0, 2,792 bytes, 73
  objects, two trees, no colour icons.  It fits the application pool
  with room to spare.
- **And it fits the screen.**  The two trees are 80x25 and 42x12
  CHARACTER CELLS; on gem4xe's 8x8 cell that is 640x200 and 336x96
  inside a 640x240 screen.  A resource laid out in cells travels, which
  is what the format is for -- the ST's own high resolution is 640x400
  with an 8x16 cell and the same 80x25.

## The one thing neither of them can do yet

GACS prints a record sheet; RetroWP prints documents.  GACS's GEM shell
writes **PostScript to a file** (`ad_render_ps`), so it needs no printer
to reach parity with the ST -- but "print" is what both programs are
for, and gem4xe has no printer workstation.  The VDI's device
independence is in `v_opnwk`'s device id, not in GDOS, so a printer is a
second driver rasterising the same 37 opcodes to `P:` rather than to
VBXE; GDOS proper is what would buy *loadable* drivers and metafiles.
That is the next thing worth building for these two, and neither is
blocked on it today.
