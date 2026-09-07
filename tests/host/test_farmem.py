#!/usr/bin/env python3
"""Host tests for the far allocator (src/sys/farmem.c).

**A block may not cross a bank boundary.**  Calypsi's `__far` pointer
arithmetic is 16 bits WITHIN a bank -- carrying into the bank byte is
what `__huge` is for -- so a buffer that straddles one wraps round to
the bottom of its own bank the moment it is indexed past the edge, and
the bottom of a far bank is the far code image.

That is not a hypothetical.  It corrupted the file selector, and the
symptom was a gate going red when thirty-five bytes were added to the
loader -- and staying green when sixteen were, because whether the
selector's 900-byte name list straddled depended on where the image
ended.  `docs/phase24.md` has the whole account.

So the allocator is run in the compiler's own simulator against the
cursors that used to break it: near the top of a bank, exactly at the
edge, one byte over, and bigger than a bank.
"""
import os
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CALYPSI = os.environ.get("CALYPSI",
                         os.path.expanduser("~/dev/toolchains/calypsi-65816"))
FARMEM_C = os.path.join(ROOT, "src", "sys", "farmem.c")
SIM_C = os.path.join(ROOT, "tests", "host", "farmem_sim.c")

sys.path.insert(0, os.path.join(ROOT, "tools", "ccbug"))
import check as ccbug                      # noqa: E402  (the simulator driver)

# what farmem_sim.c asks for, case by case: (cursor, bytes, what must
# come back -- None for "refused")
CASES = [
    (0x02FF00, 900, 0x030000),      # 256 left in the bank: start the next
    (0x02FFFC, 900, 0x030000),      # four left
    (0x02FC00, 900, 0x02FC00),      # 1024 left: it fits where it is
    (0x020000, 900, 0x020000),      # a whole bank left
    (0x03FF00, 0x100, 0x03FF00),    # exactly the rest of the bank: fits
    (0x03FF00, 0x101, 0x040000),    # one byte more: it does not
    (0x040000, 0x10004, None),      # bigger than a bank: never indexable
    (0xEFFF00, 900, None),          # the last bank's edge: no room above
]


class TestFarAlloc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = os.path.join(CALYPSI, "bin", "cc65816")
        if not os.path.exists(cc):
            raise unittest.SkipTest("Calypsi not installed")
        ld, db = (os.path.join(CALYPSI, "bin", t) for t in ("ln65816", "db65816"))
        scm = os.path.join(CALYPSI, "example", "minimal", "linker.scm")
        out = os.path.join(ROOT, "build", "farmem")
        os.makedirs(out, exist_ok=True)
        objs = []
        for src in (FARMEM_C, SIM_C):
            obj = os.path.join(out, os.path.basename(src)[:-2] + ".o")
            subprocess.run([cc, "-g", "--code-model=large", "--data-model=small",
                            "-O2", "-I", os.path.join(ROOT, "src"),
                            "-o", obj, src], check=True)
            objs.append(obj)
        elf = os.path.join(out, "farmem.elf")
        subprocess.run([ld, "-g", scm] + objs + ["-o", elf, "clib-lc-sd.a",
                        "--rtattr", "exit=simplified"], check=True)
        names = (["fm_straddled", "fm_refused"]
                 + [f"fm_base[{i}]" for i in range(len(CASES))]
                 + [f"fm_bank_lo[{i}]" for i in range(len(CASES))]
                 + [f"fm_bank_hi[{i}]" for i in range(len(CASES))])
        cls.v = ccbug.simulate(db, elf, names)

    def test_no_block_straddles_a_bank(self):
        self.assertEqual(self.v["fm_straddled"], 0,
                         "a block crossed a bank boundary: indexing it past "
                         "the edge wraps into the bottom of its own bank")

    def test_each_block_lands_where_the_contract_says(self):
        bad = []
        for i, (brk, size, want) in enumerate(CASES):
            got = self.v[f"fm_base[{i}]"]
            if (got or None) != (want or None):
                bad.append(f"[{i}] brk ${brk:06X} + {size}: got "
                           f"${got:06X}, expected "
                           + (f"${want:06X}" if want else "a refusal"))
        self.assertEqual(bad, [])

    def test_a_block_bigger_than_a_bank_is_refused(self):
        """No such block could be indexed, so handing one out would be
        handing out a fault."""
        self.assertEqual(self.v["fm_refused"], 1)


if __name__ == "__main__":
    unittest.main()
