#!/usr/bin/env python3
"""A GEM resource file, built on the host, and what the AES makes of it.

A .RSC (the DRI/RCS "old" format, the one every TOS reads) is a header of
eighteen big-endian words followed by the OBJECT, TEDINFO, ICONBLK and
BITBLK arrays, the strings and image bits they point at, and three tables
of longs -- the free strings, the free images, the trees -- with every
pointer an offset from the start of the file and every object rectangle a
(pixel offset << 8 | character position) word, so that one file serves
every screen the AES may find itself on.

`Rsc` collects the pieces and lays them out at fixed file offsets; `file()`
is the file, and `expect(base, wchar, hchar, width)` is the same file as
the AES's rsrc_load leaves it in memory at `base`: words in the 65816's
order, offsets made addresses, rectangles made pixels, the TEDINFO
lengths filled in -- together with the `Obj` lists and the address map
tools/aesref.py draws the trees from.  The two come from ONE description,
so the target's fixed-up image is compared with what the description
means, not with a second loader's reading of the same bytes.

The rules are EmuTOS aes/gemrslib.c's (rs_readit, rs_fixit, fix_chpos,
fix_long, fix_tedinfo_std, fix_nptrs, fix_objects); src/aes/rsrc.c is
the other reading of them.
"""
import struct

import aesref
from aesref import Obj, Text, Ted, Bitblk, Iconblk, Rect, G_BOX, G_IBOX, G_BOXCHAR

HDR_SIZE = 36
OBJ_SIZE, TED_SIZE, BITBLK_SIZE, ICONBLK_SIZE = 24, 28, 14, 34
NIL = -1

# rsrc_gaddr / rsrc_saddr types
(R_TREE, R_OBJECT, R_TEDINFO, R_ICONBLK, R_BITBLK, R_STRING, R_IMAGEDATA,
 R_OBSPEC, R_TEPTEXT, R_TEPTMPLT, R_TEPVALID, R_IBPMASK, R_IBPDATA,
 R_IBPTEXT, R_BIPDATA, R_FRSTR, R_FRIMG) = range(17)


def ch(chars, px=0):
    """A rectangle word: `chars` character cells, then `px` pixels (-128..127
    -- the AES reads the high byte as signed past 128, so 128 itself is
    +128)."""
    assert 0 <= chars <= 255 and -128 <= px <= 128, (chars, px)
    return ((px & 0xFF) << 8) | chars


def fix_chpos(word, which, wchar, hchar, width):
    """gemrslib.c fix_chpos, on one word: which is 0 x, 1 y, 2 w, 3 h."""
    coffset = (word >> 8) & 0xFF
    cpos = word & 0xFF
    if which == 0:
        cpos *= wchar
    elif which == 1:
        cpos *= hchar
    elif which == 2:
        cpos = width if cpos == 80 else cpos * wchar
    else:
        cpos *= hchar
    return cpos + (coffset - 256 if coffset > 128 else coffset)


class _Item:
    """Anything with a file offset once the Rsc is laid out."""
    off = None


class String(_Item):
    def __init__(self, s):
        self.s = s
        self.blob = s.encode("latin-1") + b"\0"


class ImageData(_Item):
    def __init__(self, rows):
        self.blob = bytes(rows)


class RBitblk(_Item):
    def __init__(self, data, wb, hl, x, y, color):
        self.data, self.wb, self.hl, self.x, self.y, self.color = \
            data, wb, hl, x, y, color


class RIconblk(_Item):
    def __init__(self, mask, data, text, char, xchar, ychar, xicon, yicon,
                 wicon, hicon, xtext, ytext, wtext, htext):
        (self.mask, self.data, self.text, self.char, self.xchar, self.ychar,
         self.xicon, self.yicon, self.wicon, self.hicon, self.xtext,
         self.ytext, self.wtext, self.htext) = (
            mask, data, text, char, xchar, ychar, xicon, yicon, wicon, hicon,
            xtext, ytext, wtext, htext)


class RTed(_Item):
    def __init__(self, text, tmplt, valid, font, just, color, thickness,
                 txtlen, tmplen):
        (self.text, self.tmplt, self.valid, self.font, self.just, self.color,
         self.thickness, self.txtlen, self.tmplen) = (
            text, tmplt, valid, font, just, color, thickness, txtlen, tmplen)


class RObj(_Item):
    def __init__(self, nxt, head, tail, typ, flags, state, spec, x, y, w, h):
        (self.nxt, self.head, self.tail, self.typ, self.flags, self.state,
         self.spec, self.x, self.y, self.w, self.h) = (
            nxt, head, tail, typ, flags, state, spec, x, y, w, h)


class Rsc:
    def __init__(self):
        self.strings, self.images, self.bitblks, self.iconblks = [], [], [], []
        self.teds, self.objects = [], []
        self.trees = []                 # (first object index, count)
        self.frstr, self.frimg = [], [] # String / RBitblk items
        self.size = None

    # -- the pieces ----------------------------------------------------------
    def string(self, s):
        it = String(s)
        self.strings.append(it)
        return it

    def imagedata(self, rows):
        it = ImageData(rows)
        self.images.append(it)
        return it

    def bitblk(self, rows, wb, hl, x=0, y=0, color=aesref.BLACK):
        assert len(rows) == wb * hl, (len(rows), wb, hl)
        it = RBitblk(self.imagedata(rows), wb, hl, x, y, color)
        self.bitblks.append(it)
        return it

    def iconblk(self, mask, data, text, wicon, hicon, char=0, xchar=0,
                ychar=0, xicon=0, yicon=0, xtext=0, ytext=0, wtext=0, htext=0):
        assert len(mask) == len(data) == (wicon // 8) * hicon
        it = RIconblk(self.imagedata(mask), self.imagedata(data),
                      self.string(text), char, xchar, ychar, xicon, yicon,
                      wicon, hicon, xtext, ytext, wtext, htext)
        self.iconblks.append(it)
        return it

    def ted(self, text, tmplt, valid, font=aesref.IBM, just=aesref.TE_LEFT,
            color=0x1180, thickness=0):
        """The file carries te_txtlen/te_tmplen as RCS wrote them; the AES
        overwrites both with strlen + 1 at load, so what goes in the file
        is deliberately wrong (0) to prove that.  RCS sizes the text
        buffer to the template's underscores; so does this.  `tmplt` and
        `valid` may be `String` items already in the file, so that several
        fields share one template the way RCS lets them."""
        if not isinstance(tmplt, String):
            tmplt = self.string(tmplt)
        if not isinstance(valid, String):
            valid = self.string(valid)
        room = max(tmplt.s.count("_"), len(text)) + 1
        t = String(text)
        t.blob = t.blob + b"\0" * (room - len(t.blob))
        self.strings.append(t)
        it = RTed(t, tmplt, valid, font, just, color, thickness, 0, 0)
        self.teds.append(it)
        return it

    def tree(self, objs):
        """objs: (next, head, tail, type, flags, state, spec, x, y, w, h)
        with the rectangle words from ch() and spec an item or an int."""
        first = len(self.objects)
        for o in objs:
            self.objects.append(RObj(*o))
        self.trees.append((first, len(objs)))
        return len(self.trees) - 1

    def free_string(self, s):
        it = self.string(s)
        self.frstr.append(it)
        return len(self.frstr) - 1

    def free_image(self, bb):
        self.frimg.append(bb)
        return len(self.frimg) - 1

    # -- the file --------------------------------------------------------------
    def layout(self):
        """Assign every item its file offset.  Strings first (bytes, so the
        tables after them start even), then the four arrays, then the three
        tables of longs -- and THE IMAGE BITS LAST.

        Last is not cosmetic.  A resource is loaded into the application
        pool, which is 14 KB of bank $00 for the desktop, its resource and
        everything resident beside it, and the image bits are a quarter of
        DESKTOP.RSC while being the one part of it nothing in bank $00
        needs to reach: an ICONBLK names its mask and its image in 32-bit
        fields and the VDI has taken 32-bit addresses since phase 2.  With
        the bits at the END of the file, src/aes/rsrc.c can copy them to
        far memory and hand the pool back everything above them -- without
        compacting the middle of the file, which would invalidate every
        offset already fixed up.

        The strings stay where they are.  ob_spec points at them and the
        object library reads them through a near pointer."""
        off = HDR_SIZE
        self.o_string = off
        for it in self.strings:
            it.off = off
            off += len(it.blob)
        off += off & 1
        self.o_bitblk = off
        for it in self.bitblks:
            it.off = off
            off += BITBLK_SIZE
        self.o_iconblk = off
        for it in self.iconblks:
            it.off = off
            off += ICONBLK_SIZE
        self.o_tedinfo = off
        for it in self.teds:
            it.off = off
            off += TED_SIZE
        self.o_object = off
        for it in self.objects:
            it.off = off
            off += OBJ_SIZE
        self.o_frstr = off
        off += 4 * len(self.frstr)
        self.o_frimg = off
        off += 4 * len(self.frimg)
        self.o_trindex = off
        off += 4 * len(self.trees)
        off += off & 1
        self.o_imdata = off             # last: see the note above
        for it in self.images:
            it.off = off
            off += len(it.blob)
        self.size = off
        return off

    def _spec(self, o):
        return o.spec if isinstance(o.spec, int) else o.spec.off

    def header(self, e):
        return struct.pack(e + "18H", 0, self.o_object, self.o_tedinfo,
                           self.o_iconblk, self.o_bitblk, self.o_frstr,
                           self.o_string, self.o_imdata, self.o_frimg,
                           self.o_trindex, len(self.objects), len(self.trees),
                           len(self.teds), len(self.iconblks), len(self.bitblks),
                           len(self.frstr), len(self.frimg), self.size)

    def file(self):
        """The file: big-endian, offsets, character rectangles."""
        self.layout()
        e = ">"
        out = bytearray(self.size)
        out[0:HDR_SIZE] = self.header(e)
        for it in self.strings + self.images:
            out[it.off:it.off + len(it.blob)] = it.blob
        for it in self.bitblks:
            out[it.off:it.off + BITBLK_SIZE] = struct.pack(
                e + "Ihhhhh", it.data.off, it.wb, it.hl, it.x, it.y, it.color)
        for it in self.iconblks:
            out[it.off:it.off + ICONBLK_SIZE] = struct.pack(
                e + "IIIhhhhhhhhhhh", it.mask.off, it.data.off, it.text.off,
                it.char, it.xchar, it.ychar, it.xicon, it.yicon, it.wicon,
                it.hicon, it.xtext, it.ytext, it.wtext, it.htext)
        for it in self.teds:
            out[it.off:it.off + TED_SIZE] = struct.pack(
                e + "IIIhhhhhhhh", it.text.off, it.tmplt.off, it.valid.off,
                it.font, 0, it.just, it.color, 0, it.thickness,
                it.txtlen, it.tmplen)
        for o in self.objects:
            out[o.off:o.off + OBJ_SIZE] = struct.pack(
                e + "hhhHHHIHHHH", o.nxt, o.head, o.tail, o.typ, o.flags,
                o.state, self._spec(o) & 0xFFFFFFFF, o.x, o.y, o.w, o.h)
        p = self.o_frstr
        for it in self.frstr:
            out[p:p + 4] = struct.pack(e + "I", it.off)
            p += 4
        p = self.o_frimg
        for it in self.frimg:
            out[p:p + 4] = struct.pack(e + "I", it.off)
            p += 4
        p = self.o_trindex
        for first, n in self.trees:
            out[p:p + 4] = struct.pack(e + "I", self.objects[first].off)
            p += 4
        return bytes(out)

    # -- what the AES makes of it ----------------------------------------------
    def expect(self, base, wchar, hchar, width, imbase=None):
        """(image, trees, mem): the file as rsrc_load leaves it at `base`,
        the trees as aesref Obj lists, and the address map for aesref.

        `imbase` is where the ICON BITMAPS ended up, when rs_load moved
        them to far memory and wound the pool back over them -- which it
        does for any resource with no BITBLKs and no free images, because
        an ICONBLK names its mask and its image in 32-bit fields
        (src/aes/rsrc.c).  Pass it and the ICONBLKs carry far addresses,
        as the target's do; leave it and they are near, which is what a
        resource whose bits stayed in the pool has."""
        self.layout()
        if imbase is None:
            imbase = base
        else:
            imbase -= self.o_imdata        # so + it.off lands on the bytes
        e = "<"
        out = bytearray(self.size)
        out[0:HDR_SIZE] = self.header(e)
        mem = {}
        for it in self.strings:
            out[it.off:it.off + len(it.blob)] = it.blob
            mem[base + it.off] = Text(it.s, len(it.blob))
        for it in self.images:
            out[it.off:it.off + len(it.blob)] = it.blob
            mem[imbase + it.off] = it.blob
        for it in self.bitblks:
            # never moved: rs_load keeps the bits of a resource with
            # BITBLKs in the pool, so this is still a near address
            b = Bitblk(base + it.data.off, it.wb, it.hl, it.x, it.y, it.color)
            out[it.off:it.off + BITBLK_SIZE] = b.pack()
            mem[base + it.off] = b
        for it in self.iconblks:
            ib = Iconblk(imbase + it.mask.off, imbase + it.data.off,
                         base + it.text.off, it.char, it.xchar, it.ychar,
                         Rect(it.xicon, it.yicon, it.wicon, it.hicon),
                         Rect(it.xtext, it.ytext, it.wtext, it.htext))
            out[it.off:it.off + ICONBLK_SIZE] = ib.pack()
            mem[base + it.off] = ib
        for it in self.teds:
            t = Ted(base + it.text.off, base + it.tmplt.off, base + it.valid.off,
                    font=it.font, just=it.just, color=it.color,
                    thickness=it.thickness, txtlen=len(it.text.s) + 1,
                    tmplen=len(it.tmplt.s) + 1)
            out[it.off:it.off + TED_SIZE] = t.pack()
            mem[base + it.off] = t
        objs = []
        for o in self.objects:
            spec = self._spec(o)
            if (o.typ & 0xFF) not in (G_BOX, G_IBOX, G_BOXCHAR):
                spec += base
            ob = Obj(o.nxt, o.head, o.tail, o.typ, o.flags, o.state, spec,
                     fix_chpos(o.x, 0, wchar, hchar, width),
                     fix_chpos(o.y, 1, wchar, hchar, width),
                     fix_chpos(o.w, 2, wchar, hchar, width),
                     fix_chpos(o.h, 3, wchar, hchar, width))
            out[o.off:o.off + OBJ_SIZE] = ob.pack()
            objs.append(ob)
        p = self.o_frstr
        for it in self.frstr:
            out[p:p + 4] = struct.pack(e + "I", base + it.off)
            p += 4
        p = self.o_frimg
        for it in self.frimg:
            out[p:p + 4] = struct.pack(e + "I", base + it.off)
            p += 4
        p = self.o_trindex
        for first, n in self.trees:
            out[p:p + 4] = struct.pack(e + "I", base + self.objects[first].off)
            p += 4
        trees = [objs[first:first + n] for first, n in self.trees]
        return bytes(out), trees, mem

    def addr(self, rtype, index, base):
        """What rsrc_gaddr(rtype, index) answers at `base` -- the donor's
        get_addr -- or None where it answers -1."""
        h = {R_OBJECT: (self.o_object, OBJ_SIZE),
             R_TEDINFO: (self.o_tedinfo, TED_SIZE),
             R_TEPTEXT: (self.o_tedinfo, TED_SIZE),
             R_ICONBLK: (self.o_iconblk, ICONBLK_SIZE),
             R_IBPMASK: (self.o_iconblk, ICONBLK_SIZE),
             R_BITBLK: (self.o_bitblk, BITBLK_SIZE),
             R_BIPDATA: (self.o_bitblk, BITBLK_SIZE),
             R_FRSTR: (self.o_frstr, 4),
             R_FRIMG: (self.o_frimg, 4)}
        if rtype == R_TREE:
            return base + self.objects[self.trees[index][0]].off
        if rtype == R_OBSPEC:
            return self.addr(R_OBJECT, index, base) + 12
        if rtype == R_TEPTMPLT:
            return self.addr(R_TEDINFO, index, base) + 4
        if rtype == R_TEPVALID:
            return self.addr(R_TEDINFO, index, base) + 8
        if rtype == R_IBPDATA:
            return self.addr(R_ICONBLK, index, base) + 4
        if rtype == R_IBPTEXT:
            return self.addr(R_ICONBLK, index, base) + 8
        if rtype == R_STRING:
            return base + self.frstr[index].off
        if rtype == R_IMAGEDATA:
            return base + self.frimg[index].off
        if rtype in h:
            off, size = h[rtype]
            return base + off + size * index
        return None

