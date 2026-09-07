#!/usr/bin/env python3
"""Host tests for the application kit (tools/mksdk.py).

The kit is what somebody who is not this repository needs in order to
build a program that runs on gem4xe.  The only way to know it is
complete is to use it as they would: assemble it, copy it somewhere
with no relation to the source tree, and build from there.  A file left
out of the manifest is then a failure here rather than a discovery
somebody else makes.

Two programs are built out of the copy:

  * `example/hello.c`, the kit's own -- which proves the kit builds new
    code, and pins the memory budget its README quotes;
  * `src/m11_app.c`, the Phase 10 gate application, whose `.g4a` must
    come out **byte for byte identical** to the one this tree builds --
    and `make test-m11` says that exact binary loads, relocates and
    runs on the emulated machine.  So the kit is not merely
    self-contained; the program it produces is the one already proven
    to work.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mksdk                                # noqa: E402

CALYPSI = os.environ.get("CALYPSI",
                         os.path.expanduser("~/dev/toolchains/calypsi-65816"))
M11_APP = os.path.join(ROOT, "src", "m11_app.c")
HELLO_SIM = os.path.join(ROOT, "tests", "host", "hello_sim.c")
M11_G4A = os.path.join(ROOT, "build", "m11_app.g4a")
# The kit's default budget, as its Makefile and its README have it:
# a page of direct page, 2048 bytes without bits, 256 with.
NEAR_SIZE = 0x100 + 2048 + 256


def g4a_header(path):
    """The .g4a header the loader reads (src/sys/app.c, tools/mkg4a.py)."""
    with open(path, "rb") as f:
        d = f.read(20)
    assert d[:4] == b"G4A\x01", d[:4]
    near_base, near_size, far_off = struct.unpack("<HHH", d[4:10])
    far_size, = struct.unpack("<I", d[10:14])
    return dict(near_base=near_base, near_size=near_size, far_off=far_off,
                far_size=far_size, far_bank=d[14], far_banks=d[15])


class TestManifest(unittest.TestCase):
    """These read the sources; no toolchain needed."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="gem4xe-sdk-")
        self.kit = os.path.join(self.dir, "gem4xe-sdk")
        mksdk.build(self.kit)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_every_manifest_file_arrives(self):
        for dest, _ in mksdk.MANIFEST:
            self.assertTrue(os.path.isfile(os.path.join(self.kit, dest)), dest)

    def test_nothing_in_the_build_reaches_outside_the_kit(self):
        """A path that climbs out, or an absolute one into this tree,
        would work here and nowhere else."""
        bad = []
        for name in ("Makefile", "lib/gemapp.scm"):
            with open(os.path.join(self.kit, name)) as f:
                for i, line in enumerate(f, 1):
                    code = line.split(";;;")[0].split("#")[0]
                    if "../" in code or ROOT.rstrip("/.") in code:
                        bad.append(f"{name}:{i}: {line.strip()}")
        self.assertEqual(bad, [])

    def test_the_kit_carries_its_own_licence(self):
        with open(os.path.join(self.kit, "COPYING")) as f:
            self.assertIn("GNU GENERAL PUBLIC LICENSE", f.read())


class TestBuildsFromACopy(unittest.TestCase):
    """The kit, used the way somebody else would use it."""

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(os.path.join(CALYPSI, "bin", "cc65816")):
            raise unittest.SkipTest("Calypsi not installed")
        cls.dir = tempfile.mkdtemp(prefix="gem4xe-sdk-")
        cls.kit = os.path.join(cls.dir, "gem4xe-sdk")
        mksdk.build(cls.kit)
        # the gate application's source travels in as an author's would
        shutil.copyfile(M11_APP, os.path.join(cls.kit, "m11_app.c"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def make(self, *args):
        env = dict(os.environ, CALYPSI=CALYPSI)
        p = subprocess.run(["make", "-s"] + list(args), cwd=self.kit,
                           capture_output=True, text=True, env=env, timeout=600)
        self.assertEqual(p.returncode, 0,
                         f"make {' '.join(args)} failed:\n{p.stdout}\n{p.stderr}")
        return p.stdout

    def test_the_example_builds_and_is_a_loadable_program(self):
        self.make()
        out = os.path.join(self.kit, "hello.g4a")
        self.assertTrue(os.path.isfile(out))
        h = g4a_header(out)
        # what the kit's Makefile asked for, and what the loader needs
        self.assertEqual(h["near_size"], NEAR_SIZE)
        self.assertEqual(h["near_base"], 0x1000)
        self.assertEqual(h["far_banks"], 1, "one bank, as the loader takes")
        self.assertTrue(0 < h["far_size"] < 0x10000, h["far_size"])

    def test_the_kit_rebuilds_the_gate_application_byte_for_byte(self):
        if not os.path.isfile(M11_G4A):
            self.skipTest("build/m11_app.g4a is not built "
                          "(make build/m11_app.g4a)")
        self.make("APP=m11_app.c", "BSS=2048", "BITS=256", "STACK=256")
        with open(os.path.join(self.kit, "m11_app.g4a"), "rb") as f:
            kit = f.read()
        with open(M11_G4A, "rb") as f:
            tree = f.read()
        self.assertEqual(kit, tree,
                         "the kit's build of the gate application differs "
                         "from this tree's, so what test-m11 proves does "
                         "not carry over to the kit")


class TestTheExampleIsShapedLikeAGemProgram(unittest.TestCase):
    """The example is the file an author copies first, so the ORDER of
    what it does is what matters: announce, ask, open, draw, close,
    leave.  It is run in the compiler's simulator against a recorder
    that answers plausibly (tests/host/hello_sim.c)."""

    APPL_INIT, APPL_EXIT, GRAF_HANDLE = 1010, 1019, 1077
    V_OPNVWK, V_CLSVWK = 100, 101

    @classmethod
    def setUpClass(cls):
        cc = os.path.join(CALYPSI, "bin", "cc65816")
        if not os.path.exists(cc):
            raise unittest.SkipTest("Calypsi not installed")
        ld, db = (os.path.join(CALYPSI, "bin", t)
                  for t in ("ln65816", "db65816"))
        scm = os.path.join(CALYPSI, "example", "minimal", "linker.scm")
        cls.dir = tempfile.mkdtemp(prefix="gem4xe-sdk-")
        kit = os.path.join(cls.dir, "gem4xe-sdk")
        mksdk.build(kit)
        objs = []
        for src in (os.path.join(kit, "lib", "gemlib.c"),
                    os.path.join(kit, "example", "hello.c"), HELLO_SIM):
            obj = os.path.join(cls.dir, os.path.basename(src)[:-2] + ".o")
            subprocess.run([cc, "-g", "--code-model=large",
                            "--data-model=small", "-O2",
                            "-I", os.path.join(kit, "include"),
                            "-o", obj, src], check=True)
            objs.append(obj)
        elf = os.path.join(cls.dir, "hello.elf")
        subprocess.run([ld, "-g", scm] + objs + ["-o", elf, "clib-lc-sd.a",
                        "--rtattr", "exit=simplified"], check=True)
        p = subprocess.run([db, "--nh", "--nx", "--exit-breakpoint", elf],
                           input="run\nprint hello_n\nprint hello_op\nquit\n",
                           capture_output=True, text=True, timeout=180)
        txt = p.stdout + p.stderr
        m = re.search(r"\$\d+ = (\d+)", txt)
        if not m:
            raise AssertionError(f"the simulator said nothing:\n{txt[-2000:]}")
        n = int(m.group(1))
        cls.ops = [int(v) for v in
                   re.findall(r"\[\s*\d+\s*\] = (-?\d+)", txt)][:n]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def test_it_announces_itself_first_and_leaves_last(self):
        self.assertEqual(self.ops[0], self.APPL_INIT)
        self.assertEqual(self.ops[-1], self.APPL_EXIT)

    def test_it_asks_the_aes_before_it_opens_a_workstation(self):
        self.assertLess(self.ops.index(self.GRAF_HANDLE),
                        self.ops.index(self.V_OPNVWK))

    def test_nothing_is_drawn_outside_the_workstation(self):
        first, last = (self.ops.index(self.V_OPNVWK),
                       self.ops.index(self.V_CLSVWK))
        drawing = [i for i, op in enumerate(self.ops)
                   if op < 1000 and op not in (self.V_OPNVWK, self.V_CLSVWK)]
        self.assertTrue(drawing, "the example draws nothing at all")
        self.assertGreater(min(drawing), first)
        self.assertLess(max(drawing), last)


if __name__ == "__main__":
    unittest.main()
