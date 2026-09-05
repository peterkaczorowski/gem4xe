# Patches against AltirraSDL

gem4xe's harness runs on [AltirraSDL](https://github.com/ilmenit/AltirraSDL),
the headless/SDL front end to Altirra.  What it needed that is not
upstream (as of b3061c7, 2026-09-01) is here as two patches that apply to
that revision with `git apply` or `patch -p1`; the same changes are the
commits of [pull request #88](https://github.com/ilmenit/AltirraSDL/pull/88)
against upstream, and the patches are to be dropped once it lands.

## altirra-65c816-native-mode.patch -- two CPU core bugs

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
fails deterministically on the unpatched emulator by the second one.

## altirra-sdl-u1mb-keyraw.patch -- the Ultimate 1MB, headlessly

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

## altirra-sdl-bridge-65c816-debug.patch -- a post-mortem for native mode

Not in PR #88; written for the shell-loop fault in `docs/phase14.md`
(milestone 3), on top of the two above.  Against the bridge only:

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
`docs/phase14.md` did.  To go upstream as a second pull request once
#88 has landed, so that the first stays what it was reviewed as.

## Building

    git clone https://github.com/ilmenit/AltirraSDL && cd AltirraSDL
    git checkout b3061c7
    git apply /path/to/gem4xe/tools/altirra/altirra-65c816-native-mode.patch
    git apply /path/to/gem4xe/tools/altirra/altirra-sdl-u1mb-keyraw.patch
    git apply /path/to/gem4xe/tools/altirra/altirra-sdl-bridge-65c816-debug.patch
    ./build.sh --release --system-sdl3 \
        --cmake -DALTIRRA_ENABLE_FFMPEG_RECORDING=OFF \
        --cmake -DALTIRRA_FETCH_FFMPEG=OFF -j$(nproc)
    # build/linux-release/src/AltirraSDL/AltirraSDL

Point the harness at it:

    ALTIRRASDL=/path/to/AltirraSDL make test
    ALTIRRASDL=/path/to/AltirraSDL make test-m14u   # SDX from a U1MB flash
