#!/usr/bin/env python3
"""Phase 32 gate: the ANTIC surface, on a machine with no VBXE in it.

The second display gem4xe can draw on -- stock ANTIC mode F, 320 pixels a
line and one bit each -- brought up and drawn on with nothing but the
CPU, then compared bit for bit against tools/anticref.py.

WHY THIS GATE EXISTS AT ALL.  The plan has said since phase 0 that the
ANTIC driver's job is not to be comfortable but to be PROOF THE SEAM IS
REAL: it is the thing that stops the VBXE's assumptions leaking into
code that is supposed to be portable.  A driver nobody runs proves
nothing, so this boots the emulator with `vbxe=False` -- the machine
really has no VBXE -- and requires a picture.

WHAT IT CHECKS, and what it deliberately does not.  The screen must hold
EXACTLY TWO colours, because mode F is a hires mode and a third colour
means the display list is not saying what this file thinks it says.
Which two they are is GTIA's business and not gem4xe's, so the gate does
not hardcode a palette: it takes the colour under a pixel the model says
is clear as the background and the one under a set pixel as the
foreground, requires them to differ, and then requires all 53,760 pixels
to agree with the model.  That is exact about the thing the driver
controls -- the bits -- and silent about the thing it does not.

The pattern is chosen for what it catches; src/m24_antic.c says which
part catches what.  The one worth naming here is the wide bar across
line 96, where ANTIC's memory counter wraps a 4 KB boundary: get the
second LMS wrong and the bottom of that bar is drawn from the top of the
screen, and nothing else on the screen is wrong enough to notice.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import anticref                             # noqa: E402
from anticref import AN_W, AN_H             # noqa: E402

DISK = os.path.abspath(os.path.join(ROOT, "build", "m24-boot.atr"))
SHOT = os.path.abspath(os.path.join(ROOT, "build", "m24.png"))
STATUS = 0x0600
# Where the playfield lands in a screenshot, measured on the machine: a
# normal-width mode F playfield is 320 hires pixels and the shot has one
# pixel for each of them.
SHOT_X0, SHOT_Y0 = 8, 24


def main(argv):
    keep = "--shot" in argv
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")
        return cond

    emu = launch(tag="m24", memsize="1088K", vbxe=False,
                 extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)                # -> 65C816 (resets; DOS reboots)
        b.frames(500)
        for k in ("M", "2", "4", "RETURN"):
            b.key(k)
            b.frames(6)
        for t in range(0, 3000, 50):
            b.frames(50)
            if bytes(b.memdump(STATUS, 3)) == b"ANK":
                break
        st = bytes(b.memdump(STATUS, 3))
        if not check(st == b"ANK", f"the program did not draw (status {st!r})"):
            return 1
        cpu = b.cmd("HWSTATE")["cpu"]["mode"]
        check(cpu == "65C816", f"CPU is {cpu}")
        print(f"  M24.COM drew on a machine with no VBXE, on the {cpu}")

        b.frames(20)
        b.screenshot(SHOT)
        from PIL import Image
        im = Image.open(SHOT).convert("RGB")
        px = im.load()

        want = anticref.pattern()

        # v_get_pixel, which no picture can check: the eight answers the
        # program stored, against the same eight from the model.
        got_px = list(b.memdump(STATUS + 4, 8))
        exp_px = anticref.pixels(want)
        check(got_px == exp_px,
              f"v_get_pixel answered {got_px}, the model says {exp_px}")
        print(f"  v_get_pixel: {got_px} as the model has them")
        seen = {px[SHOT_X0 + x, SHOT_Y0 + y]
                for y in range(AN_H) for x in range(AN_W)}
        check(len(seen) == 2,
              f"the playfield holds {len(seen)} colours, not 2: {sorted(seen)}")
        if len(seen) != 2:
            return 1

        # which is which, from the model rather than from a palette
        clear = next((x, y) for y in range(AN_H) for x in range(AN_W)
                     if not want.bit(x, y))
        setpx = next((x, y) for y in range(AN_H) for x in range(AN_W)
                     if want.bit(x, y))
        bg = px[SHOT_X0 + clear[0], SHOT_Y0 + clear[1]]
        fg = px[SHOT_X0 + setpx[0], SHOT_Y0 + setpx[1]]
        check(bg != fg, f"background and foreground are the same colour {bg}")
        print(f"  two colours: background {bg}, foreground {fg}")

        bad, first = 0, []
        for y in range(AN_H):
            for x in range(AN_W):
                got = px[SHOT_X0 + x, SHOT_Y0 + y]
                exp = fg if want.bit(x, y) else bg
                if got != exp:
                    bad += 1
                    if len(first) < 4:
                        first.append((x, y, want.bit(x, y)))
        check(not bad,
              f"{bad} of {AN_W * AN_H} pixels differ from the model; "
              f"first (x, y, wanted bit) {first}")
        print(f"  {AN_W * AN_H - bad:,} of {AN_W * AN_H:,} pixels as the "
              f"model has them")
    finally:
        emu.stop()

    if not fails and not keep and os.path.exists(SHOT):
        os.remove(SHOT)
    ok = not fails
    print(f"gem4xe-m24: {'PASS' if ok else 'FAIL'} -- the ANTIC surface, "
          f"{len(fails)} problem(s)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
