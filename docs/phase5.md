# Phase 5 — linear memory, and what gem4xe now requires

Status: **gate green.** `make test-m5`.

    board      : RAPIDUS
    linear RAM : banks $01-$EF  (239 banks, 14.9 MB)
    alloc test : PASS

## Requirements, now explicit

gem4xe **requires a 65C816 with linear RAM, and VBXE.** The 6502 fallback and
the ANTIC/GTIA driver the original plan carried as a secondary target are
deferred, not cancelled: the driver seam that would host an ANTIC back end is
already in place and unused, so the door stays open.

Two boards provide the CPU and the memory:

| | |
|---|---|
| **Rapidus** | 65C816 at ~20 MHz, 14.9 MB linear. Emulated by Altirra, so fully tested. |
| **Antonia** | 65C816 with 4 MB or 8 MB. **Not emulated by Altirra — untested here.** |

## Probe, do not assume — and it already paid

`farmem_probe()` writes each bank's own number into it and reads them all back.
That is the classic RAM-sizing trick and it is alias-proof by construction: a
mirrored bank has been overwritten by the later write and reads back someone
else's number; a bank with no RAM reads floating bus or ROM. Both fail the same
test. **No board-specific knowledge is needed to size the memory — only to
name the board.**

It paid immediately. The plan carried Rapidus's documented figure, *14.5 MB of
SDRAM at `$080000`*. The probe found **one unbroken run from bank `$01` to
`$EF`, 14.9 MB** — because the 448 KB of Rapidus SRAM in banks `$01-$07` is
contiguous with the SDRAM above it. Hardcoding the documented map would have
thrown that away and started 448 KB too high.

The same code path should bring up Antonia without changes. That is a
prediction, not a result, and the gate says so in its own docstring.

## Design notes

- Bank **`$FF` is never probed**: on Rapidus it holds the accelerator's
  registers, and writing bank numbers over them would be a poor way to start.
- Probing uses offset **`$0100`**, not `$0000`. If a bank turns out to mirror
  bank `$00`, offset 0 would land in the OS zero page; `$0100` is the 6502
  stack page, which the '816 is not using with its own stack elsewhere.
- The allocator is a **bump pointer**. gem4xe never frees far memory — the
  AES's lifetime is the program's — and a bump pointer over megabytes cannot
  fragment.
- Board identification (the `"6S9038E "` signature at `$FF0000`) is *reporting
  only*. It never gates the sizing, so an unrecognised board still works and is
  reported as `FARMEM_UNKNOWN`.

## A Calypsi wrinkle worth recording

The address-space qualifier must follow the base type in a declarator:

    uint8_t __far *p;      /* correct */
    __far uint8_t *p;      /* parses at FILE scope, rejected as a local */

The asymmetry is a confusing way to find out — the file-scope form compiled
cleanly in a throwaway test and then failed everywhere it was actually used.

## What this unblocks, and what is still ahead

Data can now live in the 14.9 MB. **Code cannot yet**: the program is still
built `--code-model=small`, entirely in bank `$00`, and the conformance runner
is borrowing `$4000-$73FF` as a scaffold to fit the AES at all.

The remaining step is the one the plan named from the start: build with
`--code-model=large`, place `farcode` in banks `$01+`, and copy it up at load
time — because a `.xex` loader cannot place anything outside bank `$00`.
`tools/mkxex.py` already refuses such a segment rather than truncating it, so
the failure mode is loud. That is the next piece of infrastructure, and it is
what lifts the ~28 KB ceiling the AES is currently pressed against.
