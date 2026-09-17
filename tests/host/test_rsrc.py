#!/usr/bin/env python3
"""The sizes a .RSC record has IN THE FILE, held against every place that
names them.

A resource is read as an overlay: the loader points a C struct at the
file's bytes and steps to the next record by adding a size.  The FIELD
offsets come out right because the file and the compiler lay out the same
words in the same order, but the STEP does not -- the compiler aligns a
32-bit field to four, so an ICONBLK measures 36 where the file gives it
34 and a BITBLK 16 where the file gives it 14.  OBJECT (24) and TEDINFO
(28) happen to come out even, which is why `sizeof` as a stride read as
correct for years: it is correct for the two tables every resource has
and wrong for the two an icon resource has.

Nothing catches that at run time.  The first record is always right, so a
resource with one icon behaves, and the tree's own resources have either
one or none of each.  A real ST resource with several icons reads the
second one two bytes late, and what comes back is a plausible-looking
ICONBLK made of the tail of one record and the head of the next.

So the numbers are pinned here three ways: against the ST's format, which
is what the file is; against tools/rsc.py, which writes these files;
and against the tables of the resources this tree actually builds, which
is the only one of the three that would notice if the writer and the
format ever drifted together.
"""
import glob
import os
import re
import struct
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
RSRC_C = os.path.join(ROOT, "src", "aes", "rsrc.c")
RSC_PY = os.path.join(ROOT, "tools", "rsc.py")

# The ST's, from the .RSC format itself: three LONGs and eleven WORDs make
# an ICONBLK, a LONG and five WORDs make a BITBLK.
FORMAT = {"OBJECT": 24, "TEDINFO": 28, "ICONBLK": 34, "BITBLK": 14}

# rsh_* words of the header, in the order the file has them.
HDR = ("vrsn object tedinfo iconblk bitblk frstr string imdata frimg "
       "trindex nobs ntree nted nib nbb nstring nimages rssize").split()


def header(path):
    with open(path, "rb") as f:
        return dict(zip(HDR, struct.unpack(">18H", f.read(36))))


class TestTheFileRecordSizes(unittest.TestCase):

    def test_the_loader_names_the_format_s_sizes(self):
        src = open(RSRC_C).read()
        got = {m[0]: int(m[1]) for m in
               re.findall(r"^#define RSZ_(\w+)\s+(\d+)", src, re.M)}
        self.assertEqual(got, FORMAT,
                         "src/aes/rsrc.c's RSZ_* are not the .RSC format's")

    def test_the_writer_names_the_same_sizes(self):
        src = open(RSC_PY).read()
        m = re.search(r"^OBJ_SIZE, TED_SIZE, BITBLK_SIZE, ICONBLK_SIZE = "
                      r"(\d+), (\d+), (\d+), (\d+)", src, re.M)
        self.assertIsNotNone(m, "tools/rsc.py no longer names its record sizes")
        got = dict(zip(("OBJECT", "TEDINFO", "BITBLK", "ICONBLK"),
                       (int(g) for g in m.groups())))
        self.assertEqual(got, FORMAT,
                         "the writer and the format disagree")

    def test_no_sizeof_is_used_as_a_stride(self):
        """The bug this file exists for, in the shape it had: a sizeof
        standing in for a file record's size.  rs_cicons is the one place
        left that does it, and says why."""
        src = re.sub(r"/\*.*?\*/", "", open(RSRC_C).read(), flags=re.S)
        body = "\n".join(ln for ln in src.split("rs_cicons")[0].splitlines()
                         if "rsz_" not in ln)   # the size assertions, not strides
        self.assertIn("RSZ_ICONBLK", body, "the split found no code to scan")
        for name in FORMAT:
            self.assertNotIn(f"sizeof({name})", body,
                             f"sizeof({name}) is a stride again; the file's "
                             f"record is {FORMAT[name]} bytes and the "
                             f"compiler's struct need not be")


class TestTheResourcesThisTreeBuilds(unittest.TestCase):
    """The tables of the built .RSC files, measured rather than declared.
    A table's records are contiguous, so the distance to whichever table
    starts next, divided by the count, IS the record size."""

    TABLES = (("object", "nobs", "OBJECT"), ("tedinfo", "nted", "TEDINFO"),
              ("iconblk", "nib", "ICONBLK"), ("bitblk", "nbb", "BITBLK"))

    def test_every_table_steps_by_the_format_s_size(self):
        built = sorted(glob.glob(os.path.join(ROOT, "build", "*.rsc")))
        if not built:
            raise unittest.SkipTest("no .rsc built yet (make)")
        seen = 0
        for path in built:
            h = header(path)
            starts = sorted({h[k] for k in
                             ("object", "tedinfo", "iconblk", "bitblk",
                              "frstr", "string", "imdata", "frimg",
                              "trindex")} | {h["rssize"]})
            for off, cnt, kind in self.TABLES:
                n = h[cnt]
                if n < 2:           # one record cannot show a stride
                    continue
                start = h[off]
                after = min(s for s in starts if s > start)
                self.assertEqual(
                    (after - start) // n, FORMAT[kind],
                    f"{os.path.basename(path)}: {n} {kind} records span "
                    f"{after - start} bytes, so {(after - start) / n} each, "
                    f"not the format's {FORMAT[kind]}")
                seen += 1
        self.assertTrue(seen, "no table in any built resource had two "
                              "records, so nothing was actually measured")


if __name__ == "__main__":
    unittest.main()
