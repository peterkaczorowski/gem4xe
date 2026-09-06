#!/usr/bin/env python3
"""Write a GEM .FNT file: a font gem4xe can load at run time.

    python3 tools/mkfnt.py <fnt_xx_8x8.c> <out.fnt>          from EmuTOS
    python3 tools/mkfnt.py --from-strip <in.c> <out.fnt> [--name N] [--id N]
    python3 tools/mkfnt.py --invert <in.fnt> <out.fnt>       a font that is
                                                            visibly not the
                                                            system's

EmuTOS carries five 8x8 faces and four of them are the reason a loadable
font exists at all: `fnt_st_8x8.c` is the Atari ST set gem4xe links in,
and `fnt_l2_8x8.c` (Latin-2), `fnt_ru_8x8.c` (Cyrillic), `fnt_gr_8x8.c`
(Greek) and `fnt_tr_8x8.c` (Turkish) are the character sets a
translation of LANG.RSC needs and the ST set does not have
(docs/shipping.md, section 5).  All are GPL v2-or-later, and this writes
one of them out in the format DRI's own font files use, which is the
format the VDI reads.

THE FORMAT is the `Fonthead` of `fonthdr.h`, 88 bytes big-endian, then
the offset table (`last_ade - first_ade + 2` words, each glyph's x in the
strip) and then the strip itself: `form_width` bytes a row, `form_height`
rows, character N's byte on row r at `r*form_width + N - first_ade`.  A
monospaced 8-wide font is exactly the layout gem4xe expands into VRAM, so
loading one is a copy rather than a conversion (src/vdi/font.c).

The offsets in the header are from the START OF THE FILE, which is what a
loader can use; in memory DRI's are pointers, and EmuTOS's `fntconv.c`
writes files the same way this does.
"""
import os
import re
import struct
import sys

FORM_W, FORM_H = 256, 8
HDR_SIZE = 88

# Fonthead flags (EmuTOS bios/fonthdr.h)
F_DEFAULT = 0x01                  # the system font
F_HORZ_OFF = 0x02                 # a horizontal offset table follows
F_STDFORM = 0x04                  # big-endian ("standard") bit order
F_MONOSPACE = 0x08

FIELDS = ("font_id point first_ade last_ade top ascent half descent bottom "
          "max_char_width max_cell_width left_offset right_offset thicken "
          "ul_size lighten skew flags").split()


def parse_c(path):
    """The strip and the header fields out of an EmuTOS fnt_*.c."""
    src = open(path).read()
    m = re.search(r"static const UWORD dat_table\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit(f"{path}: dat_table not found")
    words = [int(w, 16) for w in re.findall(r"0x([0-9a-fA-F]{4})", m.group(1))]
    data = bytearray()
    for w in words:                      # F_STDFORM: high byte first
        data += bytes(((w >> 8) & 0xFF, w & 0xFF))
    if len(data) < FORM_W * FORM_H:
        raise SystemExit(f"{path}: {len(data)} bytes, expected {FORM_W * FORM_H}")

    head = src[src.index("const Fonthead"):]
    name = re.search(r'"([^"]*)"', head)
    nums = {}
    for line in head.splitlines():
        c = re.search(r"/\*\s*([a-z_]+)\s*\*/", line)
        v = re.match(r"\s*(0x[0-9a-fA-F]+|\d+)\s*,", line)
        if c and v:
            nums[c.group(1)] = int(v.group(1), 0)
    return bytes(data[:FORM_W * FORM_H]), (name.group(1) if name else "8x8"), nums


def parse_strip(path):
    """The strip out of a generated C array (src/vdi/font8x8.c)."""
    src = open(path).read()
    data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", src))
    if len(data) < FORM_W * FORM_H:
        raise SystemExit(f"{path}: {len(data)} bytes, expected {FORM_W * FORM_H}")
    return data[:FORM_W * FORM_H]


def build(strip, name="gem4xe 8x8", font_id=1, point=9, first=0, last=255,
          top=6, ascent=6, half=4, descent=1, bottom=1):
    """The file: header, offset table, strip."""
    n = last - first + 1
    off_table = HDR_SIZE
    dat = off_table + (n + 1) * 2
    hdr = struct.pack(">HH32sHHHHHHHHHHHHHHHHIIIHHI",
                      font_id, point, name.encode("latin1")[:32].ljust(32, b"\0"),
                      first, last, top, ascent, half, descent, bottom,
                      8, 8,               # widest char, widest cell
                      1, 3,               # left and right offset
                      1, 1,               # thicken, underline
                      0x5555, 0x5555,     # lighten, skew
                      F_STDFORM | F_MONOSPACE | F_DEFAULT,
                      0,                  # no horizontal offset table
                      off_table, dat, FORM_W, FORM_H,
                      0)                  # next font: none
    assert len(hdr) == HDR_SIZE, len(hdr)
    offs = b"".join(struct.pack(">H", 8 * i) for i in range(n + 1))
    return hdr + offs + strip


def read(path):
    """Back again: (strip, name, header dict).  What a loader does."""
    blob = open(path, "rb").read()
    f = struct.unpack_from(">HH32sHHHHHHHHHHHHHHHHIIIHH", blob, 0)
    h = dict(zip(FIELDS, (f[0], f[1]) + f[3:20]))
    h["name"] = f[2].split(b"\0")[0].decode("latin1")
    h["off_table"], h["dat_table"] = f[20], f[21]
    h["form_width"], h["form_height"] = f[22], f[23]
    n = h["form_width"] * h["form_height"]
    return blob[h["dat_table"]:h["dat_table"] + n], h["name"], h


def invert(strip):
    """Every glyph inside out: a font that is unmistakably not the system
    one, for a gate that has no second character set to hand."""
    return bytes(b ^ 0xFF for b in strip)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = [a for a in argv[1:] if a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 2
    src, out = args
    name = next((o[7:] for o in opts if o.startswith("--name=")), None)
    if "--invert" in opts:
        strip, nm, h = read(src)
        data = build(invert(strip), name=name or (nm + " inverted")[:32],
                     font_id=h["font_id"] + 100, point=h["point"])
    elif "--from-strip" in opts:
        data = build(parse_strip(src), name=name or "gem4xe 8x8")
    else:
        strip, nm, nums = parse_c(src)
        data = build(strip, name=name or nm,
                     font_id=nums.get("font_id", 1), point=nums.get("point", 9),
                     top=nums.get("top", 6), ascent=nums.get("ascent", 6),
                     half=nums.get("half", 4), descent=nums.get("descent", 1),
                     bottom=nums.get("bottom", 1))
    with open(out, "wb") as f:
        f.write(data)
    _, nm, h = read(out)
    print(f"{out}: {len(data)} bytes, \"{nm}\", {h['form_width']}x{h['form_height']} "
          f"strip, ADE {h['first_ade']}-{h['last_ade']}, top {h['top']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
