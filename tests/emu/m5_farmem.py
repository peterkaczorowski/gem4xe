#!/usr/bin/env python3
"""Gate: linear RAM above bank $00 is found by probing, not by assumption.

gem4xe requires a 65C816 with linear RAM.  Rapidus and Antonia both provide it
and disagree about the map, so farmem_probe() writes each bank's own number
into it and reads them all back -- which sizes real RAM and rejects mirrors
without knowing anything about the board.

Only Rapidus can be tested here: Altirra does not emulate Antonia.  What this
gate proves is that the METHOD works and needs no board-specific knowledge, so
an untested board with linear RAM should work on the same code path.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkxex                            # noqa: E402
import symfile                          # noqa: E402
from a8test.launcher import launch      # noqa: E402

DISK = os.path.abspath(os.path.join(ROOT, "build", "m3-boot.atr"))
ELF = os.path.abspath(os.path.join(ROOT, "build", "m3.elf"))
SYMS = os.path.abspath(os.path.join(ROOT, "build", "m3.sym"))
STATUS = 0x0600
KINDS = {0: "NONE", 1: "RAPIDUS", 2: "UNKNOWN"}


def main():
    # Where the far CODE is, so the gate can insist the far HEAP starts above
    # it.  This is the regression that motivates the check: the first version
    # probed and allocated from bank $01, which is where the code lives, and
    # quietly overwrote three bytes of it.
    segs, _ = mkxex.read_elf(ELF)
    far = [(a, len(d)) for a, d in segs if a > 0xFFFF]
    code_top = max(a + n - 1 for a, n in far)
    want_first = (code_top >> 16) + 1
    print(f"far code   : ${far[0][0]:06X}-${code_top:06X}  "
          f"-> heap must start at bank ${want_first:02X}")

    emu = launch(tag="m5", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    fails = []
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(250)
        for _ in range(200):            # ready, not merely alive
            st = bytes(b.memdump(STATUS, 3))
            if st[:2] == b"VD" and st[2] == 1:
                break
            b.frames(4)
        s = bytes(b.memdump(STATUS, 24))
        kind, first, last, banks = s[16], s[17], s[18], s[19]
        mb = (s[20] | (s[21] << 8)) / 16.0
        print(f"board      : {KINDS.get(kind, kind)}")
        print(f"linear RAM : banks ${first:02X}-${last:02X}  "
              f"({banks} banks, {mb:.1f} MB)")
        print(f"alloc test : {'PASS' if s[22] else 'FAIL'} (4K at bank ${s[23]:02X})")

        if s[:2] != b"VD":
            fails.append("runner did not come up")
        if kind == 0:
            fails.append("no linear RAM found -- gem4xe cannot run on this machine")
        if kind != 1:
            fails.append(f"expected the probe to name Rapidus, got {KINDS.get(kind)}")
        if banks < 64:
            fails.append(f"only {banks} banks found; expected a megabyte-class board")
        # Rapidus: SRAM in the low banks is contiguous with the SDRAM above
        # it, so the probe sees one unbroken run reaching to bank $EF.
        # Hardcoding the documented "14.5 MB at $080000" would have missed the
        # first 448 KB.
        if last != 0xEF:
            fails.append(f"run ends at bank ${last:02X}, expected $EF")
        # ...and it must begin ABOVE the far code, or the program allocates
        # its own text.  Both numbers come from where the linker actually put
        # things, so this keeps working when the code grows into another bank.
        if first != want_first:
            fails.append(f"heap starts at bank ${first:02X}, but the far code "
                         f"reaches ${code_top:06X}: it must start at "
                         f"${want_first:02X}")
        if s[23] < want_first:
            fails.append(f"far_alloc returned bank ${s[23]:02X}, inside the code")
        if not s[22]:
            fails.append("far_alloc round-trip failed")
        if banks != (last - first + 1):
            fails.append("bank count does not match the reported range")
    finally:
        emu.stop()

    print()
    if fails:
        print("gem4xe-m5: FAILED")
        for f in fails:
            print("   FAIL:", f)
        return 1
    print("gem4xe-m5: PASSED -- linear RAM discovered and usable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
