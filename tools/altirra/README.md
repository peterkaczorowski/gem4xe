# Patches against AltirraSDL

gem4xe's harness runs on [AltirraSDL](https://github.com/ilmenit/AltirraSDL),
the headless/SDL front end to Altirra.  What it needed that was not
upstream went up as pull requests
[#88](https://github.com/ilmenit/AltirraSDL/pull/88) (the CPU core, the
Ultimate 1MB switches and KEYRAW) and
[#90](https://github.com/ilmenit/AltirraSDL/pull/90) (the bridge's
65C816 debugging); **both were merged on 2026-09-06** and are in
upstream `main` from 46567a14 (the 4.50-test20 sync, 2026-09-09).  The
three patches that carried them are kept here for a build pinned to
b3061c7, and as the record of what was changed and why.  One patch is
still outstanding:

## altirra-sdl-hostfs-posix-paths.patch -- the H: device on Linux

`hostdevice.cpp` joins native paths with a literal `'\\'`, which Linux
takes as a character in the file name.  `SetBasePath` canonicalises the
mount directory (which strips its trailing slash) and then appends
`L'\\'`, so with `--adddevice hostfs,path1=obj/t.run` a file opened as
`H:RWTEST.TXT` is created as `obj/t.run\RWTEST.TXT` -- in the parent
directory, with a backslash in its name -- and a later `H:RWTEST.TXT`
open for reading, which goes through a directory search, does not find
it.
Subdirectories in the Atari path (`H:FOO>BAR.TXT`) are joined the same
way.  The patch introduces `kATHostNativeSep` (`'\\'` on Windows,
`'/'` elsewhere) for the separators the emulator *emits*, and accepts
either where it *parses* them back (`VDIsPathSeparator`); the
Atari-side syntax (`>` and `\`) is untouched.  The parser's self-tests
are rewritten through a helper so they hold on both hosts.
`pclink.cpp` has the same `+= '\\'` in its own path builder and is not
touched here.

Found by the file tests of the Calypsi Atari board support package
([Calypsi-65816-Atari](https://github.com/slaapliedje/Calypsi-65816-Atari),
`test/readwrite.c`), which run against an H: directory.  Sent upstream
as [#91](https://github.com/ilmenit/AltirraSDL/pull/91) on 2026-09-13,
open.

## altirra-65c816-native-mode.patch -- two CPU core bugs (merged, #88)

Altirra's 65C816 core, in native mode (`src/Altirra/h/cpumachine.inl`):

1. **A taken branch does the 6502's page-crossing dummy read**, at the
   wrong-page address folded into bank `$00`.  Code running in a higher
   bank at `$xxD5xx` puts `$D5xx` on the bus -- cartridge control, and
   `$D504` switches a MaxFlash cartridge's bank.  The 65C816 has that
   penalty cycle only in emulation mode.
2. **`SEI` with an IRQ pending sets the "one more IRQ" shadow flag, and
   the native-mode vector states never clear it**, so the handler is
   re-entered at every opcode fetch for as long as the source is
   asserted: an IRQ storm that pushes four bytes per fetch until the
   stack has wrapped through bank 0.

How each was found, and what it cost: `docs/phase14.md`.  `make test-m12`
failed deterministically on the unpatched emulator by the second one
when it was written; it no longer does on every build, which is the
nature of a timing bug -- so a green `test-m12` is **not** evidence that
an emulator has the fix.  So `tools/a8test/launcher.py` does not
take one on trust: a build that does not answer `KEYRAW` -- which came in
the same PR -- is stopped at launch with that reason
(`require_patched=False` overrides it).

The second one bit again in phase 26 and cost three phases of the
desktop's memory budget before it was recognised (`docs/phase26.md`).
Its signature, and the cheapest way to identify it: at the stall, dump
the **whole** address space and read `HWSTATE`.  A storm that pushes
four bytes per fetch walks the stack through all of bank `$00`, so RAM
*and* the hardware registers -- `DMACTL`, `HSCROL`, GTIA's colours,
POKEY's `AUDF`, PIA's `PORTB` -- all end up holding the same repeating
pair of bytes.  Nothing a C program does can write ANTIC's registers.
`ALTIRRASDL=... make test-m19` with `DESK_BSS = 2944` is the second
reproducer.

## altirra-sdl-u1mb-keyraw.patch -- the Ultimate 1MB, headlessly (merged, #88)

The core has emulated the U1MB all along (`ATUltimate1MBEmulator`); the
SDL front end only reached it through the ImGui checkbox, and it embeds
no U1MB recovery BIOS, so a headless run could not have one.  This adds:

- `--ultimate1mb` / `--noultimate1mb`, and `--u1mbrom <file>`: a U1MB
  flash image by path, registered with the firmware manager (into
  `~/.config/altirra/settings.ini`) and made the default.  Enabling U1MB
  forces 1088K, as the simulator requires.
- bridge `CONFIG u1mb <bool>` and `CONFIG u1mbrom <path|name|id>`.
- bridge `KEYRAW name [down|up] [shift] [ctrl]`: a key held in POKEY's
  matrix rather than pushed through the OS's cooked path, which is what a
  BIOS polling `SKSTAT`/`KBCODE` with interrupts off can see.  The U1MB
  setup screen is driven with it (`docs/phase14.md`).
- key names the bridge lacked: `PLUS ASTERISK LESS GREATER INVERSE F1-F4`
  and the XL cursor keys `LEFT RIGHT UP DOWN`.
- a fix: `KEY`'s `shift` and `ctrl` words set each other's KBCODE bit
  (POKEY has shift in bit 6 and control in bit 7; the bridge had them the
  other way round).  `tools/a8test/bridge.py`'s `key()` crosses the
  words back for an unpatched build, told apart by whether `KEYRAW`
  exists.

The protocol document in the patch (`AltirraBridge/docs/PROTOCOL.md`)
describes the verbs.  `tools/a8test/bridge.py` has `key_raw()` and
`key_tap()` over `KEYRAW`.

## altirra-sdl-bridge-65c816-debug.patch -- a post-mortem for native mode (merged, #90)

PR #90 (2026-09-05), against the bridge only; written for the
shell-loop fault in `docs/phase14.md` (milestone 3) and what read back
the interrupt storm in milestone 7:

- `REGS` reports the 65C816's other half when the CPU is one: `K`, `B`,
  `D`, `SH`, `AH`, `XH`, `YH` and `E`.  Without them a native-mode
  stack pointer or program counter is a 16-bit truncation.
- `CONFIG history <bool>`: the CPU's instruction history ring, off by
  default because recording costs time.  `HISTORY n` entries then carry
  `k`, `b`, `sh`, `e` and the instruction's `bytes` for a 65C816, so a
  client can disassemble what ran.  The fault handler parks in `wai`
  (`src/sys/irq.s`) so that the ring still holds the instructions before
  the fault when the host gets round to reading it.
- a fix: a reply larger than the socket's buffer (`HISTORY 4096` is
  well over it) was sent as far as the kernel would take and the rest
  kept for "the next SendAll" -- which never came, because the client
  was blocked reading the reply and sent nothing.  The server's poll
  loop now flushes the pending tail first.

`tools/a8test/bridge.py` uses none of it from a gate; the probes in
`docs/phase14.md` did, and so does every desktop gate's post-mortem
(`tests/emu/m19_files.py`).  It went up as a *second* pull request,
against upstream `main` rather than on top of #88, so that each is
reviewed for what it is: the two touch `bridge_commands_write.cpp`'s
key tables in different places, and whichever lands second wants a
one-hunk context merge.  The copy here is the merged form -- what PR
#90 carries, rebased onto the other two patches -- so that the three
apply in the order below.

## Building

Upstream `main` already carries #88 and #90; only the host-path fix
needs applying:

    git clone https://github.com/ilmenit/AltirraSDL && cd AltirraSDL
    git checkout 46567a14          # or later
    git apply /path/to/gem4xe/tools/altirra/altirra-sdl-hostfs-posix-paths.patch
    ./build.sh --release --system-sdl3 \
        --cmake -DALTIRRA_ENABLE_FFMPEG_RECORDING=OFF \
        --cmake -DALTIRRA_FETCH_FFMPEG=OFF -j$(nproc)
    # build/linux-release/src/AltirraSDL/AltirraSDL

For a build pinned to the revision the gates were first written against,
apply the three historical patches instead, in this order:

    git checkout b3061c7
    git apply /path/to/gem4xe/tools/altirra/altirra-65c816-native-mode.patch
    git apply /path/to/gem4xe/tools/altirra/altirra-sdl-u1mb-keyraw.patch
    git apply /path/to/gem4xe/tools/altirra/altirra-sdl-bridge-65c816-debug.patch

Point the harness at it:

    ALTIRRASDL=/path/to/AltirraSDL make test
    ALTIRRASDL=/path/to/AltirraSDL make test-m14u   # SDX from a U1MB flash
