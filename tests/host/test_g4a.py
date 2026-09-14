#!/usr/bin/env python3
"""Host tests for the loadable-application format (tools/mkg4a.py).

A .G4A is a program linked at placeholder addresses plus the lists of bytes
that have to change when the loader puts it somewhere else.  Nothing in a
compile or a link checks those lists: a missing fixup is a program that
runs at the placeholders and nowhere else, and a WRONG one is worse,
because the loader will happily apply it.

So the check here is the loader's own arithmetic, done in Python: take the
.G4A, add one to every byte its far bank list names, and compare the result
with the FAR-SHIFTED link -- which is what ln65816 itself produced for the
same objects one bank higher.  If they agree byte for byte, the list is
neither short nor wrong.  It needs no emulator, and it is a stronger claim
than "the program ran", because it covers every byte rather than the paths
one run happened to take.

Both formats are covered, and that is the point of m31_huge:

  format 1 writes a far fixup offset as a u16, which caps the far image at
  a bank.  Every program in this tree but one fits, and they stay v1, so
  their bytes and the disk images that carry them do not change.

  format 2 writes it as three bytes.  m31_huge (src/m31_huge.c) exists to
  have an image over 64 KB on purpose -- 105 KB of far constants across two
  code banks -- so the format is not proved only by GACS, which lives in
  another repository and is not always present.
"""
import os
import struct
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
CALYPSI = os.environ.get("CALYPSI",
                         os.path.expanduser("~/dev/toolchains/calypsi-65816"))


def build(target):
    subprocess.run(["make", target], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)


def unpack(path):
    """The header and the four fixup lists, read the way app_load reads them."""
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:3] != b"G4A":
        raise AssertionError(f"{path}: not a G4A")
    ver = d[3]
    nb, near_size, far_off, far_size, far_bank, far_banks = \
        struct.unpack("<HHHIBB", d[4:16])
    n_nhi, n_nbank, n_fhi, n_fbank = struct.unpack("<HHHH", d[20:28])
    fw = 3 if ver == 2 else 2
    HDR = 32
    near = bytearray(d[HDR:HDR + near_size])
    far = bytearray(d[HDR + near_size:HDR + near_size + far_size])
    p = HDR + near_size + far_size

    def take(n, w):
        nonlocal p
        out = [int.from_bytes(d[p + i * w:p + i * w + w], "little")
               for i in range(n)]
        p += n * w
        return out

    lists = (take(n_nhi, 2), take(n_nbank, 2), take(n_fhi, fw), take(n_fbank, fw))
    if p != len(d):
        raise AssertionError(f"{path}: {len(d) - p} bytes past the fixup lists")
    return dict(ver=ver, near_base=nb, near=near, far=far, far_off=far_off,
                far_bank=far_bank, far_banks=far_banks, lists=lists, size=len(d))


def elf_bytes(path):
    from mkxex import read_elf
    segs, syms = read_elf(path)
    return segs, syms


class G4AFormat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(os.path.join(CALYPSI, "bin", "ln65816")):
            raise unittest.SkipTest("Calypsi not installed")

    def relocate_and_compare(self, name):
        """Apply the bank fixups and check against the far-shifted link."""
        build(f"build/{name}.g4a")
        g = unpack(os.path.join(ROOT, "build", f"{name}.g4a"))
        far = bytearray(g["far"])
        _n_hi, _n_bank, f_hi, f_bank = g["lists"]
        for o in f_bank:                      # a bank moves it by exactly 1
            far[o] = (far[o] + 1) & 0xFF

        n = len(far)

        def laid_out(elf, base):
            """(bytes, which-of-them-the-linker-actually-wrote), over the
            image's own extent.  Not every byte of it is written: a code
            bank is two memories either side of the $D5 page, and a
            fragment the tail of one cannot hold leaves the rest of it
            empty.  Those holes are in the file as zeroes and are not
            the linker's output, so they are compared to nothing."""
            want, seen = bytearray(n), bytearray(n)
            for a, data in elf_bytes(elf)[0]:
                for i, b in enumerate(data):
                    off = a + i - base
                    if 0 <= off < n:
                        want[off] = b
                        seen[off] = 1
            return want, seen

        fbase = ((g["far_bank"] + 1) << 16) | g["far_off"]
        bbase = (g["far_bank"] << 16) | g["far_off"]
        want, seen = laid_out(os.path.join(ROOT, "build", f"{name}-far.elf"), fbase)
        _, seen0 = laid_out(os.path.join(ROOT, "build", f"{name}.elf"), bbase)

        # The two links must lay the same bytes in the same places -- if the
        # shifted one moved something, the fixups would be describing a
        # different program and the comparison below would mean nothing.
        self.assertEqual(bytes(seen), bytes(seen0),
                         f"{name}: the base and far-shifted links populate "
                         f"different parts of the image")
        bad = [i for i in range(n) if seen[i] and want[i] != far[i]]
        self.assertEqual(bad[:8], [],
                         f"{name}: {len(bad)} byte(s) differ from the "
                         f"far-shifted link after applying {len(f_bank)} "
                         f"bank fixups")
        self.assertGreater(sum(seen), 0, f"{name}: nothing to compare")
        return g, f_hi, f_bank

    def test_v1_relocates(self):
        """m29_big: a large-DATA program, one bank of image, format 1."""
        g, _, _ = self.relocate_and_compare("m29_big")
        self.assertEqual(g["ver"], 1, "an image inside one bank stays v1")
        self.assertLessEqual(len(g["far"]), 0x10000)

    def test_v2_relocates(self):
        """m31_huge: an image OVER a bank, which only format 2 can name."""
        g, f_hi, f_bank = self.relocate_and_compare("m31_huge")
        self.assertEqual(g["ver"], 2, "an image over a bank must be v2")
        self.assertGreater(len(g["far"]), 0x10000,
                           "m31_huge is supposed to be bigger than a bank")
        self.assertGreater(g["far_banks"], 1)
        # The whole point: offsets a u16 could not have held.
        self.assertTrue(any(o >= 0x10000 for o in f_hi + f_bank),
                        "no fixup past 64K -- this program is not testing "
                        "what it exists to test")

    def test_every_shipped_app_stays_v1(self):
        """The format change costs the existing programs nothing.

        They are on floppies with sectors to spare rather than to waste
        (the DOS 2 product disk holds GEM, the desktop and its resource in
        707 sectors of 253 bytes), and a far fixup list that grew by half
        for no reason would come out of that.
        """
        for name in ("desktop", "calc", "clock", "clockacc", "m11_app",
                     "m28_acc", "m29_big"):
            with self.subTest(app=name):
                build(f"build/{name}.g4a")
                g = unpack(os.path.join(ROOT, "build", f"{name}.g4a"))
                self.assertEqual(g["ver"], 1)

    def test_fixup_offsets_are_inside_the_image(self):
        """app_load bounds-checks these; a packer that emitted one outside
        would turn a load into APP_E_FIXUP rather than a running program."""
        for name in ("m29_big", "m31_huge", "desktop"):
            with self.subTest(app=name):
                build(f"build/{name}.g4a")
                g = unpack(os.path.join(ROOT, "build", f"{name}.g4a"))
                n_hi, n_bank, f_hi, f_bank = g["lists"]
                for o in n_hi + n_bank:
                    self.assertLess(o, len(g["near"]))
                for o in f_hi + f_bank:
                    self.assertLess(o, len(g["far"]))

    def test_no_byte_is_in_two_lists(self):
        """A byte that is both a page fixup and a bank fixup is address
        arithmetic the loader cannot undo; mkg4a refuses to emit one, and
        this is that refusal checked from the outside."""
        for name in ("m29_big", "m31_huge", "desktop"):
            with self.subTest(app=name):
                build(f"build/{name}.g4a")
                n_hi, n_bank, f_hi, f_bank = unpack(
                    os.path.join(ROOT, "build", f"{name}.g4a"))["lists"]
                self.assertEqual(set(n_hi) & set(n_bank), set())
                self.assertEqual(set(f_hi) & set(f_bank), set())


if __name__ == "__main__":
    unittest.main()
