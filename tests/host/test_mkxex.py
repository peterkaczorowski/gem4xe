"""The .xex packer, and in particular the far-code staging it emits.

What is being tested is a CONTRACT BETWEEN TWO FILES: tools/mkxex.py cuts the
far image into chunks, and src/farload.s reassembles them on the target.  The
model below is the second half of that contract written in Python -- it loads a
.xex the way DOS does and copies the way farload does -- so a chunking mistake
shows up here, in a second, instead of as a machine that boots to nothing.

It is a model, not the article: it proves the FORMAT is right, not that the
65816 code is.  tests/emu/m6_farcode.py is what proves the code runs.
"""
import os
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import mkxex  # noqa: E402

RUNAD, INITAD = 0x02E0, 0x02E2


def load_xex(blob):
    """Load a .xex the way an Atari DOS loader does.

    Returns (memory, runad).  `memory` is a dict of 24-bit address -> byte, so
    that a write the loader could not have made -- anything outside bank $00 --
    would be visible rather than silently wrapped.

    INITAD is honoured after EVERY segment once it has been set, which is the
    more aggressive of the two readings DOSes take; the packer has to be
    correct under it.
    """
    assert blob[:2] == b"\xff\xff", "missing $FFFF magic"
    mem, initad, runad = {}, None, None
    i = 2
    while i < len(blob):
        start, end = struct.unpack_from("<HH", blob, i)
        i += 4
        if start == 0xFFFF:                      # an optional repeated magic
            continue
        if end < start:
            raise AssertionError(f"segment ${start:04X}-${end:04X} runs backwards")
        n = end - start + 1
        body = blob[i:i + n]
        assert len(body) == n, "segment runs off the end of the file"
        i += n
        for k, b in enumerate(body):
            mem[start + k] = b
        if start <= INITAD <= end:
            initad = mem[INITAD] | (mem[INITAD + 1] << 8)
        if start <= RUNAD <= end:
            runad = mem[RUNAD] | (mem[RUNAD + 1] << 8)
        if initad:
            farload_copy(mem)
    return mem, runad


def farload_copy(mem, hdr=None):
    """src/farload.s, in Python: copy one staged chunk, then consume it."""
    hdr = SYMS["_fl_hdr"] if hdr is None else hdr
    buf = SYMS["_fl_buf"]
    pages = mem.get(hdr + 3, 0)
    if not pages:
        return
    dst = mem[hdr] | (mem[hdr + 1] << 8) | (mem[hdr + 2] << 16)
    for k in range(pages * 256):
        mem[dst + k] = mem[buf + k]
    mem[hdr + 3] = 0


SYMS = {"_fl_hdr": 0x8000, "_fl_buf": 0x8004, "_fl_scr": 0x8004 + 0x1F00,
        "_fl_copy": 0x3802}


class FarChunks(unittest.TestCase):
    """far_chunks must tile a segment exactly, in whole pages."""

    def tile(self, base, n, chunk):
        data = bytes((i * 7 + 3) & 0xFF for i in range(n))
        out = {}
        for dst, piece in mkxex.far_chunks(base, data, chunk):
            self.assertEqual(len(piece) % 256, 0,
                             "every chunk must be a whole number of pages")
            self.assertLessEqual(len(piece), chunk)
            for k, b in enumerate(piece):
                out[dst + k] = b
        return data, out

    def test_exact_multiple(self):
        data, out = self.tile(0x010000, 0x1F00 * 2, 0x1F00)
        self.assertEqual(bytes(out[0x010000 + i] for i in range(len(data))), data)
        self.assertEqual(len(out), len(data), "no byte written outside the segment")

    def test_tail_slides_back_instead_of_padding(self):
        n = 0x1F00 + 100
        data, out = self.tile(0x010000, n, 0x1F00)
        self.assertEqual(bytes(out[0x010000 + i] for i in range(n)), data)
        self.assertEqual(len(out), n,
                         "the tail chunk must stay inside the segment, not pad past it")

    def test_short_segment_is_padded_forward(self):
        # Nothing to slide back into, so this is the one case that overruns.
        data, out = self.tile(0x010000, 40, 0x1F00)
        self.assertEqual(bytes(out[0x010000 + i] for i in range(40)), data)
        self.assertEqual(len(out), 256)

    def test_every_page_boundary(self):
        for n in (256, 257, 511, 512, 0x1F00 - 1, 0x1F00 + 1, 0x1F00 * 3 - 5):
            with self.subTest(n=n):
                data, out = self.tile(0x010000, n, 0x1F00)
                self.assertEqual(bytes(out[0x010000 + i] for i in range(n)), data)


class Staging(unittest.TestCase):
    """A packed far image must reload byte-for-byte through the copier."""

    def pack_and_load(self, far):
        near = [(0x3000, b"\xea" * 16)]
        blob, _, _ = mkxex.build_xex(near + far, 0x3000, SYMS)
        return load_xex(blob)

    def test_round_trip(self):
        img = bytes((i * 31 + 17) & 0xFF for i in range(13767))
        mem, runad = self.pack_and_load([(0x010000, img)])
        self.assertEqual(runad, 0x3000)
        got = bytes(mem.get(0x010000 + i, 0xFF) for i in range(len(img)))
        self.assertEqual(got, img)

    def test_two_far_segments(self):
        a = bytes(range(256)) * 4
        b = bytes((0xFF - i) & 0xFF for i in range(3000))
        mem, _ = self.pack_and_load([(0x010000, a), (0x018000, b)])
        self.assertEqual(bytes(mem[0x010000 + i] for i in range(len(a))), a)
        self.assertEqual(bytes(mem[0x018000 + i] for i in range(len(b))), b)

    def test_length_field_is_consumed(self):
        # Otherwise the INITAD call DOS makes after the run-vector segment
        # would copy the last chunk a second time, over live memory.
        mem, _ = self.pack_and_load([(0x010000, b"\x5a" * 1024)])
        self.assertEqual(mem[SYMS["_fl_hdr"] + 3], 0)

    def test_refuses_a_straddling_segment(self):
        with self.assertRaises(SystemExit):
            mkxex.build_xex([(0xFF00, b"\x00" * 0x400)], 0x3000, SYMS)

    def test_refuses_far_code_without_the_loader(self):
        with self.assertRaises(SystemExit) as e:
            mkxex.build_xex([(0x010000, b"\x00" * 256)], 0x3000, {})
        self.assertIn("farload", str(e.exception))


class RealBinary(unittest.TestCase):
    """The build product itself, if it has been built."""

    def test_m3_far_image_reassembles(self):
        elf = os.path.join(ROOT, "build", "m3.elf")
        if not os.path.exists(elf):
            self.skipTest("build/m3.elf not built")
        segs, syms = mkxex.read_elf(elf)
        far = [(a, d) for a, d in segs if a > 0xFFFF]
        self.assertTrue(far, "m3 should have far code; is --code-model=large set?")
        blob, _, _ = mkxex.build_xex(segs, syms["_atari_entry"], syms)
        global SYMS
        keep, SYMS = SYMS, syms
        try:
            mem, runad = load_xex(blob)
        finally:
            SYMS = keep
        self.assertEqual(runad, syms["_atari_entry"])
        for base, data in far:
            got = bytes(mem.get(base + i, 0xFF) for i in range(len(data)))
            self.assertEqual(got, data, f"far image at ${base:06X} did not reassemble")


if __name__ == "__main__":
    unittest.main()
