#!/usr/bin/env python3
"""The MODEL's side of the VDI's device seam -- src/vdi/vdidev.h, in Python.

tools/vdiref.py is the specification the target's src/vdi/vdi.c has to
agree with, and until now it was written to ONE surface: 640x240 at 4bpp,
with the VBXE's nibble packing and its `map_col` permutation inline in
the rasteriser.  That was true of vdi.c too until phase 32, and stopped
being true of it in phase 34.  This is the model catching up, and it is
what lets the AES model (tools/aesref.py) and the desktop model
(tools/deskref.py) be run against a SECOND screen without either of them
changing a line -- which is the same claim the C side makes, checked the
same way.

THE CONTRACT IS THE C's, name for name where the shapes allow it.  A
device declares its geometry and the system font's metrics as fields,
and answers the calls below.  Everything above the seam -- clipping,
writing modes, attributes, text placement, the workstation -- belongs to
vdiref and is device-independent.

    w, h, stride              the surface; SCR_W, SCR_H, SCR_STRIDE
    font_w .. font_point      the system font's cell and its head
    face                      the 1bpp strip that cell is drawn from

    clear()                   v_clrwk's screen
    fill_rect(x1,y1,x2,y2,pen)  solid, corners inclusive, already clipped
    xor_rect(x1,y1,x2,y2)       the same, complemented
    plot(x, y, pen)             one pixel, clipped to the SCREEN only
    plot_xor(x, y)
    pixel(x, y)                 what the device stores there
    pen_of(value)               ...and the VDI pen it means
    pen_value(pen)              the inverse

    screen_form()               the screen as a raster form
    copy(sb,ss,sx,sy, db,ds,dx,dy, w,h)   a rectangle moved, in PIXELS
    read_pixel(base, stride, x, y)        one pixel out of a form

    cursor_save(cx,cy) / cursor_restore() / cursor_discard()

    colours() / planes()
    palette_all(rgb) / palette_one(pen, rgb)
    to_rgb() / key()

THE PEN IS THE VDI'S, not the hardware's -- the same rule the C states.
A caller passes the pen it was given; `map_col` and the nibble packing
are the VBXE device's business and a device with two colours has neither
to map.
"""
import zlib

import vbxeref


class Vbxe:
    """640x240, 4bpp, two pixels to a byte with the LEFT one in the high
    nibble -- so every rectangle has up to two partial ends, which is
    where 4bpp VDI drivers historically went wrong and why the
    conformance suite has eleven cases about edges alone.

    The code here was inline in vdiref until the seam was drawn; it is
    the same code, and the digests of all 99 model cases are what says
    so.
    """

    # -- the surface -----------------------------------------------------
    w, h = vbxeref.SCR_W, vbxeref.SCR_H
    stride = vbxeref.STRIDE

    # -- the system font's cell, and the rest of its head ----------------
    font_w, font_h, font_top = 8, 8, 6
    font_ascent, font_half, font_descent, font_bottom = 6, 4, 1, 1
    font_point = 9

    # The VDI's pen order into this device's hardware indices.  XOR mode
    # complements pixel bits and the AES needs black <-> white to survive
    # that, so black is stored as 15 and white as 0; the palette is
    # loaded in hardware order (src/vdi/dev_vbxe.c, map_col).
    MAP_COL = (0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13)

    def __init__(self, face, pal):
        self.s = vbxeref.Surface()      # all 512 KB: forms live above the screen
        self.base = 0
        self.face = face
        self.hw_pal = bytearray(48)
        self.palette_all(pal)
        self.sv = None                  # (bx, y, nb, nr, bytes) under the cursor
        self.clear()

    # -- colours ---------------------------------------------------------
    @staticmethod
    def colours():
        return 16

    @staticmethod
    def planes():
        return 4

    def pen_value(self, pen):
        return self.MAP_COL[pen & 15]

    def pen_of(self, value):
        return self.MAP_COL.index(value)

    def palette_all(self, pal):
        """Sixteen VDI pens' worth of 8-bit RGB, in VDI PEN ORDER.  The
        device puts them wherever its hardware keeps them."""
        for pen in range(16):
            self.palette_one(pen, pal[pen * 3:pen * 3 + 3])

    def palette_one(self, pen, rgb):
        """Three 8-bit components for one VDI pen, permuted into the order
        the hardware holds them."""
        self.hw_pal[self.MAP_COL[pen] * 3:self.MAP_COL[pen] * 3 + 3] = bytes(rgb)

    def palette_of(self, pen):
        i = self.MAP_COL[pen] * 3
        return tuple(self.hw_pal[i:i + 3])

    # -- pixels ----------------------------------------------------------
    def clear(self):
        self.s.fill(self.base, self.stride, self.stride, self.h, 0x00)

    def fill_rect(self, x1, y1, x2, y2, pen):
        """Mirrors dev_fill_rect() in src/vdi/dev_vbxe.c exactly, edges
        included."""
        hwpen = self.MAP_COL[pen & 15]
        stride = self.stride
        base = self.base + y1 * stride
        rows = y2 - y1 + 1
        c = ((hwpen & 0x0F) << 4) | (hwpen & 0x0F)
        bl, br = x1 >> 1, x2 >> 1
        if bl == br:
            if (x1 & 1) == 0 and (x2 & 1) == 1:
                self.s.fill(base + bl, stride, 1, rows, c)
            elif x1 & 1:
                self.s.rmw(base + bl, stride, 1, rows, 0xF0, 4)
                self.s.rmw(base + bl, stride, 1, rows, c & 0x0F, 3)
            else:
                self.s.rmw(base + bl, stride, 1, rows, 0x0F, 4)
                self.s.rmw(base + bl, stride, 1, rows, c & 0xF0, 3)
            return
        if x1 & 1:
            self.s.rmw(base + bl, stride, 1, rows, 0xF0, 4)
            self.s.rmw(base + bl, stride, 1, rows, c & 0x0F, 3)
            bl += 1
        if (x2 & 1) == 0:
            self.s.rmw(base + br, stride, 1, rows, 0x0F, 4)
            self.s.rmw(base + br, stride, 1, rows, c & 0xF0, 3)
            br -= 1
        if br >= bl:
            self.s.fill(base + bl, stride, br - bl + 1, rows, c)

    def xor_rect(self, x1, y1, x2, y2):
        """Mirrors dev_xor_rect(): complement every pixel, edges by nibble."""
        stride = self.stride
        base = self.base + y1 * stride
        rows = y2 - y1 + 1
        bl, br = x1 >> 1, x2 >> 1
        if bl == br:
            m = 0x0F if (x1 & 1) else (0xF0 if (x2 & 1) == 0 else 0xFF)
            self.s.rmw(base + bl, stride, 1, rows, m, 5)
            return
        if x1 & 1:
            self.s.rmw(base + bl, stride, 1, rows, 0x0F, 5)
            bl += 1
        if (x2 & 1) == 0:
            self.s.rmw(base + br, stride, 1, rows, 0xF0, 5)
            br -= 1
        if br >= bl:
            self.s.rmw(base + bl, stride, br - bl + 1, rows, 0xFF, 5)

    def plot(self, x, y, pen):
        """One pixel in a VDI pen, clipped to the SCREEN only: the
        workstation's own clip belongs above the seam."""
        if not (0 <= x < self.w and 0 <= y < self.h):
            return
        hwpen = self.MAP_COL[pen & 15]
        a = self.base + y * self.stride + (x >> 1)
        b = self.s.mem[a]
        if x & 1:
            self.s.mem[a] = (b & 0xF0) | (hwpen & 0x0F)
        else:
            self.s.mem[a] = (b & 0x0F) | ((hwpen & 0x0F) << 4)

    def plot_xor(self, x, y):
        if not (0 <= x < self.w and 0 <= y < self.h):
            return
        a = self.base + y * self.stride + (x >> 1)
        self.s.mem[a] ^= 0x0F if (x & 1) else 0xF0

    def pixel(self, x, y):
        """What the device stores at (x, y) -- a hardware pen here.  Zero
        off the screen, which is what v_get_pixel reports there."""
        if not (0 <= x < self.w and 0 <= y < self.h):
            return 0
        return self.read_pixel(self.base, self.stride, x, y)

    # -- raster forms ----------------------------------------------------
    def screen_form(self):
        return self.base, self.stride, self.w, self.h, True

    def read_pixel(self, base, stride, x, y):
        v = self.s.mem[base + y * stride + (x >> 1)]
        return (v & 0x0F) if (x & 1) else (v >> 4)

    def write_pixel(self, base, stride, x, y, value):
        a = base + y * stride + (x >> 1)
        b = self.s.mem[a]
        if x & 1:
            self.s.mem[a] = (b & 0xF0) | value
        else:
            self.s.mem[a] = (b & 0x0F) | (value << 4)

    def copy(self, sb, ss, sx1, sy1, db, ds, dx1, dy1, w, h):
        """A rectangle moved between forms, in PIXELS, already clipped.

        The blitter has no shifter, so 4bpp pixels can only be moved
        between positions of the same parity: when they are -- and the
        run is a whole number of bytes -- it is one blit, and otherwise it
        is pixel by pixel through the MEMAC window.  Both paths must
        produce the same pixels, which is what the conformance cases
        check.  The direction is chosen so an overlapping move does not
        eat its own source.
        """
        if ((sx1 ^ dx1) & 1) == 0 and (sx1 & 1) == 0 and (w & 1) == 0:
            self.s.move(sb + sy1 * ss + (sx1 >> 1), ss,
                        db + dy1 * ds + (dx1 >> 1), ds, w >> 1, h)
            return
        for y in range(h):
            sy = (sy1 + h - 1 - y) if dy1 > sy1 else (sy1 + y)
            dy = (dy1 + h - 1 - y) if dy1 > sy1 else (dy1 + y)
            for i in range(w):
                sx = (sx1 + w - 1 - i) if dx1 > sx1 else (sx1 + i)
                dx = (dx1 + w - 1 - i) if dx1 > sx1 else (dx1 + i)
                self.write_pixel(db, ds, dx, dy,
                                 self.read_pixel(sb, ss, sx, sy))

    # -- the cursor ------------------------------------------------------
    # The VDI owns WHERE the pointer is and whether it is shown; the
    # device owns what was UNDER it.  Nine bytes at odd x, not eight
    # (docs/phase3a.md).
    def cursor_save(self, cx, cy):
        bx0, bx1 = cx >> 1, (cx + 15) >> 1
        y0, y1 = cy, cy + 15
        bx0 = max(bx0, 0); y0 = max(y0, 0)
        bx1 = min(bx1, self.stride - 1); y1 = min(y1, self.h - 1)
        if bx1 < bx0 or y1 < y0:
            self.sv = None
            return
        nb, nr = bx1 - bx0 + 1, y1 - y0 + 1
        buf = bytearray()
        for r in range(nr):
            a = self.base + (y0 + r) * self.stride + bx0
            buf += self.s.mem[a:a + nb]
        self.sv = (bx0, y0, nb, nr, bytes(buf))

    def cursor_restore(self):
        if not self.sv:
            return
        bx0, y0, nb, nr, buf = self.sv
        for r in range(nr):
            a = self.base + (y0 + r) * self.stride + bx0
            self.s.mem[a:a + nb] = buf[r * nb:(r + 1) * nb]
        self.sv = None

    def cursor_discard(self):
        self.sv = None

    # -- readback --------------------------------------------------------
    def to_rgb(self):
        return self.s.to_rgb(self.base, bytes(self.hw_pal))

    def key(self):
        return zlib.crc32(self.s.mem[self.base:
                                     self.base + self.stride * self.h])
