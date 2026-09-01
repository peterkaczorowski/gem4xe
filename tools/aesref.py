#!/usr/bin/env python3
"""Host reference for the AES object library.

Mirrors src/aes/objc.c, drawing through the same tools/vdiref.py VDI the target
draws through its own.  The AES gets no private path to the screen on either
side, which is what keeps the comparison meaningful.
"""
import struct

import vdiref

# ob_type
G_BOX, G_TEXT, G_BOXTEXT, G_IMAGE = 20, 21, 22, 23
G_USERDEF, G_IBOX, G_BUTTON, G_BOXCHAR = 24, 25, 26, 27
G_STRING, G_FTEXT, G_FBOXTEXT, G_ICON, G_TITLE = 28, 29, 30, 31, 32
# ob_flags
NONE, SELECTABLE, DEFAULT, EXIT, EDITABLE = 0, 1, 2, 4, 8
RBUTTON, LASTOB, TOUCHEXIT, HIDETREE = 0x10, 0x20, 0x40, 0x80
# ob_state
NORMAL, SELECTED, CROSSED, CHECKED = 0, 1, 2, 4
DISABLED, OUTLINED, SHADOWED = 8, 0x10, 0x20
NIL = -1

FONT_W, FONT_H, FONT_TOP = 8, 8, 6
OBJ_SIZE = 24


class Obj:
    def __init__(self, nxt, head, tail, typ, flags, state, spec, x, y, w, h):
        (self.ob_next, self.ob_head, self.ob_tail, self.ob_type, self.ob_flags,
         self.ob_state, self.ob_spec, self.ob_x, self.ob_y,
         self.ob_width, self.ob_height) = (nxt, head, tail, typ, flags, state,
                                           spec, x, y, w, h)


def crack(color):
    """border 15-12, text 11-8, mode bit 7, pattern 6-4, inside 3-0."""
    return ((color >> 12) & 0xF, (color >> 8) & 0xF,
            vdiref.MD_REPLACE if (color & 0x80) else vdiref.MD_TRANS,
            (color >> 4) & 7, color & 0xF)


class AES:
    """Draws a tree into a vdiref.VDI.  `strings` maps an address to text."""

    def __init__(self, vdi, tree, strings=None, tedinfo=None):
        self.v = vdi
        self.tree = tree
        self.strings = strings or {}
        self.tedinfo = tedinfo or {}

    # -- primitives, all through the VDI ---------------------------------
    def _rect(self, x, y, w, h, color):
        if w <= 0 or h <= 0:
            return
        self.v.call(vdiref.VSF_COLOR, (), (color,))
        self.v.call(vdiref.VSF_INTERIOR, (), (1,))
        self.v.call(vdiref.VR_RECFL, (x, y, x + w - 1, y + h - 1), ())

    def _border(self, x, y, w, h, th, color):
        n = abs(th)
        for i in range(n):
            ox = i if th > 0 else -(i + 1)
            bx, by = x + ox, y + ox
            bw, bh = w - 2 * ox, h - 2 * ox
            if bw <= 0 or bh <= 0:
                continue
            self._rect(bx, by, bw, 1, color)
            self._rect(bx, by + bh - 1, bw, 1, color)
            self._rect(bx, by, 1, bh, color)
            self._rect(bx + bw - 1, by, 1, bh, color)

    def _text(self, x, y, w, s, color, just, mode):
        if not s:
            return
        n = len(s)
        if just == 2:
            tx = x + (w - n * FONT_W) // 2
        elif just == 1:
            tx = x + w - n * FONT_W
        else:
            tx = x
        self.v.call(vdiref.VSWR_MODE, (), (mode,))
        self.v.call(vdiref.VST_COLOR, (), (color,))
        self.v.call(vdiref.V_GTEXT, (tx, y + FONT_TOP), tuple(s.encode("latin-1")))
        self.v.call(vdiref.VSWR_MODE, (), (vdiref.MD_TRANS,))

    # -- tree ------------------------------------------------------------
    def parent(self, obj):
        for i, o in enumerate(self.tree):
            c = o.ob_head
            while c != NIL:
                if c == obj:
                    return i
                if c == o.ob_tail:
                    break
                c = self.tree[c].ob_next
            if o.ob_flags & LASTOB:
                break
        return NIL

    def offset(self, obj):
        x = y = 0
        o, guard = obj, 0
        while o != NIL and guard < 64:
            x += self.tree[o].ob_x
            y += self.tree[o].ob_y
            o = self.parent(o)
            guard += 1
        return x, y

    def _draw_one(self, obj, x, y):
        ob = self.tree[obj]
        w, h = ob.ob_width, ob.ob_height
        t = ob.ob_type & 0xFF
        if t in (G_BOX, G_IBOX, G_BOXCHAR):
            # bits 31-24 char, 23-16 thickness, 15-0 colour -- the 68000 byte
            # order inside the LONG, which is what a .RSC contains.
            color = ob.ob_spec & 0xFFFF
            th = struct.unpack("b", bytes([(ob.ob_spec >> 16) & 0xFF]))[0]
            bc, tc, md, ip, ic = crack(color)
            if t != G_IBOX:
                self._rect(x, y, w, h, ic)
            self._border(x, y, w, h, th, bc)
            if t == G_BOXCHAR:
                ch = (ob.ob_spec >> 24) & 0xFF
                if ch:
                    self._text(x, y + (h - FONT_H) // 2, w, chr(ch), tc, 2,
                               vdiref.MD_TRANS)
        elif t == G_BUTTON:
            # thickness is computed, not stored: -1, one more for EXIT, one
            # more for DEFAULT; negative grows the border OUTWARD.
            s = self.strings.get(ob.ob_spec, "")
            th = -1
            if ob.ob_flags & EXIT:
                th -= 1
            if ob.ob_flags & DEFAULT:
                th -= 1
            self._rect(x, y, w, h, 0)
            self._border(x, y, w, h, th, 1)
            self._text(x, y + (h - FONT_H) // 2, w, s, 1, 2, vdiref.MD_TRANS)
        elif t == G_TITLE:
            self._border(x, y, w, h, 1, 1)
            self._text(x, y, w, self.strings.get(ob.ob_spec, ""), 1, 0,
                       vdiref.MD_TRANS)
        elif t == G_STRING:
            self._text(x, y, w, self.strings.get(ob.ob_spec, ""), 1, 0,
                       vdiref.MD_TRANS)
        elif t in (G_TEXT, G_BOXTEXT, G_FTEXT, G_FBOXTEXT):
            ted = self.tedinfo[ob.ob_spec]
            bc, tc, md, ip, ic = crack(ted["color"])
            if t in (G_BOXTEXT, G_FBOXTEXT):
                self._rect(x, y, w, h, ic)
                self._border(x, y, w, h, ted["thickness"], bc)
            self._text(x, y + (h - FONT_H) // 2, w, ted["text"], tc,
                       ted["just"], md)

        if ob.ob_state & DISABLED:
            self.v.call(vdiref.VSL_COLOR, (), (0,))
            self.v.call(vdiref.VSL_UDSTY, (), (0xAAAA,))
            self.v.call(vdiref.VSL_TYPE, (), (7,))
            for i in range(0, h, 2):
                self.v.call(vdiref.V_PLINE, (x, y + i, x + w - 1, y + i), ())
            self.v.call(vdiref.VSL_TYPE, (), (1,))
        if ob.ob_state & OUTLINED:
            self._border(x - 3, y - 3, w + 6, h + 6, 1, 1)
        if ob.ob_state & SHADOWED:
            self._rect(x + w, y + 2, 2, h, 1)
            self._rect(x + 2, y + h, w, 2, 1)
        if ob.ob_state & CHECKED:
            self._text(x, y, FONT_W, "\010", 1, 0, vdiref.MD_TRANS)
        if ob.ob_state & SELECTED:
            self.v.call(vdiref.VSL_COLOR, (), (1,))
            self.v.call(vdiref.VSWR_MODE, (), (vdiref.MD_XOR,))
            for i in range(h):
                self.v.call(vdiref.V_PLINE, (x, y + i, x + w - 1, y + i), ())
            self.v.call(vdiref.VSWR_MODE, (), (vdiref.MD_REPLACE,))

    def _walk(self, obj, depth, px, py):
        if obj == NIL or (self.tree[obj].ob_flags & HIDETREE):
            return
        x, y = px + self.tree[obj].ob_x, py + self.tree[obj].ob_y
        self._draw_one(obj, x, y)
        if depth <= 0:
            return
        c = self.tree[obj].ob_head
        while c != NIL:
            self._walk(c, depth - 1, x, y)
            if c == self.tree[obj].ob_tail:
                break
            c = self.tree[c].ob_next

    def draw(self, start, depth, clip):
        self.v.call(vdiref.VS_CLIP,
                    (clip[0], clip[1], clip[0] + clip[2] - 1,
                     clip[1] + clip[3] - 1), (1,))
        px = py = 0
        if start != 0:
            p = self.parent(start)
            if p != NIL:
                px, py = self.offset(p)
        self._walk(start, depth, px, py)
        self.v.call(vdiref.VS_CLIP, (), (0,))

    def find(self, start, depth, mx, my):
        if start == NIL or (self.tree[start].ob_flags & HIDETREE):
            return NIL
        x, y = self.offset(start)
        o = self.tree[start]
        if not (x <= mx < x + o.ob_width and y <= my < y + o.ob_height):
            return NIL
        found = start
        if depth > 0:
            c = o.ob_head
            while c != NIL:
                hit = self.find(c, depth - 1, mx, my)
                if hit != NIL:
                    found = hit
                if c == o.ob_tail:
                    break
                c = self.tree[c].ob_next
        return found


def pack_tree(objs):
    """Serialise to the 24-byte-per-object layout the target reads."""
    out = b""
    for o in objs:
        out += struct.pack("<hhhHHHIhhhh", o.ob_next, o.ob_head, o.ob_tail,
                           o.ob_type, o.ob_flags, o.ob_state, o.ob_spec,
                           o.ob_x, o.ob_y, o.ob_width, o.ob_height)
    return out
