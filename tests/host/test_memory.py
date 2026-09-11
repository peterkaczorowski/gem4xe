"""Bank $00's budget, asserted rather than discovered.

tools/memreport.py simulates the pool's bump allocator in the order the
machine runs it and reads every size out of a build artefact.  This runs
it, so that a change which eats the last of LoRAM, or which leaves the
pool without a read slice worth having, fails here with a number instead
of at link time in the middle of something else.
"""
import os
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
REPORT = os.path.join(ROOT, "tools", "memreport.py")
MAP = os.path.join(ROOT, "build", "gem.map")


@unittest.skipUnless(os.path.exists(MAP), "build/gem.map: build GEM.COM first")
class Memory(unittest.TestCase):
    def test_budget(self):
        r = subprocess.run([sys.executable, REPORT], capture_output=True,
                           text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_report_names_every_region(self):
        """The report is only worth having if it covers the whole bank."""
        r = subprocess.run([sys.executable, REPORT], capture_output=True,
                           text=True)
        for name in ("DirectPage", "LoRAM", "Near", "Window", "AppPool"):
            self.assertIn(name, r.stdout)
