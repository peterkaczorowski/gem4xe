#!/usr/bin/env python3
"""The 1bpp font strips, read from the files the target links.

A face belongs to the DEVICE -- 8x8 where there are 80 columns to spend
and Atari's condensed 6x6 where there are not (src/vdi/vdidev.h) -- so
this is where the strips live, below both the VDI model and the two
surface models, and it imports neither.  It exists because putting them
in vdiref made the import graph a cycle the moment devref grew an ANTIC
device: vdiref -> devref -> anticref -> vdiref.

THE STRIP'S LAYOUT is the same for every face and is not the cell's:
FONT_STRIDE bytes to a row, character N's byte on row r at
r * FONT_STRIDE + N, the glyph LEFT-ALIGNED in that byte.  A 6x6 face
uses the top six bits of six rows; an 8x8 one uses all of eight.  That
is what lets one blitter path draw either (src/vdi/vdi.h).

Read from the generated C rather than from EmuTOS, so the model cannot
drift from what the target actually links.
"""
import os
import re

FONT_STRIDE = 256

_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "vdi")


def load_strip(path, rows):
    """The bytes of a generated strip: `rows` rows of FONT_STRIDE."""
    text = open(path).read()
    body = text[text.index("{"):]
    data = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    want = FONT_STRIDE * rows
    if len(data) < want:
        raise RuntimeError(f"{path}: {len(data)} bytes, expected {want}")
    return bytes(data[:want])


# The GEM 8x8 system font (EmuTOS bios/fnt_st_8x8.c, via tools/fontconv.py)
FONT_8X8 = load_strip(os.path.join(_SRC, "font8x8.c"), 8)

# Atari's condensed 6x6, the face the ST uses for icon labels in low
# resolution (bios/fnt_st_6x6.c, via tools/fontconv6.py).  Six bits per
# character in the donor; repacked to a byte here, which is the whole
# reason that converter exists.
FONT_6X6 = load_strip(os.path.join(_SRC, "font6x6.c"), 6)
