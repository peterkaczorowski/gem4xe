#!/usr/bin/env python3
"""The ANTIC mode F surface, on the host: the specification the target
must match bit for bit (src/antic/antic.c).

Mode F is 320 pixels a line, one bit each, 40 bytes a line, and the
leftmost pixel of a byte is bit 7.  This model keeps the same bytes the
Atari does rather than a list of pixels, so an off-by-one in a run's edge
mask is a difference in the framebuffer and not something that has to be
inferred from a picture.
"""

AN_W, AN_H = 320, 168
AN_STRIDE = AN_W // 8

LEFT = (0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01)
RIGHT = (0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF)

MD_REPLACE, MD_TRANS, MD_XOR, MD_ERASE = 1, 2, 3, 4

# An eight-row fill pattern for the gate, in the same form fillpat.c
# stores GEM's: one UWORD a row, bit 15 the leftmost pixel of a
# 16-aligned screen word.
PATT = (0xFF00, 0x8080, 0x8080, 0x8080, 0x0FF0, 0x0808, 0x0808, 0x0808)
AN_GLYPH_W = AN_GLYPH_H = 8

# The same font the target links, read from the same file, so the model
# cannot drift from the device (vdiref does this too).
from vdiref import FONT, FONT_STRIDE      # noqa: E402


def apply(dst, src, m, mode, pen):
    """One destination byte: `src` applied through mask `m` in `mode`
    (src/antic/antic.c an_apply)."""
    ink = 0xFF if pen else 0x00
    if mode == MD_TRANS:
        out = (dst & ~src) | (ink & src)
    elif mode == MD_XOR:
        out = dst ^ src
    elif mode == MD_ERASE:
        out = (dst & src) | (ink & ~src)
    else:
        out = src if pen else ~src
    return ((dst & ~m) | (out & m)) & 0xFF


class Antic:
    def __init__(self):
        self.mem = bytearray(AN_STRIDE * AN_H)

    # -- the primitives, in the order antic.c has them -------------------
    def clear(self, value=0):
        self.mem = bytearray([value & 0xFF]) * (AN_STRIDE * AN_H)

    def plot(self, x, y, set_):
        if x < 0 or y < 0 or x >= AN_W or y >= AN_H:
            return
        i = y * AN_STRIDE + (x >> 3)
        bit = 0x80 >> (x & 7)
        self.mem[i] = (self.mem[i] | bit) if set_ else (self.mem[i] & ~bit & 0xFF)

    def hline(self, x1, x2, y, set_):
        if y < 0 or y >= AN_H:
            return
        if x1 > x2:
            x1, x2 = x2, x1
        if x2 < 0 or x1 >= AN_W:
            return
        x1 = max(x1, 0)
        x2 = min(x2, AN_W - 1)
        b1, b2 = x1 >> 3, x2 >> 3
        lm, rm = LEFT[x1 & 7], RIGHT[x2 & 7]
        base = y * AN_STRIDE

        def paint(i, m):
            self.mem[i] = (self.mem[i] | m) if set_ else (self.mem[i] & ~m & 0xFF)

        if b1 == b2:
            paint(base + b1, lm & rm)
            return
        paint(base + b1, lm)
        for b in range(b1 + 1, b2):
            self.mem[base + b] = 0xFF if set_ else 0x00
        paint(base + b2, rm)

    def rect(self, x1, y1, x2, y2, set_):
        if y1 > y2:
            y1, y2 = y2, y1
        y1 = max(y1, 0)
        y2 = min(y2, AN_H - 1)
        for y in range(y1, y2 + 1):
            self.hline(x1, x2, y, set_)

    # -- the writing modes ----------------------------------------------
    def span(self, x1, x2, y, mode, pen):
        if y < 0 or y >= AN_H:
            return
        if x1 > x2:
            x1, x2 = x2, x1
        if x2 < 0 or x1 >= AN_W:
            return
        x1, x2 = max(x1, 0), min(x2, AN_W - 1)
        b1, b2 = x1 >> 3, x2 >> 3
        lm, rm = LEFT[x1 & 7], RIGHT[x2 & 7]
        base = y * AN_STRIDE

        def at(i, m):
            self.mem[i] = apply(self.mem[i], 0xFF, m, mode, pen)

        if b1 == b2:
            at(base + b1, lm & rm)
            return
        at(base + b1, lm)
        for b in range(b1 + 1, b2):
            at(base + b, 0xFF)
        at(base + b2, rm)

    def rect_mode(self, x1, y1, x2, y2, mode, pen):
        if y1 > y2:
            y1, y2 = y2, y1
        y1, y2 = max(y1, 0), min(y2, AN_H - 1)
        for y in range(y1, y2 + 1):
            self.span(x1, x2, y, mode, pen)

    def glyph(self, ch, x, y, mode, pen):
        """A whole cell or none of it, as antic_glyph has it."""
        if x < 0 or y < 0 or x + AN_GLYPH_W > AN_W or y + AN_GLYPH_H > AN_H:
            return
        shift = x & 7
        for row in range(AN_GLYPH_H):
            g = FONT[row * FONT_STRIDE + (ch & 0xFF)]
            i = (y + row) * AN_STRIDE + (x >> 3)
            if shift == 0:
                self.mem[i] = apply(self.mem[i], g, 0xFF, mode, pen)
            else:
                hi, lo = g >> shift, (g << (8 - shift)) & 0xFF
                mh, ml = 0xFF >> shift, (0xFF << (8 - shift)) & 0xFF
                self.mem[i] = apply(self.mem[i], hi, mh, mode, pen)
                self.mem[i + 1] = apply(self.mem[i + 1], lo, ml, mode, pen)

    def patt_byte(self, patrow, bx):
        return (patrow & 0xFF) if (bx & 1) else (patrow >> 8)

    def patt_span(self, x1, x2, y, patrow, mode, pen):
        if y < 0 or y >= AN_H:
            return
        if x1 > x2:
            x1, x2 = x2, x1
        if x2 < 0 or x1 >= AN_W:
            return
        x1, x2 = max(x1, 0), min(x2, AN_W - 1)
        b1, b2 = x1 >> 3, x2 >> 3
        lm, rm = LEFT[x1 & 7], RIGHT[x2 & 7]
        base = y * AN_STRIDE

        def at(b, m):
            i = base + b
            self.mem[i] = apply(self.mem[i], self.patt_byte(patrow, b), m,
                                mode, pen)

        if b1 == b2:
            at(b1, lm & rm)
            return
        at(b1, lm)
        for b in range(b1 + 1, b2):
            at(b, 0xFF)
        at(b2, rm)

    def vline(self, x, y1, y2, mask, mode, pen):
        if x < 0 or x >= AN_W:
            return
        if y1 > y2:
            y1, y2 = y2, y1
        y1, y2 = max(y1, 0), min(y2, AN_H - 1)
        for y in range(y1, y2 + 1):
            bit = (mask >> (15 - (y & 15))) & 1
            self.patt_span(x, x, y, 0xFFFF if bit else 0x0000, mode, pen)

    def get_pixel(self, x, y):
        if x < 0 or y < 0 or x >= AN_W or y >= AN_H:
            return 0
        return self.bit(x, y)

    # -- what the gate compares -----------------------------------------
    def bit(self, x, y):
        return (self.mem[y * AN_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1

    def bits(self):
        """The screen as rows of 0/1, which is what a shot reduces to
        once its two colours are known."""
        return [[self.bit(x, y) for x in range(AN_W)] for y in range(AN_H)]


def pattern():
    """The milestone's drawing (src/m24_antic.c), in the same order."""
    a = Antic()
    a.clear(0)
    a.hline(0, AN_W - 1, 0, 1)
    a.hline(0, AN_W - 1, AN_H - 1, 1)
    for i in range(AN_H):
        a.plot(0, i, 1)
        a.plot(AN_W - 1, i, 1)
    a.rect(40, 80, 279, 120, 1)
    a.rect(100, 90, 219, 110, 0)
    for i in range(AN_H):
        a.plot(i, i, 1)
    for i in range(8, AN_W, 16):
        a.rect(i, 130, i, 150, 1)
    for i in range(8):
        a.hline(200 + 16 * i, 200 + 16 * i + i, 20 + i, 1)
    # the writing modes, over a band
    a.rect_mode(20, 32, 299, 44, MD_REPLACE, 1)
    a.rect_mode(30, 34, 60, 42, MD_REPLACE, 0)
    a.rect_mode(70, 34, 100, 42, MD_XOR, 1)
    a.rect_mode(110, 34, 140, 42, MD_ERASE, 0)
    a.rect_mode(150, 34, 180, 42, MD_TRANS, 0)
    # text at every shift
    for i in range(8):
        a.glyph(ord('A') + i, 20 + i * 9, 50, MD_REPLACE, 1)
    a.rect_mode(20, 60, 200, 67, MD_REPLACE, 1)
    for i in range(8):
        a.glyph(ord('a') + i, 20 + i * 9, 60, MD_TRANS, 0)
    for i in range(8):
        a.glyph(ord('0') + i, 20 + i * 9, 70, MD_XOR, 1)
    # patterned fills: two 8-row patterns, side by side so the screen's
    # own 16-pixel alignment shows as one continuous pattern
    for k, row in enumerate(PATT):
        a.patt_span(30, 148, 152 + k, row, MD_REPLACE, 1)
        a.patt_span(149, 269, 152 + k, row, MD_REPLACE, 1)
    # styled lines: a dashed horizontal and two dashed verticals
    a.patt_span(30, 269, 162, 0xF0F0, MD_REPLACE, 1)
    a.vline(24, 150, 165, 0xCCCC, MD_REPLACE, 1)
    a.vline(275, 150, 165, 0xAAAA, MD_REPLACE, 1)
    return a


def pixels(a):
    """The v_get_pixel answers the milestone stores (src/m24_antic.c)."""
    return [a.get_pixel(0, 0), a.get_pixel(5, 5), a.get_pixel(50, 100),
            a.get_pixel(150, 100), a.get_pixel(120, 38), a.get_pixel(80, 38),
            a.get_pixel(-1, 5), a.get_pixel(5, AN_H)]
