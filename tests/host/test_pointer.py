#!/usr/bin/env python3
"""Host tests for the pointing-device layer.

The VDI conformance suite tests everything ABOVE the seam in
src/vdi/pointer.h -- it only ever sees an absolute position.  This tests what
is below it, which the emulator's bridge cannot drive directly: the quadrature
decoder, and the guarantee that its table has not drifted from the reference.
"""
import os
import re
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import vdiref  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, "tools", "ccbug"))
import check as ccbug  # noqa: E402  (the simulator driver)

POINTER_C = os.path.join(ROOT, "src", "vdi", "pointer.c")
GRAY = (0, 1, 3, 2)          # the quadrature phase order, one full cycle
CALYPSI = os.environ.get("CALYPSI", os.path.expanduser("~/dev/toolchains/calypsi-65816"))


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


class TestXem1(unittest.TestCase):
    """mouSTer's XEM1 mode: the decode in tools/vdiref.py follows the
    adapter's sample driver instruction by instruction; these pin what that
    arithmetic means, and the last one runs the target's C over every pair
    of readings in the compiler's simulator and compares."""

    def test_readings_are_64_to_191(self):
        self.assertEqual([p for p in range(256) if vdiref.xem1_valid(p)],
                         list(range(64, 192)))
        self.assertFalse(vdiref.xem1_valid(228))     # an empty port

    def test_no_change_is_no_movement(self):
        for p in range(64, 192):
            self.assertEqual(vdiref.decode_xem1(p, p), (0, p))

    def test_low_bit_is_noise_and_accumulates(self):
        """One count halves to nothing and does not move the reference, so
        a second count a frame later delivers the pixel."""
        d, ref = vdiref.decode_xem1(128, 129)
        self.assertEqual((d, ref), (0, 128))
        self.assertEqual(vdiref.decode_xem1(ref, 130), (1, 130))
        self.assertEqual(vdiref.decode_xem1(128, 127), (0, 128))
        self.assertEqual(vdiref.decode_xem1(128, 126), (-1, 126))

    def test_halves_toward_zero(self):
        self.assertEqual(vdiref.decode_xem1(128, 131)[0], 1)
        self.assertEqual(vdiref.decode_xem1(128, 125)[0], -1)
        self.assertEqual(vdiref.decode_xem1(128, 191)[0], 31)
        self.assertEqual(vdiref.decode_xem1(128, 64)[0], -32)

    def test_counter_wraps_at_seven_bits(self):
        """191 -> 64 is one step forward, not 127 back."""
        self.assertEqual(vdiref.decode_xem1(190, 65), (1, 65))    # +3
        self.assertEqual(vdiref.decode_xem1(65, 190), (-1, 190))  # -3
        self.assertEqual(vdiref.decode_xem1(191, 64), (0, 191))   # +1: noise

    def test_target_decode_matches_reference(self):
        cc = os.path.join(CALYPSI, "bin", "cc65816")
        if not os.path.exists(cc):
            self.skipTest("Calypsi not installed")
        ld, db = (os.path.join(CALYPSI, "bin", t) for t in ("ln65816", "db65816"))
        scm = os.path.join(CALYPSI, "example", "minimal", "linker.scm")
        out = os.path.join(ROOT, "build", "xem1")
        os.makedirs(out, exist_ok=True)
        objs = []
        for src in (POINTER_C, os.path.join(ROOT, "tests", "host", "xem1_sim.c")):
            obj = os.path.join(out, os.path.basename(src)[:-2] + ".o")
            subprocess.run([cc, "-g", "--code-model=large", "--data-model=small",
                            "-O2", "-I", os.path.join(ROOT, "src"), "-o", obj, src],
                           check=True)
            objs.append(obj)
        elf = os.path.join(out, "xem1.elf")
        subprocess.run([ld, "-g", scm] + objs + ["-o", elf, "clib-lc-sd.a",
                        "--rtattr", "exit=simplified"], check=True)
        names = ["r_hash", "r_valid", "r_wrap", "r_neg"]
        got = ccbug.simulate(db, elf, names)

        h = v = 0
        for p in range(256):
            v = (v * 3 + (1 if vdiref.xem1_valid(p) else 0)) & 0xFFFFFFFF
        for ref in range(64, 192):
            for now in range(64, 192):
                h = (h * 31 + (vdiref.decode_xem1(ref, now)[0] & 0xFF)) & 0xFFFFFFFF
        self.assertEqual(got["r_wrap"], 1)
        self.assertEqual(got["r_neg"], -1)
        self.assertEqual(got["r_valid"] & 0xFFFFFFFF, v)
        self.assertEqual(got["r_hash"] & 0xFFFFFFFF, h)


if __name__ == "__main__":
    unittest.main(verbosity=2)
