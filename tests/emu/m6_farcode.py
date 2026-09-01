#!/usr/bin/env python3
"""Gate: the program's code really lives in, and runs from, bank $01.

An Atari DOS loader cannot place anything above $FFFF, so gem4xe's code
travels in the .xex as chunks aimed at a staging buffer and is copied up by
src/farload.s as DOS reads the file (tools/mkxex.py builds the chunks).  The
other gates would pass a build where that went subtly wrong -- a mangled byte
usually lands in a function nothing in the suite exercises -- so this one
checks the mechanism itself rather than its consequences:

  1. every byte of the far image, at the chunk seams and across the whole
     span, matches what the linker emitted;
  2. the code is EXECUTING from bank $01, not from a copy that fell back into
     bank $00 (which would silently defeat the whole exercise);
  3. the far heap starts above the far code -- see docs/phase6.md, this is
     the bug that made three VDI cases fail with a different three at every
     optimisation level;
  4. the staging buffer is inside the MEMAC A window, where it costs nothing;
  5. and on a machine that CANNOT run it -- the same disk, booted without
     switching the CPU -- the loader says so and gives DOS its machine back
     rather than writing bank $01 with an opcode the 6502 does not have.

Reading bank $01 needs the debugger's `EVAL db($xxxxxx)`, one byte per call
and the only 24-bit path the bridge has, so the whole image is not swept: the
chunk seams are, densely, plus a spread over everything else.
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
SEAM = 48           # bytes checked either side of every chunk boundary
SPREAD = 300        # additional probes spread over the image


def probe_addresses(base, size, chunk):
    """Chunk seams densely, plus a spread over the rest."""
    want = set()
    for dst, piece in mkxex.far_chunks(base, bytes(size), chunk):
        for edge in (dst, dst + len(piece) - 1):
            for d in range(-SEAM, SEAM + 1):
                if base <= edge + d < base + size:
                    want.add(edge + d)
    step = max(1, size // SPREAD)
    want.update(range(base, base + size, step))
    want.add(base + size - 1)
    return sorted(want)


def check_refuses_6502(base):
    """Boot the same disk on a 6502 and require a clean refusal.

    On an NMOS 6502 the long store the copier needs ($9F) is an unstable
    undocumented opcode, so "it crashes" is not an acceptable answer: nothing
    may be written at all.  src/farload.s identifies the CPU before its first
    store and prints a line instead.
    """
    fails = []
    emu = launch(tag="m6no816", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)                       # no CPU switch: still a 6502
        before = b.cmd(f"EVAL db(${base:06x})").get("value")
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(250)
        mode = b.cmd("HWSTATE").get("cpu", {}).get("mode")
        after = b.cmd(f"EVAL db(${base:06x})").get("value")
        came_up = bytes(b.memdump(STATUS, 2)) == b"VD"

        # The message goes to E: through CIO, so it is in the text screen that
        # SAVMSC points at, in ATASCII.
        savmsc = b.peek(0x58) | (b.peek(0x59) << 8)
        screen = bytes(b.memdump(savmsc, 960)) if savmsc else b""
        said = b"gem4xe" in bytes((c + 32) & 0xFF if c < 64 else c for c in screen)
        print(f"on a 6502  : CPU {mode}, bank $01 ${before:02X}->${after:02X}, "
              f"runner {'STARTED' if came_up else 'did not start'}, "
              f"message {'on screen' if said else 'NOT FOUND'}")
        if mode != "6502":
            fails.append(f"the refusal check needs a 6502; the CPU is {mode}")
        elif after != before:
            fails.append(f"bank $01 was written on a 6502: ${before:02X} -> ${after:02X}")
        if came_up:
            fails.append("the runner started on a 6502; the guard did not fire")
        if not said:
            fails.append("no diagnostic reached the screen; the user is told nothing")
    finally:
        emu.stop()
    return fails


def main():
    segs, syms = mkxex.read_elf(ELF)
    far = [(a, d) for a, d in segs if a > 0xFFFF]
    if not far:
        print("gem4xe-m6: FAILED\n   FAIL: no far segments; is --code-model=large set?")
        return 1
    base, img = far[0]
    chunk = syms["_fl_scr"] - syms["_fl_buf"]
    probes = probe_addresses(base, len(img), chunk)
    print(f"far image  : ${base:06X}-${base + len(img) - 1:06X}  "
          f"({len(img)} bytes, {-(-len(img) // chunk)} chunks of {chunk})")

    fails = []
    emu = launch(tag="m6", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(250)
        if bytes(b.memdump(STATUS, 2)) != b"VD":
            print("gem4xe-m6: FAILED\n   FAIL: runner did not come up")
            return 1

        # 1. the image arrived intact
        bad = []
        for a in probes:
            r = b.cmd(f"EVAL db(${a:06x})")
            if r.get("value") != img[a - base]:
                bad.append((a, img[a - base], r.get("value")))
        print(f"copy-up    : {len(probes) - len(bad)}/{len(probes)} probed bytes match")
        if bad:
            for a, w, g in bad[:6]:
                got = "??" if g is None else f"${g:02X}"
                print(f"   ${a:06X}: linker says ${w:02X}, target has {got}")
            fails.append(f"{len(bad)} of {len(probes)} probed bytes differ; "
                         f"first at ${bad[0][0]:06X}")

        # 2. it is running there.  The bridge reports a 16-bit PC and no K
        #    register, so the answer has to come from the target: a routine in
        #    `farcode` does phk and the runner publishes what it said.
        ran = b.peek(STATUS + 24)
        want_bank = base >> 16
        mode = b.cmd("HWSTATE").get("cpu", {}).get("mode")
        print(f"execution  : far code reports bank ${ran:02X} "
              f"(linked for ${want_bank:02X}), CPU {mode}")
        if mode != "65C816":
            fails.append(f"CPU is not the 65C816: {mode}")
        if ran != want_bank:
            fails.append(f"far code is executing in bank ${ran:02X}, not "
                         f"${want_bank:02X} -- the copy-up did not take effect")

        # 3. the far heap is above the far code
        heap_first = b.peek(STATUS + 17)
        want_first = ((base + len(img) - 1) >> 16) + 1
        print(f"far heap   : starts at bank ${heap_first:02X} "
              f"(code reaches bank ${(base + len(img) - 1) >> 16:02X})")
        if heap_first < want_first:
            fails.append(f"far heap starts at bank ${heap_first:02X}: it overlaps "
                         f"the code, which reaches ${base + len(img) - 1:06X}")

        # 4. staging cost nothing: it is inside the reserved MEMAC A window
        hdr, scr = syms["_fl_hdr"], syms["_fl_scr"]
        print(f"staging    : ${hdr:04X}-${scr + 15:04X} (MEMAC A window)")
        if not (0x8000 <= hdr and scr + 16 <= 0xA000):
            fails.append(f"staging buffer ${hdr:04X}-${scr + 15:04X} is outside the "
                         f"MEMAC A window, so it costs real bank $00 space")
    finally:
        emu.stop()

    fails += check_refuses_6502(base)

    print()
    if fails:
        print("gem4xe-m6: FAILED")
        for f in fails:
            print("   FAIL:", f)
        return 1
    print("gem4xe-m6: PASSED -- far code copied up and running in bank $01")
    return 0


if __name__ == "__main__":
    sys.exit(main())
