#!/usr/bin/env python3
"""Phase 33 gate: the VDI itself, on the ANTIC device.

The same src/vdi/vdi.c the VBXE build uses -- its dispatcher, workstation
state, clipping and attributes -- compiled with GEM4XE_DEV_ANTIC and
linked against dev_antic.c instead of dev_vbxe.c.  NOTHING IN THE VDI
WAS CHANGED to make that possible, which is the claim the seam was built
to be able to make, and this gate is what makes it checkable.

The milestone drives the VDI through its own interface (contrl, intin,
ptsin and a call to vdi()) rather than calling the device: a filled
rectangle in pen 1, the same rectangle again in XOR so a hole appears in
it, and a line of text.  The model draws the same three things with
tools/anticref.py's primitives, and every one of the 53,760 pixels has
to agree.

WHAT THE PENS DO HERE IS WORTH READING.  GEM numbers its pens white 0,
black 1.  The device has two colours and no palette, so pen 0 is the
background and anything else is ink -- and mode F gives the background
its hue and set pixels COLPF1's luminance, so a GEM screen comes out
white with black ink without anything having to arrange it.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import anticref                             # noqa: E402
from anticref import AN_W, AN_H, MD_REPLACE, MD_XOR   # noqa: E402

DISK = os.path.abspath(os.path.join(ROOT, "build", "m25-boot.atr"))
SHOT = os.path.abspath(os.path.join(ROOT, "build", "m25.png"))
STATUS = 0x0600
SHOT_X0, SHOT_Y0 = 8, 24
FONT_TOP = 6                                # src/vdi/vdi.h: baseline to top


def model():
    """The three VDI calls the milestone makes, as the device sees them.

    v_gtext is given a BASELINE and the default vertical alignment puts
    the cell's top FONT_TOP above it (vdi.c align_y), which is the one
    piece of VDI arithmetic this gate has to know about -- everything
    else the milestone asks for lands on the device unchanged.
    """
    a = anticref.pattern.__globals__["Antic"]()
    a.clear(0)
    a.rect_mode(20, 20, 200, 60, MD_REPLACE, 1)
    a.rect_mode(60, 30, 160, 50, MD_XOR, 1)
    text = "GEM ON ANTIC"
    for i, ch in enumerate(text):
        a.glyph(ord(ch), 20 + i * 8, 80 - FONT_TOP, MD_REPLACE, 1)
    return a


def main(argv):
    keep = "--shot" in argv
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")
        return cond

    emu = launch(tag="m25", memsize="1088K", vbxe=False,
                 extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)                # -> 65C816 (resets; DOS reboots)
        b.frames(500)
        for k in ("M", "2", "5", "RETURN"):
            b.key(k)
            b.frames(6)
        for t in range(0, 4000, 50):
            b.frames(50)
            if bytes(b.memdump(STATUS, 3)) == b"AVK":
                break
        st = bytes(b.memdump(STATUS, 3))
        if not check(st == b"AVK", f"the VDI did not draw (status {st!r})"):
            return 1
        cpu = b.cmd("HWSTATE")["cpu"]["mode"]
        check(cpu == "65C816", f"CPU is {cpu}")
        print(f"  the VDI opened a workstation on the ANTIC device, {cpu}, "
              f"no VBXE in the machine")

        # -- what the AES made of the device -------------------------
        # gsx_start asks the VDI for the extent, the depth and the system
        # font's cell, and everything it lays out afterwards comes off
        # those.  The gate does the same arithmetic the AES does (graf.c
        # gsx_start) rather than checking numbers someone wrote down: the
        # claim is that a GEM ADAPTS to a second screen, and a constant
        # would not be evidence of it.
        g = [b.peek16(0x0610 + i * 2) for i in range(10)]
        (width, height, planes, wchar, hchar, wbox, hbox,
         menu_w, menu_h, full_h) = g
        check((width, height) == (AN_W, AN_H),
              f"the AES thinks the screen is {width}x{height}, not "
              f"{AN_W}x{AN_H}")
        check(planes == 1,
              f"the AES thinks the device has {planes} planes, not 1 -- it "
              f"sizes its menu save buffer from that")
        check((wchar, hchar) == (8, 8),
              f"the system font's cell came back {wchar}x{hchar}")
        check(hbox == hchar + 3,
              f"a box is {hbox} tall, not the cell plus three ({hchar + 3})")
        check(wbox == max(hbox * 372 // 372, wchar + 4),
              f"a box is {wbox} wide; the pixels are square here so it is "
              f"the taller of hbox and wchar+4")
        check((menu_w, menu_h) == (width, hbox),
              f"the menu bar is {menu_w}x{menu_h}, not the screen's width "
              f"by a box's height")
        check(full_h == height - hbox,
              f"the desk under the menu is {full_h} tall, not {height - hbox}")
        print(f"  the AES laid out on {width}x{height}, {planes} plane: "
              f"cell {wchar}x{hchar}, box {wbox}x{hbox}, menu bar {menu_h} "
              f"tall, desk {full_h}")

        b.frames(20)
        b.screenshot(SHOT)
        from PIL import Image
        im = Image.open(SHOT).convert("RGB")
        px = im.load()

        want = model()
        seen = {px[SHOT_X0 + x, SHOT_Y0 + y]
                for y in range(AN_H) for x in range(AN_W)}
        check(len(seen) == 2,
              f"the playfield holds {len(seen)} colours, not 2: {sorted(seen)}")
        if len(seen) != 2:
            return 1
        clear = next((x, y) for y in range(AN_H) for x in range(AN_W)
                     if not want.bit(x, y))
        setpx = next((x, y) for y in range(AN_H) for x in range(AN_W)
                     if want.bit(x, y))
        bg = px[SHOT_X0 + clear[0], SHOT_Y0 + clear[1]]
        fg = px[SHOT_X0 + setpx[0], SHOT_Y0 + setpx[1]]
        check(bg != fg, f"pen 0 and pen 1 are the same colour {bg}")
        check(sum(bg) > sum(fg),
              f"pen 0 {bg} is not lighter than pen 1 {fg}: GEM's pen 0 is "
              f"WHITE and its 1 is black, and on this device that has to "
              f"come out of the luminance without anyone arranging it")
        print(f"  pen 0 (white) {bg}, pen 1 (black) {fg}")

        bad, first = 0, []
        for y in range(AN_H):
            for x in range(AN_W):
                exp = fg if want.bit(x, y) else bg
                if px[SHOT_X0 + x, SHOT_Y0 + y] != exp:
                    bad += 1
                    if len(first) < 4:
                        first.append((x, y, want.bit(x, y)))
        check(not bad, f"{bad} of {AN_W * AN_H} pixels differ from the model; "
                       f"first (x, y, wanted bit) {first}")
        print(f"  {AN_W * AN_H - bad:,} of {AN_W * AN_H:,} pixels as the "
              f"model has them")
    finally:
        emu.stop()

    if not fails and not keep and os.path.exists(SHOT):
        os.remove(SHOT)
    ok = not fails
    print(f"gem4xe-m25: {'PASS' if ok else 'FAIL'} -- the VDI on ANTIC, "
          f"{len(fails)} problem(s)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
