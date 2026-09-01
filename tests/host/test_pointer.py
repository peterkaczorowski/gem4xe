#!/usr/bin/env python3
"""Host tests for the pointing-device layer.

The VDI conformance suite tests everything ABOVE the seam in
src/vdi/pointer.h -- it only ever sees an absolute position.  This tests what
is below it, which the emulator's bridge cannot drive directly: the quadrature
decoder, and the guarantee that its table has not drifted from the reference.
"""
import os
import re
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import vdiref  # noqa: E402

POINTER_C = os.path.join(ROOT, "src", "vdi", "pointer.c")
GRAY = (0, 1, 3, 2)          # the quadrature phase order, one full cycle


def c_table():
    with open(POINTER_C) as f:
        src = f.read()
    m = re.search(r"static const signed char qdec\[16\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise AssertionError("qdec[] not found in pointer.c")
    vals = [int(v) for v in re.findall(r"[-+]?\d+", m.group(1))]
    assert len(vals) == 16, f"qdec has {len(vals)} entries"
    return vals


class TestQuadrature(unittest.TestCase):
    def test_c_and_reference_tables_agree(self):
        """The driver's table and the reference's must be identical."""
        c = c_table()
        ref = [vdiref.decode_quad(p, n) for p in range(4) for n in range(4)]
        self.assertEqual(c, ref)

    def test_full_cycle_forward_is_four_counts(self):
        total = sum(vdiref.decode_quad(GRAY[i], GRAY[(i + 1) % 4])
                    for i in range(4))
        self.assertEqual(total, 4)

    def test_reverse_cycle_is_minus_four(self):
        total = sum(vdiref.decode_quad(GRAY[(i + 1) % 4], GRAY[i])
                    for i in range(4))
        self.assertEqual(total, -4)

    def test_motion_then_reverse_returns_to_origin(self):
        """Any walk and its reverse must net to zero -- no drift."""
        walk = [0, 1, 3, 2, 0, 2, 3, 1, 0, 1, 1, 3, 3, 2]
        fwd = sum(vdiref.decode_quad(walk[i], walk[i + 1])
                  for i in range(len(walk) - 1))
        rev = sum(vdiref.decode_quad(walk[i + 1], walk[i])
                  for i in range(len(walk) - 1))
        self.assertEqual(fwd + rev, 0)

    def test_no_movement_reads_zero(self):
        for p in range(4):
            self.assertEqual(vdiref.decode_quad(p, p), 0)

    def test_two_step_jump_drops_motion(self):
        """A missed sample is ambiguous: report 0 rather than guess a
        direction.  Inventing motion the wrong way is worse than losing it."""
        for p, n in ((0, 3), (3, 0), (1, 2), (2, 1)):
            self.assertEqual(vdiref.decode_quad(p, n), 0)


class TestTabletScaling(unittest.TestCase):
    """POKEY's pot counter tops out near 228 against a 640x240 screen."""
    POT_MAX = 228

    def scale(self, pot, span):
        return (pot * (span - 1)) // self.POT_MAX

    def test_endpoints_reach_the_corners(self):
        self.assertEqual(self.scale(0, 640), 0)
        self.assertEqual(self.scale(self.POT_MAX, 640), 639)
        self.assertEqual(self.scale(0, 240), 0)
        self.assertEqual(self.scale(self.POT_MAX, 240), 239)

    def test_horizontal_granularity_is_coarse(self):
        """~2.8 screen pixels per step across: fine for hitting a menu item,
        too coarse to draw with.  Recorded so it is a known property rather
        than a surprise."""
        step = self.scale(1, 640) - self.scale(0, 640)
        self.assertEqual(step, 2)
        self.assertGreaterEqual(639 / self.POT_MAX, 2.7)

    def test_vertical_is_about_one_to_one(self):
        self.assertLessEqual(abs(239 / self.POT_MAX - 1.0), 0.1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
