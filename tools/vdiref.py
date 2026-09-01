#!/usr/bin/env python3
"""Host reference implementation of the gem4xe VDI.

This is the SPECIFICATION.  src/vdi/vdi.c must agree with it byte for byte on
every script the conformance suite runs; when they disagree, this one is right
until proven otherwise, and the disagreement gets shrunk into a new case.

It models the same blit-list strategy the target uses -- including the AND/OR
pair that handles a 4bpp rectangle's odd-pixel edges -- so a mismatch points at
a real difference in behaviour rather than at two unrelated rasterisers.
"""
import os
import re

import vbxeref

SCR_W, SCR_H, STRIDE = vbxeref.SCR_W, vbxeref.SCR_H, vbxeref.STRIDE

# opcodes
V_OPNWK, V_CLSWK, V_CLRWK, V_PLINE = 1, 2, 3, 6
VSL_TYPE, VSL_WIDTH, VSL_COLOR = 15, 16, 17
VST_COLOR, VSF_INTERIOR, VSF_STYLE, VSF_COLOR = 22, 23, 24, 25
VSWR_MODE = 32
V_OPNVWK, V_CLSVWK, VQ_EXTND = 100, 101, 102
VRO_CPYFM, VR_TRNFM, VR_RECFL, VS_CLIP = 109, 110, 114, 129
V_GTEXT = 8
VRT_CPYFM = 121
VSC_FORM, V_SHOW_C, V_HIDE_C, VQ_MOUSE = 111, 122, 123, 124
VSIN_MODE, VQIN_MODE, VEX_TIMV, VSL_UDSTY = 33, 115, 118, 113
VST_HEIGHT, VQT_ATTRIBUTES, V_ESCAPE = 12, 38, 5
V_LOCATOR = 28
MD_REPLACE, MD_TRANS, MD_XOR, MD_ERASE = 1, 2, 3, 4

LINE_STYLES = [0xFFFF, 0xFFFF, 0xFFF0, 0xE0E0, 0xFF18, 0xFF00, 0xF191, 0xFFFF]

# GEM 8x8 system font, read from the SAME generated file the target links, so
# the reference cannot drift from the device.
FONT_W, FONT_H, FONT_STRIDE, FONT_TOP = 8, 8, 256, 6
_FONT_C = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "src", "vdi", "font8x8.c")


def _load_font():
    text = open(_FONT_C).read()
    body = text[text.index("{"):]
    data = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    want = FONT_STRIDE * FONT_H
    if len(data) < want:
        raise RuntimeError(f"{_FONT_C}: {len(data)} bytes, expected {want}")
    return bytes(data[:want])


FONT = _load_font()

# GEM's standard palette order: pen 0 is WHITE, pen 1 is BLACK.
GEM_PAL = bytes((
    0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00,  0xFF, 0x00, 0x00,  0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF,  0x00, 0xFF, 0xFF,  0xFF, 0xFF, 0x00,  0xFF, 0x00, 0xFF,
    0xBB, 0xBB, 0xBB,  0x77, 0x77, 0x77,  0xBB, 0x00, 0x00,  0x00, 0xBB, 0x00,
    0x00, 0x00, 0xBB,  0x00, 0xBB, 0xBB,  0xBB, 0xBB, 0x00,  0xBB, 0x00, 0xBB))


class VDI:
    def __init__(self):
        self.s = vbxeref.Surface(0x20000)
        self.base = 0
        self.reset()
        self.s.fill(self.base, STRIDE, STRIDE, SCR_H, 0x00)   # pen 0 = white

    def reset(self):
        self.clip = 0
        self.xmn, self.ymn, self.xmx, self.ymx = 0, 0, SCR_W - 1, SCR_H - 1
        self.wrt_mode = 0
        self.line_color, self.line_width, self.line_index = 1, 1, 1
        self.fill_color, self.fill_index, self.fill_style = 1, 1, 1
        self.text_color = 1
        # mouse cursor
        self.cur_xhot = self.cur_yhot = 0
        self.cur_bg, self.cur_fg = 0, 1
        self.cur_mask = [0] * 16
        self.cur_data = [0] * 16
        self.cur_hide = 1          # visible only at 0; starts hidden
        self.cur_drawn = False
        self.sv = None             # (bx, y, nb, nr, bytes)
        self.ptr_x, self.ptr_y = 0, 0
        self.buttons = 0
        self.in_mode = [0, 2, 2, 2, 2]
        self.line_styles = list(LINE_STYLES)
        self.contrl2 = self.contrl4 = 0
        self.intout = [0, 0, 0]
        self.ptsout = [0, 0, 0]

    # -- geometry --------------------------------------------------------
    def _clip_rect(self, x1, y1, x2, y2):
        if self.clip:
            x1 = max(x1, self.xmn); y1 = max(y1, self.ymn)
            x2 = min(x2, self.xmx); y2 = min(y2, self.ymx)
        x1 = max(x1, 0); y1 = max(y1, 0)
        x2 = min(x2, SCR_W - 1); y2 = min(y2, SCR_H - 1)
        return (x1, y1, x2, y2) if (x1 <= x2 and y1 <= y2) else None

    def _fill_rect_dev(self, x1, y1, x2, y2, color):
        """Mirrors fill_rect_dev() in src/vdi/vdi.c exactly, edges included."""
        base = self.base + y1 * STRIDE
        rows = y2 - y1 + 1
        c = ((color & 0x0F) << 4) | (color & 0x0F)
        bl, br = x1 >> 1, x2 >> 1
        if bl == br:
            if (x1 & 1) == 0 and (x2 & 1) == 1:
                self.s.fill(base + bl, STRIDE, 1, rows, c)
            elif x1 & 1:
                self.s.rmw(base + bl, STRIDE, 1, rows, 0xF0, 4)
                self.s.rmw(base + bl, STRIDE, 1, rows, c & 0x0F, 3)
            else:
                self.s.rmw(base + bl, STRIDE, 1, rows, 0x0F, 4)
                self.s.rmw(base + bl, STRIDE, 1, rows, c & 0xF0, 3)
            return
        if x1 & 1:
            self.s.rmw(base + bl, STRIDE, 1, rows, 0xF0, 4)
            self.s.rmw(base + bl, STRIDE, 1, rows, c & 0x0F, 3)
            bl += 1
        if (x2 & 1) == 0:
            self.s.rmw(base + br, STRIDE, 1, rows, 0x0F, 4)
            self.s.rmw(base + br, STRIDE, 1, rows, c & 0xF0, 3)
            br -= 1
        if br >= bl:
            self.s.fill(base + bl, STRIDE, br - bl + 1, rows, c)

    def _plot(self, x, y, color):
        if self.clip and not (self.xmn <= x <= self.xmx and
                              self.ymn <= y <= self.ymx):
            return
        if not (0 <= x < SCR_W and 0 <= y < SCR_H):
            return
        a = self.base + y * STRIDE + (x >> 1)
        b = self.s.mem[a]
        if x & 1:
            self.s.mem[a] = (b & 0xF0) | (color & 0x0F)
        else:
            self.s.mem[a] = (b & 0x0F) | ((color & 0x0F) << 4)

    def _line(self, x1, y1, x2, y2):
        mask = self.line_styles[self.line_index if 1 <= self.line_index <= 7 else 1]
        if y1 == y2 and mask == 0xFFFF:
            a, b = sorted((x1, x2))
            r = self._clip_rect(a, y1, b, y2)
            if r:
                self._fill_rect_dev(*r, self.line_color)
            return
        if x1 == x2 and mask == 0xFFFF:
            a, b = sorted((y1, y2))
            r = self._clip_rect(x1, a, x2, b)
            if r:
                self._fill_rect_dev(*r, self.line_color)
            return
        dx, dy = abs(x2 - x1), abs(y2 - y1)
        sx = 1 if x1 < x2 else -1
        sy = 1 if y1 < y2 else -1
        err, bit = dx - dy, 0
        while True:
            if mask & (1 << (15 - (bit & 15))):
                self._plot(x1, y1, self.line_color)
            bit += 1
            if x1 == x2 and y1 == y2:
                break
            e2 = err << 1
            if e2 > -dy:
                err -= dy; x1 += sx
            if e2 < dx:
                err += dx; y1 += sy

    def _cpyfm(self, pts):
        """Mirrors vdi_vro_cpyfm(), including the alignment fast/slow split.

        The VBXE blitter has no shifter, so 4bpp pixels can only be moved
        between positions of the same parity.  Both paths must produce the
        same pixels -- that is what the conformance cases check.
        """
        sx1, sx2 = sorted((pts[0], pts[2]))
        sy1, sy2 = sorted((pts[1], pts[3]))
        dx1, dx2 = sorted((pts[4], pts[6]))
        dy1, dy2 = sorted((pts[5], pts[7]))
        w, h = sx2 - sx1 + 1, sy2 - sy1 + 1
        if w <= 0 or h <= 0:
            return
        if dx1 < 0 or dy1 < 0 or dx1 + w > SCR_W or dy1 + h > SCR_H:
            return
        if sx1 < 0 or sy1 < 0 or sx2 >= SCR_W or sy2 >= SCR_H:
            return
        if ((sx1 ^ dx1) & 1) == 0 and (sx1 & 1) == 0 and (w & 1) == 0:
            self.s.copy(self.base + sy1 * STRIDE + (sx1 >> 1), STRIDE,
                        self.base + dy1 * STRIDE + (dx1 >> 1), STRIDE,
                        w >> 1, h)
            return
        for y in range(h):
            sy = (sy1 + h - 1 - y) if dy1 > sy1 else (sy1 + y)
            dy = (dy1 + h - 1 - y) if dy1 > sy1 else (dy1 + y)
            for i in range(w):
                sx = (sx1 + w - 1 - i) if dx1 > sx1 else (sx1 + i)
                dx = (dx1 + w - 1 - i) if dx1 > sx1 else (dx1 + i)
                v = self.s.mem[self.base + sy * STRIDE + (sx >> 1)]
                self._plot(dx, dy, (v & 0x0F) if (sx & 1) else (v >> 4))

    def _vrt_cpyfm(self, pts, ints, form):
        """Mirrors vdi_vrt_cpyfm(): a 1-plane source expanded into colours.

        `form` is (bits, wdwidth) -- the same bytes the harness poked into the
        target's scratch area, MSB-first, rows fd_wdwidth WORDS apart.
        """
        bits, wdwidth = form
        mode, fg, bg = ints[0], ints[1], ints[2]
        sx1, sx2 = sorted((pts[0], pts[2]))
        sy1, sy2 = sorted((pts[1], pts[3]))
        dx1, dy1 = pts[4], pts[5]
        w, h = sx2 - sx1 + 1, sy2 - sy1 + 1
        if w <= 0 or h <= 0:
            return
        stride = wdwidth * 2
        for row in range(h):
            base = (sy1 + row) * stride
            for col in range(w):
                sx = sx1 + col
                on = (bits[base + (sx >> 3)] >> (7 - (sx & 7))) & 1
                dx, dy = dx1 + col, dy1 + row
                if mode == MD_TRANS:
                    if on:
                        self._plot(dx, dy, fg)
                elif mode == MD_XOR:
                    if on:
                        v = self.s.mem[self.base + dy * STRIDE + (dx >> 1)]
                        old = (v & 0x0F) if (dx & 1) else (v >> 4)
                        self._plot(dx, dy, old ^ 0x0F)
                elif mode == MD_ERASE:
                    if not on:
                        self._plot(dx, dy, bg)
                else:
                    self._plot(dx, dy, fg if on else bg)

    def _gtext(self, pts, ints):
        """Mirrors vdi_v_gtext().  Left/baseline alignment: y is the BASELINE
        and the cell top is y - FONT_TOP.  Replace mode paints pen 0 behind
        the glyph; transparent mode does not."""
        x, y = pts[0], pts[1]
        cy = y - FONT_TOP
        opaque = (self.wrt_mode == 0)
        for i, code in enumerate(ints):
            cx = x + i * FONT_W
            for row in range(FONT_H):
                b = FONT[row * FONT_STRIDE + (code & 0xFF)]
                for col in range(FONT_W):
                    if b & (0x80 >> col):
                        self._plot(cx + col, cy + row, self.text_color)
                    elif opaque:
                        self._plot(cx + col, cy + row, 0)

    def _workout(self):
        """The first three intout and ptsout words of v_opnwk's work_out, which
        is all the per-call record carries.  Must match fill_workout() in
        src/vdi/vdi.c."""
        self.intout = [SCR_W - 1, SCR_H - 1, 0]
        self.ptsout = [8, 8, 8]
        self.contrl2, self.contrl4 = 6, 45

    # -- cursor ----------------------------------------------------------
    def _cursor_save(self, cx, cy):
        bx0, bx1 = cx >> 1, (cx + 15) >> 1
        y0, y1 = cy, cy + 15
        bx0 = max(bx0, 0); y0 = max(y0, 0)
        bx1 = min(bx1, STRIDE - 1); y1 = min(y1, SCR_H - 1)
        if bx1 < bx0 or y1 < y0:
            self.sv = None
            return
        nb, nr = bx1 - bx0 + 1, y1 - y0 + 1
        buf = bytearray()
        for r in range(nr):
            a = self.base + (y0 + r) * STRIDE + bx0
            buf += self.s.mem[a:a + nb]
        self.sv = (bx0, y0, nb, nr, bytes(buf))

    def _cursor_restore(self):
        if not self.sv:
            return
        bx0, y0, nb, nr, buf = self.sv
        for r in range(nr):
            a = self.base + (y0 + r) * STRIDE + bx0
            self.s.mem[a:a + nb] = buf[r * nb:(r + 1) * nb]
        self.sv = None

    def _cursor_paint(self, cx, cy):
        for row in range(16):
            m, d = self.cur_mask[row], self.cur_data[row]
            for col in range(16):
                bit = 0x8000 >> col
                if m & bit:
                    self._plot(cx + col, cy + row, self.cur_bg)
                if d & bit:
                    self._plot(cx + col, cy + row, self.cur_fg)

    def _cursor_show_now(self):
        cx, cy = self.ptr_x - self.cur_xhot, self.ptr_y - self.cur_yhot
        self._cursor_save(cx, cy)
        self._cursor_paint(cx, cy)
        self.cur_drawn = True

    def _cursor_hide_now(self):
        if self.cur_drawn:
            self._cursor_restore()
            self.cur_drawn = False

    # -- dispatch --------------------------------------------------------
    def call(self, op, pts=(), ints=(), form=None):
        pts = list(pts); ints = list(ints)
        # Per-call outputs, mirroring what src/m3_vdi.c records: this is how
        # the input and inquiry opcodes get tested at all.
        self.contrl2 = 0
        self.contrl4 = 0
        self.intout = [0, 0, 0]
        self.ptsout = [0, 0, 0]
        if op == V_CLRWK:
            self.s.fill(self.base, STRIDE, STRIDE, SCR_H, 0x00)
        elif op in (V_OPNWK, V_OPNVWK):
            keep = (self.ptr_x, self.ptr_y)
            self.reset()
            self.ptr_x, self.ptr_y = keep
            self._workout()
        elif op == VQ_EXTND:
            if ints and ints[0]:
                # second capability block; intout[4] = planes is the one the
                # AES actually reads (gsx_nplanes)
                self.intout = [0, 16, 0]
                self.ptsout = [0, 0, 0]
                self.contrl2, self.contrl4 = 6, 45
            else:
                self._workout()
        elif op == VS_CLIP:
            self.clip = ints[0]
            if self.clip:
                x1, x2 = sorted((pts[0], pts[2]))
                y1, y2 = sorted((pts[1], pts[3]))
                self.xmn, self.ymn, self.xmx, self.ymx = x1, y1, x2, y2
            else:
                self.xmn, self.ymn = 0, 0
                self.xmx, self.ymx = SCR_W - 1, SCR_H - 1
        elif op == VR_RECFL:
            x1, x2 = sorted((pts[0], pts[2]))
            y1, y2 = sorted((pts[1], pts[3]))
            r = self._clip_rect(x1, y1, x2, y2)
            if r and self.fill_index != 0:
                self._fill_rect_dev(*r, self.fill_color)
        elif op == VRO_CPYFM:
            self._cpyfm(pts)
        elif op == V_LOCATOR:
            if pts:
                self.ptr_x = max(0, min(pts[0], SCR_W - 1))
                self.ptr_y = max(0, min(pts[1], SCR_H - 1))
            self.ptsout[0], self.ptsout[1] = self.ptr_x, self.ptr_y
            self.intout[0] = 0
            self.contrl2, self.contrl4 = 1, 1
        elif op == VQ_MOUSE:
            self.intout[0] = self.buttons
            self.ptsout[0], self.ptsout[1] = self.ptr_x, self.ptr_y
            self.contrl2, self.contrl4 = 1, 1
        elif op == VSIN_MODE:
            dev, mode = ints[0], ints[1]
            if 1 <= dev <= 4 and mode in (1, 2):
                self.in_mode[dev] = mode
            self.intout[0] = 1
            self.contrl4 = 1
        elif op == VQIN_MODE:
            dev = ints[0]
            self.intout[0] = self.in_mode[dev] if 1 <= dev <= 4 else 2
            self.contrl4 = 1
        elif op == VST_HEIGHT:
            self.ptsout = [FONT_W, FONT_H, FONT_W]     # 4th word not recorded
            self.contrl2 = 2
        elif op == VQT_ATTRIBUTES:
            self.intout = [1, self.text_color, 0]
            self.ptsout = [FONT_W, FONT_H, FONT_W]
            self.contrl2, self.contrl4 = 2, 6
        elif op == VEX_TIMV:
            self.intout[0] = 20          # 50 Hz PAL frame, in ms
            self.contrl4 = 1
        elif op == VSL_UDSTY:
            self.line_styles[7] = ints[0] & 0xFFFF
        elif op == VSC_FORM:
            self.cur_xhot, self.cur_yhot = ints[0], ints[1]
            self.cur_bg, self.cur_fg = ints[3], ints[4]
            self.cur_mask = [ints[5 + i] & 0xFFFF for i in range(16)]
            self.cur_data = [ints[21 + i] & 0xFFFF for i in range(16)]
        elif op == V_SHOW_C:
            if ints and ints[0] != 0:
                if self.cur_hide > 0:
                    self.cur_hide -= 1
            else:
                self.cur_hide = 0
            if self.cur_hide == 0 and not self.cur_drawn:
                self._cursor_show_now()
        elif op == V_HIDE_C:
            if self.cur_hide == 0:
                self._cursor_hide_now()
            self.cur_hide += 1
        elif op == VRT_CPYFM:
            if form:
                self._vrt_cpyfm(pts, ints, form)
        elif op == V_GTEXT:
            self._gtext(pts, ints)
        elif op == V_PLINE:
            for i in range(len(pts) // 2 - 1):
                self._line(pts[i * 2], pts[i * 2 + 1],
                           pts[i * 2 + 2], pts[i * 2 + 3])
        elif op == VSL_TYPE:
            self.line_index = ints[0] if 1 <= ints[0] <= 7 else 1
            self.intout[0] = self.line_index
            self.contrl4 = 1
        elif op == VSL_COLOR:
            self.line_color = ints[0] if 0 <= ints[0] <= 15 else 1
            self.intout[0] = self.line_color
            self.contrl4 = 1
        elif op == VSF_INTERIOR:
            self.fill_index = ints[0] if 0 <= ints[0] <= 4 else 0
            self.intout[0] = self.fill_index
            self.contrl4 = 1
        elif op == VSF_STYLE:
            self.fill_style = max(1, ints[0])
            self.intout[0] = self.fill_style
            self.contrl4 = 1
        elif op == VSF_COLOR:
            self.fill_color = ints[0] if 0 <= ints[0] <= 15 else 1
            self.intout[0] = self.fill_color
            self.contrl4 = 1
        elif op == VST_COLOR:
            self.text_color = ints[0] if 0 <= ints[0] <= 15 else 1
            self.intout[0] = self.text_color
            self.contrl4 = 1
        elif op == VSWR_MODE:
            self.wrt_mode = (ints[0] - 1) if 1 <= ints[0] <= 4 else 0
            self.intout[0] = self.wrt_mode + 1
            self.contrl4 = 1
        # everything else is a documented no-op on this driver

    def run(self, script):
        self.results = []
        for rec in script:
            self.call(rec[0],
                      rec[1] if len(rec) > 1 else (),
                      rec[2] if len(rec) > 2 else (),
                      rec[3] if len(rec) > 3 else None)
            # Same masking as the target: only the declared words count.
            i_, p_, n, m = self.intout, self.ptsout, self.contrl4, self.contrl2
            self.results.append((self.contrl2, self.contrl4,
                                 i_[0] if n > 0 else 0,
                                 i_[1] if n > 1 else 0,
                                 i_[2] if n > 2 else 0,
                                 p_[0] if m > 0 else 0,
                                 p_[1] if m > 0 else 0,
                                 p_[2] if m > 1 else 0))

    def to_rgb(self):
        return self.s.to_rgb(self.base, GEM_PAL)


def encode(script, mfdb_addr=0):
    """Serialise a script into the WORD stream src/m3_vdi.c expects.

    A record carrying a `form` gets mfdb_addr planted in contrl[7..8], which is
    where the harness has staged the MFDB on the target.
    """
    out = []
    for rec in script:
        op = rec[0]
        pts = list(rec[1]) if len(rec) > 1 else []
        ints = list(rec[2]) if len(rec) > 2 else []
        form = rec[3] if len(rec) > 3 else None
        c7 = mfdb_addr & 0xFFFF if form else 0
        c8 = (mfdb_addr >> 16) & 0xFFFF if form else 0
        out += [op, len(pts) // 2, len(ints), c7, c8, 0, 0]
        out += pts
        out += ints
    out.append(0)
    return out


def decode_quad(prev, now):
    """Reference for ptr_decode_quad() in src/vdi/pointer.c.

    Classic 2-bit Gray-code table.  The four two-step ("impossible") entries
    are 0 on purpose: on a missed sample it is better to drop motion than to
    invent it in the wrong direction.
    """
    table = (0, +1, -1, 0,
             -1, 0, 0, +1,
             +1, 0, 0, -1,
             0, -1, +1, 0)
    return table[((prev & 3) << 2) | (now & 3)]


def pack_mfdb(bits_addr, w, h, wdwidth):
    """The 20-byte MFDB the target reads, little-endian."""
    import struct
    return struct.pack("<Ihhhhhhhh", bits_addr, w, h, wdwidth, 0, 1, 0, 0, 0)
