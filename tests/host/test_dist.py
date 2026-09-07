#!/usr/bin/env python3
"""Host tests for the distribution (tools/mkdist.py).

The artefact is what a tester is handed, and its README is the only
thing standing between them and a machine that boots as a 6502 and
refuses.  Two halves of that page are generated -- what the desktop's
menu offers and what it offers disabled, and what is really on each
disk image -- so what is checked here is that the generation is honest:
every menu item lands in exactly one of the two lists, the files the
page names are the files on the images, and nothing is left unfilled.

The disks themselves are checked by `make test-boot`, which boots them.
"""
import os
import re
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import atr                                  # noqa: E402
import deskrsc                              # noqa: E402
import mkdist                               # noqa: E402

BUILD = os.path.join(ROOT, "build")


def built(name):
    return os.path.isfile(os.path.join(BUILD, name))


class TestDistribution(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for src, _ in mkdist.SYSTEM:
            if not built(src):
                raise unittest.SkipTest(f"build/{src} is not built")
        cls.dir = tempfile.mkdtemp(prefix="gem4xe-dist-")
        cls.out = os.path.join(cls.dir, "gem4xe-test")
        cls.made, cls.missing = mkdist.build(cls.out)
        with open(os.path.join(cls.out, "README.md")) as f:
            cls.page = f.read()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def test_the_page_has_nothing_left_unfilled(self):
        """A template placeholder that survives into the artefact is a
        hole in the page nobody would notice until a tester read it."""
        self.assertNotRegex(self.page, r"\{[a-z_]+\}")

    def test_the_system_files_are_there_and_are_this_tree_s(self):
        for src, name in mkdist.SYSTEM:
            with open(os.path.join(BUILD, src), "rb") as f:
                want = f.read()
            with open(os.path.join(self.out, "system", name), "rb") as f:
                self.assertEqual(f.read(), want, name)
            self.assertIn(f"`{name}`", self.page)

    def test_every_disk_this_tree_built_is_in_it(self):
        for src, dest, _kind, _prose in mkdist.DISKS:
            if not built(src):
                continue
            p = os.path.join(self.out, dest)
            self.assertTrue(os.path.isfile(p), dest)
            self.assertEqual(os.path.getsize(p),
                             os.path.getsize(os.path.join(BUILD, src)))
            self.assertIn(f"`{dest}`", self.page)

    def test_the_page_lists_the_files_that_are_really_on_the_images(self):
        """Read back independently of the tool that wrote the page."""
        for src, dest, kind, _prose in mkdist.DISKS:
            if kind is None or not built(src):
                continue
            image = atr.ATRImage.load(os.path.join(self.out, dest))
            if kind == "dos2":
                names = [e.filename for e in atr.Dos2(image).entries()
                         if e.in_use]
            else:
                names = [e.filename for e in atr.Sdfs(image).entries("")]
            section = self.page.split(f"`{dest}`")[1].split("###")[0]
            for n in names:
                self.assertIn(f"`{n}", section, f"{dest} holds {n} and the "
                              f"page does not say so")

    def test_every_menu_item_is_in_exactly_one_of_the_two_lists(self):
        """The half of the page that says what works, and the half that
        says what does not, are generated from the same menu; an item in
        neither would be a quiet omission."""
        items = mkdist.menu_items()
        works = self.page.split("## What works")[1].split("## What is not")[0]
        notyet = self.page.split("## What is not there yet")[1]
        self.assertTrue(items)
        for _idx, label, enabled in items:
            here, there = (works, notyet) if enabled else (notyet, works)
            self.assertIn(f"**{label}**", here, label)
            self.assertNotIn(f"**{label}**", there, label)

    def test_the_disabled_list_is_the_resource_s_own(self):
        items = {idx for idx, _l, enabled in mkdist.menu_items()
                 if not enabled}
        self.assertEqual(items, set(deskrsc.NOT_YET))

    def test_the_kit_travels_with_it(self):
        tgz = os.path.join(self.out, "sdk", "gem4xe-sdk.tar.gz")
        self.assertTrue(os.path.isfile(tgz))
        self.assertGreater(os.path.getsize(tgz), 8192)
        self.assertTrue(os.path.isfile(os.path.join(self.out, "COPYING")))

    def test_a_disk_this_tree_cannot_build_is_said_to_be_missing(self):
        """The fixtures are not always here, and the artefact must say
        which disks it does not have rather than quietly omit them."""
        out = os.path.join(self.dir, "gem4xe-missing")
        disks = mkdist.DISKS
        try:
            mkdist.DISKS = disks + [("no-such.atr", "disks/no-such.atr",
                                     None, "not built here")]
            mkdist.build(out)
        finally:
            mkdist.DISKS = disks
        with open(os.path.join(out, "README.md")) as f:
            page = f.read()
        self.assertIn("is not in this build", page)
        self.assertFalse(os.path.exists(os.path.join(out, "disks",
                                                     "no-such.atr")))


if __name__ == "__main__":
    unittest.main()
