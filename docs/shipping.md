# Shipping gem4xe: how it boots, what it lives on, what language it speaks

Everything before this document is about making GEM *work*.  This one is
about making it something a person installs and uses: a disk that comes
up in the desktop, a volume big enough to hold more than the system
itself, and a text file a translator can replace.  None of it is built
yet; it is written down now because the decisions shape what the next
milestones do -- particularly localization, which is cheap to design for
and expensive to retrofit.

## 1. The product does not fit on a floppy, and that is the point

Measured, today:

    GEM.COM        92,230 bytes   the VDI, the AES, GEMDOS and the shell
    DESKTOP.G4A    23,724         the desktop
    DESKTOP.RSC     4,652         its resource
                  --------
                  120,606

against what the formats hold:

    single density    720 x 128 =  92,160   less than GEM.COM alone
    enhanced          1040 x 128 = 133,120  the system, and 12 KB over
    double density    720 x 256 = 184,320   the system, and 63 KB over
    SDFS, our gates   2048 x 128 = 262,144  the system, and 140 KB over

The DOS 2 product disk (`build/gem-boot.atr`, enhanced density) has
**five sectors free** with the system on it and nothing else.  It cannot
hold an application, so the one thing a desktop is for -- launching
several programs from one volume -- it cannot do.

That is not a packing problem to be squeezed out of; it is the honest
shape of the thing.  A 640x240 GUI with a resident AES belongs on a
volume measured in megabytes, and the machine this project targets
(Rapidus, VBXE, U1MB) is a machine that has one.  The floppy stays as a
*bootstrap*: enough to start the system and reach the real volume.

## 2. Booting straight into the desktop

The convention the platform already has:

- **DOS 2.x / MyDOS**: a file called `AUTORUN.SYS` is loaded and run at
  boot.  `tools/mkdisk.py` already writes the program as `AUTORUN.SYS`
  by default -- the gates deliberately do not use it, because the
  Rapidus must switch to the 65C816 *before* the program loads and the
  harness wants to type the name after the switch.  The product disk
  names the file `GEM.COM` for the same reason today; a shipping disk
  wants both: `AUTORUN.SYS` for the person, and the CPU already switched
  by the boot path.
- **SpartaDOS / SpartaDOS X**: `CONFIG.SYS` then `AUTOEXEC.BAT` on `D1:`
  (or the cartridge's own).  A one-line `AUTOEXEC.BAT` running `GEM` is
  all it takes; `tools/mkspdisk.py` does not write one yet.
- **Ultimate 1MB flash / a cartridge**: the deployment story
  flashjazzcat's GUI uses, and the one that makes gem4xe feel like part
  of the machine rather than a program.  It is a later phase: the
  system would live in flash and the disk would hold only documents.

None of these is hard; what makes them worth writing down is the
ordering rule that the Rapidus imposes and that every gate here already
obeys -- **the program must arrive after the CPU switch, through the
boot path** (`docs/phase0.md`).  An `AUTORUN.SYS` satisfies that; a
program typed at a prompt on a machine that has not switched does not.

## 3. Bigger volumes: partitions, APT, and hard media

What the project can build today is ATR floppy images: `tools/atr.py`
does 128- and 256-byte sectors, DOS 2 and SDFS, and `tools/mkdisk.py` /
`tools/mkspdisk.py` build the gates' disks from them.

What a real installation wants is a hard disk or a CF card, which on
this platform means:

- **APT** (Konrad Kokoszkiewicz's Advanced Partition Table), the
  partition scheme SpartaDOS X uses for ATA media.  A partitioned
  device gives several SDFS volumes on one card, which is exactly the
  install layout below.  The details -- the table's layout, the
  partition-size limits of the SDFS versions SDX supports -- are to be
  read out of the SDX manual and the fixture before anything is
  written; nothing here has been verified yet.
- **The hardware**: SIDE 2/3, KMK/JZ IDE (IDE Plus), MyIDE.  Altirra
  emulates all of them (`side2`, `side3`, `kmkjzide`, `myide` device
  tags; `src/Altirra/source/ide*.cpp` handles raw and VHD images), so a
  gate can boot a partitioned CF image headlessly the same way the
  floppy gates boot an ATR.  Untested here.
- **`--hdpath`**, Altirra's host-filesystem device (`H:`), is a
  development shortcut: a host directory mounted read-only in the
  emulator.  Useful for iterating on an application without rebuilding
  an image; not a shipping story, because a real machine has no `H:`.

The work this implies is in `tools/`: an image writer that lays out an
APT table and SDFS partitions, the way `mkspdisk.py` lays out a floppy.
It is a host-side job with a host-side model, which is the kind of thing
this project is already good at.

## 4. An install layout

With a volume that has room, the system stops being one lump:

    \GEM\GEM.COM         the system: VDI, AES, GEMDOS, the shell
    \GEM\DESKTOP.G4A     the desktop
    \GEM\DESKTOP.RSC     its resource (its own strings, its own layout)
    \GEM\LANG.RSC        the system's strings -- see below
    \GEM\*.FNT           fonts, when they are loadable
    \APPS\...            applications, one directory each
    \...                 the user's documents

The desktop already opens a folder in a window and runs a `.G4A` from
its icon, so this layout is usable the day the volume exists.  Two
things follow for the loader: an application is found by path, not by
being on `D1:`, and the shell's command tail (`SH_TAILLEN`, 128 bytes)
is what carries arguments -- both already true.

## 5. Localization: `LANG.RSC`

**The rule: no string a person reads is in the C.**  Today eleven are
(`grep form_alert src/desk/*.c`) -- the desktop's alerts, written as
literals while the milestones were about mechanism.  The donor does not
do that: EmuTOS's desktop keeps them as *free strings* in its resource
and asks for them by index (`fun_alert(1, STDELDIR)`), which is exactly
the seam a translator needs.  Our resource builder already makes free
strings (`tools/rsc.py`'s `free_string`, `R_FRSTR`) and the AES already
resolves them (`rsrc_gaddr(R_STRING, n)`), so moving the eleven is a
small, self-contained milestone -- and the last chance to do it cheaply,
because every milestone after this one adds more.

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

Two mechanisms have to exist before either file can:

1. **Strings must be reachable without spending bank $00.**  The pool
   that holds a resident resource is 12.5 KB in the gates and 14 KB
   under `GEM.COM`, and the desktop and its own resource already take
   8.7 KB of it.  So `LANG.RSC` should live in *far* memory with a
   helper that copies the string being used into a small near buffer --
   `form_alert` wants a bank-$00 string, and one 128-byte scratch
   buffer serves every call.  That keeps the language file as large as
   a language needs.
2. **The character set has to survive the round trip.**  The 8x8 system
   font is EmuTOS's, extracted by `tools/fontconv.py`: the Atari ST
   character set, whose high half carries the accented Latin letters
   (and a little Greek).  That covers Western Europe.  It does not
   cover Polish, Czech, Cyrillic or Greek properly, and those want a
   *loadable* font -- which the VDI has no path for yet (the font is
   linked in, `build/font8x8.o`).  A `.FNT` loader and a font in the
   resource are the natural next step after `LANG.RSC`, and the AES's
   `vst_font`/`vqt_attributes` seam is already where it would go.

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

1. **The eleven strings out of the C and into `DESKTOP.RSC`'s free
   strings**, with `fun_alert(defbut, index)` in the donor's shape.  A
   milestone of its own, or the first half of the next one.
2. **`AUTOEXEC.BAT` on the SpartaDOS product disk and `AUTORUN.SYS` on
   the DOS 2 one**, so the disks that already exist boot into the
   desktop rather than to a prompt.

After those, in the order they unlock things: the APT/CF image writer in
`tools/` and a gate that boots one; the install layout on it;
`LANG.RSC` and the far-string helper; a loadable font.
