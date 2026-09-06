#!/usr/bin/env python3
"""The .FNT files gem4xe writes, against the rules its loader applies.

src/vdi/font.c refuses a font it cannot use rather than drawing half of
one, and the rules are narrow on purpose (the cell is fixed: font.h says
why).  These check that what tools/mkfnt.py writes passes them, that the
format round-trips, and -- where an EmuTOS checkout is at hand -- that
the character sets a translation would actually ship do too.
"""
import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkfnt                                        # noqa: E402

STRIP_C = os.path.join(ROOT, "src", "vdi", "font8x8.c")
EMUTOS = os.environ.get("EMUTOS", os.path.expanduser("~/dev/emutos"))
SETS = ("l2", "ru", "gr", "tr")                     # Latin-2, Cyrillic, Greek, Turkish

# src/vdi/font.c's font_ok(), in Python: what the target insists on.
FF_HORZ_OFF, FF_STDFORM, FF_MONOSPACE = 0x02, 0x04, 0x08


def loadable(h):
    return (h["form_width"] == 256 and h["form_height"] == 8
            and h["first_ade"] == 0 and h["last_ade"] == 255
            and h["top"] == 6 and h["max_cell_width"] == 8
            and (h["flags"] & (FF_STDFORM | FF_MONOSPACE)) == (FF_STDFORM | FF_MONOSPACE)
            and not (h["flags"] & FF_HORZ_OFF)
            and h["dat_table"] >= mkfnt.HDR_SIZE)


class Format(unittest.TestCase):
    def setUp(self):
        self.strip = mkfnt.parse_strip(STRIP_C)
        self.data = mkfnt.build(self.strip, name="gem4xe 8x8")
        self.path = os.path.join(ROOT, "build", "test-st.fnt")
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        with open(self.path, "wb") as f:
            f.write(self.data)

    def test_the_header_is_88_bytes(self):
        """Which is where the offset table starts, and the loader reads
        exactly that many before deciding anything."""
        _, _, h = mkfnt.read(self.path)
        self.assertEqual(mkfnt.HDR_SIZE, 88)
        self.assertEqual(h["off_table"], 88)

    def test_it_round_trips(self):
        strip, name, h = mkfnt.read(self.path)
        self.assertEqual(strip, self.strip)
        self.assertEqual(name, "gem4xe 8x8")
        self.assertEqual((h["form_width"], h["form_height"]), (256, 8))

    def test_the_target_would_take_it(self):
        _, _, h = mkfnt.read(self.path)
        self.assertTrue(loadable(h), h)

    def test_the_strip_is_the_font_the_target_links(self):
        """A font file of the system font must BE the system font: it is
        what test-m21 inverts to make a font that is visibly not."""
        self.assertEqual(len(self.strip), 256 * 8)
        strip, _, _ = mkfnt.read(self.path)
        self.assertEqual(strip, mkfnt.parse_strip(STRIP_C))

    def test_inverting_changes_every_glyph_and_nothing_else(self):
        inv = mkfnt.invert(self.strip)
        self.assertEqual(len(inv), len(self.strip))
        self.assertTrue(all(a ^ b == 0xFF for a, b in zip(inv, self.strip)))
        data = mkfnt.build(inv, name="inverted", font_id=101)
        p = os.path.join(ROOT, "build", "test-inv.fnt")
        with open(p, "wb") as f:
            f.write(data)
        strip, name, h = mkfnt.read(p)
        self.assertEqual(strip, inv)
        self.assertEqual(h["font_id"], 101)
        self.assertTrue(loadable(h))

    def test_a_font_of_another_size_is_refused(self):
        """Not by this tool -- by the target.  The check is that the
        rules can tell: a 16-row form fails loadable()."""
        data = bytearray(self.data)
        struct.pack_into(">H", data, 82, 16)        # form_height
        p = os.path.join(ROOT, "build", "test-16.fnt")
        with open(p, "wb") as f:
            f.write(bytes(data))
        _, _, h = mkfnt.read(p)
        self.assertFalse(loadable(h))


class EmuTOSSets(unittest.TestCase):
    """The four character sets a translation would ship.  Skipped without
    an EmuTOS checkout, since they are extracted from one."""

    def test_each_is_loadable_and_differs_from_the_st_font(self):
        st = mkfnt.parse_strip(STRIP_C)
        for tag in SETS:
            src = os.path.join(EMUTOS, "bios", f"fnt_{tag}_8x8.c")
            if not os.path.exists(src):
                self.skipTest(f"no {src}")
            strip, name, nums = mkfnt.parse_c(src)
            data = mkfnt.build(strip, name=name, font_id=nums.get("font_id", 1),
                               top=nums.get("top", 6))
            p = os.path.join(ROOT, "build", f"test-{tag}.fnt")
            with open(p, "wb") as f:
                f.write(data)
            got, gotname, h = mkfnt.read(p)
            self.assertTrue(loadable(h), (tag, h))
            self.assertEqual(got, strip, tag)
            self.assertEqual(gotname, name, tag)
            self.assertNotEqual(got, st, f"{tag} is the ST font")


if __name__ == "__main__":
    unittest.main()
