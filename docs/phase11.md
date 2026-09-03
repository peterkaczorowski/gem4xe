# Phase 11 — the file layer

Status: **complete, in Altirra.** `make test-m12` PASS — CIO through the
OS, the resource library, the shell library, and the file selector driven
over two disks: nine cases of clicking, double-clicking, scrolling,
typing, changing drives and asking for one that is not there, each
compared against `tools/aesref.py` in what it returns *and* in what it
draws. Nothing here has run on a Rapidus or a VBXE: Altirra's device
models are the hardware every number below was measured against.

Everything before this phase drew from memory it already had. This one
reads and writes a disk, which on this machine means calling code that
predates every choice gem4xe has made — the Atari OS's CIO, 6502 code
that wants emulation mode, page one, and its own interrupt handlers.

## Step 1 — CIO from native mode (`src/sys/cio.s`, `src/sys/cio.c`)

gem4xe runs native: its own vectors, its own direct page, DB = 0, a
16-bit stack in bank `$00`. CIOV is none of that, and DOS's disk I/O is
SIO, which POKEY's serial interrupts drive and the OS's VBI times. So a
call is a round trip that becomes the machine DOS was running on and then
comes back:

    D = $0000, DB = $00      the OS's zero page and its page 2/3 tables
    POKMSK = IRQEN           what DOS ran with -- keyboard and break, not
                             gem4xe's timer
    CRITIC = 1               the OS's own critical-I/O flag, so the VBI's
                             stage 2 keeps out; SIO sets and clears it too
    S = $01xx                DOS's stack at the depth DOS left it, set
                             BEFORE `xce` so an NMI between the two lands
                             on it rather than on a truncated native S
    sec, xce, cli            emulation mode: $FFFA/$FFFE are the OS's again
    jsr CIOV
    sei, clc, xce            back

Phase 9 left the emulation-mode vectors exactly as the OS wrote them for
this reason: setting E puts the OS's handlers back with one instruction.
The routine is bank-`$00` `code`, not `farcode` — an interrupt taken in
emulation mode returns to a 16-bit PC in bank `$00`, so the instruction
after `xce` has to be there.

Afterwards gem4xe catches up on the time the OS kept for it: `irq_frames`
is advanced from `RTCLOK`, and a key the OS left in `CH` is put into
gem4xe's ring the way its own handler would have. The pointer sampler was
off throughout — quadrature counts during a disk read are lost, which is
what happens on a real Atari when SIO runs with the keyboard IRQ off.

`src/sys/cio.c` is the C side: an IOCB found free, the name assembled in
bank `$00` (a name with no device gets `D:`), the call, the status back.
It is deliberately thin — no buffering, no retries.

## Step 2 — the resource library (`src/aes/rsrc.c`)

A `.RSC` is the 68000's own memory image: `OBJECT`, `TEDINFO`, `ICONBLK`
and `BITBLK` arrays with every pointer an offset from the start of the
file and every word big-endian. gem4xe's structures are those bytes in
the 65816's order, so `rsrc_load` is: read the file whole into the pool,
swap the words the header counts, and add the pool's address to every
offset — the donor's `fix_long`, with a 16-bit bank-`$00` address where
the 68000 had a pointer. Strings and image data are bytes and are left
alone: a one-plane image is MSB-first in both worlds and `vrt_cpyfm`
reads it that way.

Rectangles are stored as `(pixel offset << 8) | character position` and
made pixels from the workstation's cell (`fix_chpos`); 80 characters wide
means the whole screen. The donor fixes its own resource before the
workstation is open and so splits the job; here the workstation always is,
and one call does all of it.

**One resource at a time.** `rsrc_load` takes from the bump allocator and
`rsrc_free` winds it back, which is only right while nothing else was
taken above it. The file selector's tree is the one other user, and it is
taken and released inside a single call.

## Step 3 — the shell library (`src/aes/shel.c`)

What the shell library keeps — the command and tail the next application
starts with, the environment, the 4 KB the desktop parks its state in —
the AES never reads. It is memory lent to applications across their
lifetimes, so it is taken from the far allocator **once**, at AES
start-up, before any application is loaded: `far_alloc` is a bump
allocator that `app_free` winds back to where `app_load` found it, and
anything taken after an application would go with it.

The environment is the exception and lives in bank `$00` as a constant,
because `shel_envrn` hands out a pointer *into* it and an application's
pointers are 16 bits. It is TOS-shaped — `PATH=\0D:\0` — because every
application that reads `PATH=` skips that NUL, as the donor's `sh_path`
does.

`shel_write` **records** a request rather than acting on it: which program
to run next and how. Acting is the shell loop's job, which gem4xe does not
have until there is a desktop to come back to.

**`sh_cioname`** is where TOS names meet Atari names. A GEM application
says `X:\DIR\NAME.EXT`; CIO says `Dn:NAME.EXT`. Drive A..H maps to D1..D8,
the directory part is dropped (DOS 2 has none), and anything else is left
for CIO to judge. Every file the AES opens for an application goes through
it.

## Step 4 — the file selector (`src/aes/fsel.c`)

The donor's selector lives in the AES's own resident resource. gem4xe has
no resident resource and no bank-`$00` room for a 35-object tree, so the
tree is a `.RSC` file's bytes in the far image (`tools/fselrsc.py`, which
also emits `build/fsel_rsc.h` so the indices come from one script) and is
copied into the application pool on every call, fixed up by the same
`rs_fixit()` an application's resource gets, used, and released. What the
donor's `fs_start()` does to the tree's widths is done to each fresh copy,
since no copy lives long enough to be done twice.

**The pool is the budget.** The tree is 1516 bytes and the work area 272,
against a 2 KB pool shared with whatever resource the application has
loaded. An application holding more than the remainder gets `FALSE` back,
with nothing drawn and the pool exactly as it was — the gate proves that
path with the test resource loaded.

**The names** are the one thing per call that will not fit: 64 slots of a
flag byte, the name and its NUL. They sit in a far buffer taken once at
start-up and read through `far_strget`. Because the slots are one size,
the list holds slot numbers and the sort swaps those, where the donor's
`g_fslist` holds offsets into `ad_fsnames`. The flag is the donor's —
`0x07` a folder, `' '` a file — and is always `' '`: DOS 2 has no folders,
and `FCLSBOX` therefore does what the donor does at the root, nothing.

**The directory** is `dos_sfirst`/`dos_snext` on the ST and a CIO
directory read here: the path's file part replaced by `*.*`, mapped
through `sh_cioname`, opened with aux1 = 6 and read a record at a time.
A record is DOS 2's 17-character line — a lock mark, a space, the name in
8, the extension in 3, a space, the sector count in 3 — and `fs_entry()`
takes only lines of that shape with a name made of DOS 2's characters.
That one rule passes over the deleted entries, the `FREE SECTORS` line,
and the 34 lines of noise an unformatted RAM disk answers with. **The mask
is applied here**, with the donor's `wildcmp`, never by the DOS, so `*.C`
and `A?.*` mean on gem4xe what they mean on the ST.

**A drive that does not answer** costs CIO's timeout and then follows the
donor's own error path: the path put back and the last directory read
again. `gl_drvbits` says which buttons are live; DOS 2 keeps no drive map
worth reading, so all eight are, and a click on an empty drive waits.

## The reference, and the gate (`tools/aesref.py`, `tests/emu/m12_file.py`)

The model gained the selector: the same `.RSC` from `tools/fselrsc.py`,
the same tree geometry, the same directory rule (`aesref.fs_entry` *is*
`fsel.c`'s), and directory listings handed to it as a dictionary of
`Dn:` to names. The names themselves are read from the disk images with
`tools/atr.py`, so what the model expects comes from the image rather
than from the target.

The gate drives the selector through `tests/emu/m7_form.py`'s plan
machinery: a step pokes the pointer or a key and runs frames; a click is
a press, the double-click delay, and a release. On top of it are the
selector's own idioms — `TAP` for a scroll arrow released before the delay
turns it into a click, `HOLD(n)` for auto-repeat run to a stop, `drag` for
the elevator — and screenshots taken *inside* the call, so the listing
itself is compared and not only what the call returns.

When a case does not complete, the gate now says what the machine was
doing rather than only that it stopped: the screen at that moment goes to
`build/shots/m12-fsel-NN-blocked.png`, and the message carries
`irq_fault`, the frame counter, and the pointer. That is what separates
"the target crashed" from "the target is alive and waiting" — a
distinction this phase needed and could not make.

## What the gate says

    CIO through the OS:
      open D:TEST.TXT -> IOCB 1, 1 round trip(s), frames 19 -> 33
      directory: nine names, three placeholder entries, and
                 |0217 Free 12 Fil-E| last
      ATRImage(128b x 1040): M3.COM 695 sectors from 721; 217 free, as the image says
    the resource library:
      rsrc_load: 654 bytes at $A000, trindex at $A286, 1394 bytes of pool left
      the fixed-up image matches tools/rsc.py, all 654 bytes
      rsrc_gaddr: 42 addresses over 17 types checked
      the loaded dialog draws pixel for pixel as the model draws it
    the shell library:
      shel_write/shel_read: 'A:\APP.PRG' with a 5-byte tail, through far memory
      shel_envrn: PATH= at $3E14, followed by D:
    the file selector:
      the disks, by the images: D1 9 name(s), D2 20; pool $A000, 2048 free
      [0] click the second name, OK                            ok -> 1, 'A:\*.*', 'M3.COM'
      [1] double-click the first name                          ok -> 1, 'A:\*.*', 'DOS.SYS'
      [2] a selection in, the closer, a name, Cancel           ok -> 0, 'A:\*.*', 'MEMTEST.DOC'
      [3] an empty path means A:\*.*                           ok -> 0, 'A:\*.*', ''
      [4] a mask, *.TXT; OK with nothing chosen                ok -> 1, 'A:\*.TXT', ''
      [5] a name typed, RETURN                                 ok -> 1, 'A:\*.*', 'XMST.TXT'
      [6] drive B: the slider, the arrows; a name; OK          ok -> 1, 'B:\*.*', 'DOS.SYS'
      [7] drive C is absent: the error path, back to A:        ok -> 0, 'A:\*.*', ''
      [8] fsel_exinput: the caller's title, a mask on B:       ok -> 1, 'B:\*.C', 'DELTA.C'
      refused with 1394 bytes of pool: the tree alone is 1516; nothing drawn

## Four bugs, and what caught them

**A field decremented in place, on the pointer's own slot.** The selector
listed nothing but templates: every name field came out empty. In
`inf_sset()`,

    WORD n = ted->te_txtlen - 1;

compiled to a `dec` on the stack slot holding `ted` — the B5 shape from
`tools/ccbug`, which until now had only been seen with a shift or a
double. So `n` was `$A14F`, negative, and the copy loop never ran. A
scalar first fixes it:

    WORD len = ted->te_txtlen;
    WORD n = (WORD)(len - 1);

The catalogue has the new variant (`b5_dec_bug`/`b5_dec_fix`), so
`make check-cc` reports it as one of the eleven shapes still present —
and would say `FIXED upstream` the day it goes, which is the cue to drop
the workaround rather than to rediscover the bug.

**A constant named twice.** The CIO read cases came back with status 0
and 0 bytes, every time. The gate had `READ = SYS + 7` for the CIO op and,
250 lines later, `SETTLE, READ, ABSENT = 40, 80, 150` for plan timing —
so the target was being asked for VDI op 80. The diagnostic that found it
printed the op number actually sent; the second name is now `LISTED`.

**A test that changed what it later measured.** Four selector cases
differed from the model by a few hundred pixels, all in the name lines.
The listing on the target had one more name than the model's: `OUT.TXT`,
which the gate's own CIO cases write earlier in the same run. Altirra's
disk is virtual — the write never reaches the image on the host — so the
model, reading the image, could not see it. The image now carries
`OUT.TXT` (`DISK_FILES`), so the write overwrites a file that is already
there and the directory the selector reads is the directory the host
read. **Only the screenshots caught this**: every returned value was
identical either way, because the extra name changed no selection.

**A press nobody sampled.** With the ninth name on the disk, one case
stopped completing: the selector sat in its wait and the click on Cancel
did nothing. It was not a crash — `irq_fault` was 0 and the frame counter
was still climbing — and a second click, poked by hand, finished the call
at once. The AES's event layer only looks at the pointer from inside its
own wait (`ev_poll` in `ev_wait`), and the button is a **level**, not an
edge queued in hardware: a press made *and released* while the selector is
inside `fs_active()`'s directory read is never seen by anything. The
harness's settle was 40 frames and the listing took longer than the settle
plus the press, so the whole click fell in the blind window. Measured:
the dialog is up and listening 28 frames after the call for a nine-name
directory, and over 57 when the emulator's disk timing runs long. The
settles are now 120 frames, with what was measured written next to them.

The **target** is not wrong here — a real machine reading a floppy does
not see the mouse either, and the donor's AES has the same shape. What
was wrong was a harness bound that had been true by two frames.

## Debts

- **DOS 2 only.** SpartaDOS's and MyDOS's directory formats are not read,
  and neither is a subdirectory: `fs_entry()` knows one line shape, and
  the selector's folder path is written but never taken. A MyDOS disk
  lists its files and cannot be descended.
- **The path is cut, not copied whole.** The donor copies the caller's
  path into its work area with no bound; gem4xe copies `LEN_FSPATH - 1`
  bytes. A path longer than the field can hold is truncated rather than
  overrunning the pool.
- **No hourglass, and the input of that moment is lost.** The donor
  changes the pointer while it reads a directory. gem4xe does not, so a
  drive that is not there looks like a hang for the length of CIO's
  timeout — and, worse, a click made and released during the read is not
  seen at all: nothing samples the button but the AES's own wait, and the
  wait is not running. A busy pointer would at least say so. Buffering
  the press in the interrupt handler, where the keyboard's already goes,
  is the real fix and is not done.
- **No drive map.** `gl_drvbits` enables A..H because DOS 2 keeps nothing
  worth reading; the honest alternative is probing every drive at
  start-up, which costs a timeout for each one that is absent.
- **The pool is 2 KB** and the selector needs 1788 of it. An application
  with a large resource loaded cannot open the selector. `$4000-$7FFF` is
  16 KB of fast SRAM reserved for U1MB's PORTB window by
  `src/gem4xe.scm`; moving the pool there is a decision, not a fix, and it
  is the user's.
- **`shel_write` records and nothing acts.** There is no shell loop until
  there is a desktop.
- **One resource at a time**, because `rsrc_free` winds a bump allocator
  back.
- **`rsrc_load` does not load colour icons** (`CICONBLK`), as the donor's
  does on TOS 4.
- The far allocator still has no free; `fs_start`'s name buffer is taken
  once and kept.
- Everything is Altirra.

## Lessons

- **A field arithmetic-ed in place is the same bug as a field shifted in
  place.** `tools/ccbug`'s rule 5 was written as "never shift or double a
  16-bit load through a local pointer"; the compiler will decrement one on
  the pointer's slot too. The rule is now about *any* arithmetic on such a
  load, and the catalogue holds a case for it, so the next occurrence is a
  lookup rather than a hunt.
- **Two constants with one name is a bug the target reports honestly and
  the harness hides.** The target did exactly what it was asked — VDI op
  80 — and answered zero. What found it was a diagnostic that printed the
  op number *sent*, not the op number meant.
- **A test that writes to the disk it later reads has changed its own
  fixture.** The values the selector returned were identical with the
  extra file and without it; only the pixels differed. Screenshots inside
  the call are what made a silent divergence loud.
- **A bound that passes is not a bound with room.** The 40-frame settle
  was correct for an eight-name directory and wrong for a nine-name one:
  the margin was smaller than one file. A timing bound in a gate should be
  written with the measurement beside it, so the next reader can see how
  much room it has — which is what these now carry.
- **Measure the target's timing, do not assume it.** The finishing
  window in `m7_form.drive()` exists because the selector's
  `fm_dial(FMD_FINISH)` redraws the desktop before `fs_input` returns,
  and the reference does that in no time at all. One frame, measured;
  sixteen allowed, so the bound is not a measurement of this machine's
  speed.

## Next

`form_alert`, icons and `graf_mouse` — the rest of what an application
draws — and then the desktop, which is the first program that will use
this phase for what it is for.
