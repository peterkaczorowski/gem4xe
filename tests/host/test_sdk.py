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
    assert d[:4] == b"G4A\x03", d[:4]
    near_base, near_size, far_off = struct.unpack("<HHH", d[4:10])
    far_size, = struct.unpack("<I", d[10:14])
    return dict(near_base=near_base, near_size=near_size, far_off=far_off,
                far_size=far_size, far_bank=d[14], far_banks=d[15])


class TestTheObjectLayoutIsTheSTs(unittest.TestCase):
    """An OBJECT is 24 bytes with ob_spec at offset 12, in BOTH data
    models -- which is what lets ob_spec be declared as gemlib's union
    rather than a bare LONG.

    The union is only safe because of an accident worth pinning down: a
    pointer is two bytes under --data-model=small and lands on the low
    word, which is where a bank-$00 address is kept, and four bytes under
    --data-model=large, little-endian with the bank in byte 2, so the
    whole four bytes read as a far pointer whose bank is the high word's
    zero.  Either way a member sees the address the AES put there.

    Checked by compiling, because a size that has gone wrong will not
    announce itself at run time: it moves every field after ob_spec and
    the AES and the application then disagree about a tree neither can
    see the other reading.  Three sizes pin the offset between them --
    the six words before ob_spec are 12 bytes, ob_spec is 4, the whole
    is 24 -- which leaves no room for padding anywhere.
    """

    CASES = (("the six words before ob_spec are 12 bytes",
              "sizeof(struct { WORD a, b, c; UWORD d, e, f; }) == 12"),
             ("ob_spec is four bytes", "sizeof(OBSPEC) == 4"),
             ("an OBJECT is 24 bytes", "sizeof(OBJECT) == 24"),
             ("a TEDINFO is the ST's 28", "sizeof(TEDINFO) == 28"),
             # ICONBLK and BITBLK are the two the compiler PADS: they begin
             # with LONGs and end on an odd number of WORDs, so 34 and 14 in
             # the file become 36 and 16 here.  What matters to a program
             # reading a loaded resource is that the struct is not SHORTER
             # than the record, and that it never be used as a file stride
             # (tests/host/test_rsrc.py).
             ("an ICONBLK covers the file's 34", "sizeof(ICONBLK) >= 34"),
             ("a BITBLK covers the file's 14", "sizeof(BITBLK) >= 14"))

    @classmethod
    def setUpClass(cls):
        cls.cc = os.path.join(CALYPSI, "bin", "cc65816")
        if not os.path.exists(cls.cc):
            raise unittest.SkipTest("Calypsi not installed")
        cls.dir = tempfile.mkdtemp(prefix="gem4xe-sdk-")
        cls.kit = os.path.join(cls.dir, "gem4xe-sdk")
        mksdk.build(cls.kit)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def check(self, model, expr, what):
        """A negative-size array is the assertion: it compiles when the
        expression holds and cannot when it does not."""
        src = os.path.join(self.dir, "layout.c")
        with open(src, "w") as f:
            f.write('#include "gem.h"\n'
                    f"char probe[({expr}) ? 1 : -1];\n")
        r = subprocess.run(
            [self.cc, "--code-model=large", f"--data-model={model}", "-O2",
             "-I", os.path.join(self.kit, "include"),
             "-o", os.path.join(self.dir, "layout.o"), src],
            capture_output=True, text=True)
        self.assertEqual(r.returncode, 0,
                         f"--data-model={model}: {what} is not true "
                         f"({expr})\n{r.stdout}{r.stderr}")

    def test_the_layout_holds_in_the_small_data_model(self):
        for what, expr in self.CASES:
            self.check("small", expr, what)

    def test_the_layout_holds_in_the_large_data_model(self):
        for what, expr in self.CASES:
            self.check("large", expr, what)

    def test_a_pointer_is_the_size_the_union_argument_rests_on(self):
        self.check("small", "sizeof(char *) == 2",
                   "a small-model pointer is the low word")
        self.check("large", "sizeof(char *) == 4",
                   "a large-model pointer is the whole four bytes")


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
        # ...with its one COP that is not gem4xe's, in assembly (src/m11_cop.s)
        shutil.copyfile(os.path.join(os.path.dirname(M11_APP), "m11_cop.s"),
                        os.path.join(cls.kit, "m11_cop.s"))

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
        self.make("APP=m11_app.c", "ASM=m11_cop.s", "BSS=2048", "BITS=256", "STACK=256")
        with open(os.path.join(self.kit, "m11_app.g4a"), "rb") as f:
            kit = f.read()
        with open(M11_G4A, "rb") as f:
            tree = f.read()
        self.assertEqual(kit, tree,
                         "the kit's build of the gate application differs "
                         "from this tree's, so what test-m11 proves does "
                         "not carry over to the kit")

    def test_a_program_linked_with_the_old_gates_is_refused_on_the_host(self):
        """tools/mkg4a.py checks the gates it is about to stamp format 3 or
        4 over.  Objects built from an older kit's gemabi.s -- COP #$73,
        #$C8, #$01 -- must stop the build here, not come out as a file the
        Atari's loader accepts and whose every call it then refuses."""
        old = os.path.join(self.dir, "old-gates")
        shutil.copytree(self.kit, old,
                        ignore=shutil.ignore_patterns("build", "*.g4a"))
        p = os.path.join(old, "lib", "gemabi.s")
        with open(p) as f:
            s = f.read()
        for new, was in (("#0x56", "#0x73"), ("#0x41", "#0xc8"), ("#0x44", "#0x01")):
            self.assertIn(new, s)
            s = s.replace(new, was)
        with open(p, "w") as f:
            f.write(s)
        env = dict(os.environ, CALYPSI=CALYPSI)
        r = subprocess.run(["make", "-s"], cwd=old, capture_output=True,
                           text=True, env=env, timeout=600)
        self.assertNotEqual(r.returncode, 0,
                            "a program with the old gates was packed")
        self.assertIn("not COP #$56", r.stdout + r.stderr)
        self.assertFalse(os.path.exists(os.path.join(old, "hello.g4a")))


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
