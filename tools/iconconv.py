#!/usr/bin/env python3
"""Extract the GEM Desktop's icons from EmuTOS into a Python module.

EmuTOS's desk/icons.c (GPL v2+, generated from its icon.rsc) carries each
icon as two arrays of 16-bit words -- the mask, then the image, 32x32 so
64 words each, bit 15 the leftmost pixel -- and one table of ICONBLKs
naming them with the character drawn in the icon, its position, and the
icon and label rectangles.  The desktop's icons are the first eight:
hard disk, floppy, folder, trash, printer, removable disk, application,
document (deskapp.h's IG_*); the ones under `#if 0` are not built.

The output is checked in (tools/deskicons.py) so that the resource the
desktop loads and the host reference that draws it read the same bits;
tests/host/test_deskicons.py re-parses the donor and fails if the two
ever differ.

  python3 tools/iconconv.py <icons.c> <deskicons.py>
"""
import re
import sys

NAMES = ("hard", "floppy", "folder", "trash", "printer", "removable",
         "application", "document")
WORDS = 64                      # 32 rows of 32 bits


def parse(path):
    with open(path) as f:
        src = f.read()
    src = re.sub(r"#if 0.*?#endif", "", src, flags=re.S)   # the built icons only
    arrays = {}
    for m in re.finditer(r"static const WORD (rs_icon\w+)\[\]\s*=\s*\{(.*?)\};",
                         src, re.S):
        words = [int(w, 16) for w in re.findall(r"0x([0-9a-fA-F]{1,4})", m.group(2))]
        if len(words) != WORDS:
            raise SystemExit(f"{path}: {m.group(1)} has {len(words)} words, not {WORDS}")
        arrays[m.group(1)] = words
    m = re.search(r"const ICONBLK icon_rs_iconblk\[\]\s*=\s*\{(.*)", src, re.S)
    if not m:
        raise SystemExit(f"{path}: icon_rs_iconblk not found")
    rows = re.findall(r"\{\s*\(WORD \*\)(rs_\w+),\s*\(WORD \*\)(rs_\w+),\s*\"\",\s*"
                      r"0x1000\|'(\\000|[A-Z])',\s*([-\d,\s]+)\}", m.group(1))
    if len(rows) != len(NAMES):
        raise SystemExit(f"{path}: {len(rows)} ICONBLKs, expected {len(NAMES)}")
    icons = []
    for name, (mask, data, ch, nums) in zip(NAMES, rows):
        nums = [int(x) for x in nums.replace(",", " ").split()]
        if len(nums) != 10:
            raise SystemExit(f"{path}: {name}: {len(nums)} numbers, expected 10")
        icons.append((name, arrays[mask], arrays[data], 0 if ch == "\\000" else ord(ch),
                      nums))
    return icons


def emit(icons, out):
    w = open(out, "w")
    w.write('"""The GEM Desktop\'s icons, from EmuTOS desk/icons.c by tools/iconconv.py.\n\n'
            "Generated: do not edit.  Each icon is (mask, data, char, xchar, ychar,\n"
            "xicon, yicon, wicon, hicon, xtext, ytext, wtext, htext): the mask and\n"
            "the image as bytes, 32 rows of 4, MSB the leftmost pixel; `char` the\n"
            "letter drawn in the icon (0 for none) at (xchar, ychar); the two\n"
            "rectangles relative to the object.  IG_* index the table as in\n"
            'EmuTOS deskapp.h.\n"""\n\n')
    for k, name in enumerate(NAMES):
        w.write(f"IG_{name.upper()} = {k}\n")
    w.write("\nICONS = [\n")
    for name, mask, data, ch, nums in icons:
        w.write(f"    # {name}\n    (\n")
        for words in (mask, data):
            w.write("        bytes([\n")
            for r in range(0, WORDS, 4):
                row = []
                for wd in words[r:r + 4]:
                    row += [wd >> 8, wd & 0xFF]
                w.write("            " + ", ".join(f"0x{b:02X}" for b in row) + ",\n")
            w.write("        ]),\n")
        w.write(f"        {ch}, " + ", ".join(str(n) for n in nums) + ",\n    ),\n")
    w.write("]\n")
    w.close()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    emit(parse(sys.argv[1]), sys.argv[2])
