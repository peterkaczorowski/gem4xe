#!/usr/bin/env python3
"""Phase 0 gate: prove the gem4xe target machine is real and reachable.

  1. VBXE present, full FX core, version >= 1.20        ($D640/$D641)
  2. Rapidus present as PBI device $01                  ($D1FF -> $D190..)
  3. VBXE VRAM read/write through MEMAC A at $8000, and relocatable
     (NOT MEMAC B -- U1MB extended memory wins at $4000; see docs/phase0.md)
  4. Rapidus 24-bit space live: signature "6S9038E " at $FF0000
  5. CPU switches from 6502 to 65C816 and the machine keeps running

Bridge note: every write verb (POKE/MEMLOAD/HWPOKE) is 16-bit only.
`EVAL db($xxxxxx)` is the only 24-bit path and it is READ-ONLY, so writing
to Rapidus SDRAM has to be done by code running on the target.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from a8test.launcher import launch  # noqa: E402

FX = 0xD640                                   # VBXE register page + $40
RAPIDUS_PBI_BIT = 0x01                        # rapidus.cpp: mDeviceId = 0x01


def evb(b, addr):
    """Read one byte of 24-bit address space via the debugger evaluator."""
    r = b.cmd(f"EVAL db(${addr:06x})")
    return r.get("value") if r.get("ok") else None


def main():
    emu = launch(tag="p0probe", memsize="1088K")
    b = emu.bridge
    fails, checks = [], 0
    try:
        b.frames(30)

        # 1. VBXE ---------------------------------------------------------
        checks += 1
        core, minor = b.peek(FX + 0x00), b.peek(FX + 0x01)
        major = (core >> 4) & 0x0F
        bcd = ((minor >> 4) & 7) * 10 + (minor & 0x0F)
        print(f"1. VBXE  CORE=${core:02X} MINOR=${minor:02X}  -> FX {major}.{bcd:02d}"
              f"  full_fx={(core & 0x0F) == 0}  shared_mem={bool(minor & 0x80)}")
        if (core & 0x0F) != 0 or major != 1 or bcd < 20:
            fails.append(f"VBXE: want a full FX core >= 1.20, got ${core:02X}/${minor:02X}")

        # 2. Rapidus PBI registers ---------------------------------------
        checks += 1
        unsel = b.peek(0xD191)
        b.poke(0xD1FF, RAPIDUS_PBI_BIT)
        cfg, bank = b.peek(0xD191), b.peek(0xD190)
        b.poke(0xD1FF, 0x00)
        print(f"2. Rapidus $D191 unselected=${unsel:02X} selected=${cfg:02X} $D190=${bank:02X}"
              f"  -> CPU={'6502' if cfg & 0x40 else '65C816'}")
        if cfg == 0xFF:
            fails.append("Rapidus: $D191 still $FF with PBI device 1 selected")
        if unsel != 0xFF:
            fails.append("Rapidus: registers responded while PBI device was NOT selected")

        # 3. MEMAC A round-trip + relocation ------------------------------
        checks += 1
        pat = bytes((0x5A, 0xA5, 0x00, 0xFF, 0x12, 0x34, 0x56, 0x78))
        b.poke(FX + 0x1E, 0x88)                   # base $8000, CPU enable, 4K
        b.poke(FX + 0x1F, 0x80)                   # enable, VRAM bank $00000
        b.memload(0x8000, pat)
        got = bytes(b.memdump(0x8000, len(pat)))
        b.poke(FX + 0x1E, 0x98)                   # relocate window to $9000
        b.poke(FX + 0x1F, 0x80)
        got2 = bytes(b.memdump(0x9000, len(pat)))
        print(f"3. MEMAC A  wrote {pat.hex()}  read@8000 {got.hex()}  read@9000 {got2.hex()}")
        if got != pat:
            fails.append(f"MEMAC A round-trip: wrote {pat.hex()}, read {got.hex()}")
        if got2 != pat:
            fails.append(f"MEMAC A relocation: read {got2.hex()} at $9000")

        # 4. Rapidus 24-bit space -----------------------------------------
        checks += 1
        sig = bytes(evb(b, 0xFF0000 + i) or 0 for i in range(8))
        print(f"4. Rapidus signature @ $FF0000 = {sig!r}")
        if not sig.startswith(b"6S"):
            fails.append(f"Rapidus: signature at $FF0000 is {sig!r}, expected '6S9038E '")

        # 5. Switch to the 65C816 ------------------------------------------
        checks += 1
        mode0 = b.cmd("HWSTATE")["cpu"]["mode"]
        b.poke(0xD1FF, RAPIDUS_PBI_BIT)
        b.poke(0xD191, 0x00)                      # clear bit 6 -> select 65C816
        b.frames(60)
        st = b.cmd("HWSTATE")["cpu"]
        print(f"5. CPU {mode0} -> {st['mode']}   PC={st['PC']} cycles={st['cycles']}")
        if st["mode"] != "65C816":
            fails.append(f"CPU did not switch: still {st['mode']}")
        if st["cycles"] <= 0:
            fails.append("CPU stalled after switch")
    finally:
        emu.stop()

    print()
    if fails:
        print(f"gem4xe-p0: {len(fails)} of {checks} checks FAILED")
        for f in fails:
            print("   FAIL:", f)
        return 1
    print(f"gem4xe-p0: {checks}/{checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
