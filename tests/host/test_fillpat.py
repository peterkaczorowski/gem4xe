"""The checked-in fill patterns are the donor's, word for word.

src/vdi/fillpat.c is generated from EmuTOS by tools/patconv.py and committed,
so that the target and the host reference read the same words without a build
dependency on the donor tree.  A checked-in copy can drift, though, and the
conformance suite would never notice: it compares the target with the
reference, and both read the same file.  This test is what notices.  It needs
the donor tree ($EMUTOS or ~/dev/emutos) and is skipped where there is none.
"""
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import patconv  # noqa: E402
import vdiref   # noqa: E402

EMUTOS = os.environ.get("EMUTOS", os.path.expanduser("~/dev/emutos"))
DONOR = os.path.join(EMUTOS, "vdi", "vdi_fill.c")


class FillPatterns(unittest.TestCase):
    def test_shapes(self):
        """The reference's view of the checked-in file has the donor's shape."""
        self.assertEqual(len(vdiref.FILL_DITHER), 8 * 4)
        self.assertEqual(len(vdiref.FILL_OEM), 16 * 8)
        self.assertEqual(len(vdiref.FILL_HATCH0), 6 * 8)
        self.assertEqual(len(vdiref.FILL_HATCH1), 6 * 16)
        # the last dither is solid, the first is the sparsest
        self.assertEqual(vdiref.FILL_DITHER[28:32], [0xFFFF] * 4)
        self.assertEqual(vdiref.FILL_DITHER[0:4], [0x0000, 0x4444, 0x0000, 0x1111])

    @unittest.skipUnless(os.path.exists(DONOR), "no EmuTOS tree to compare against")
    def test_matches_donor(self):
        donor = patconv.parse(DONOR)
        self.assertEqual(vdiref.FILL_DITHER, donor["fill_dither"])
        self.assertEqual(vdiref.FILL_OEM, donor["fill_oem"])
        self.assertEqual(vdiref.FILL_HATCH0, donor["fill_hatch0"])
        self.assertEqual(vdiref.FILL_HATCH1, donor["fill_hatch1"])

    def test_pattern_lookup(self):
        """st_fl_ptr: interior + index -> (rows, mask), as the donor resolves it."""
        v = vdiref.VDI()
        self.assertEqual(v._fill_pattern(), ([0xFFFF], 0))          # AES work_in: solid
        v.call(vdiref.VSF_INTERIOR, (), (0,))
        self.assertEqual(v._fill_pattern(), ([0x0000], 0))
        v.call(vdiref.VSF_INTERIOR, (), (1,))
        self.assertEqual(v._fill_pattern(), ([0xFFFF], 0))
        v.call(vdiref.VSF_INTERIOR, (), (2,))
        v.call(vdiref.VSF_STYLE, (), (4,))
        self.assertEqual(v._fill_pattern(), ([0xAAAA, 0x5555, 0xAAAA, 0x5555], 3))
        v.call(vdiref.VSF_STYLE, (), (9,))                          # first OEM: brick
        rows, msk = v._fill_pattern()
        self.assertEqual((rows[0], rows[1], msk), (0xFFFF, 0x8080, 7))
        v.call(vdiref.VSF_STYLE, (), (25,))                         # out of range -> 1
        self.assertEqual(v.intout[0], 1)
        v.call(vdiref.VSF_INTERIOR, (), (3,))
        v.call(vdiref.VSF_STYLE, (), (7,))                          # first fine hatch
        rows, msk = v._fill_pattern()
        self.assertEqual((rows[0], len(rows), msk), (0x0001, 16, 15))
        v.call(vdiref.VSF_STYLE, (), (13,))                         # out of range -> 1
        self.assertEqual(v.intout[0], 1)
        v.call(vdiref.VSF_INTERIOR, (), (4,))
        v.call(vdiref.VSF_UDPAT, (), tuple(range(16)))
        self.assertEqual(v._fill_pattern(), (list(range(16)), 15))

    def test_work_in(self):
        """v_opnwk applies work_in -- validated -- and defaults to the AES's."""
        v = vdiref.VDI()
        v.call(vdiref.V_OPNWK, (), (1, 3, 2, 1, 1, 1, 5, 2, 9, 4, 2))
        self.assertEqual((v.line_index, v.line_color, v.text_color), (3, 2, 5))
        self.assertEqual((v.fill_style, v.fill_index, v.fill_color),
                         (vdiref.FIS_PATTERN, 8, 4))
        v.call(vdiref.V_OPNWK, (), (1, 9, 99, 1, 1, 1, -1, 7, 0, 16, 2))   # all invalid
        self.assertEqual((v.line_index, v.line_color, v.text_color), (1, 1, 1))
        self.assertEqual((v.fill_style, v.fill_index, v.fill_color),
                         (vdiref.FIS_HOLLOW, 0, 1))
        v.call(vdiref.V_OPNWK, (), (1, 1, 1, 1, 1, 1, 1, 3, 13, 1, 2))     # hatch 13 -> 1
        self.assertEqual((v.fill_style, v.fill_index), (vdiref.FIS_HATCH, 0))
        v.call(vdiref.V_OPNWK, (), ())                                    # no work_in
        self.assertEqual((v.fill_style, v.fill_index), (vdiref.FIS_SOLID, 0))


if __name__ == "__main__":
    unittest.main()
