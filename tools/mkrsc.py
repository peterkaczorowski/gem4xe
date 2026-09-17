#!/usr/bin/env python3
"""The resource file the file layer's gate loads: build/test.rsc.

    python3 tools/mkrsc.py build/test.rsc

`build()` is the description; tests/emu/m12_file.py imports it and asks
tools/rsc.py what the AES should make of the same file, so the bytes on
the disk and the expectation come from one place.  What is in it is
chosen to exercise every fixup rsrc_load makes: a pixel offset that is
negative, one that is exactly +128 (the AES reads the offset as signed
only PAST 128), a width of 80 characters (the whole screen, whatever its
width), a TEDINFO whose lengths the file has wrong, an image, an icon,
free strings and a free image.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsc                                              # noqa: E402
from rsc import ch, NIL                                 # noqa: E402
from aesref import (G_BOX, G_IBOX, G_STRING, G_BUTTON, G_FTEXT, G_BOXTEXT,
                    G_IMAGE, G_ICON, G_CICON, SELECTABLE, EXIT, DEFAULT, EDITABLE,
                    LASTOB, HIDETREE, SHADOWED, TE_LEFT, TE_RIGHT)  # noqa: E402

ICON_ROWS = bytes([
    0x00, 0x7F, 0xFE, 0x00,
    0x01, 0x80, 0x01, 0x80,
    0x02, 0x00, 0x00, 0x40,
    0x04, 0x18, 0x18, 0x20,
    0x04, 0x18, 0x18, 0x20,
    0x04, 0x00, 0x00, 0x20,
    0x04, 0x20, 0x04, 0x20,
    0x04, 0x1F, 0xF8, 0x20,
    0x02, 0x00, 0x00, 0x40,
    0x01, 0x80, 0x01, 0x80,
    0x00, 0x7F, 0xFE, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
])
ICON_MASK = bytes([0xFF] * len(ICON_ROWS))

# The trees, by index, and the free strings and images
DIALOG, ICONS = 0, 1
FS_ONE, FS_TWO = 0, 1
FI_LOGO = 0


def build():
    r = rsc.Rsc()
    logo = r.bitblk(ICON_ROWS, 4, 12, color=1)
    r.tree([
        #  next head tail type      flags                 state     spec
        (NIL, 1,  7, G_BOX,     0,                    SHADOWED, 0x00021100,
         ch(20), ch(6, 4), ch(40), ch(14)),                      # 160,52 320x112
        (2, NIL, NIL, G_STRING, 0, 0, r.string("Resource test"),
         ch(2), ch(1), ch(20), ch(1)),
        (3, NIL, NIL, G_FTEXT,  EDITABLE, 0,
         r.ted("GEM4XE", "Name: __________", "X"),
         ch(2), ch(3), ch(24), ch(1)),
        (4, NIL, NIL, G_BOXTEXT, 0, 0,
         r.ted("right", "", "", just=TE_RIGHT, thickness=2),
         ch(2), ch(5), ch(20), ch(2)),
        (5, NIL, NIL, G_IMAGE,  0, 0, logo,
         ch(30), ch(2), ch(4), ch(1, 4)),                        # 32x12
        (6, NIL, NIL, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, r.string("OK"),
         ch(6, 4), ch(11), ch(9), ch(2, 4)),                     # 72x20
        (7, NIL, NIL, G_BUTTON, SELECTABLE | EXIT, 0, r.string("Cancel"),
         ch(20), ch(11), ch(9), ch(2, 4)),
        # Never drawn (HIDETREE): the two boundary offsets and the screen
        # width.  -2 pixels, +128 pixels, 80 characters.
        (0, NIL, NIL, G_IBOX,   HIDETREE | LASTOB, 0, 0x00000000,
         ch(1, -2), ch(0, 128), ch(80), ch(3, 128)),
    ])
    icon = r.iconblk(ICON_MASK, ICON_ROWS, "LOGO", 32, 12,
                     char=0x1041, xchar=1, ychar=2, xicon=3, yicon=4,
                     xtext=5, ytext=6, wtext=7, htext=8)
    # A COLOUR ICON, which makes this a new-format file: its CICONBLK goes
    # in the extension past rsh_rssize, with one 4-plane colour form so the
    # loader has a form to step over.  The object's spec is its INDEX; the
    # loader makes that the address of the near record it builds.
    cicon = r.cicon(ICON_MASK, ICON_ROWS, "COLOUR", 32, 12,
                    forms=[(4, bytes(ICON_ROWS) * 4, bytes(ICON_MASK))],
                    char=0x2043, xchar=2, ychar=3, xicon=4, yicon=5,
                    xtext=6, ytext=7, wtext=8, htext=9)
    # A SECOND ONE, because one proves nothing about the walk.  Every
    # CICONBLK is read by stepping over the one before it -- its header,
    # its two mono planes, its text and each colour form -- so a stride
    # that is wrong by two bytes leaves the FIRST icon perfect and every
    # one after it built out of the tail of its neighbour.  This one is a
    # different size and carries two forms rather than one, so a stride
    # taken from the wrong icon is wrong twice over.
    cicon2 = r.cicon(ICON_MASK[:24], ICON_ROWS[:24], "SECOND", 32, 6,
                     forms=[(4, bytes(ICON_ROWS[:24]) * 4, bytes(ICON_MASK[:24])),
                            (1, bytes(ICON_ROWS[:24]), bytes(ICON_MASK[:24]))],
                     char=0x3044, xchar=3, ychar=4, xicon=5, yicon=6,
                     xtext=7, ytext=8, wtext=9, htext=10)
    r.tree([
        (NIL, 1, 3, G_BOX, 0, 0, 0x00FF1100, ch(0), ch(0), ch(10), ch(5)),
        (2, NIL, NIL, G_ICON, 0, 0, icon, ch(1), ch(1), ch(4), ch(3)),
        (3, NIL, NIL, G_CICON, 0, 0, cicon, ch(5), ch(1), ch(4), ch(3)),
        (0, NIL, NIL, G_CICON, LASTOB, 0, cicon2, ch(5), ch(3), ch(4), ch(2)),
    ])
    r.free_string("Free string one")
    r.free_string("Second free string, longer")
    r.free_image(logo)
    return r


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    data = build().file()
    with open(argv[1], "wb") as f:
        f.write(data)
    print(f"{argv[1]}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
