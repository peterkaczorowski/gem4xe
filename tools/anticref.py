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
    return a
