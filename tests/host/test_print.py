"""The page, and what leaves the machine carrying it.

src/vdi/dev_print.c rasterises 640 x 800 dots and src/vdi/emit.c writes
them out as PCL 5 or PostScript.  Neither is inspectable by eye and
neither FAILS when it is wrong: a bad page prints, and comes out blank,
or shifted by a row, or in negative, and the thing that tells you is a
laser printer in another room.  So both are pinned here.

Three claims:

  * the numbers in src/vdi/print.h, src/sys/config.h, tools/emitref.py
    and tools/devref.py are ONE set of numbers, not four copies that
    drift;
  * PCL 5 decodes back to the page it came from, by a decoder strict
    enough to have caught the bug;
  * the PostScript means what it says -- handed to Ghostscript at 100
    dpi it renders to the page, bit for bit, which is the only honest
    check of a program written in a language with an interpreter.
"""
import os
import re
import shutil
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import devref                                            # noqa: E402
import emitref as E                                      # noqa: E402

GS = shutil.which("gs")


def defines(path):
    """The integer #defines of a C header, in order, each evaluated
    against the ones before it -- because print.h writes PR_STRIDE as
    (PR_W / 8) and PR_BYTES as a cast expression, and a test that could
    only read literals would be a test of how the header is punctuated."""
    with open(os.path.join(ROOT, path)) as f:
        src = f.read()
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    out = {}
    for name, body in re.findall(r"^#\s*define\s+(\w+)\s+([^\n]+)$",
                                 src, re.M):
        body = re.sub(r"\((?:u?int\d+_t|unsigned|signed|short|long|char|"
                      r"WORD|UWORD)\s*\)", "", body).strip()
        if not re.fullmatch(r"[\w\s()+\-*/|&<>]+", body):
            continue
        try:
            out[name] = int(eval(body, {"__builtins__": {}}, dict(out)))
        except Exception:
            pass
    return out


class Constants(unittest.TestCase):
    def setUp(self):
        self.h = defines("src/vdi/print.h")
        self.c = defines("src/sys/config.h")

    def test_geometry_is_one_set_of_numbers(self):
        for name, want in (("PR_W", E.PR_W), ("PR_H", E.PR_H),
                           ("PR_STRIDE", E.PR_STRIDE),
                           ("PR_BYTES", E.PR_BYTES), ("PR_DPI", E.PR_DPI)):
            self.assertEqual(self.h[name], want, name)

    def test_the_model_device_has_the_same_page(self):
        p = devref.Printer()
        self.assertEqual((p.w, p.h, p.stride),
                         (E.PR_W, E.PR_H, E.PR_STRIDE))
        self.assertEqual(len(p.a.mem), E.PR_BYTES)

    def test_the_page_is_one_far_bank(self):
        """The whole reason the page is this size and no larger -- a
        second bank would make row_get()'s offset 24 bits and every
        access in dev_print.c a long one (docs/printing.md)."""
        self.assertEqual(self.h["PR_BYTES"], self.h["PR_STRIDE"] * self.h["PR_H"])
        self.assertLessEqual(self.h["PR_BYTES"], 0x10000)

    def test_the_printer_kinds_agree_with_the_config_file(self):
        """print.h names them PR_*, config.h names them CFG_PRINT_*, and
        src/gem.c assigns one to the other with no translation."""
        for a, b in (("PR_NONE", "CFG_PRINT_NONE"), ("PR_PCL", "CFG_PRINT_PCL"),
                     ("PR_PS", "CFG_PRINT_PS")):
            self.assertEqual(self.h[a], self.c[b], a + " vs " + b)

    def test_the_clock_choices_agree_with_the_config_file(self):
        """The same arrangement for the clock: clock.h names them
        CLOCK_HOW_*, config.h names them CFG_CLOCK_*, and src/gem.c
        assigns one to the other with no translation."""
        k = defines("src/sys/clock.h")
        for a, b in (("CLOCK_HOW_AUTO", "CFG_CLOCK_AUTO"),
                     ("CLOCK_HOW_DOS", "CFG_CLOCK_DOS"),
                     ("CLOCK_HOW_NONE", "CFG_CLOCK_NONE")):
            self.assertEqual(k[a], self.c[b], a + " vs " + b)

    def test_declared_at_100_dpi_is_64_by_80_tenths_of_an_inch(self):
        """work_out[3] and [4] are microns per pixel and the VDI derives
        them from these; 100 dpi is 254 microns exactly, which is why the
        number was chosen over 96 or 72."""
        self.assertEqual(25400 // self.h["PR_DPI"], 254)


def pages():
    """A blank page, a solid one, one dot, and something with structure
    in it -- between them they exercise the blank-row skip, the row
    writer, and both ends of a byte."""
    blank = E.blank()

    solid = E.blank()
    for i in range(E.PR_BYTES):
        solid[i] = 0xFF

    dot = E.blank()
    dot[E.PR_STRIDE * 399 + 40] = 0x10

    art = E.blank()
    for x in range(E.PR_STRIDE):                  # a rule across the top
        art[x] = 0xFF
    for y in range(E.PR_H):                       # and one down each edge
        art[y * E.PR_STRIDE] |= 0x80
        art[y * E.PR_STRIDE + E.PR_STRIDE - 1] |= 0x01
    for y in range(100, 140):                     # a solid block
        for x in range(10, 30):
            art[y * E.PR_STRIDE + x] = 0xA5

    return [("blank", blank), ("solid", solid), ("dot", dot), ("art", art)]


class Pcl(unittest.TestCase):
    def test_round_trip(self):
        for name, page in pages():
            with self.subTest(page=name):
                self.assertEqual(E.from_pcl(E.pcl(page)), bytes(page))

    def test_a_blank_page_costs_nothing(self):
        """Every row skipped, so the file is a header and a trailer.
        This is the whole point of the skip: a page of GEM is mostly
        paper, and 64,000 bytes down a serial line is a minute."""
        self.assertLess(len(E.pcl(E.blank())), 32)

    def test_a_solid_page_carries_every_byte(self):
        n = len(E.pcl(dict(pages())["solid"]))
        self.assertGreater(n, E.PR_BYTES)
        self.assertLess(n, E.PR_BYTES + 16 * E.PR_H)

    def test_the_decoder_refuses_what_it_should(self):
        good = E.pcl(dict(pages())["art"])
        for bad in (good[:-1],                       # truncated trailer
                    good + b"\033*b80W",             # trailing rubbish
                    b"\033*b80W" + good,             # no header
                    good.replace(b"\033*b80W", b"\033*b79W", 1)):
            with self.assertRaises(ValueError):
                E.from_pcl(bad)


@unittest.skipUnless(GS, "ghostscript not installed")
class PostScript(unittest.TestCase):
    """The oracle.  gs is told to shift the origin by the BoundingBox and
    render 640x800 at 100 dpi, which puts exactly one device pixel on
    each of the page's dots; pbmraw writes 1 for black, and a 1 in the
    page means ink, so the bits come back in the same polarity they went
    in -- through an `image` operator that inverted them and flipped y."""

    def render(self, ps):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "page.ps")
            out = os.path.join(d, "page.pbm")
            with open(src, "wb") as f:
                f.write(ps)
            r = subprocess.run(
                [GS, "-q", "-dNOPAUSE", "-dBATCH", "-dSAFER",
                 "-sDEVICE=pbmraw", "-r100", "-g%dx%d" % (E.PR_W, E.PR_H),
                 "-sOutputFile=" + out, "-c", "-76 -108 translate",
                 "-f", src], capture_output=True)
            self.assertEqual(r.returncode, 0, r.stderr.decode("latin-1"))
            with open(out, "rb") as f:
                return self.pbm(f.read())

    def pbm(self, d):
        i, fields = 0, []
        while len(fields) < 3:
            while d[i:i + 1].isspace():
                i += 1
            if d[i:i + 1] == b"#":
                while d[i:i + 1] not in (b"\n", b""):
                    i += 1
                continue
            j = i
            while not d[j:j + 1].isspace():
                j += 1
            fields.append(d[i:j])
            i = j
        self.assertEqual(fields, [b"P4", b"%d" % E.PR_W, b"%d" % E.PR_H])
        return d[i + 1:]

    def test_ghostscript_renders_the_page_back(self):
        for name, page in pages():
            with self.subTest(page=name):
                self.assertEqual(self.render(E.ps(page)), bytes(page))

    def test_it_is_conforming_dsc(self):
        ps = E.ps(E.blank())
        self.assertTrue(ps.startswith(b"%!PS-Adobe-3.0\n"))
        self.assertIn(b"%%BoundingBox: 76 108 537 684\n", ps)
        self.assertTrue(ps.endswith(b"%%EOF\n"))
