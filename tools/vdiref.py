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
VEX_BUTV, VEX_MOTV = 125, 126
VST_HEIGHT, VQT_ATTRIBUTES, V_ESCAPE = 12, 38, 5
V_LOCATOR = 28
V_STRING, VQ_KEY_S = 31, 128
VSF_UDPAT = 112
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

# The fill patterns, likewise read from the generated file the target links
# (tools/patconv.py from EmuTOS's vdi_fill.c).  tests/host/test_fillpat.py
# holds that file to the donor.  Interior styles, as vsf_interior takes them:
FIS_HOLLOW, FIS_SOLID, FIS_PATTERN, FIS_HATCH, FIS_USER = 0, 1, 2, 3, 4
MAX_FILL_PATTERN, MAX_FILL_HATCH = 24, 12
# The work_in the AES opens the screen with: every attribute 1 -- line style
# 1, colours 1 (black), SOLID fill, index 1 -- and raster coordinates.  A
# workstation opened without a work_in gets this one.
WORK_IN = (1,) * 10 + (2,)
_PAT_C = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "src", "vdi", "fillpat.c")


def _load_patterns():
    text = open(_PAT_C).read()
    out = {}
    for m in re.finditer(r"const UWORD (\w+)\[(\d+)\]\s*=\s*\{(.*?)\};", text, re.S):
        words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(3))]
        if len(words) != int(m.group(2)):
            raise RuntimeError(f"{_PAT_C}: {m.group(1)} has {len(words)} words")
        out[m.group(1)] = words
    return out


_PAT = _load_patterns()
FILL_DITHER, FILL_OEM = _PAT["fill_dither"], _PAT["fill_oem"]
FILL_HATCH0, FILL_HATCH1 = _PAT["fill_hatch0"], _PAT["fill_hatch1"]

# GEM's standard palette order: pen 0 is WHITE, pen 1 is BLACK.
GEM_PAL = bytes((
    0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00,  0xFF, 0x00, 0x00,  0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF,  0x00, 0xFF, 0xFF,  0xFF, 0xFF, 0x00,  0xFF, 0x00, 0xFF,
    0xBB, 0xBB, 0xBB,  0x77, 0x77, 0x77,  0xBB, 0x00, 0x00,  0x00, 0xBB, 0x00,
    0x00, 0x00, 0xBB,  0x00, 0xBB, 0xBB,  0xBB, 0xBB, 0x00,  0xBB, 0x00, 0xBB))

# VDI pen -> hardware pen (map_col[] in src/vdi/vdi.c).  XOR mode complements
# pixel bits, and the AES needs black <-> white to survive that, so black is
# stored as 15 and white as 0; the palette is loaded in hardware order.
MAP_COL = (0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13)
HW_PAL = bytearray(48)
for _pen in range(16):
    HW_PAL[MAP_COL[_pen] * 3:MAP_COL[_pen] * 3 + 3] = GEM_PAL[_pen * 3:_pen * 3 + 3]
HW_PAL = bytes(HW_PAL)



# The target's result record (src/m3_vdi.c): contrl[2], contrl[4], then
# RESULT_INTOUT words of intout and three of ptsout.  Fifteen intout words
# because evnt_multi returns seven values and an eight-word message.
RESULT_INTOUT = 15
RESULT_WORDS = 2 + RESULT_INTOUT + 3


def record(c2, c4, intout, ptsout):
    """The target's result record, (contrl[2], contrl[4], intout[0..14],
    ptsout[0..2]): only the declared words are non-zero."""
    io = tuple((intout[k] if k < len(intout) else 0) if c4 > k else 0
               for k in range(RESULT_INTOUT))
    po = (ptsout[0] if c2 > 0 else 0,
          ptsout[1] if c2 > 0 else 0,
          ptsout[2] if c2 > 1 else 0)
    return (c2, c4) + io + po


def decode(blob, n):
    """The n result records in a dump of the target's results area, as the
    tuples record() builds."""
    size = RESULT_WORDS * 2
    return [tuple(int.from_bytes(bytes(blob[i * size + k * 2:i * size + k * 2 + 2]),
                                 "little", signed=True)
                  for k in range(RESULT_WORDS))
            for i in range(n)]


class VDI:
    def __init__(self):
        self.s = vbxeref.Surface()          # all 512 KB: forms live above the screen
        self.base = 0
        # The input vectors and the button edge detector are the driver's,
        # not the workstation's: v_opnwk does not reset them (vdi.c).
        self.vec_motv = self.vec_butv = self.vec_timv = None
        self.last_buttons = 0
        self.reset()
        self.s.fill(self.base, STRIDE, STRIDE, SCR_H, 0x00)   # pen 0 = white

    def reset(self):
        self.clip = 0
        self.xmn, self.ymn, self.xmx, self.ymx = 0, 0, SCR_W - 1, SCR_H - 1
        self.wrt_mode = 0
        self.line_width = 1
        self.ud_patrn = [0] * 16
        self._init_wk(WORK_IN)
        # mouse cursor
        self.cur_xhot = self.cur_yhot = 0
        self.cur_bg, self.cur_fg = 0, 1
        self.cur_mask = [0] * 16
        self.cur_data = [0] * 16
        self.cur_hide = 1          # visible only at 0; starts hidden
        self.cur_drawn = False
        self.cur_lastx = self.cur_lasty = -1
        self.sv = None             # (bx, y, nb, nr, bytes)
        self.ptr_x, self.ptr_y = 0, 0
        self.buttons = 0
        # The keyboard: codes waiting to be read one per v_string, and the
        # modifier bits vq_key_s reports for the key held right now.
        self.keys = []
        self.key_mods = 0
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

    def _fill_rect_dev(self, x1, y1, x2, y2, hwpen):
        """Mirrors fill_rect_dev() in src/vdi/vdi.c exactly, edges included.
        Takes a HARDWARE pen, like the C: callers map through MAP_COL."""
        base = self.base + y1 * STRIDE
        rows = y2 - y1 + 1
        c = ((hwpen & 0x0F) << 4) | (hwpen & 0x0F)
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

    def _xor_rect_dev(self, x1, y1, x2, y2):
        """Mirrors xor_rect_dev(): complement every pixel, edges by nibble."""
        base = self.base + y1 * STRIDE
        rows = y2 - y1 + 1
        bl, br = x1 >> 1, x2 >> 1
        if bl == br:
            m = 0x0F if (x1 & 1) else (0xF0 if (x2 & 1) == 0 else 0xFF)
            self.s.rmw(base + bl, STRIDE, 1, rows, m, 5)
            return
        if x1 & 1:
            self.s.rmw(base + bl, STRIDE, 1, rows, 0x0F, 5)
            bl += 1
        if (x2 & 1) == 0:
            self.s.rmw(base + br, STRIDE, 1, rows, 0xF0, 5)
            br -= 1
        if br >= bl:
            self.s.rmw(base + bl, STRIDE, br - bl + 1, rows, 0xFF, 5)

    def _visible(self, x, y):
        if self.clip and not (self.xmn <= x <= self.xmx and
                              self.ymn <= y <= self.ymx):
            return False
        return 0 <= x < SCR_W and 0 <= y < SCR_H

    def _plot(self, x, y, hwpen):
        """Write one HARDWARE pen; callers map through MAP_COL."""
        if not self._visible(x, y):
            return
        self._plot_raw(x, y, hwpen)

    def _plot_raw(self, x, y, hwpen):
        """The same, clipped to the screen only -- the pointer."""
        if not (0 <= x < SCR_W and 0 <= y < SCR_H):
            return
        a = self.base + y * STRIDE + (x >> 1)
        b = self.s.mem[a]
        if x & 1:
            self.s.mem[a] = (b & 0xF0) | (hwpen & 0x0F)
        else:
            self.s.mem[a] = (b & 0x0F) | ((hwpen & 0x0F) << 4)

    def _plot_xor(self, x, y):
        if not self._visible(x, y):
            return
        a = self.base + y * STRIDE + (x >> 1)
        self.s.mem[a] ^= 0x0F if (x & 1) else 0xF0

    def _init_wk(self, w):
        """init_wk: the attributes a workstation opens with, from work_in,
        validated as the setters validate them.  Donor names: fill_style is
        the INTERIOR (hollow/solid/pattern/hatch/user), fill_index is
        vsf_style's index minus one -- the minus one the ROM's init forgets,
        which gem4xe does not reproduce."""
        self.line_index = w[1] if 1 <= w[1] <= 7 else 1
        self.line_color = w[2] if 0 <= w[2] <= 15 else 1
        self.text_color = w[6] if 0 <= w[6] <= 15 else 1
        self.fill_style = w[7] if FIS_HOLLOW <= w[7] <= FIS_USER else FIS_HOLLOW
        top = MAX_FILL_PATTERN if self.fill_style == FIS_PATTERN else MAX_FILL_HATCH
        self.fill_index = (w[8] if 1 <= w[8] <= top else 1) - 1
        self.fill_color = w[9] if 0 <= w[9] <= 15 else 1

    def _paint_pixel(self, x, y, pen, is_set):
        """One pixel of a primitive in the current writing mode, given
        whether the pattern bit is set there: replace = pen / pen 0,
        transparent = pen where set, XOR = complement where set, erase =
        pen where clear."""
        m = self.wrt_mode + 1
        if m == MD_TRANS:
            if is_set:
                self._plot(x, y, MAP_COL[pen & 15])
        elif m == MD_XOR:
            if is_set:
                self._plot_xor(x, y)
        elif m == MD_ERASE:
            if not is_set:
                self._plot(x, y, MAP_COL[pen & 15])
        else:
            self._plot(x, y, MAP_COL[pen & 15] if is_set else MAP_COL[0])

    def _paint_rect(self, x1, y1, x2, y2, pen):
        """A solid rectangle in the current writing mode (already clipped)."""
        m = self.wrt_mode + 1
        if m == MD_XOR:
            self._xor_rect_dev(x1, y1, x2, y2)
        elif m == MD_ERASE:
            return
        else:
            self._fill_rect_dev(x1, y1, x2, y2, MAP_COL[pen & 15])

    def _fill_pattern(self):
        """st_fl_ptr: what the interior and index resolve to -- the rows of
        the current fill pattern and the mask that picks a row from y.  A
        hollow fill is a pattern of no bits, a solid one of all bits; the
        writing mode then says what a clear bit does (replace writes pen 0
        there, which is why a hollow box in replace mode is WHITE, not
        untouched)."""
        fs, fi = self.fill_style, self.fill_index
        if fs == FIS_SOLID:
            return [0xFFFF], 0
        if fs == FIS_PATTERN:
            if fi < 8:
                return FILL_DITHER[fi * 4:fi * 4 + 4], 3
            return FILL_OEM[(fi - 8) * 8:(fi - 8) * 8 + 8], 7
        if fs == FIS_HATCH:
            if fi < 6:
                return FILL_HATCH0[fi * 8:fi * 8 + 8], 7
            return FILL_HATCH1[(fi - 6) * 16:(fi - 6) * 16 + 16], 15
        if fs == FIS_USER:
            return list(self.ud_patrn), 15
        return [0x0000], 0

    def _patt_rect(self, x1, y1, x2, y2, pen):
        """A rectangle in the current fill pattern and writing mode (already
        clipped).  The pattern is anchored to the SCREEN, not the rectangle:
        row y uses pattern row (y & mask), bit 15 is pixel x = 0 mod 16."""
        rows, msk = self._fill_pattern()
        if all(r == 0xFFFF for r in rows):
            self._paint_rect(x1, y1, x2, y2, pen)
            return
        m = self.wrt_mode + 1
        if all(r == 0 for r in rows):
            if m == MD_REPLACE:
                self._fill_rect_dev(x1, y1, x2, y2, MAP_COL[0])
            elif m == MD_ERASE:
                self._fill_rect_dev(x1, y1, x2, y2, MAP_COL[pen & 15])
            return
        for y in range(y1, y2 + 1):
            r = rows[y & msk]
            for x in range(x1, x2 + 1):
                self._paint_pixel(x, y, pen, (r >> (15 - (x & 15))) & 1)

    def _line(self, x1, y1, x2, y2):
        mask = self.line_styles[self.line_index if 1 <= self.line_index <= 7 else 1]
        if y1 == y2 and mask == 0xFFFF:
            a, b = sorted((x1, x2))
            r = self._clip_rect(a, y1, b, y2)
            if r:
                self._paint_rect(*r, self.line_color)
            return
        if x1 == x2 and mask == 0xFFFF:
            a, b = sorted((y1, y2))
            r = self._clip_rect(x1, a, x2, b)
            if r:
                self._paint_rect(*r, self.line_color)
            return
        dx, dy = abs(x2 - x1), abs(y2 - y1)
        sx = 1 if x1 < x2 else -1
        sy = 1 if y1 < y2 else -1
        err, bit = dx - dy, 0
        while True:
            self._paint_pixel(x1, y1, self.line_color,
                              mask & (1 << (15 - (bit & 15))))
            bit += 1
            if x1 == x2 and y1 == y2:
                break
            e2 = err << 1
            if e2 > -dy:
                err -= dy; x1 += sx
            if e2 < dx:
                err += dx; y1 += sy

    def _rform(self, f):
        """(base, stride, w, h, is_screen) of a raster form: None is the
        screen, otherwise a VramForm (vdi.c rform_of)."""
        if f is None:
            return self.base, STRIDE, SCR_W, SCR_H, True
        return f.addr, f.stride, f.w, f.h, False

    @staticmethod
    def _clip_to(x1, y1, x2, y2, w, h):
        x1 = max(x1, 0); y1 = max(y1, 0)
        x2 = min(x2, w - 1); y2 = min(y2, h - 1)
        return (x1, y1, x2, y2) if (x1 <= x2 and y1 <= y2) else None

    def _cpyfm(self, pts, src=None, dst=None):
        """Mirrors vdi_vro_cpyfm(), including the alignment fast/slow split.

        The VBXE blitter has no shifter, so 4bpp pixels can only be moved
        between positions of the same parity.  Both paths must produce the
        same pixels -- that is what the conformance cases check.

        src and dst are the forms: None for the screen, a VramForm for the
        AES's save buffer.  The destination is clipped to the workstation's
        rectangle and the screen when it IS the screen and to its own bounds
        otherwise; the source is clipped to its form's bounds, each clip
        dropping the same span from the other end -- the driver's rule, not
        the donor's, which never clips a source.
        """
        sx1, sx2 = sorted((pts[0], pts[2]))
        sy1, sy2 = sorted((pts[1], pts[3]))
        dx1, dx2 = sorted((pts[4], pts[6]))
        dy1, dy2 = sorted((pts[5], pts[7]))
        w, h = sx2 - sx1 + 1, sy2 - sy1 + 1
        if w <= 0 or h <= 0:
            return
        sb, ss, sw, sh, _ = self._rform(src)
        db, ds, dw, dh, dscreen = self._rform(dst)
        if dscreen:
            c = self._clip_rect(dx1, dy1, dx1 + w - 1, dy1 + h - 1)
        else:
            c = self._clip_to(dx1, dy1, dx1 + w - 1, dy1 + h - 1, dw, dh)
        if c is None:
            return
        cx1, cy1, cx2, cy2 = c
        sx1 += cx1 - dx1
        sy1 += cy1 - dy1
        dx1, dy1 = cx1, cy1
        w, h = cx2 - cx1 + 1, cy2 - cy1 + 1
        sx2, sy2 = sx1 + w - 1, sy1 + h - 1
        c = self._clip_to(sx1, sy1, sx2, sy2, sw, sh)
        if c is None:
            return
        cx1, cy1, cx2, cy2 = c
        dx1 += cx1 - sx1
        dy1 += cy1 - sy1
        sx1, sy1 = cx1, cy1
        w, h = cx2 - cx1 + 1, cy2 - cy1 + 1
        if ((sx1 ^ dx1) & 1) == 0 and (sx1 & 1) == 0 and (w & 1) == 0:
            self.s.move(sb + sy1 * ss + (sx1 >> 1), ss,
                        db + dy1 * ds + (dx1 >> 1), ds,
                        w >> 1, h)
            return
        for y in range(h):
            sy = (sy1 + h - 1 - y) if dy1 > sy1 else (sy1 + y)
            dy = (dy1 + h - 1 - y) if dy1 > sy1 else (dy1 + y)
            for i in range(w):
                sx = (sx1 + w - 1 - i) if dx1 > sx1 else (sx1 + i)
                dx = (dx1 + w - 1 - i) if dx1 > sx1 else (dx1 + i)
                v = self.s.mem[sb + sy * ss + (sx >> 1)]
                pen = (v & 0x0F) if (sx & 1) else (v >> 4)
                a = db + dy * ds + (dx >> 1)
                b = self.s.mem[a]
                if dx & 1:
                    self.s.mem[a] = (b & 0xF0) | pen
                else:
                    self.s.mem[a] = (b & 0x0F) | (pen << 4)

    def _vrt_cpyfm(self, pts, ints, form):
        """Mirrors vdi_vrt_cpyfm(): a 1-plane source expanded into colours.

        `form` is (bits, wdwidth) -- the same bytes the harness poked into the
        target's scratch area, MSB-first, rows fd_wdwidth WORDS apart.
        """
        bits, wdwidth = form
        mode = ints[0]
        fg, bg = MAP_COL[ints[1] & 15], MAP_COL[ints[2] & 15]
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
                        self._plot_xor(dx, dy)
                elif mode == MD_ERASE:
                    if not on:
                        self._plot(dx, dy, bg)
                else:
                    self._plot(dx, dy, fg if on else bg)

    def _gtext(self, pts, ints):
        """Mirrors vdi_v_gtext().  Left/baseline alignment: y is the BASELINE
        and the cell top is y - FONT_TOP.  Each glyph pixel goes through the
        writing-mode rules of _paint_pixel (replace paints pen 0 behind the
        glyph, transparent does not, XOR complements, erase paints the
        paper)."""
        x, y = pts[0], pts[1]
        cy = y - FONT_TOP
        for i, code in enumerate(ints):
            cx = x + i * FONT_W
            for row in range(FONT_H):
                b = FONT[row * FONT_STRIDE + (code & 0xFF)]
                for col in range(FONT_W):
                    self._paint_pixel(cx + col, cy + row, self.text_color,
                                      b & (0x80 >> col))

    def _workout(self):
        """v_opnwk's work_out, in full: the per-call record carries only the
        first three words of each, but the AES reads further in (pixel size
        at intout[3..4], the character cell at ptsout[0..3]) and derives its
        layout from them.  Must match fill_workout() in src/vdi/vdi.c."""
        self.intout = [0] * 45
        self.ptsout = [0] * 12
        self.intout[0:15] = [SCR_W - 1, SCR_H - 1, 0,
                             372, 372,          # pixel width/height, microns
                             1, 7, 1, 6, 8, 1,  # char heights, line types,
                                                # widths, marker types/sizes,
                                                # faces
                             24, 12, 16, 0]     # patterns, hatches, colours,
                                                # GDPs
        self.intout[35:43] = [1, 0, 1, 0, 16, 1, 1, 1]
        self.ptsout[0:4] = [FONT_W, FONT_H, FONT_W, FONT_H]
        self.ptsout[4:8] = [1, 0, 1, 0]         # line width range
        self.contrl2, self.contrl4 = 6, 45

    def _extnd1(self):
        """vq_extnd(1): the second capability block.  intout[4] = planes is
        the one the AES reads (gsx_start), intout[1] = 16 the one the
        harness records."""
        self.intout = [0] * 45
        self.ptsout = [0] * 12
        self.intout[0:7] = [0, 16, 0, 0, 4, 1, 1]
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
        """Data over mask over the screen.  Like real GEM's, the pointer
        ignores vs_clip: it is drawn wherever it is on the screen.  (Both
        this model and the target once clipped it -- agreeing, and wrong.)"""
        for row in range(16):
            m, d = self.cur_mask[row], self.cur_data[row]
            for col in range(16):
                bit = 0x8000 >> col
                if m & bit:
                    self._plot_raw(cx + col, cy + row, MAP_COL[self.cur_bg & 15])
                if d & bit:
                    self._plot_raw(cx + col, cy + row, MAP_COL[self.cur_fg & 15])

    def _cursor_show_now(self):
        cx, cy = self.ptr_x - self.cur_xhot, self.ptr_y - self.cur_yhot
        self._cursor_save(cx, cy)
        self._cursor_paint(cx, cy)
        self.cur_drawn = True

    def _cursor_hide_now(self):
        if self.cur_drawn:
            self._cursor_restore()
            self.cur_drawn = False

    def _cursor_move(self):
        """vdi_cursor_move(): erase-move-redraw, only when the pointer
        moved and the cursor is shown."""
        if self.cur_hide:
            return
        if ((self.ptr_x, self.ptr_y) == (self.cur_lastx, self.cur_lasty)
                and self.cur_drawn):
            return
        self._cursor_hide_now()
        self.cur_lastx, self.cur_lasty = self.ptr_x, self.ptr_y
        self._cursor_show_now()

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
            if len(ints) >= 11:
                self._init_wk(ints)
            self._workout()
        elif op == VQ_EXTND:
            if ints and ints[0]:
                self._extnd1()
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
            if r:
                self._patt_rect(*r, self.fill_color)
        elif op == VRO_CPYFM:
            if form:
                self._cpyfm(pts, form[0], form[1])
            else:
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
            # char w, char h, cell w, cell h -- and "char height" is the
            # font's TOP (baseline to top of cell), as the ST ROM and EmuTOS
            # both return it; the AES adds it to a cell top for v_gtext.
            self.ptsout = [FONT_W, FONT_TOP, FONT_W, FONT_H]
            self.contrl2 = 2
        elif op == VQT_ATTRIBUTES:
            # font, colour, rotation, h/v alignment (never set here), and
            # the writing mode as vswr_mode numbers it (1 = replace)
            self.intout = [1, self.text_color, 0, 0, 0, self.wrt_mode + 1]
            self.ptsout = [FONT_W, FONT_TOP, FONT_W, FONT_H]
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
            self.fill_style = ints[0] if 0 <= ints[0] <= 4 else FIS_HOLLOW
            self.intout[0] = self.fill_style
            self.contrl4 = 1
        elif op == VSF_STYLE:
            # The range depends on the interior in force: 1..24 for patterns,
            # 1..12 for hatches; anything else becomes 1.  Stored minus one.
            top = MAX_FILL_PATTERN if self.fill_style == FIS_PATTERN else MAX_FILL_HATCH
            fi = ints[0] if 1 <= ints[0] <= top else 1
            self.fill_index = fi - 1
            self.intout[0] = fi
            self.contrl4 = 1
        elif op == VSF_UDPAT:
            # Only a 16-word (single-plane) pattern is accepted; anything
            # else leaves the old one alone.  Nothing is returned.
            if len(ints) == 16:
                self.ud_patrn = [w & 0xFFFF for w in ints]
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
        elif op == V_STRING:
            # one key per call; contrl[4] = 0 says there was none
            if self.keys:
                self.intout[0] = self.keys.pop(0)
                self.contrl4 = 1
        elif op == VQ_KEY_S:
            self.intout[0] = self.key_mods
            self.contrl4 = 1
        # everything else is a documented no-op on this driver

    def input_poll(self, tick=False):
        """vdi_input_poll(): the cursor follows the pointer, then motion
        every pass, the button vector on a change, the timer vector once
        per frame.  The key poll has no counterpart here -- a plan step
        puts its key straight in `keys`.  `tick` says this pass is the
        first of a frame, where VCOUNT's wrap lands."""
        self._cursor_move()
        if self.vec_motv:
            self.vec_motv()
        if self.buttons != self.last_buttons:
            self.last_buttons = self.buttons
            if self.vec_butv:
                self.vec_butv()
        if tick and self.vec_timv:
            self.vec_timv()

    def result(self):
        """The record src/m3_vdi.c writes for the last call, with
        the same masking as the target: only the declared words count."""
        return record(self.contrl2, self.contrl4, self.intout, self.ptsout)

    def run(self, script):
        self.results = []
        for rec in script:
            self.call(rec[0],
                      rec[1] if len(rec) > 1 else (),
                      rec[2] if len(rec) > 2 else (),
                      rec[3] if len(rec) > 3 else None)
            self.results.append(self.result())

    def to_rgb(self):
        return self.s.to_rgb(self.base, HW_PAL)


_VBXE_H = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "src", "vbxe", "vbxe.h")


def vram_symbol(name):
    """A VRAM address from vbxe.h's map, read rather than restated, so the
    reference's picture of VRAM cannot drift from the driver's."""
    m = re.search(r"#define\s+%s\s+0x([0-9A-Fa-f]+)UL" % name, open(_VBXE_H).read())
    if not m:
        raise KeyError(name)
    return int(m.group(1), 16)


class VramForm:
    """An MFDB whose fd_addr is a VRAM address -- the AES's save buffer
    (vbxe.h VR_SAVE), which bb_save and bb_restore copy the screen to and
    from.  mfdb_addr is where the harness staged the 20-byte MFDB on the
    target, for encode(); the reference reads the form's fields directly."""

    def __init__(self, addr, w, h, wdwidth, planes=4, mfdb_addr=0):
        self.addr, self.w, self.h = addr, w, h
        self.wdwidth, self.planes, self.mfdb_addr = wdwidth, planes, mfdb_addr

    @classmethod
    def save_buffer(cls, mfdb_addr=0):
        """The AES's: a whole screen at VR_SAVE, laid out like the screen."""
        return cls(vram_symbol("VR_SAVE"), SCR_W, SCR_H, SCR_W // 16, 4,
                   mfdb_addr)

    @property
    def stride(self):
        return self.wdwidth * 2 * self.planes

    def pack(self):
        return pack_mfdb(self.addr, self.w, self.h, self.wdwidth, self.planes)


def encode(script, mfdb_addr=0, screen_mfdb=0):
    """Serialise a script into the WORD stream src/m3_vdi.c expects.

    A record carrying a `form` gets mfdb_addr planted in contrl[7..8], which is
    where the harness has staged the MFDB on the target.  A vro_cpyfm record's
    form is the (source, destination) pair of VramForm-or-None, planted in
    contrl[7..8] and contrl[9..10]: None becomes screen_mfdb, the address of
    a staged MFDB with fd_addr 0 (or 0, which the driver takes as the screen).
    """
    def addr_of(f):
        return screen_mfdb if f is None else f.mfdb_addr

    out = []
    for rec in script:
        op = rec[0]
        pts = list(rec[1]) if len(rec) > 1 else []
        ints = list(rec[2]) if len(rec) > 2 else []
        form = rec[3] if len(rec) > 3 else None
        c7 = c8 = c9 = c10 = 0
        if form and op == VRO_CPYFM:
            c7, c9 = addr_of(form[0]), addr_of(form[1])
        elif form:
            c7 = mfdb_addr & 0xFFFF
            c8 = (mfdb_addr >> 16) & 0xFFFF
        out += [op, len(pts) // 2, len(ints), c7, c8, c9, c10]
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


def decode_tb(prev, now):
    """Reference for ptr_decode_tb() in src/vdi/pointer.c: the CX80 trak-ball
    in trak-ball mode, which is not quadrature.  Each axis has a direction
    line (bit 1 of the pair) and a pulse line (bit 0) that toggles once per
    count; a count is a change on the pulse line, and its sign is the
    direction line AS SAMPLED WITH THE EDGE -- Altirra's model changes the
    two in the same update, so the previous sample's direction is stale.
    High is + on the port's active-low lines.
    """
    if ((prev ^ now) & 1) == 0:
        return 0
    return +1 if now & 2 else -1


def xem1_valid(pot):
    """A mouSTer XEM1 pot reading is 64..191: bit 6 and bit 7 differ."""
    return (((pot >> 1) ^ pot) & 0x40) != 0


def decode_xem1(old, now):
    """Reference for ptr_decode_xem1() in src/vdi/pointer.c: the movement
    between two XEM1 readings, and the reading to measure the next from.

    This is the firmware's sample driver (Mad-Pascal samples/a8/mouSTer/
    vbl.asm, calcDX) followed instruction by instruction rather than a
    formula of my own, so that what the C is checked against is what the
    device's author wrote: the 7-bit difference, sign-extended from bit 6
    by the rol/eor/and/eor trick, halved toward zero by cmp/ror/adc, and a
    reading that halves to nothing left as the reference for next time.
    """
    a = (now - old) & 0xFF                      # txa; sec; sbc oldX
    if a == 0:                                  # beq endCalcX
        return 0, old
    t = a                                       # sta @+
    a = (a << 1) & 0xFF                         # rol (the carry in is masked out below)
    a ^= t                                      # eor @: -- bit 7 = d6 ^ d7
    a &= 0x80                                   # and #$80
    a ^= t                                      # eor @- -- bit 7 := d6
    c = 1 if a >= 0x80 else 0                   # cmp #$80
    c, a = a & 1, (a >> 1) | (c << 7)           # ror
    if a & 0x80:                                # bpl @+
        a = (a + c) & 0xFF                      # adc #0
    if a == 0:                                  # @ beq endcalcX
        return 0, old
    return (a - 256 if a & 0x80 else a), now    # stx oldX


def pack_mfdb(bits_addr, w, h, wdwidth, planes=1):
    """The 20-byte MFDB the target reads, little-endian."""
    import struct
    return struct.pack("<Ihhhhhhhh", bits_addr, w, h, wdwidth, 0, planes,
                       0, 0, 0)
