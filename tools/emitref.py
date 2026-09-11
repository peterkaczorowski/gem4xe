#!/usr/bin/env python3
"""The MODEL's side of src/vdi/emit.c -- a page, in PCL 5 or PostScript.

src/vdi/dev_print.c rasterises 640 x 800 dots into far memory and
v_updwk writes them out; this says, byte for byte, what it must write.
tests/emu/m30_print.py runs the real thing on the Atari and compares the
file it produced against `pcl()` and `ps()` here.

WHY A MODEL AND NOT A LOOK.  A page of PCL is not inspectable by eye and
a wrong one does not fail: it prints, and comes out blank, or shifted by
a row, or inverted, and the machine that told you is a laser printer in
another room.  So the bytes are pinned here, and two DECODERS below take
them back to a page -- `from_pcl` by hand, and PostScript by handing it
to Ghostscript, which is the only honest way to check a PostScript
program says what it means.
"""
PR_W, PR_H = 640, 800
PR_STRIDE = PR_W // 8
PR_BYTES = PR_STRIDE * PR_H
PR_DPI = 100

# GEM4XE.CFG's PRINTER=, and src/vdi/print.h's PR_* -- the same numbers.
PR_NONE, PR_PCL, PR_PS = 0, 1, 2


def blank():
    return bytearray(PR_BYTES)


def row_of(page, y):
    return bytes(page[y * PR_STRIDE:(y + 1) * PR_STRIDE])


def pcl(page):
    """emit_pcl().  A 1 bit is a dot, which is what PCL means by one too,
    so the rows go out as they lie.  Blank rows are skipped with
    `ESC *b<n>Y` -- most of a page of GEM is blank, and the skip is the
    difference between 64,000 bytes down a serial line and a few
    thousand."""
    out = bytearray()
    out += b"\033E\033*t%dR\033*r0A" % PR_DPI
    skip = 0
    for y in range(PR_H):
        r = row_of(page, y)
        if not any(r):
            skip += 1
            continue
        if skip:
            out += b"\033*b%dY" % skip
            skip = 0
        out += b"\033*b%dW" % PR_STRIDE + r
    out += b"\033*rC\033E"
    return bytes(out)


def ps(page):
    """emit_ps().  640 x 800 at 100 dpi is 6.4 x 8.0 inches = 460.8 x 576
    points.  The image matrix flips y because PostScript counts from the
    bottom left and a raster from the top left, and the BYTES ARE
    INVERTED because DeviceGray reads 0 as black."""
    out = bytearray()
    out += (b"%!PS-Adobe-3.0\n%%BoundingBox: 76 108 537 684\n"
            b"%%Creator: gem4xe\n%%Pages: 1\n%%EndComments\n")
    out += b"/picstr %d string def\n" % PR_STRIDE
    out += b"gsave\n76 108 translate\n460.8 576 scale\n"
    out += b"%d %d 1 [%d 0 0 -%d 0 %d]\n" % (PR_W, PR_H, PR_W, PR_H, PR_H)
    out += b"{currentfile picstr readhexstring pop} image\n"
    for y in range(PR_H):
        r = row_of(page, y)
        out += b"".join(b"%02X" % (b ^ 0xFF) for b in r)
        out += b"\n"
    out += b"grestore\nshowpage\n%%EOF\n"
    return bytes(out)


def from_pcl(data):
    """PCL 5 back to a page.  Deliberately strict -- it accepts exactly
    what pcl() emits and nothing else, because a decoder that shrugs at
    an unexpected escape is a decoder that would have passed the bug."""
    page = blank()
    i, y = 0, 0
    if not data.startswith(b"\033E\033*t%dR\033*r0A" % PR_DPI):
        raise ValueError("no PCL header")
    i = len(b"\033E\033*t%dR\033*r0A" % PR_DPI)
    while True:
        if data[i:i + 2] == b"\033*" and data[i + 2:i + 3] == b"r":
            if data[i:i + 6] != b"\033*rC\033E":
                raise ValueError("bad trailer at %d" % i)
            if i + 6 != len(data):
                raise ValueError("%d bytes after the trailer" % (len(data) - i - 6))
            return bytes(page)
        if data[i:i + 3] != b"\033*b":
            raise ValueError("unexpected %r at %d" % (data[i:i + 8], i))
        i += 3
        n = 0
        while data[i:i + 1].isdigit():
            n = n * 10 + (data[i] - 48)
            i += 1
        kind = data[i:i + 1]
        i += 1
        if kind == b"Y":
            y += n
        elif kind == b"W":
            if n != PR_STRIDE:
                raise ValueError("row of %d bytes, not %d" % (n, PR_STRIDE))
            page[y * PR_STRIDE:(y + 1) * PR_STRIDE] = data[i:i + n]
            i += n
            y += 1
        else:
            raise ValueError("unknown command %r at %d" % (kind, i - 1))
        if y > PR_H:
            raise ValueError("ran past the page at row %d" % y)
