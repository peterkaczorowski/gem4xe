#!/usr/bin/env python3
"""How many times does a DOS call INITAD?

`tools/mkxex.py` writes INITAD after EVERY far chunk.  This measures why,
because the reason this file used to give was wrong.

A seven-segment `.xex`: a counter at $0600, a routine at $0700 that
increments it, INITAD set to that routine ONCE, then two ordinary
segments that never touch $02E2, then RUNAD.  If the DOS called INITAD
after every segment the counter would read 3; if it called it only for
the segment that set it, 1.

On **DOS II+/D 6.4 it reads 1** -- and $02E2 afterwards points at $1507,
where the byte is $60, an RTS.  So the DOS *does* consider INITAD after
every segment and NEUTRALISES THE VECTOR once it has fired.  One
mechanism that looks like both of the readings `mkxex.py` used to say
DOSes "disagree" about.

Which is why a loader that wrote INITAD once would unpack its first chunk
and silently skip every chunk after it.

Corrected after a reader on AtariAge pointed out the RTS reset, 2026-09-18.

    ALTIRRASDL=... python3 tools/initad_probe.py
"""
import os
import struct
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from a8test.launcher import launch            # noqa: E402

OUT = os.path.join(ROOT, "build", "initad")
COUNTER, ROUTINE, EXIT = 0x0600, 0x0700, 0x0710


def seg(start, data):
    return struct.pack("<HH", start, start + len(data) - 1) + bytes(data)


def main():
    import subprocess
    os.makedirs(OUT, exist_ok=True)
    xex = os.path.join(OUT, "initad.xex")
    atr = os.path.join(OUT, "initad.atr")
    open(xex, "wb").write(
        b"\xff\xff"
        + seg(COUNTER, [0x00])
        + seg(ROUTINE, [0xEE, COUNTER & 0xFF, COUNTER >> 8, 0x60])
        + seg(EXIT, [0x60])
        + seg(0x02E2, [ROUTINE & 0xFF, ROUTINE >> 8])
        + seg(0x0690, [0xAA])
        + seg(0x0691, [0xBB])
        + seg(0x02E0, [EXIT & 0xFF, EXIT >> 8]))
    with open(os.path.join(ROOT, "fixtures.toml"), "rb") as f:
        dos = tomllib.load(f)["dos"]["sd_dos2"]
    if os.path.exists(atr):
        os.remove(atr)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkdisk.py"),
                    dos, xex, atr, "INITAD.COM"],
                   check=True, capture_output=True)

    emu = launch(tag="initad", memsize="1088K", vbxe=False, rapidus=False,
                 extra_args=["--disk", atr])
    b = emu.bridge
    try:
        b.frames(300)
        for ch in "INITAD":
            b.key(ch)
            b.frames(4)
        b.key("RETURN")
        b.frames(400)
        n = b.memdump(COUNTER, 1)[0]
        plain = b.memdump(0x0690, 2)
        ini = b.memdump(0x02E2, 2)
        tgt = ini[0] | (ini[1] << 8)
        op = b.memdump(tgt, 1)[0]
        print(f"INITAD fired            : {n} time(s)")
        print(f"the two plain segments  : {plain[0]:02X} {plain[1]:02X}  "
              f"(AA BB means the file really loaded)")
        print(f"INITAD afterwards       : ${tgt:04X}, holding ${op:02X}"
              f"{'  (RTS)' if op == 0x60 else ''}")
        print()
        ok = (n == 1 and plain[0] == 0xAA and plain[1] == 0xBB and op == 0x60)
        if plain[0] != 0xAA:
            print("initad: INCONCLUSIVE -- the probe never loaded")
            return 1
        if ok:
            print("initad: the DOS neutralises INITAD once it has fired, so a\n"
                  "        loader must rewrite it for EVERY chunk it wants run")
            return 0
        print(f"initad: UNEXPECTED -- {n} firing(s), vector ${tgt:04X} = ${op:02X}.\n"
              "        Re-read tools/mkxex.py's account before trusting it.")
        return 1
    finally:
        emu.stop()


if __name__ == "__main__":
    sys.exit(main())
