# Phase 16 — the GEMDOS gaps

Four calls an application falls into, filled in.  `Dfree` answered
nonsense on anything bigger than a floppy; `Fseek`, `Fdatime`,
`Tgetdate` and `Tgettime` answered `EINVFN`.

    make test-m15    PASS   SpartaDOS 3.2
    make test-m15x   PASS   SpartaDOS X from the cartridge
    make test-m15d   PASS   DOS 2
    make test-m15u   PASS   SpartaDOS X from an Ultimate 1MB flash --
                            the only machine here with a clock

## `Dfree`: the file system's own count

It used to read the DIRECTORY LISTING and parse the trailer -- `nnn FREE
SECTORS` -- because that is what CIO offers.  Three characters cannot say
16116, and the two DOSes do not even agree what they put there when it
overflows: SpartaDOS 3.2g prints the low three digits, SDX stops at 999.
The CF card made the wrongness visible (`docs/shipping.md`, section 3).

So read what the file system itself keeps, one sector through the OS's
**SIO** rather than CIO -- `dsk_read` in `src/sys/cio.c`, sharing the
round trip that CIO calls go through (`src/sys/cio.s` grew a second way
in, `dsk_call`, which ends at `SIOV` instead of `CIOV`).  SIO is the
path a PBI hard disk answers on as well as a floppy, so the same code
reaches the CF card's partitions.

    DOS 2     the VTOC, sector 360: bytes 3-4 free, 1-2 total.  On an
              ENHANCED disk that count is the lower half only, and DOS
              2.5 keeps the upper half's in VTOC2, sector 1024, bytes
              122-123 (Altirra's diskfsdos2.cpp; tools/atr.py agrees)
    SDFS      the superblock, sector 1: byte 7 is $80 or $40, bytes
              13-14 free, 11-12 total

The geometry comes from the drive (PERCOM, SIO command `N`), because the
sector size decides how much to read and how many sectors there are
decides whether a DOS 2 disk has that second VTOC at all.

A drive that will not answer SIO -- a DOS's own virtual drive -- still
gets the listing, which is why that code is still there.  It is the slow
path in both senses.

**The gate's disk grew to prove it.** `SP_SECTORS` is 2560 now rather
than 2048, which leaves more than 999 free: `test-m15` reports 1489 free
against the image's 1489, exactly, where the old ceiling would have said
999.  Three round trips on DOS 2, two on a SpartaDOS, against a whole
directory read before.

## `Fseek`: the count GEMDOS keeps

CIO does not count.  Its `POINT` takes a sector and an offset inside it,
which is a position in the FILE only if you know the sector chain, so
this does not use `POINT` at all: it keeps the count itself and gets
where it is going the way a program with no seek would.

    forward        read and throw away
    backward       reopen the file ON ITS OWN IOCB and read forward
    from the end   read to the end, which is also how the size is learned

The reopen is why `src/sys/cio.c` grew `cio_reopen`: a GEMDOS handle IS
its IOCB (`handle = iocb + GD_HANDLE_BASE`), so a file that comes back
must come back on the same one or the handle would change under the
application.

**Where the state lives, and what that cost.** Each open file needs its
position, the path it was opened by and the mode -- 69 bytes, eight of
them.  Putting just the position in near memory (32 bytes) made the
linker refuse the program: *"failed to place 1 section fragment(s): data
000015"*.  Bank $00's data is full, and that is not a figure of speech.
All of it went far, one record per IOCB, at two far accesses per
transfer.

### A DOS 2 that reads past the end twice

`Fseek(-4, h, 2)` failed on DOS 2 and only on DOS 2, and the position
afterwards was five bytes into a 149-byte file.  Instrumenting `gd_seek`
through a window in page 6 gave the answer in one run: the position the
seek started from was **173**, not 149.

The file's last sector holds 24 bytes (149 = 125 + 24).  A read at EOF
that had already been told EOF returned **those 24 bytes again**, and the
count believed them.  So a skip must stop at the end and REMEMBER it:
`gd_skip` refuses to read when the handle is already at EOF and sets the
flag when a read comes up short -- the same `gd_ateof` `Fread` keeps --
and a seek that does not move the file no longer clears it.

## `Fdatime`: the stamp, without disturbing a search

A file's stamp is in its directory entry, and nothing CIO says about an
open file mentions it.  So `Fdatime` looks the file up by the path the
handle was opened with, which the handle now remembers, and takes the
stamp out of the search.

That search would land in the application's DTA and lose the place of a
`Fsfirst`/`Fsnext` walk it might be in the middle of, so it runs against
a DTA of gemdos's own and puts the caller's back -- which the gate
checks by starting a walk, calling `Fdatime`, and requiring both the DTA
and the next `Fsnext` to be undisturbed.

A DOS 2 disk has no stamps and answers the ST's epoch, 1 January 1980 --
the same answer its directory gives for the same file.  Setting a stamp
is `EINVFN`: neither DOS here has a call for it.

## `Tgetdate` and `Tgettime`: the Ultimate 1MB's DS1305

The Atari has no clock.  This machine's is on the U1MB: a DS1305,
bit-banged through one register, `$D3E2`.

    write   bit 0  CE       the chip is selected while this is 1
            bit 1  SCLK     INVERTED: a 1 here is the clock LOW
            bit 2  DATA     the bit going in
    read    bit 3  the bit coming out

Raise CE, clock in eight address bits most significant first, then clock
out the data; an address under $80 is a read and the chip advances it
itself, so seven clocked bytes from address 0 are seconds, minutes,
hours, day, date, month and year, all BCD.  Which edge does what is the
part worth writing down: **the level the clock is at when CE goes up is
the resting level, a move away from it presents the next output bit, and
the move back takes the next input bit.**  Read out of Altirra's
`rtcds1305.cpp`, which is the machine the gates run on.

Detection is the same code: a machine with no U1MB reads a register
nothing drives, and what comes back fails the check that it is a
plausible BCD time.  Then GEMDOS answers the epoch, as a TOS with a dead
clock does.  `test-m15u` compares the answer with the host's own clock --
`2026-09-06 00:43:24` against the host's `00:43:37`, thirteen seconds of
emulator start-up apart -- and the gates without a U1MB require the
epoch.

### Four bits out of step: byte locals again

The first working transaction came back **four bits late**: the seven
registers read `f2 33 80 00 10 60 92`, and dropping four bits off the
front of that bit stream gives `23 38 00 01 06 09` -- the right time,
shifted.  Four of the eight address bits had gone missing, and the first
four output bits were the chip's idle level.

It was not the protocol.  It was `for (uint8_t i = 0; i < 8; i++)` with
another byte local beside it: the same Calypsi slot-sharing this project
has met before, which `src/sys/gemdos.c` already carries a comment about
(*"not a byte: B8, tools/ccbug -- a byte st shared m's slot"*).  Every
counter and shift register in `src/sys/clock.c` is 16 bits now, and the
file says why.

**Nothing here has run on a real Ultimate 1MB.** The DS1305 has hold
times that a 1.79 MHz bus satisfies by being slow; `$D3E2` is I/O so
every access goes at that speed however fast the CPU is, but Altirra's
own manual warns that an accelerator can still violate them, so each
edge is held for a few bus cycles rather than trusting it.

## And a gate that was testing a disk from an hour ago

`test-boot` boots BOTH product disks, and its Makefile rule named only
one of them.  So `build/gem-boot.atr` -- the DOS 2 one -- was rebuilt by
`make` and by nothing else, and any run after GEM.COM changed booted
whatever had been written to it last.

What that looks like is not "a stale disk".  It looks like the DOS
mangling the far image: the gate reports a thousand probed bytes wrong,
the near data reads as garbage, `DOS kind 50`, `far brk $B00D800`, the
desktop apparently 3839 calls into a session.  All of it true, and all
of it because the loader on the disk was a different program from the
one the symbols and the ELF described.  It cost an afternoon twice
before the check that settles it in one line:

    on disk 94201  built 97833  same: False

The rule names both disks now.  Worth remembering next to the MyDOS
finding in `docs/shipping.md` section 2, which was diagnosed the other
way round -- there the file on the disk WAS byte-identical to
`build/gem.xex`, which is what made it a real bug rather than this.
