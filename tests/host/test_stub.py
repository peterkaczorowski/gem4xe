#!/usr/bin/env python3
"""What `printf` asks the system for.

src/app/gemstub.c answers the nine routines Calypsi's C library asks the
board for -- without them a program that prints does not link, and what
the linker says names the board support rather than anything the author
wrote.  Its correctness is one mapping: a file descriptor IS a GEMDOS
handle, unchanged, so stdout is handle 1 and the bytes leave through
Fwrite.

None of that shows at link time.  A wrong handle links.  A count and a
buffer the wrong way round link.  So the program is run, in the
compiler's own simulator, with the GEMDOS gate replaced by a recorder
(tests/host/stub_sim.c), and what it recorded is compared with what it
printed.
"""
import os
import re
import subprocess
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CALYPSI = os.environ.get("CALYPSI",
                         os.path.expanduser("~/dev/toolchains/calypsi-65816"))
GEMLIB_C = os.path.join(ROOT, "src", "app", "gemlib.c")
GEMSTUB_C = os.path.join(ROOT, "src", "app", "gemstub.c")
STUB_SIM_C = os.path.join(ROOT, "tests", "host", "stub_sim.c")

WANT = "gem4xe 43\n"
GD_FWRITE = 0x40


class TestPrintfReachesTheConsole(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cc = os.path.join(CALYPSI, "bin", "cc65816")
        if not os.path.exists(cc):
            raise unittest.SkipTest("Calypsi not installed")
        ld, db = (os.path.join(CALYPSI, "bin", t) for t in ("ln65816", "db65816"))
        scm = os.path.join(CALYPSI, "example", "minimal", "linker.scm")
        out = os.path.join(ROOT, "build", "stub")
        os.makedirs(out, exist_ok=True)
        objs = []
        for src in (GEMLIB_C, GEMSTUB_C, STUB_SIM_C):
            obj = os.path.join(out, os.path.basename(src)[:-2] + ".o")
            subprocess.run([cc, "-g", "--code-model=large", "--data-model=small",
                            "-O2", "-I", os.path.join(ROOT, "src"),
                            "-I", os.path.join(ROOT, "src", "app"),
                            "-o", obj, src], check=True)
            objs.append(obj)
        elf = os.path.join(out, "stub.elf")
        subprocess.run([ld, "-g", scm] + objs + ["-o", elf, "clib-lc-sd.a",
                        "--rtattr", "exit=simplified"], check=True)
        p = subprocess.run(
            [db, "--nh", "--nx", "--exit-breakpoint", elf],
            input="run\nprint stub_calls\nprint stub_fn\nprint stub_handle\n"
                  "print stub_writes\nprint stub_len\nprint stub_text\nquit\n",
            capture_output=True, text=True, timeout=180)
        txt = p.stdout + p.stderr
        vals = re.findall(r"\$\d+ = (-?\d+)", txt)
        if len(vals) < 4:
            raise AssertionError(f"the simulator said nothing:\n{txt[-2000:]}")
        (cls.calls, cls.fn, cls.handle, cls.writes,
         cls.length) = (int(v) for v in vals[:5])
        cls.text = "".join(
            chr(int(v)) for v in re.findall(r"\[\s*\d+\s*\] = (\d+)", txt))

    def test_printf_becomes_one_fwrite(self):
        self.assertEqual(self.fn, GD_FWRITE,
                         f"printf left through GEMDOS ${self.fn:02X}, not "
                         f"Fwrite (${GD_FWRITE:02X})")

    def test_it_goes_to_handle_1(self):
        self.assertEqual(self.handle, 1,
                         f"stdout arrived on GEMDOS handle {self.handle}.  "
                         f"1 is the console; 0 is its input and 2 is AUX:, "
                         f"so a program printing to either is writing "
                         f"somewhere a reader will not look")

    def test_the_bytes_are_the_ones_it_printed(self):
        self.assertEqual(self.text[:len(WANT)], WANT,
                         "the bytes Fwrite was given are not the ones printf "
                         f"formatted: {self.text[:32]!r}")

    def test_the_count_is_the_length_of_them(self):
        self.assertEqual(self.length, len(WANT),
                         f"Fwrite was handed {self.length} bytes over "
                         f"{self.writes} call(s) for a {len(WANT)}-byte line "
                         f"-- the count and the buffer are the two arguments "
                         f"easiest to swap")

    def test_an_unbuffered_stdout_costs_a_call_per_character(self):
        """Not a fault, but the number a program should know: this C
        library leaves stdout unbuffered, and every byte is then its own
        trip through the call gate and into GEMDOS.  A program that
        prints in quantity gives stdio a buffer with setvbuf."""
        self.assertEqual(self.writes, len(WANT),
                         f"{self.writes} Fwrite(s) for {len(WANT)} bytes: "
                         f"stdout's buffering has changed, and the kit's "
                         f"README says it is a call per character")


if __name__ == "__main__":
    unittest.main()
