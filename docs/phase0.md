# Phase 0 — harness and toolchain

Status: **both gates green.** `make test` runs them.

    make test-emu   5/5  VBXE / Rapidus / MEMAC A / 24-bit space / CPU switch
    make test-m1    5/5  Calypsi C compiled, linked, packed, booted, running on the 65C816

## What the target machine actually is

Verified live, not from documentation (`tests/emu/p0_probe.py`):

| | |
|---|---|
| VBXE | `CORE_REVISION $10`, `MINOR_REVISION $26` -> **FX 1.26**, full FX core |
| Rapidus | **PBI device `$01`**; signature `6S9038E ` at `$FF0000` |
| CPU switch | 6502 -> **65C816** confirmed via `HWSTATE` |
| MEMAC A | round-trips at `$8000` and relocates to `$9000` |

**Core detection:** test `(CORE_REVISION & $0F) == 0` for "full FX core" and
`CORE_REVISION >> 4` for the major version. Never compare the whole byte —
Altirra's own prose says FX 1.26 reads `$11`, which contradicts its bit table
and is an erratum. The hardware reads `$10`.

**Rapidus registers are invisible until the PBI device is selected.** Write
`$01` to PDVREG (`$D1FF`), then `$D190-$D1A0` respond; deselect and they read
`$FF` again. The probe asserts both directions.

## MEMAC A, not MEMAC B

`src/gem4xe.scm` reserves `$4000-$7FFF` and `$8000-$9FFF` and places nothing in
either. MEMAC **B** is a fixed 16 KB window at `$4000-$7FFF` and U1MB's extended
memory overrides it there, so gem4xe uses the relocatable MEMAC **A** at
`$8000-$9FFF` instead:

    MEMAC_CONTROL  ($D65E)  D7:D4 base ($x000) · D3 CPU · D2 ANTIC · D1:D0 size (0=4K,1=8K,2=16K,3=32K)
    MEMAC_BANK_SEL ($D65F)  D7 enable · D6:D0 VRAM address bits 18:12

Do not lift `~/dev/vbxetxtadv/src/vbxe/memac.s` unchanged — that project has no
U1MB and uses MEMAC B freely.

## The boot-order problem, and why it is not optional

**The Rapidus always cold-boots as a 6502, and switching to the 65C816 resets
the CPU.** `ATRapidusDevice::ColdReset()` is explicit: *"reset FPGA, force boot
on 6502"*, and `SwitchCPU()` ends in `ResetCPU()`. So a program can never switch
the CPU and keep running — and `BOOT`, which cold-resets, silently undoes a
switch made before it.

The working order is therefore:

1. launch with `--disk <image>` (**not** `MOUNT`: `--cleardevices` leaves no disk
   drive for `MOUNT` to reach, and the machine drops to self-test at `$505C`)
2. write `$01` to `$D1FF`, then `$00` to `$D191` — the CPU switches and resets
3. DOS re-boots, now on the 65C816
4. start the program **after** the switch

The fixture DOS (DOS II+/D 6.4) boots to a `D1:` prompt rather than running
`AUTORUN.SYS`, which is convenient rather than annoying: typing `HELLO` is how
step 4 happens. `tools/mkdisk.py` writes the program onto a **copy** of the
user's own image (path in the gitignored `fixtures.toml`), never the original.

## Why `src/crt_atari.s` exists

The single most valuable thing learned in Phase 0.

Calypsi's library `cstartup` opens with `clc; xce` to enter native mode. On an
Atari that is fatal, because the 65816 changes its interrupt vectors:

    emulation   NMI $FFFA   RESET $FFFC   IRQ/BRK $FFFE
    native      NMI $FFEA                 IRQ     $FFEE   BRK $FFE6

**The Atari OS ROM only fills the emulation-mode vectors.** So the first VBI
after `xce` — within 20 ms, always — vectors through `$FFEA`, reads whatever
bytes are there, and derails.

Observed exactly that: a breakpoint at `__program_start` was hit, so the program
*did* start; moments later the CPU sat at `$FFFE` with `$3000` overwritten by a
repeating `b9 00 46 27` pattern. The program was running and being destroyed by
its own interrupts.

`src/crt_atari.s` is entered from the `.xex` run vector while still in emulation
mode and does `sei`, `IRQEN=0`, `POKMSK=0`, `NMIEN=0`, then falls into the
library startup. `tools/mkxex.py` is pointed at `_atari_entry`, not
`__program_start`.

**A gem4xe that wants to return to DOS, or use interrupts at all, must install
native-mode vector stubs instead of just switching them off.** That belongs with
the VDI interrupt work (the blitter-complete IRQ is the obvious first customer),
not with this stub.

## Toolchain

Calypsi 5.18 extracted to `~/dev/toolchains/calypsi-65816` — the Arch package
unpacked into a user prefix, no sudo, no system install.

    cc65816 --code-model=small --data-model=small -O2
    ln65816 src/gem4xe.scm ... clib-sc-sd.a --rtattr exit=simplified
    tools/mkxex.py build/hello.elf build/hello.xex --entry _atari_entry

Confirmed on target: **`sizeof(int) == 2`** and `sizeof(void *) == 2`, and a
16-bit multiply-accumulate loop returns `$29AE`, the correct value. The 16-bit
`int` matches GEM's `WORD` exactly, which is the reason no int-width porting
sweep is needed.

Small code + small data keeps everything in bank `$00`. `tools/mkxex.py`
**refuses** a segment above `$FFFF` rather than truncating it: an Atari DOS
loader has no concept of 65816 banks, so code destined for Rapidus banks `$01+`
has to be copied up at runtime. That is a later milestone.

## Harness notes

- `--disk` at launch, not `MOUNT` after it.
- Every bridge **write** verb (`POKE`, `MEMLOAD`, `HWPOKE`) is 16-bit only.
  `EVAL db($xxxxxx)` is the only 24-bit path and is **read-only**, so writes to
  Rapidus banks must come from code running on the target.
- `HISTORY` returns "no instructions recorded yet" unless tracing is enabled;
  `DISASM` and `BP_SET` work and were what actually found the vector bug.
- A hit breakpoint pauses the emulator, so a following `frames()` blocks
  forever. Set breakpoints only in scripts that then poll `HWSTATE`.
- **Never `pkill -f AltirraSDL`** — the pattern matches the shell running it and
  kills the shell. (Confirmed the hard way, twice; it is in vbxetxtadv's
  CLAUDE.md for this reason.) Use `make emu-stop`, which matches `pgrep -x`.

## Deferred: U1MB

AltirraSDL exposes **no way to enable Ultimate 1MB** — no command-line switch
(`--memsize` only reaches `1088K` standard banking) and no bridge verb; it is a
simulator-level setting (`ATSimulator::SetUltimate1MBEnabled`).

This costs nothing for now. On a Rapidus machine U1MB is not the memory story —
its role is flash/SDX/PBI/RTC — and the one interaction that matters, MEMAC B
being overridden at `$4000-$7FFF`, is already designed around by using MEMAC A.
The harness runs `--memsize 1088K` so PORTB banking is at least present.

When U1MB does become necessary (deployment as a flash ROM image, and the RTC),
the options are: patch AltirraSDL to add a switch, or capture a savestate with
it enabled and load that through `STATE_LOAD`.

**Settled in Phase 14, the first way** (`docs/phase14.md`): the core emulates
U1MB and always did — what the SDL front end lacked was a way to reach it, so
`tools/altirra/` adds `--ultimate1mb`, `--u1mbrom <file>` and the bridge's
`CONFIG u1mb`, and the gates that need the real machine (`test-m14u`,
`test-m15u`, `test-cf`) run on a user's own flash image. The flash turned out
to matter for more than the RTC: it is where SpartaDOS X, the PBI BIOS that
mounts a CF card's APT partitions, and the SIDE Loader all live
(`docs/shipping.md`, section 3).
