"""The checked-in desktop icons are the donor's, bit for bit.

tools/deskicons.py is generated from EmuTOS desk/icons.c by tools/iconconv.py
and committed, so that the resource the desktop loads (tools/deskrsc.py) and
the host reference that draws it read the same bits without a build
dependency on the donor tree.  A checked-in copy can drift, and the desktop
gate would never notice: the target and the model both draw from the same
file.  This test is what notices.  The donor half needs the EmuTOS tree
($EMUTOS or ~/dev/emutos) and is skipped where there is none.
"""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import deskicons  # noqa: E402
import deskrsc    # noqa: E402
import iconconv   # noqa: E402
from rsc import R_ICONBLK, ICONBLK_SIZE  # noqa: E402

EMUTOS = os.environ.get("EMUTOS", os.path.expanduser("~/dev/emutos"))
DONOR = os.path.join(EMUTOS, "desk", "icons.c")
ROWS, BYTES = 32, 4                     # 32 x 32, one bit a pixel


class DeskIcons(unittest.TestCase):
    def test_shapes(self):
        """Eight icons in deskapp.h's order, each 32x32 with its two rectangles."""
        self.assertEqual(len(deskicons.ICONS), len(iconconv.NAMES))
        self.assertEqual((deskicons.IG_HARD, deskicons.IG_FLOPPY, deskicons.IG_TRASH,
                          deskicons.IG_DOCUMENT), (0, 1, 3, 7))
        for k, icon in enumerate(deskicons.ICONS):
            (mask, data, char, xchar, ychar, xicon, yicon, wicon, hicon,
             xtext, ytext, wtext, htext) = icon
            with self.subTest(icon=iconconv.NAMES[k]):
                self.assertEqual((len(mask), len(data)), (ROWS * BYTES,) * 2)
                self.assertEqual((wicon, hicon), (32, 32))
                # the image lies within the mask: every set image bit is masked
                self.assertEqual(bytes(d & ~m & 0xFF for d, m in zip(data, mask)),
                                 bytes(ROWS * BYTES))
                # the label sits under the icon, a character cell high
                self.assertEqual(ytext, yicon + hicon)
                self.assertEqual(htext, 8)
                self.assertTrue(0 <= xchar < wicon and 0 <= ychar < hicon)
        # the drives carry a letter, drawn inside the icon; the trash none
        self.assertEqual([i[2] for i in deskicons.ICONS[:4]],
                         [ord("C"), ord("A"), 0, 0])

    @unittest.skipUnless(os.path.exists(DONOR), "no EmuTOS tree to compare against")
    def test_matches_donor(self):
        donor = iconconv.parse(DONOR)
        self.assertEqual([d[0] for d in donor], list(iconconv.NAMES))
        for (name, mask, data, ch, nums), icon in zip(donor, deskicons.ICONS):
            with self.subTest(icon=name):
                words = lambda b: [b[i] << 8 | b[i + 1] for i in range(0, len(b), 2)]
                self.assertEqual(words(icon[0]), mask)
                self.assertEqual(words(icon[1]), data)
                self.assertEqual(icon[2], ch)
                self.assertEqual(list(icon[3:]), nums)

    def test_resource_carries_them(self):
        """The desktop's resource holds the table's icons unchanged, the
        letter left for the desktop to fill in (desk_icon), at the ICONBLK
        indices the desktop names."""
        r = deskrsc.build()
        base = 0x5500
        image, trees, mem = r.expect(base, 8, 8, 640)
        for ib, ig in deskrsc.IB_TABLE:
            icon = deskicons.ICONS[ig]
            addr = r.addr(R_ICONBLK, ib, base)
            blk = mem[addr]
            with self.subTest(iconblk=ib):
                self.assertEqual(addr, r.addr(R_ICONBLK, 0, base) + ib * ICONBLK_SIZE)
                self.assertEqual(mem[blk.pmask], icon[0])
                self.assertEqual(mem[blk.pdata], icon[1])
                self.assertEqual(blk.char, 0x1000)
                self.assertEqual((blk.xchar, blk.ychar), icon[3:5])
                self.assertEqual((blk.icon.x, blk.icon.y, blk.icon.w, blk.icon.h),
                                 icon[5:9])
                self.assertEqual((blk.text.x, blk.text.y, blk.text.w, blk.text.h),
                                 icon[9:13])
                # the ICONBLK in the image is the one the model holds
                off = addr - base
                self.assertEqual(bytes(image[off:off + ICONBLK_SIZE]), blk.pack())


if __name__ == "__main__":
    unittest.main()
