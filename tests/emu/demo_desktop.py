#!/usr/bin/env python3
"""Draw a GEM-style desktop through the VDI and photograph it.

Every pixel here goes through the same 37 opcodes the AES uses -- no back
doors, no direct framebuffer writes.  It is a demo, but it is also a test: the
result is compared against tools/vdiref.py like any conformance case, so the
screenshot cannot quietly rot.

  python3 tests/emu/demo_desktop.py [-o build/shots/desktop.png]
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from a8test.launcher import launch      # noqa: E402
import vbxeref, vdiref, symfile         # noqa: E402
from vdiref import (V_CLRWK, V_PLINE, VSL_COLOR, VSF_COLOR, VSF_INTERIOR,     # noqa: E402
                    VR_RECFL, V_GTEXT, VST_COLOR, VSWR_MODE, VS_CLIP,
                    VSC_FORM, V_SHOW_C, V_LOCATOR, VRT_CPYFM)

DISK = os.path.abspath(os.path.join(ROOT, "build", "m3-boot.atr"))
SYMS = os.path.join(ROOT, "build", "m3.sym")
STATUS, ST_GO, ST_DONE = 0x0600, 3, 4

WHITE, BLACK, GREY, DGREY = 0, 1, 8, 9

# GEM's arrow, and a folder icon drawn as a 1-plane form for vrt_cpyfm.
ARROW_MASK = (0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
              0xFF80, 0xFC00, 0xEC00, 0xCE00, 0x0600, 0x0700, 0x0300, 0x0000)
ARROW_DATA = (0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
              0x7800, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000, 0x0000)


def folder_form():
    """A 32x24 folder outline, 2 words (4 bytes) per row."""
    w, h, wdw = 32, 24, 2
    rows = [[0] * (wdw * 2) for _ in range(h)]

    def px(x, y):
        if 0 <= x < w and 0 <= y < h:
            rows[y][x >> 3] |= 0x80 >> (x & 7)

    for x in range(2, 14):                      # tab
        px(x, 3)
    px(1, 4); px(14, 4)
    for x in range(1, 31):                      # top edge
        px(x, 5)
    for y in range(5, 22):                      # sides
        px(1, y); px(30, y)
    for x in range(1, 31):                      # bottom
        px(x, 21)
    for x in range(4, 28, 3):                   # a few "documents"
        px(x, 9); px(x, 13); px(x, 17)
    return bytes(b for r in rows for b in r), w, h, wdw


ICON_BITS, ICON_W, ICON_H, ICON_WDW = folder_form()


def txt(x, y, s, color=BLACK):
    return [(VST_COLOR, (), (color,)), (V_GTEXT, (x, y), tuple(s.encode()))]


def rect(x1, y1, x2, y2, color):
    return [(VSF_COLOR, (), (color,)), (VR_RECFL, (x1, y1, x2, y2), ())]


def frame(x1, y1, x2, y2, color=BLACK):
    return [(VSL_COLOR, (), (color,)),
            (V_PLINE, (x1, y1, x2, y1, x2, y2, x1, y2, x1, y1), ())]


def build():
    """The desktop, as a list of scripts (the runner takes 512 words a time)."""
    s1 = [(V_CLRWK,), (VSWR_MODE, (), (2,)), (VSF_INTERIOR, (), (1,))]
    s1 += rect(0, 0, 639, 239, GREY)                       # desktop
    s1 += rect(0, 0, 639, 13, WHITE)                       # menu bar
    s1 += [(VSL_COLOR, (), (BLACK,)), (V_PLINE, (0, 14, 639, 14), ())]
    s1 += txt(8, 10, "Desk")
    s1 += txt(56, 10, "File")
    s1 += txt(104, 10, "View")
    s1 += txt(152, 10, "Options")

    # an open menu, as the AES would drop it
    s2 = rect(48, 15, 199, 78, WHITE)
    s2 += frame(48, 15, 199, 78)
    s2 += txt(56, 24, "Open")
    s2 += txt(56, 34, "Show Info...")
    s2 += [(VSL_COLOR, (), (BLACK,)), (V_PLINE, (49, 39, 198, 39), ())]
    s2 += txt(56, 48, "Format...")
    s2 += txt(56, 58, "Delete")
    s2 += rect(49, 62, 198, 71, BLACK)                     # highlighted item
    s2 += txt(56, 70, "Quit", WHITE)

    # a window: title bar, close box, content
    s3 = rect(230, 40, 590, 210, WHITE)
    s3 += frame(230, 40, 590, 210)
    s3 += rect(231, 41, 589, 54, WHITE)
    s3 += [(VSL_COLOR, (), (BLACK,)), (V_PLINE, (231, 55, 589, 55), ())]
    s3 += frame(233, 43, 244, 52)                          # close box
    s3 += txt(340, 51, "FLOPPY DISK")
    for i, line in enumerate(("GEM4XE.PRG    11708", "VDI.C         28114",
                              "README.TXT     1024")):
        s3 += txt(300, 90 + i * 12, line)
    return [s1, s2, s3]


def build_icons():
    s = [(VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 40, 100, 0, 0),
          (1, BLACK, WHITE), "icon")]
    s += txt(36, 138, "FLOPPY")
    s += [(VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 40, 160, 0, 0),
           (1, BLACK, WHITE), "icon")]
    s += txt(40, 198, "TRASH")
    return s


def main(argv):
    out = os.path.join(ROOT, "build", "shots", "desktop.png")
    if "-o" in argv:
        out = argv[argv.index("-o") + 1]
    os.makedirs(os.path.dirname(out), exist_ok=True)

    syms = symfile.load(SYMS)
    sa, sc = syms["vdi_script"], syms["vdi_scratch"]

    scripts = build() + [build_icons()]
    cursor = [(V_LOCATOR, (196, 68), ()),
              (VSC_FORM, (), (0, 0, 1, BLACK, WHITE) + ARROW_MASK + ARROW_DATA),
              (V_SHOW_C, (), (0,))]
    scripts.append(cursor)

    ref = vdiref.VDI()
    for sc_ in scripts:
        ref.run([(r[0], r[1] if len(r) > 1 else (), r[2] if len(r) > 2 else (),
                  (ICON_BITS, ICON_WDW) if len(r) > 3 else None) for r in sc_])

    emu = launch(tag="demo", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(200)
        b.memload(sc, ICON_BITS)
        b.memload(sc + 512, vdiref.pack_mfdb(sc, ICON_W, ICON_H, ICON_WDW))

        for chunk in scripts:
            resolved = [(r[0], r[1] if len(r) > 1 else (),
                         r[2] if len(r) > 2 else (),
                         (ICON_BITS, ICON_WDW) if len(r) > 3 else None)
                        for r in chunk]
            words = vdiref.encode(resolved, sc + 512)
            if len(words) > 512:
                raise SystemExit(f"script chunk is {len(words)} words, max 512")
            b.memload(sa, b"".join(struct.pack("<h", w if w < 32768 else w - 65536)
                                   for w in words))
            b.poke(STATUS + ST_DONE, 0)
            b.poke(STATUS + ST_GO, 1)
            for _ in range(150):
                if b.peek(STATUS + ST_DONE) == 0xA5:
                    break
                b.frames(4)
        b.frames(8)
        b.screenshot(out)
    finally:
        emu.stop()

    bad, shown = vbxeref.compare_to_shot(ref.to_rgb(), out)
    total = vbxeref.SCR_W * vbxeref.SCR_H
    print(f"{out}\n  {total - bad}/{total} pixels match the reference")
    if bad:
        print("  first differences:", shown[:3])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
