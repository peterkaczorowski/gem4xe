# Shipping gem4xe: how it boots, what it lives on, what language it speaks

Everything before this document is about making GEM *work*.  This one is
about making it something a person installs and uses: a disk that comes
up in the desktop, a volume big enough to hold more than the system
itself, and a text file a translator can replace.  None of it is built
yet; it is written down now because the decisions shape what the next
milestones do -- particularly localization, which is cheap to design for
and expensive to retrofit.

## 1. The system fills a floppy, and that is the point

Measured, today:

    GEM.COM        92,230 bytes   the VDI, the AES, GEMDOS and the shell
    DESKTOP.G4A    23,737         the desktop
    DESKTOP.RSC     5,082         its resource
                  --------
                  121,049

against what the formats hold, in bytes a file system can actually use:

    single density    707 x 125 =  88,375   less than GEM.COM alone
    enhanced          1009 x 125 = 126,125  the system, and 5 KB over
    double density     707 x 253 = 178,871  the system, and 57 KB over
    SDFS, our gates   2048 x 128 = 262,144  the system, and 140 KB over

Enhanced density is where the DOS 2 product disk used to live, and it
was too tight to be a product: the system left **three sectors free**,
which is no room for the applications a desktop exists to launch -- and
none for the DOS's own shell either, which is worse (section 2).
**Double density is where it lives now**: the system, the DOS, its
`DUP.SYS` and a demonstration application, with 42 KB still free.

Forty-two kilobytes is room for a few programs, not for a library, and
that is the honest shape of the thing: a 640x240 GUI with a resident AES
belongs on a volume measured in megabytes, and the machine this project
targets (Rapidus, VBXE, U1MB) is a machine that has one.  The floppy is
a *bootstrap* -- enough to start the system, and to carry it to the real
volume.

## 2. Booting straight into the desktop

**Since phase 23 the machine also switches its own CPU** (`phase23.md`):
a Rapidus cold-boots as a 6502, and rather than refuse a machine that
could run it, the loader probes the PBI slots for the card, sets
`COLDST` and switches. What follows is about the other half -- which
file each DOS runs at boot -- and is unchanged by that.

Every DOS on this platform runs something at boot, and no two of them
agree on its name.  What follows was read out of the DOSes themselves
and then booted, because the received wisdom was wrong twice.

**SpartaDOS 3.2g** -- the fixture, and what the product disk boots --
looks for `D1:AUTORUN.SYS` and for `STARTUP.BAT`: both names are in
`X32G.DOS`, and `CHANGES.32G` on the same disk documents the batch file.
It does *not* look for `AUTOEXEC.BAT`.  **SpartaDOS X** boots from the
cartridge or from U1MB flash and reads `CONFIG.SYS` then `AUTOEXEC.BAT`
off `D1:`.  A product disk cannot know which one booted it, so
`build/gem-sp.atr` carries **both batch files**, four bytes each (`GEM`
and an EOL), and the program keeps the name a person would type
(`tools/mkspdisk.py --boot GEM`).  `test-boot` boots that disk with
nothing typed and compares the desk it comes up in against the model.

**DOS II+/D 6.4** -- the DOS 2 fixture -- **has no `AUTORUN.SYS` at
all**: the string is nowhere in its `DOS.SYS`, and a disk built with the
program under that name boots to its `D1:` prompt and waits (measured).
Its command processor is *inside* `DOS.SYS`, which is the only reason it
fits on a disk beside GEM.

**DOS 2.5 does run `AUTORUN.SYS`**, and its `DOS.SYS` is 37 sectors --
one *less* than DOS II+/D's.  But its command processor is a separate
`DUP.SYS` of 42 more, and an enhanced-density disk holds 1,009:
`GEM.COM` (738), `DESKTOP.G4A` (190), `DESKTOP.RSC` (41) and `DOS.SYS`
(37) leave three, so `DUP.SYS` is 39 sectors short of fitting.  Built
without `DUP.SYS` the disk does boot GEM -- and then dies the moment GEM
hands the machine back, because DOS 2.5 goes looking for `DUP.SYS` and
it is not there (an illegal instruction inside DOS at `$144C`, measured;
with `DUP.SYS` present the same disk returns to its menu).  **So on an
enhanced-density floppy you can have a DOS shell or an auto-start, and
not both** -- which is the argument for the density above it.

### Double density, which is where the DOS 2 disk belongs

A double-density disk is the same DOS 2 file system with 253 data bytes
to a sector instead of 125: 707 sectors, 174 KB, room for the system and
the DOS and 42 KB besides.  `tools/atr.py` writes it now.  The format is
Altirra's `ATDiskFSDOS2` (`diskfsdos2.cpp`), read rather than
remembered, and the one thing that is genuinely different is the byte
count in the sector link: **a whole byte in double density**, because
253 does not fit in the seven bits a single-density disk leaves it.  The
directory stays eight entries to a sector and uses half of one.
`tests/host/test_atr.py` checks both densities against those rules, and
`test-boot` boots the result.

The disk is built by sweeping a fixture down to its DOS (`mkdisk.py
--sweep`) and writing GEM onto it as `AUTORUN.SYS`, so what ships is the
DOS's boot sectors, `DOS.SYS`, `DUP.SYS` and ours.  The DOS is the
German Atari **"DISK OPERATING SYSTEM II"** of 1990 (H. Barth and
F. Bruchhäuser), which does double density and does run `AUTORUN.SYS`.

**MyDOS does not work, and the reason is not known.**  It is the obvious
choice -- double density, hard disks, subdirectories, `AUTORUN.SYS` --
and GEM crashes under it every time, at the same place: twelve bytes of
the far image are missing at bank `$01` offset `$20`, the first
`gemdos_call` runs into the zeros and takes a BRK.  What is established:
the file on the disk is byte-for-byte `build/gem.xex`; the near part of
the program loads correctly; the staging buffer holds the right bytes
when the load is over, so they *were* read; and it happens under MyDOS
4.50T and 4.53/4 alike, from `AUTORUN.SYS` and from the DUP menu, while
the same file under this DOS and under SpartaDOS arrives perfect.  So it
is something about MyDOS's binary loader and our chunk staging
(`tools/mkxex.py`, `src/farload.s`), and it wants an hour with a
watchpoint that the bridge does not have yet.  It matters for the hard
media of section 3 only if the hard-disk DOS is MyDOS; SpartaDOS X, which
is what APT wants, is unaffected.

**Ultimate 1MB flash / a cartridge**: the deployment story
flashjazzcat's GUI uses, and the one that makes gem4xe feel like part
of the machine rather than a program.  It is a later phase: the system
would live in flash and the disk would hold only documents.

What makes all of this worth writing down is the ordering rule the
Rapidus imposes and that every gate here obeys -- **the program must
arrive after the CPU switch, through the boot path** (`docs/phase0.md`).
A start-up file satisfies it only if the machine is *already* the 65C816
when the DOS boots, and that is not free: the switch resets the CPU, the
OS treats that reset as a **warm** start, and a warm start is exactly
when a DOS does not run its start-up file.  On a real machine that is
what U1MB's Rapidus plugin is for -- it sets the CPU over the M1 signal
before the OS runs.  The gate arranges the same thing by hand: it lets
the 6502 pass finish (the batch runs GEM, GEM refuses through CIO, the
prompt comes back), sets `COLDST` (`$0244`) so the OS comes up cold, and
only then switches -- with the machine idle, because a write made while
the DOS is mid-SIO is lost and the DOS hangs.  Two boots of a 92 KB
program is also why that gate takes a few minutes.

## 3. Bigger volumes: partitions, APT, and hard media

**The card is built.**  `make` writes `build/gem-cf.img`: a 16 MB image
of 512-byte blocks, an **APT** table (Konrad Kokoszkiewicz's Advanced
Partition Table, which is what SpartaDOS X mounts), and two 8 MB SDFS
partitions -- the system in `\GEM\`, a demonstration application in
`\APPS\`, an `AUTOEXEC.BAT` that changes into `\GEM` and runs `GEM`,
and 7 MB free.  `tools/apt.py` lays out the table and
`tools/mkcf.py` fills it, the way `mkspdisk.py` fills a floppy, and it
needs no fixture: a card carries no DOS of its own, because SDX boots
from a cartridge or from U1MB flash.

The layout was read rather than remembered, out of Altirra's own
`ATDecodePartitionTable` (`src/ATIO/source/partitiontable.cpp`), which
reads real APT disks, and its APT writer in `blockdevdiskadapter.cpp`:

    LBA 0    a protective MBR -- one entry, type $7F, pointing at the
             table, so a PC does not offer to format the card
    LBA 1    the table: sixteen-byte entries, the first the header
             ('APT' in bytes 1-3), the rest partitions.  Entries 1-15
             are the mapping slots, and a DOS mounts them as D1:..D15:
    LBA 8+   the partitions

A partition entry says where it starts and how long it is in blocks,
and how the DOS's sectors sit inside those blocks.  These are **512-byte
sectors, one to a block**, which is also what SDFS wants on anything
bigger than a floppy: `Sdfs.format` grew that case (one boot sector
instead of three, a size byte of 1, a boot header that loads at $0440 --
`ATDiskFSSDX2::InitNew`, and CLX 1.9 checks it).
`tests/host/test_apt.py` holds it to those rules, field by field.

**`make test-cf` boots it, and nothing on the card is a driver.**  The
machine this project is for has an Ultimate 1MB, and the U1MB's flash
carries three things that matter here: SpartaDOS X, the **PBI BIOS**,
and the SIDE Loader.  The PBI BIOS is the disk driver.  It reads the APT
table itself, mounts the mapping-slot partitions as `D1:`, `D2:`, ...
before any DOS runs, and SDX then finds `AUTOEXEC.BAT` on `D1:` exactly
as it would on a floppy.  So the card needs no `SIDE.SYS`, no
`CONFIG.SYS` line and no driver file of its own -- which is just as
well, because neither SDX we have carries an IDE driver at all.  Listing
the ROM file systems by hand: the 4.49b in the U1MB flash holds ARCLOCK,
ATARIDOS, COMEXE, CON64, CONFIG, DOSKEY, ENV, INDUS, JIFFY, QUICKED,
RAMDISK, RTIME8, RUNEXT, SIO, SPARTA, ULTIME and XEP80, and the 4.50
cartridge the same less ARCLOCK, CON64 and ULTIME.  The driver was never
going to come from the DOS.

Three facts about that machine had to be measured, and each is a step of
`tests/emu/cf_boot.py`:

- **The BIOS setup has to be walked, and the gate walks it.**  A fresh
  U1MB profile boots into *Ultimate Setup*, and what the card needs is
  off by default: page 3, *PBI BIOS: Enabled* and *Hard disk: Enabled*.
  The pages step along the icon row with LEFT and RIGHT, the field
  cursor moves with UP and DOWN, RETURN changes the field under it, and
  page 8 offers *Save changes and boot* (`B`) and *SIDE Loader* (`L`).
  On a machine already configured, HELP with RESET reopens it.  The gate
  drives all of that through `KEYRAW`, from a config directory of its
  own (`build/altirra-cf`), so every run starts from the same fresh
  NVRAM and the user's emulator profile is left alone.
- **The PBI device ID must not be 0.**  Setting 0 is PBI bit 0, the
  Rapidus's (docs/phase14.md).
- **The SIDE must let go of the cartridge window.**  The PBI BIOS's
  first act is a wait, at `$D803` in its ROM:

        LDA #$80 / STA $D5E4 / BIT $D384 / BVS $D803

  `$D384` bit 6 is the U1MB's *external cart active* sense, and the SIDE
  asserts it while its own SDX module is mapped -- the state of a SIDE 2
  whose SDX switch is on.  A machine that runs SpartaDOS X from the U1MB
  has that switch off.  AltirraSDL has the switch as a device button
  with no command-line or bridge verb, so the gate unmaps the SDX bank
  the way the switch does, by writing `$80` to the SIDE's bank register
  at `$D5E1`; a reset puts the bank back, so it writes it again through
  the run.  Left alone, the machine sits in that loop forever after
  printing `Ultimate PBI v.1.85` -- which is what "the card does not
  work" looked like before it was read out of the ROM.

With those three, the boot is the floppy's boot: the card starts GEM on
the 6502, the loader refuses the machine, COLDST goes in and the CPU is
switched, and the desktop comes up on the restart with `DISK A` and
`DISK B` on it -- the card's two partitions -- pixel for pixel against
`tools/deskref.py`.

The table itself was never the problem, and there is direct evidence:
the firmware's own parser state, read out of the machine while its list
was on screen, showed the signature accepted, three entries counted and
both partitions intact.  The SIDE Loader's `APT` page saying *No
Entries* is correct as well -- that list is of partitions **beyond** the
fifteen mapping slots, and ours are in slots 1 and 2, which is what
makes them `D1:` and `D2:`.

Two findings from the same afternoon, so nobody repeats them:

- **SIDE 2 and a cartridge cannot both be in the machine.**  SDX 4.50
  from a MaxFlash `.car` plus `--adddevice side2` boots to a black
  screen: they are both cartridges.  The pairing that works is SDX in
  U1MB flash with SIDE 2 in the slot -- the machine this project targets
  -- or SDX in a cartridge with a PBI interface beside it.
- **A FAT16 card works too, and is the quickest way to prove the
  plumbing.**  The SIDE Loader browses FAT volumes off the same card and
  will mount an `.ATR` from one as `D1:`; a card with `GEM.XEX` in a
  FAT16 partition shows up in its file list.  That was the control that
  said the emulated SIDE 2 and its IDE bus were fine long before the
  APT path ran.

~~One thing on the target has to move with it: `Dfree` cannot answer
more than 999.~~ **Fixed** (`docs/phase16.md`): `Dfree` reads the file
system's own count now -- the VTOC on a DOS 2 disk, the superblock on a
SpartaDOS one, one sector through the OS's SIO, which is the path a PBI
hard disk answers on as well as a floppy -- instead of the three
characters CIO's directory trailer gives it.  `make test-m15` reports
1489 free against an image's 1489, where the old ceiling would have said
999, and the card's 16116 is no longer a problem waiting to be found.
The listing is still the fallback for a drive that will not answer SIO.

## 4. An install layout

With a volume that has room, the system stops being one lump:

    \GEM\GEM.COM         the system: VDI, AES, GEMDOS, the shell
    \GEM\DESKTOP.G4A     the desktop
    \GEM\DESKTOP.RSC     its resource (its own strings, its own layout)
    \GEM\LANG.RSC        the system's strings -- see below
    \GEM\*.FNT           fonts, when they are loadable
    \APPS\...            applications, one directory each
    \...                 the user's documents

`build/gem-cf.img` is that layout, less the two files that do not exist
yet (section 5's `LANG.RSC` and a font).  The desktop opens a folder in
a window and runs a `.G4A` from its icon, and `make test-cf` boots the
card into that desktop, so the layout is not a plan.  Two things follow for the loader: an
application is found by path, not by being on `D1:`, and the shell's
command tail (`SH_TAILLEN`, 128 bytes) is what carries arguments -- both
already true.

## 5. Localization: `LANG.RSC`

**The rule: no string a person reads is in the C.**  Eleven were, when
this was written -- the desktop's alerts, written as literals while the
milestones were about mechanism -- and they are not any more.  The
donor's shape is the one they moved into: EmuTOS's desktop keeps them
as *free strings* in its resource and asks for them by index
(`fun_alert(1, STDELDIR)`), our builder already made free strings
(`tools/rsc.py`'s `free_string`) and the AES already resolved them
(`rsrc_gaddr(R_STRING, n)` answers a bank-$00 address, which is what
`form_alert` wants).  Eleven call sites became **nine strings** -- two
of the texts were used twice -- with the donor's names where the donor
has the same alert (`STNOWIND`, `STDEFDIR`, `STDELFIL`, `STDELDIR`,
`STFOFAIL`, `STFO8DEE`, `STDEEPPA`), and `test-m19` puts one on the
screen (New folder, the name it already has) and compares it against
the model.  The resource grew 430 bytes and the desktop's near
constants shrank by 450, which gave a page of the pool back.

**One string cannot come from the resource**: the alert that says
`DESKTOP.RSC` is not on the disk.  It stays a literal in `desktop.c`,
with a comment saying why -- and it is the one line a translator will
have to accept in English.

The split, once they are out of the C:

- **`LANG.RSC` -- what the *system* says.**  The AES's and the shell's
  own text: `form_alert`'s default button labels, the file selector's
  strings, the error texts GEMDOS returns as words, month names, the
  "no such program" alert.  `GEM.COM` loads it once at start and keeps
  it; a translator ships one file.
- **An application's own resource -- what the *application* says.**
  `DESKTOP.RSC` is one of these.  Translations are longer than English
  by a third or more, and a GEM dialog's geometry is *in* the resource,
  so a translated application ships a translated resource with its
  boxes widened -- which is how DRI and Atari did it, one `.RSC` per
  language, and why the split above puts the layout with the text
  rather than the text on its own.

**`LANG.RSC` exists, and `make test-m20` proves a translation is a
file.**  `tools/langrsc.py` describes what the system says -- eight
strings today: `form_error`'s five alerts, the one carrying a DOS error
number, and the shell's two failures -- and builds three things from
that one description, because the three must not disagree: the file
itself, the same bytes as a `__far` array in the image, and the indices
the C uses.  `src/aes/lang.c` reads the file at start-up into far memory,
and `lang_str()` copies the string being used into one near buffer, which
is what `form_alert` wants.  The built-in copy is what a disk without the
file falls back on, because a system that cannot say "this application
cannot be found" *because its language file is missing* is worse than one
that says it in English.  **The file overrides; it is not required.**

The gate runs `form_error` -- the call that takes a number and no string,
so every character that reaches the screen came from the system -- on
three disks: the product's `LANG.RSC`, a German one whose every string
differs and is longer, and no file at all.  Each is compared pixel for
pixel with `tools/aesref.py` given the strings that disk carries.  The
first and the third draw the same screen; the second draws the
translation, which is what says the file is being read rather than
ignored.

Two rules a translation must keep, and the gate holds one to both:
`form_alert`'s grammar (`[icon][text|lines][buttons]`), and one `#` with
two characters after it in the string that carries an error number --
the number is written over them, found by searching for the `#` rather
than by counting, so the phrase may move.

What is not in `LANG.RSC`, and why:

1. ~~Strings must be reachable without spending bank $00~~ -- done, as
   above.  But the file selector's **tree** is still in the far image
   (`tools/fselrsc.py`) rather than in `LANG.RSC`, because the selector
   copies its whole resource into the application pool each time it
   opens: a kilobyte of alert text there would be a kilobyte less for
   the application's own resource, every time (`docs/phase11.md` has
   that budget).  So a translation cannot widen the selector's boxes
   yet.  The desktop's "`DESKTOP.RSC` is not on the disk" alert stays a
   literal for the reason above it; `LANG.RSC` could serve it once the
   application ABI has a call for a system string, which it has not.
2. ~~**The character set has to survive the round trip.**~~ Done, and
   `make test-m21` boots it.  The 8x8 face gem4xe links is EmuTOS's
   Atari ST set, whose high half is the accented Latin letters of
   Western Europe; Polish, Czech, Greek and Cyrillic need a different
   set, so **the strip is loadable**.  `SYSTEM.FNT` beside `GEM.COM` is
   read at start-up (`src/vdi/font.c`, from `lang_init`, so a
   translation's two files are read together), and 2 KB of glyphs
   replace the linked ones in far memory and in the VRAM masks.

   EmuTOS ships exactly the sets that are wanted, all GPL: `make fonts`
   writes `l2.fnt` (Latin-2), `ru.fnt` (Cyrillic), `gr.fnt` (Greek) and
   `tr.fnt` (Turkish) out of a checkout in DRI's own `.FNT` format
   (`tools/mkfnt.py`).  The gate uses none of them -- a gate should not
   need a checkout -- but the *system font inverted*, built from the
   strip that is committed, so every glyph differs and "the file is what
   is being drawn from" is a screenshot rather than a matter of trust.

   **The cell stays 8x8, and that is a decision.**  The AES asks for the
   character cell once, at start-up, and lays the desktop out with the
   answer; the blitter has no shifter, so every glyph is also a
   pre-shifted second copy in VRAM; and the host reference draws the
   same cells.  A face of another SIZE is all of that again and earns
   its place only when there is something to do with it.  A face of
   another ALPHABET is 2 KB and a file.  So the loader refuses -- and
   keeps the face it had -- a form that is not 256x8, a character range
   that is not 0..255, a `top` that is not the linked font's, and the
   colour or word-swapped variants of the format.

   **GDOS's own calls do this**, since they are the ones an application
   would use: `vst_load_fonts` (119) reads `SYSTEM.FNT` and answers how
   many faces that added (0 or 1, there being one place to look),
   `vst_unload_fonts` (120) goes back to the linked face, `vst_font`
   (21) chooses between them and `vqt_name` (130) names them.  That is
   the font half of GDOS and not the rest of it: no `ASSIGN.SYS`, no
   NDC, no Bezier, no metafile.  With 14 MB of RAM the memory was never
   the constraint -- the geometry is.

Two more things a translator will ask for, recorded so they are not
forgotten: **the keyboard** (gem4xe reads POKEY scan codes directly,
`src/vdi/pointer.c` and `src/sys/irq.s`, through a table that is the US
layout -- a localized layout is another table, not new code), and
**date and time formats** in the window's information line and the file
selector (`DD/MM/YY` against `MM/DD/YY` is a one-line difference and a
real one).

## 6. What this means for the milestones

Nothing above blocks the desktop's remaining features, but two pieces
are cheapest now and dear later:

1. ~~The eleven strings out of the C and into `DESKTOP.RSC`'s free
   strings~~ -- done, above.
2. ~~`AUTOEXEC.BAT` on the SpartaDOS product disk and `AUTORUN.SYS` on
   the DOS 2 one~~ -- done.  Both product disks boot into the desktop
   with nothing typed and `make test-boot` requires it.  The SpartaDOS
   one runs `STARTUP.BAT` (3.2) or `AUTOEXEC.BAT` (X); the DOS 2 one had
   to become **double density** first, which `tools/atr.py` writes now.
   Section 2 has the measurements, the names each DOS actually looks
   for, and the one DOS that still will not do it.

3. ~~The APT/CF image writer in `tools/`, and a gate that boots one~~ --
   done: `make` writes `build/gem-cf.img`, an APT card with the install
   layout on it; `tests/host/test_apt.py` holds the table and the
   512-byte SDFS to the rules a reader applies; and `make test-cf` boots
   it into the desktop on the machine this project is for -- U1MB flash
   for SpartaDOS X *and* the PBI BIOS that mounts the partitions, a
   SIDE 2 with the card on its bus.  No driver file on the card, and no
   SDX distribution disk needed after all.  Section 3 says why, and what
   three things about that machine had to be measured first.

4. ~~`LANG.RSC` and the far-string helper; a loadable font~~ -- done,
   both, in section 5: `make test-m20` proves a translation of what the
   system says is a file, and `make test-m21` proves the character set
   it says it in is another.  What a translator still cannot change is
   the file selector's dialog (its tree is in the image, for the pool
   reason in section 5), the keyboard layout, and the date format.
