#!/usr/bin/env python3
"""Phase 2 gate: VDI conformance.

Each case is a script of VDI calls.  The host runs it through tools/vdiref.py
(the specification) and pokes the identical script to the Atari, which runs it
through src/vdi/vdi.c.  The two framebuffers must match pixel for pixel.

The cases lean hard on 4bpp odd-pixel edges, because a rectangle whose left or
right edge falls in the middle of a byte is exactly where VDI drivers on
packed-pixel devices historically went wrong -- and it is silent when wrong.

Usage: m3_vdi.py [--case N] [--shot]
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from a8test.launcher import launch     # noqa: E402
import vbxeref                         # noqa: E402
import vdiref                          # noqa: E402
import symfile                         # noqa: E402
from vdiref import (V_CLRWK, V_PLINE, VSL_TYPE, VSL_COLOR, VSF_INTERIOR,      # noqa: E402
                    VSF_COLOR, VR_RECFL, VS_CLIP, VRO_CPYFM, V_GTEXT)
from vdiref import (VST_COLOR, VSWR_MODE, VRT_CPYFM, pack_mfdb,           # noqa: E402
                     VSC_FORM, V_SHOW_C, V_HIDE_C, V_LOCATOR,
                     VSIN_MODE, VQIN_MODE, VEX_TIMV, VSL_UDSTY, VQ_MOUSE,
                     VST_HEIGHT, VQT_ATTRIBUTES, V_ESCAPE, VSF_STYLE, VSF_UDPAT)

# A user fill pattern with every row different -- a diagonal -- so that a
# fill anchored to the wrong row, or to the rectangle instead of the screen,
# cannot pass.
UD_DIAG = tuple(0x8000 >> i for i in range(16))

# A GEM-style arrow: mask is the outline+body, data is the white interior.
# Painted mask-then-data, so a mask bit with no data bit is the outline.
ARROW_MASK = (0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
              0xFF80, 0xFC00, 0xEC00, 0xCE00, 0x0600, 0x0700, 0x0300, 0x0000)
ARROW_DATA = (0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
              0x7800, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000, 0x0000)


def cursor_form(xhot=0, yhot=0, bg=1, fg=0):
    """The 37 intin words vsc_form takes."""
    return (xhot, yhot, 1, bg, fg) + ARROW_MASK + ARROW_DATA

DISK = os.path.abspath(os.path.join(ROOT, "build", "m3-boot.atr"))
SYMS = os.path.join(ROOT, "build", "m3.sym")
SHOTDIR = os.path.join(ROOT, "build", "shots")

STATUS, ST_STAGE, ST_GO, ST_DONE = 0x0600, 2, 3, 4

def make_icon():
    """A 32x24 one-plane form: frame, diagonal, dotted row, solid block.
    wdwidth is 2 WORDS (= 4 bytes) per row, MSB-first, as the VDI defines."""
    w, h, wdw = 32, 24, 2
    rows = [[0] * (wdw * 2) for _ in range(h)]

    def setpx(x, y):
        rows[y][x >> 3] |= 0x80 >> (x & 7)

    for x in range(w):
        setpx(x, 0); setpx(x, h - 1)
    for y in range(h):
        setpx(0, y); setpx(w - 1, y)
    for i in range(min(w, h)):
        setpx(i, i)
    for x in range(2, w - 2, 2):
        setpx(x, 4)
    for y in range(14, 20):
        for x in range(20, 28):
            setpx(x, y)
    bits = bytes(b for r in rows for b in r)
    return bits, w, h, wdw


ICON_BITS, ICON_W, ICON_H, ICON_WDW = make_icon()

# --- the cases ----------------------------------------------------------
# Every case starts from a cleared screen so each is independent.
CASES = [
    ("rect byte-aligned", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 63, 31), ())]),

    ("rect odd left edge", [
        (VSF_COLOR, (), (4,)), (VR_RECFL, (1, 2, 40, 20), ())]),

    ("rect odd right edge (x2 even)", [
        (VSF_COLOR, (), (3,)), (VR_RECFL, (10, 5, 50, 25), ())]),

    ("rect both edges odd", [
        (VSF_COLOR, (), (7,)), (VR_RECFL, (7, 3, 41, 19), ())]),

    ("one-pixel column, even x", [
        (VSF_COLOR, (), (1,)), (VR_RECFL, (100, 10, 100, 60), ())]),

    ("one-pixel column, odd x", [
        (VSF_COLOR, (), (1,)), (VR_RECFL, (101, 10, 101, 60), ())]),

    ("rect entirely inside one byte", [
        (VSF_COLOR, (), (6,)), (VR_RECFL, (200, 8, 201, 40), ())]),

    ("adjacent rects must not bleed", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (20, 20, 29, 40), ()),
        (VSF_COLOR, (), (4,)), (VR_RECFL, (30, 20, 39, 40), ()),
        (VSF_COLOR, (), (3,)), (VR_RECFL, (40, 20, 40, 40), ())]),

    # A hollow fill is a pattern with no bits set, and what a clear bit does
    # is the writing mode's decision: replace paints pen 0 (white -- GEM
    # dialogs are opaque because of this), erase paints the pen, and the
    # transparent and XOR modes leave the screen alone.
    ("hollow fill: white in replace, pen in erase, else nothing", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (10, 10, 100, 100), ()),
        (VSF_INTERIOR, (), (0,)),
        (VR_RECFL, (21, 20, 80, 60), ()),
        (VSWR_MODE, (), (2,)), (VR_RECFL, (30, 70, 90, 95), ()),
        (VSWR_MODE, (), (3,)), (VR_RECFL, (0, 0, 15, 15), ()),
        (VSWR_MODE, (), (4,)), (VSF_COLOR, (), (5,)),
        (VR_RECFL, (0, 90, 120, 110), ()),
        (VSWR_MODE, (), (1,)), (VSF_INTERIOR, (), (1,))]),

    # Pattern fills.  The pattern is anchored to the screen (row y AND mask,
    # bit 15 at every 16th pixel), so two fills that abut must tile as one.
    ("dither fill, replace, odd edges, abutting fills tile", [
        (VSF_INTERIOR, (), (2,)), (VSF_STYLE, (), (4,)), (VSF_COLOR, (), (1,)),
        (VR_RECFL, (3, 5, 200, 90), ()),
        (VSF_COLOR, (), (3,)), (VR_RECFL, (201, 5, 300, 90), ()),
        (VSF_STYLE, (), (2,)), (VR_RECFL, (7, 91, 13, 108), ()),
        (VSF_INTERIOR, (), (1,))]),

    ("OEM pattern over a coloured field in all four modes", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 319, 119), ()),
        (VSF_INTERIOR, (), (2,)), (VSF_STYLE, (), (9,)), (VSF_COLOR, (), (1,)),
        (VSWR_MODE, (), (1,)), (VR_RECFL, (10, 10, 79, 50), ()),
        (VSWR_MODE, (), (2,)), (VR_RECFL, (90, 10, 159, 50), ()),
        (VSWR_MODE, (), (3,)), (VR_RECFL, (170, 10, 239, 50), ()),
        (VSWR_MODE, (), (4,)), (VR_RECFL, (250, 10, 319, 50), ()),
        (VSF_STYLE, (), (17,)), (VSF_COLOR, (), (7,)),
        (VSWR_MODE, (), (2,)), (VR_RECFL, (11, 60, 78, 100), ()),
        (VSWR_MODE, (), (4,)), (VR_RECFL, (91, 60, 158, 100), ()),
        (VSWR_MODE, (), (1,)), (VSF_INTERIOR, (), (1,))]),

    # Pen 0 is hardware nibble 0, which the blitter's stencil mode cannot
    # write: transparent and erase fills in pen 0 take a different path.
    ("pattern fill in pen 0 clears pixels in transparent and erase", [
        (VSF_COLOR, (), (4,)), (VR_RECFL, (0, 0, 199, 99), ()),
        (VSF_INTERIOR, (), (2,)), (VSF_STYLE, (), (12,)), (VSF_COLOR, (), (0,)),
        (VSWR_MODE, (), (2,)), (VR_RECFL, (5, 5, 90, 60), ()),
        (VSWR_MODE, (), (4,)), (VR_RECFL, (101, 5, 190, 60), ()),
        (VSWR_MODE, (), (2,)), (VR_RECFL, (20, 70, 20, 95), ()),
        (VSWR_MODE, (), (4,)), (VR_RECFL, (23, 70, 23, 95), ()),
        (VSWR_MODE, (), (1,)), (VR_RECFL, (30, 70, 180, 95), ()),
        (VSF_INTERIOR, (), (1,))]),

    # The expansion holds 16 rows; taller fills go in bands, and a band
    # boundary must not show.  Fine hatches have 16 distinct rows.
    ("hatch fills across the 16-row band boundaries", [
        (VSF_INTERIOR, (), (3,)), (VSF_STYLE, (), (9,)), (VSF_COLOR, (), (6,)),
        (VR_RECFL, (0, 0, 639, 239), ()),                # the whole screen, 15 bands
        (VSF_STYLE, (), (7,)), (VSF_COLOR, (), (1,)),
        (VR_RECFL, (10, 13, 100, 70), ()),
        (VSF_STYLE, (), (3,)), (VSF_COLOR, (), (2,)),
        (VR_RECFL, (110, 31, 200, 33), ()),
        (VSF_STYLE, (), (12,)), (VR_RECFL, (210, 47, 300, 48), ()),
        (VSF_INTERIOR, (), (1,))]),

    ("user pattern: XOR twice restores, and clips", [
        (VSF_COLOR, (), (3,)), (VR_RECFL, (0, 0, 159, 79), ()),
        (VSF_UDPAT, (), UD_DIAG),
        (VSF_INTERIOR, (), (4,)), (VSF_COLOR, (), (5,)),
        (VS_CLIP, (21, 9, 140, 60), (1,)),
        (VSWR_MODE, (), (3,)), (VR_RECFL, (0, 0, 200, 100), ()),
        (VR_RECFL, (60, 30, 200, 100), ()),
        (VS_CLIP, (0, 0, 639, 239), (0,)),
        (VSWR_MODE, (), (1,)), (VR_RECFL, (160, 0, 319, 79), ()),
        (VSF_INTERIOR, (), (1,))]),

    ("vsf_udpat takes 16 words and nothing else", [
        (VSF_UDPAT, (), UD_DIAG),
        (VSF_UDPAT, (), (0xFFFF,) * 8),                 # refused: pattern stays
        (VSF_INTERIOR, (), (4,)), (VSF_COLOR, (), (1,)),
        (VR_RECFL, (0, 0, 63, 47), ()),
        (VSF_UDPAT, (), (0x00FF,) * 16),                # a new one takes effect
        (VR_RECFL, (64, 0, 127, 47), ()),
        (VSF_INTERIOR, (), (1,))]),

    ("clipping to an odd-edged window", [
        (VS_CLIP, (15, 12, 84, 51), (1,)),
        (VSF_COLOR, (), (5,)), (VR_RECFL, (0, 0, 639, 239), ()),
        (VS_CLIP, (0, 0, 639, 239), (0,))]),

    ("horizontal and vertical polylines", [
        (VSL_COLOR, (), (1,)),
        (V_PLINE, (5, 5, 300, 5), ()),
        (V_PLINE, (5, 5, 5, 200), ()),
        (V_PLINE, (7, 199, 301, 199), ()),
        (V_PLINE, (301, 6, 301, 199), ())]),

    ("box as a closed polyline", [
        (VSL_COLOR, (), (4,)),
        (V_PLINE, (11, 11, 90, 11, 90, 70, 11, 70, 11, 11), ())]),

    ("diagonal lines (Bresenham)", [
        (VSL_COLOR, (), (2,)), (V_PLINE, (0, 0, 200, 100), ()),
        (VSL_COLOR, (), (3,)), (V_PLINE, (200, 0, 0, 100), ()),
        (VSL_COLOR, (), (7,)), (V_PLINE, (10, 120, 400, 130), ())]),

    ("dotted and dashed line styles", [
        (VSL_COLOR, (), (1,)),
        (VSL_TYPE, (), (3,)), (V_PLINE, (10, 20, 400, 20), ()),
        (VSL_TYPE, (), (5,)), (V_PLINE, (10, 40, 400, 40), ()),
        (VSL_TYPE, (), (6,)), (V_PLINE, (10, 60, 400, 60), ()),
        (VSL_TYPE, (), (2,)), (V_PLINE, (10, 80, 400, 80), ()),
        (VSL_TYPE, (), (1,))]),

    ("all sixteen pens", [
        (VSF_COLOR, (), (i,)) if False else rec
        for i in range(16)
        for rec in ((VSF_COLOR, (), (i,)),
                    (VR_RECFL, (i * 40, 0, i * 40 + 39, 100), ()))]),

    ("edges at the screen boundary", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 0, 239), ()),
        (VSF_COLOR, (), (3,)), (VR_RECFL, (639, 0, 639, 239), ()),
        (VSF_COLOR, (), (4,)), (VR_RECFL, (0, 239, 639, 239), ())]),

    ("off-screen rect is clipped, not wrapped", [
        (VSF_COLOR, (), (6,)), (VR_RECFL, (600, 200, 700, 300), ())]),

    # --- raster copy.  The blitter has no shifter, so a 4bpp copy is one blit
    # only when source and destination x share parity AND both are even; every
    # other case falls back to the CPU.  Both paths must produce identical
    # pixels, which is the point of testing them side by side.
    ("cpyfm aligned (both even, even width)", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 39, 19), ()),
        (VSF_COLOR, (), (4,)), (VR_RECFL, (0, 20, 39, 39), ()),
        (VRO_CPYFM, (0, 0, 39, 39, 100, 100, 139, 139), (3,))]),

    ("cpyfm unaligned: odd source x", [
        (VSF_COLOR, (), (3,)), (VR_RECFL, (0, 0, 60, 30), ()),
        (VSF_COLOR, (), (7,)), (VR_RECFL, (10, 5, 20, 25), ()),
        (VRO_CPYFM, (1, 0, 40, 30, 200, 60, 239, 90), (3,))]),

    ("cpyfm unaligned: parities differ", [
        (VSF_COLOR, (), (5,)), (VR_RECFL, (0, 0, 60, 30), ()),
        (VSF_COLOR, (), (1,)), (VR_RECFL, (3, 3, 9, 27), ()),
        (VRO_CPYFM, (0, 0, 39, 30, 101, 120, 140, 150), (3,))]),

    ("cpyfm odd width", [
        (VSF_COLOR, (), (6,)), (VR_RECFL, (0, 0, 40, 20), ()),
        (VSF_COLOR, (), (2,)), (VR_RECFL, (5, 5, 8, 15), ()),
        (VRO_CPYFM, (0, 0, 30, 20, 300, 40, 330, 60), (3,))]),

    # --- text.  One 4bpp glyph mask serves every ink colour: AND clears the
    # ink pixels (mode 4 is the one mode that writes 0 rather than skipping),
    # then OR paints them.  Ink 0 needs no OR at all, which is why no inverted
    # mask is kept -- and why mode 6's nibble stencil is not used, since it
    # cannot write colour 0.
    ("text, transparent, black on white", [
        (VSWR_MODE, (), (2,)), (VST_COLOR, (), (1,)),
        (V_GTEXT, (16, 20), tuple(b"GEM for the Atari 8-bit"))]),

    ("text, every printable ASCII row", [
        (VSWR_MODE, (), (2,)), (VST_COLOR, (), (1,)),
        (V_GTEXT, (0, 20), tuple(range(32, 112))),
        (V_GTEXT, (0, 40), tuple(range(112, 192)))]),

    ("text, ink 0 (white) on a filled field -- the AND-only path", [
        (VSF_COLOR, (), (1,)), (VR_RECFL, (0, 0, 400, 60), ()),
        (VSWR_MODE, (), (2,)), (VST_COLOR, (), (0,)),
        (V_GTEXT, (16, 20), tuple(b"INVERTED MENU ITEM"))]),

    ("text, replace mode paints its own background", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 400, 60), ()),
        (VSWR_MODE, (), (1,)), (VST_COLOR, (), (4,)),
        (V_GTEXT, (16, 20), tuple(b"OPAQUE")),
        (VSWR_MODE, (), (2,))]),

    ("text at odd x (CPU fallback) must match", [
        (VSWR_MODE, (), (2,)), (VST_COLOR, (), (1,)),
        (V_GTEXT, (16, 20), tuple(b"even x")),
        (V_GTEXT, (17, 40), tuple(b"odd x"))]),

    ("text clipped, whole and partial glyphs", [
        (VS_CLIP, (40, 16, 143, 39), (1,)),
        (VSWR_MODE, (), (2,)), (VST_COLOR, (), (1,)),
        (V_GTEXT, (8, 22), tuple(b"clipping test line")),
        (V_GTEXT, (8, 34), tuple(b"second line here")),
        (VS_CLIP, (0, 0, 639, 239), (0,))]),

    # XOR, erase and a glyph the screen edge cuts all take the 1bpp raster
    # path (raster_1bpp in vdi.c), the same one vrt_cpyfm uses.
    ("text in XOR and erase modes, and cut by the screen edge", [
        (VSF_INTERIOR, (), (1,)), (VSF_COLOR, (), (3,)),
        (VR_RECFL, (0, 0, 200, 60), ()),
        (VSWR_MODE, (), (3,)), (VST_COLOR, (), (1,)),
        (V_GTEXT, (16, 20), tuple(b"XOR text")),
        (VSWR_MODE, (), (4,)), (VST_COLOR, (), (5,)),
        (V_GTEXT, (16, 40), tuple(b"erase text")),
        (VSWR_MODE, (), (1,)), (VST_COLOR, (), (1,)),
        (V_GTEXT, (-3, 100), tuple(b"left edge")),
        (V_GTEXT, (600, 120), tuple(b"right edge")),
        (V_GTEXT, (300, 3), tuple(b"top")),
        (V_GTEXT, (300, 243), tuple(b"bottom")),
        (VSWR_MODE, (), (3,)),
        (VS_CLIP, (21, 131, 100, 145), (1,)),
        (V_GTEXT, (16, 140), tuple(b"XOR clipped")),
        (VS_CLIP, (0, 0, 0, 0), (0,)),
        (VSWR_MODE, (), (1,))]),

    ("text in every pen", [
        (VSWR_MODE, (), (2,))] + [
        rec for i in range(16)
        for rec in ((VST_COLOR, (), (i,)),
                    (V_GTEXT, (8, 16 + i * 10), tuple(b"pen %02d ABCdef" % i)))]),

    # --- vrt_cpyfm: a ONE-PLANE form expanded into device colours.  This is
    # how the AES draws icons.  The source lives in RAM, out of the blitter's
    # reach: the CPU expands it into AND/OR strips in VRAM and the blitter
    # applies them (it was plotted pixel by pixel until Phase 8b measured
    # that at three frames an icon).
    ("vrt_cpyfm replace: fg and bg both painted", [
        (VSF_COLOR, (), (8,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 16, 16, 0, 0),
         (1, 2, 6), "icon")]),

    ("vrt_cpyfm transparent: background survives", [
        (VSF_COLOR, (), (5,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 16, 16, 0, 0),
         (2, 1, 0), "icon")]),

    ("vrt_cpyfm reverse-transparent paints the clear pixels", [
        (VSF_COLOR, (), (3,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 16, 16, 0, 0),
         (4, 1, 7), "icon")]),

    ("vrt_cpyfm XOR inverts under the set pixels", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 16, 16, 0, 0),
         (3, 0, 0), "icon")]),

    ("vrt_cpyfm at odd destination x", [
        (VSF_COLOR, (), (8,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 17, 9, 0, 0),
         (1, 1, 6), "icon")]),

    ("vrt_cpyfm sub-rectangle of the form", [
        (VSF_COLOR, (), (0,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (8, 4, 23, 19, 40, 20, 0, 0), (1, 4, 6), "icon")]),

    # The strip path clips to the pixel: a clip edge at odd x lands inside a
    # byte, and every mode has its own "leave alone" pair.
    ("vrt_cpyfm clipped by vs_clip at odd edges, every mode", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (0, 0, 300, 80), ()),
        (VS_CLIP, (19, 18, 140, 41), (1,)),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 16, 16, 0, 0),
         (1, 4, 6), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 50, 16, 0, 0),
         (2, 1, 0), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 84, 20, 0, 0),
         (4, 0, 7), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 118, 30, 0, 0),
         (3, 0, 0), "icon"),
        (VS_CLIP, (0, 0, 639, 239), (0,))]),

    ("vrt_cpyfm transparent in pen 0: the AND strip alone", [
        (VSF_COLOR, (), (8,)), (VR_RECFL, (0, 0, 200, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 17, 16, 0, 0),
         (2, 0, 0), "icon")]),

    ("vrt_cpyfm at the screen corners", [
        (VSF_COLOR, (), (6,)), (VR_RECFL, (0, 0, 639, 239), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, -5, -3, 0, 0),
         (1, 1, 0), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 621, 226, 0, 0),
         (2, 1, 0), "icon")]),

    # Blitter mode 6, the nibble stencil, cannot write hardware 0, so white
    # takes the other blits: a replace with a white pen is a plain copy of
    # the strip when every strip byte lies inside the clip, and an OR strip
    # under a one-row AND blit when the first or the last does not; a raster
    # that writes only white is an AND strip.  Each of those, at even and odd
    # x, from a shifted source, and cut by a clip.
    ("vrt_cpyfm with white: the copy, the AND row, the AND strip", [
        (VSF_COLOR, (), (6,)), (VR_RECFL, (0, 0, 300, 80), ()),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 16, 4, 0, 0),
         (1, 1, 0), "icon"),
        (VRT_CPYFM, (3, 2, 24, 21, 60, 4, 0, 0), (1, 0, 1), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 101, 4, 0, 0),
         (1, 1, 0), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 150, 30, 0, 0),
         (4, 3, 0), "icon"),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 201, 30, 0, 0),
         (1, 0, 0), "icon"),
        (VS_CLIP, (251, 33, 278, 50), (1,)),
        (VRT_CPYFM, (0, 0, ICON_W - 1, ICON_H - 1, 248, 30, 0, 0),
         (1, 0, 1), "icon"),
        (VS_CLIP, (0, 0, 639, 239), (0,))]),

    # --- mouse cursor.  The pointer is placed with v_locator, which is how
    # GEM itself sets the locator's initial position (gsx_setmousexy), so no
    # test-only back door is needed.  Everything below the seam in
    # src/vdi/pointer.h is irrelevant here: the VDI only ever sees an
    # absolute position.
    ("cursor on a plain background", [
        (V_LOCATOR, (100, 60), ()),
        (VSC_FORM, (), cursor_form()),
        (V_SHOW_C, (), (0,))]),

    ("cursor over drawn content, then hidden again -- must restore exactly", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (80, 40, 200, 120), ()),
        (VSF_COLOR, (), (4,)), (VR_RECFL, (100, 55, 130, 75), ()),
        (V_LOCATOR, (110, 60), ()),
        (VSC_FORM, (), cursor_form()),
        (V_SHOW_C, (), (0,)),
        (V_HIDE_C, (), ())]),

    ("cursor moved across content leaves no trail", [
        (VSF_COLOR, (), (6,)), (VR_RECFL, (0, 0, 300, 120), ()),
        (VSC_FORM, (), cursor_form()),
        (V_LOCATOR, (40, 40), ()), (V_SHOW_C, (), (0,)),
        (V_HIDE_C, (), ()), (V_LOCATOR, (41, 41), ()), (V_SHOW_C, (), (0,)),
        (V_HIDE_C, (), ()), (V_LOCATOR, (77, 53), ()), (V_SHOW_C, (), (0,)),
        (V_HIDE_C, (), ()), (V_LOCATOR, (150, 90), ()), (V_SHOW_C, (), (0,))]),

    ("cursor at odd x (save span is 9 bytes, not 8)", [
        (VSF_COLOR, (), (3,)), (VR_RECFL, (0, 0, 200, 100), ()),
        (V_LOCATOR, (101, 51), ()),
        (VSC_FORM, (), cursor_form()),
        (V_SHOW_C, (), (0,))]),

    ("cursor with a hotspot offset", [
        (VSF_COLOR, (), (5,)), (VR_RECFL, (0, 0, 200, 100), ()),
        (V_LOCATOR, (60, 50), ()),
        (VSC_FORM, (), cursor_form(7, 7)),
        (V_SHOW_C, (), (0,))]),

    ("cursor clipped at the screen edges", [
        (VSF_COLOR, (), (8,)), (VR_RECFL, (0, 0, 639, 239), ()),
        (VSC_FORM, (), cursor_form()),
        (V_LOCATOR, (2, 2), ()), (V_SHOW_C, (), (0,)), (V_HIDE_C, (), ()),
        (V_LOCATOR, (634, 234), ()), (V_SHOW_C, (), (0,))]),

    # Real GEM's pointer is drawn wherever it is, whatever vs_clip says.
    # Both the target and the model once clipped it -- and agreed.
    ("cursor ignores vs_clip: drawn whole under a clip that excludes it", [
        (VSF_COLOR, (), (4,)), (VR_RECFL, (0, 0, 200, 100), ()),
        (VS_CLIP, (300, 150, 400, 200), (1,)),
        (V_LOCATOR, (95, 45), ()),
        (VSC_FORM, (), cursor_form()),
        (V_SHOW_C, (), (0,)),
        (VS_CLIP, (0, 0, 639, 239), (0,))]),

    ("hide nesting: two hides need two shows", [
        (VSF_COLOR, (), (7,)), (VR_RECFL, (0, 0, 200, 100), ()),
        (V_LOCATOR, (90, 50), ()),
        (VSC_FORM, (), cursor_form()),
        (V_SHOW_C, (), (0,)),
        (V_HIDE_C, (), ()), (V_HIDE_C, (), ()),
        (V_SHOW_C, (), (1,)),          # still hidden: one show, two hides
        ]),

    ("hide nesting: the matching second show brings it back", [
        (VSF_COLOR, (), (7,)), (VR_RECFL, (0, 0, 200, 100), ()),
        (V_LOCATOR, (90, 50), ()),
        (VSC_FORM, (), cursor_form()),
        (V_SHOW_C, (), (0,)),
        (V_HIDE_C, (), ()), (V_HIDE_C, (), ()),
        (V_SHOW_C, (), (1,)), (V_SHOW_C, (), (1,))]),

    # --- returned values.  These opcodes draw nothing, so the pixel check
    # cannot see them at all; they are verified by comparing what each call
    # RETURNS against the reference, call for call.
    ("attribute setters report what they selected", [
        (VSL_COLOR, (), (7,)), (VSL_COLOR, (), (99,)),      # out of range -> 1
        (VSF_COLOR, (), (3,)), (VSF_COLOR, (), (-4,)),
        (VSF_INTERIOR, (), (2,)), (VSF_INTERIOR, (), (9,)),  # -> 0
        (VSF_INTERIOR, (), (2,)), (VSF_STYLE, (), (24,)),
        (VSF_STYLE, (), (25,)), (VSF_STYLE, (), (0,)),       # -> 1, 1
        (VSF_INTERIOR, (), (3,)), (VSF_STYLE, (), (12,)),
        (VSF_STYLE, (), (13,)),                              # -> 1
        (VSF_INTERIOR, (), (1,)), (VSF_STYLE, (), (20,)),    # solid: hatch range -> 1
        (VST_COLOR, (), (5,)),
        (VSWR_MODE, (), (3,)), (VSWR_MODE, (), (7,))]),      # -> replace

    ("input modes round-trip through vsin/vqin", [
        (VSIN_MODE, (), (1, 1)), (VQIN_MODE, (), (1,)),
        (VSIN_MODE, (), (1, 2)), (VQIN_MODE, (), (1,)),
        (VSIN_MODE, (), (4, 1)), (VQIN_MODE, (), (4,)),
        (VQIN_MODE, (), (2,)),                               # untouched: 2
        (VSIN_MODE, (), (9, 1)), (VQIN_MODE, (), (1,))]),    # bad dev ignored

    ("locator and vq_mouse agree on position", [
        (V_LOCATOR, (123, 45), ()),
        (VQ_MOUSE, (), ()),
        (V_LOCATOR, (700, 300), ()),                         # clamped
        (VQ_MOUSE, (), ()),
        (V_LOCATOR, (-5, -5), ()),
        (VQ_MOUSE, (), ())]),

    # Axis-aligned styled lines are pattern blits (style_line in vdi.c): the
    # style anchored to the line's first point, in either direction, at odd
    # ends, in every writing mode, and clipped without losing its phase.
    ("styled lines: backwards, vertical, odd ends, every mode, clipped", [
        (VSL_COLOR, (), (1,)), (VSL_TYPE, (), (3,)),
        (V_PLINE, (401, 20, 11, 20), ()),
        (V_PLINE, (11, 22, 401, 22), ()),
        (V_PLINE, (20, 30, 20, 199), ()),
        (V_PLINE, (23, 199, 23, 30), ()),
        (VSL_UDSTY, (), (0x5555,)), (VSL_TYPE, (), (7,)),
        (VSF_INTERIOR, (), (1,)), (VSF_COLOR, (), (4,)),
        (VR_RECFL, (40, 40, 200, 120), ()),
        (VSWR_MODE, (), (3,)),
        (V_PLINE, (30, 60, 300, 60), ()),
        (V_PLINE, (100, 30, 100, 150), ()),
        (V_PLINE, (300, 62, 30, 62), ()),
        (V_PLINE, (300, 62, 30, 62), ()),
        (VSWR_MODE, (), (2,)), (VSL_COLOR, (), (6,)),
        (V_PLINE, (30, 80, 300, 80), ()),
        (V_PLINE, (101, 30, 101, 150), ()),
        (VSWR_MODE, (), (4,)),
        (V_PLINE, (30, 100, 300, 100), ()),
        (V_PLINE, (102, 30, 102, 150), ()),
        (VSWR_MODE, (), (2,)), (VSL_COLOR, (), (0,)),
        (V_PLINE, (30, 110, 300, 110), ()),
        (V_PLINE, (103, 30, 103, 150), ()),
        (VSWR_MODE, (), (4,)),
        (V_PLINE, (30, 112, 300, 112), ()),
        (V_PLINE, (104, 30, 104, 150), ()),
        (VSWR_MODE, (), (1,)), (VSL_COLOR, (), (1,)),
        (VS_CLIP, (50, 50, 150, 150), (1,)),
        (V_PLINE, (0, 55, 639, 55), ()),
        (V_PLINE, (61, 239, 61, 0), ()),
        (VS_CLIP, (0, 0, 0, 0), (0,)),
        (VSL_TYPE, (), (1,))]),

    # Diagonals step pixel by pixel through the MEMAC window (line_diag in
    # vdi.c): the style rotates with the pixel count from the first point --
    # and restarts at every vertex -- the writing mode decides what a clear
    # style bit does, and the clip is applied per pixel without disturbing
    # the phase, including the pixels a line spends off the screen.
    ("diagonals: styled, every mode, clipped, off the screen", [
        (VSF_INTERIOR, (), (1,)), (VSF_COLOR, (), (4,)),
        (VR_RECFL, (40, 40, 300, 160), ()),
        (VSL_COLOR, (), (1,)), (VSL_TYPE, (), (3,)),
        (V_PLINE, (10, 10, 330, 90), ()),
        (V_PLINE, (330, 95, 10, 15), ()),
        (V_PLINE, (10, 200, 60, 230, 110, 200, 160, 235), ()),
        (VSL_TYPE, (), (1,)), (VSWR_MODE, (), (2,)),
        (V_PLINE, (20, 170, 200, 30), ()),
        (VSL_TYPE, (), (5,)),
        (V_PLINE, (25, 170, 205, 30), ()),
        (VSWR_MODE, (), (3,)),
        (V_PLINE, (30, 170, 210, 30), ()),
        (VSWR_MODE, (), (4,)),
        (V_PLINE, (35, 170, 215, 30), ()),
        (VSWR_MODE, (), (1,)), (VSL_TYPE, (), (1,)),
        (V_PLINE, (-20, -10, 120, 60), ()),
        (V_PLINE, (600, 200, 700, 260), ()),
        (VS_CLIP, (51, 61, 250, 141), (1,)),
        (VSL_TYPE, (), (3,)),
        (V_PLINE, (0, 0, 400, 200), ()),
        (V_PLINE, (400, 0, 0, 200), ()),
        (VSWR_MODE, (), (3,)),
        (V_PLINE, (0, 200, 400, 0), ()),
        (VS_CLIP, (0, 0, 0, 0), (0,)),
        (VSWR_MODE, (), (1,)), (VSL_TYPE, (), (1,))]),

    ("vex_timv reports the tick length", [
        (VEX_TIMV, (), ())]),

    # vsl_udsty installs the pattern used by line type 7, which the AES uses
    # for rubber-band outlines -- so this one IS visible.
    ("user-defined line style", [
        (VSL_COLOR, (), (1,)),
        (VSL_UDSTY, (), (0xF0F0,)), (VSL_TYPE, (), (7,)),
        (V_PLINE, (10, 20, 400, 20), ()),
        (VSL_UDSTY, (), (0xAAAA,)),
        (V_PLINE, (10, 40, 400, 40), ()),
        (VSL_UDSTY, (), (0xFFFF,)),
        (V_PLINE, (10, 60, 400, 60), ()),
        (VSL_TYPE, (), (1,))]),

    ("text metrics report the real cell, not what was asked for", [
        (VST_HEIGHT, (0, 13), ()),          # a size this driver does not have
        (VST_HEIGHT, (0, 8), ()),
        (VST_COLOR, (), (9,)),
        (VQT_ATTRIBUTES, (), ()),
        (VSWR_MODE, (), (2,)),
        (VQT_ATTRIBUTES, (), ()),
        (VSWR_MODE, (), (1,)),
        (V_ESCAPE, (), ())]),               # sub 2/3: nop on a graphics driver

    ("cpyfm overlapping, moving right and down", [
        (VSF_COLOR, (), (4,)), (VR_RECFL, (20, 20, 79, 59), ()),
        (VSF_COLOR, (), (6,)), (VR_RECFL, (30, 30, 49, 49), ()),
        (VRO_CPYFM, (20, 20, 79, 59, 40, 40, 99, 79), (3,))]),

    # --- forms in VRAM.  An MFDB with fd_addr != 0 names a form off the
    # screen; the AES's menu and alert save buffer (bb_save / bb_restore) is
    # one.  "to_save" copies screen -> buffer, "from_save" buffer -> screen.
    ("cpyfm to the save form and back", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (20, 20, 79, 59), ()),
        (VSF_COLOR, (), (6,)), (VR_RECFL, (30, 30, 49, 49), ()),
        (VRO_CPYFM, (20, 20, 79, 59, 20, 20, 79, 59), (3,), "to_save"),
        (VSF_COLOR, (), (0,)), (VR_RECFL, (0, 0, 99, 99), ()),
        (VSF_COLOR, (), (3,)), (VR_RECFL, (40, 40, 44, 44), ()),
        (VRO_CPYFM, (20, 20, 79, 59, 20, 20, 79, 59), (3,), "from_save")]),

    ("cpyfm save form at odd x, odd width: the pixel path into a form", [
        (VSF_COLOR, (), (5,)), (VR_RECFL, (21, 20, 79, 59), ()),
        (VSF_COLOR, (), (1,)), (VR_RECFL, (33, 30, 49, 49), ()),
        (VRO_CPYFM, (21, 20, 79, 59, 21, 20, 79, 59), (3,), "to_save"),
        (VSF_COLOR, (), (0,)), (VR_RECFL, (0, 0, 99, 99), ()),
        (VRO_CPYFM, (21, 20, 79, 59, 21, 20, 79, 59), (3,), "from_save"),
        (VRO_CPYFM, (21, 20, 79, 59, 200, 100, 258, 139), (3,), "from_save")]),

    ("cpyfm from a form, destination clipped by the screen edge", [
        (VSF_COLOR, (), (4,)), (VR_RECFL, (0, 0, 39, 19), ()),
        (VSF_COLOR, (), (7,)), (VR_RECFL, (10, 5, 29, 14), ()),
        (VRO_CPYFM, (0, 0, 39, 19, 0, 0, 39, 19), (3,), "to_save"),
        (VRO_CPYFM, (0, 0, 39, 19, 620, 230, 659, 249), (3,), "from_save")]),

    # The workstation's clip rectangle applies to the screen only: a save
    # under a clip that excludes it must still take the whole rectangle,
    # or the restore after the clip is lifted comes back short.
    ("cpyfm to a form ignores the clip rectangle", [
        (VSF_COLOR, (), (2,)), (VR_RECFL, (100, 50, 179, 89), ()),
        (VS_CLIP, (0, 0, 9, 9), (1,)),
        (VRO_CPYFM, (100, 50, 179, 89, 100, 50, 179, 89), (3,), "to_save"),
        (VS_CLIP, (0, 0, 0, 0), (0,)),
        (VSF_COLOR, (), (0,)), (VR_RECFL, (90, 40, 189, 99), ()),
        (VRO_CPYFM, (100, 50, 179, 89, 100, 50, 179, 89), (3,), "from_save")]),

    # A source past the edge of its form is clipped, and the destination
    # loses the same span.  The donor reads on past the edge instead.
    ("cpyfm source off the screen edge is clipped", [
        (VSF_COLOR, (), (4,)), (VR_RECFL, (0, 0, 29, 19), ()),
        (VSF_COLOR, (), (2,)), (VR_RECFL, (610, 220, 639, 239), ()),
        (VRO_CPYFM, (-10, -10, 29, 19, 100, 100, 139, 129), (3,)),
        (VRO_CPYFM, (620, 230, 659, 269, 200, 100, 239, 139), (3,))]),
]


def poke_script(b, addr, script, mfdb_addr=0, room=None, screen_mfdb=0):
    words = vdiref.encode(script, mfdb_addr, screen_mfdb)
    data = b"".join(struct.pack("<h", w if w < 32768 else w - 65536)
                    for w in words)
    # The runner stops at the end of its buffer, mid-script, and the words
    # past it land on whatever follows.
    assert room is None or len(data) <= room, (len(data), room)
    b.memload(addr, data)


def wait_done(b, timeout_frames=600):
    n = 0
    while n < timeout_frames:
        if b.peek(STATUS + ST_DONE) == 0xA5:
            return True
        b.frames(4)
        n += 4
    return False


def main(argv):
    only = None
    keep_shots = "--shot" in argv
    if "--case" in argv:
        only = int(argv[argv.index("--case") + 1])
    os.makedirs(SHOTDIR, exist_ok=True)

    syms = symfile.load(SYMS)
    script_addr = syms["vdi_script"]
    script_room = min(a for a in syms.values() if a > script_addr) - script_addr
    scratch_addr = syms["vdi_scratch"]
    results_addr = syms["vdi_results"]
    count_addr = syms["vdi_result_count"]
    scratch_room = min(a for a in syms.values() if a > scratch_addr) - scratch_addr
    assert 552 + 20 <= scratch_room, "the MFDBs must fit vdi_scratch"
    save = vdiref.VramForm.save_buffer(scratch_addr + 552)
    FORMS = {"icon": (ICON_BITS, ICON_WDW),
             "to_save": (None, save), "from_save": (save, None)}

    emu = launch(tag="m3", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    results = []
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(200)
        # Ready is STATUS[2] == 1, not the signature: the runner raises
        # 'VD' early and finishes starting up (farmem_probe among it) some
        # frames later, and how many moves with the size of the image.
        for _ in range(200):
            tag = bytes(b.memdump(STATUS, 3))
            if tag[:2] == b"VD" and tag[2] == 1:
                break
            b.frames(4)

        tag = bytes(b.memdump(STATUS, 3))
        print(f"runner: {tag[:2]!r} stage=${tag[2]:02X}  "
              f"vdi_script=${script_addr:04X} vdi_scratch=${scratch_addr:04X}")
        if tag[:2] != b"VD" or tag[2] != 1:
            print("FAIL: runner did not come up")
            return 1

        for idx, (name, script) in enumerate(CASES):
            if only is not None and idx != only:
                continue
            # v_opnwk first, with the AES's work_in: it resets driver state
            # (cursor included), which the host reference gets for free by
            # constructing a new VDI and the target must be told to do.
            full = [(vdiref.V_OPNWK, (), vdiref.WORK_IN), (V_CLRWK,)] + script

            resolved = [
                (r[0], r[1] if len(r) > 1 else (), r[2] if len(r) > 2 else (),
                 FORMS[r[3]] if len(r) > 3 else None)
                for r in full]
            ref = vdiref.VDI()
            ref.run(resolved)

            # Stage the forms' MFDBs where the driver will read them: the
            # icon's, the screen's (fd_addr 0) and the save buffer's.
            b.memload(scratch_addr, ICON_BITS)
            b.memload(scratch_addr + 512,
                      pack_mfdb(scratch_addr, ICON_W, ICON_H, ICON_WDW))
            b.memload(scratch_addr + 532, pack_mfdb(0, 0, 0, 0))
            b.memload(scratch_addr + 552, save.pack())
            poke_script(b, script_addr, resolved, scratch_addr + 512, script_room,
                        screen_mfdb=scratch_addr + 532)
            b.poke(STATUS + ST_DONE, 0)
            b.poke(STATUS + ST_GO, 1)
            if not wait_done(b):
                results.append((idx, name, "timed out"))
                continue
            b.frames(4)

            # Returned values, call for call.  Skipped for the two opcodes
            # whose result depends on live hardware (v_string, vq_key_s) and
            # for vex_* , whose "old vector" is a target address.
            err = None
            nres = b.peek16(count_addr)
            if nres != len(ref.results):
                err = f"{nres} calls recorded, expected {len(ref.results)}"
            else:
                got = vdiref.decode(
                    b.memdump(results_addr, nres * vdiref.RESULT_WORDS * 2), nres)
                for i, rec in enumerate(got):
                    if rec != ref.results[i]:
                        err = (f"call {i} (op {full[i][0]}) returned {rec}, "
                               f"expected {ref.results[i]}")
                        break

            shot = os.path.join(SHOTDIR, f"m3-{idx:02d}.png")
            b.screenshot(shot)
            bad, shown = vbxeref.compare_to_shot(ref.to_rgb(), shot)
            if bad and not err:
                err = f"{bad} px differ; first {shown[:3]}"
            results.append((idx, name, err))
            if not err and not keep_shots:
                os.remove(shot)
            print(f"  [{idx:2d}] {name:<38s} {'ok' if not err else 'FAIL'}")
    finally:
        emu.stop()

    fails = [r for r in results if r[2]]
    print()
    for idx, name, err in fails:
        print(f"   FAIL [{idx}] {name}: {err}")
    print(f"gem4xe-m3: {len(results) - len(fails)}/{len(results)} VDI cases passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
