"""The storm detector says "emulator", and only when it is one.

`tests/emu/m7_form.py storm_check` is what a desktop gate prints instead of
a mystery when the machine underneath it has been painted by Altirra's
SEI-shadow IRQ storm (tools/altirra/altirra-65c816-native-mode.patch).  It
took three phases to recognise that failure the first time (docs/phase26.md)
and the whole value of the check is that nobody has to recognise it again --
so the check itself is worth a test, and it cannot have one on the target:
a storm is the emulator's to produce, not the program's.

The bridge is faked here, which is the point: what is being tested is the
RULE -- memory that is two bytes AND hardware registers that are the same
two -- and not any particular run.
"""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests", "emu"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from m7_form import storm_check  # noqa: E402


class FakeBridge:
    """memdump and HWSTATE, which is all storm_check asks for."""

    def __init__(self, mem, antic, gtia, pokey, ok=True):
        self.mem, self.ok_ = mem, ok
        self.state = {"ok": ok, "antic": antic, "gtia": gtia, "pokey": pokey}

    def memdump(self, addr, length):
        return bytes(self.mem[:length])

    def cmd(self, line):
        assert line == "HWSTATE", line
        return self.state


def regs(names, values):
    """A register block: `values` cycled over `names`."""
    return {n: values[i % len(values)] for i, n in enumerate(names)}


ANTIC = ["DMACTL", "CHACTL", "DLISTL", "DLISTH", "HSCROL", "VSCROL", "PMBASE",
         "CHBASE", "NMIEN"]
GTIA = ["HPOSP0", "HPOSP1", "HPOSP2", "HPOSP3", "COLPM0", "COLPM1", "COLPF0",
        "COLPF1", "COLPF2", "COLPF3", "COLBK", "PRIOR", "GRACTL", "CONSOL"]
POKEY = ["AUDF1", "AUDC1", "AUDF2", "AUDC2", "AUDF3", "AUDC3", "AUDF4",
         "AUDC4", "AUDCTL", "IRQEN", "SKCTL"]
PAIR = ["$02", "$04"]


class Storm(unittest.TestCase):
    def test_a_painted_machine_is_named(self):
        """Memory alternating $02/$04 and the hardware holding the same."""
        b = FakeBridge(b"\x02\x04" * 128, regs(ANTIC, PAIR), regs(GTIA, PAIR),
                       regs(POKEY, PAIR))
        msg = storm_check(b)
        self.assertIsNotNone(msg)
        self.assertIn("$02/$04", msg)
        self.assertIn("ALTIRRASDL", msg)

    def test_a_few_bytes_out_of_place_do_not_hide_it(self):
        """The real one left four bytes of the direct page alone."""
        mem = bytearray(b"\x02\x04" * 128)
        mem[62:66] = b"\x7e\xfe\x04\x02"
        b = FakeBridge(bytes(mem), regs(ANTIC, PAIR), regs(GTIA, PAIR),
                       regs(POKEY, PAIR))
        self.assertIsNotNone(storm_check(b))

    def test_a_healthy_machine_is_not_named(self):
        """Ordinary memory: the direct page of a program that is running."""
        mem = bytes(range(256))
        b = FakeBridge(mem, regs(ANTIC, ["$22", "$00", "$40"]),
                       regs(GTIA, ["$0f", "$00", "$94", "$c6"]),
                       regs(POKEY, ["$00", "$a0", "$03"]))
        self.assertIsNone(storm_check(b))

    def test_memory_alone_is_not_enough(self):
        """A program CAN fill its own memory with a repeating pair -- what it
        cannot do is write ANTIC's registers with it."""
        b = FakeBridge(b"\x02\x04" * 128, regs(ANTIC, ["$22", "$00", "$40"]),
                       regs(GTIA, ["$0f", "$00", "$94", "$c6"]),
                       regs(POKEY, ["$00", "$a0", "$03"]))
        self.assertIsNone(storm_check(b))

    def test_hardware_alone_is_not_enough(self):
        """A quiet machine's registers are mostly zero, and that is not a
        storm either."""
        b = FakeBridge(bytes(range(256)), regs(ANTIC, ["$00", "$02"]),
                       regs(GTIA, ["$00", "$02"]), regs(POKEY, ["$00", "$02"]))
        self.assertIsNone(storm_check(b))

    def test_zeroed_memory_and_a_quiet_machine_are_not_a_storm(self):
        """The pair has to be a real alternation.  A page a program has
        just cleared is one value and a handful of others, and a machine
        with its sound off has registers to match -- and the two together
        must not read as a painted machine."""
        mem = bytearray(256)
        mem[:12] = b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c"
        b = FakeBridge(bytes(mem), regs(ANTIC, ["$00", "$01"]),
                       regs(GTIA, ["$00", "$01"]), regs(POKEY, ["$00", "$01"]))
        self.assertIsNone(storm_check(b))

    def test_a_bridge_that_cannot_answer_says_nothing(self):
        b = FakeBridge(b"\x02\x04" * 128, {}, {}, {}, ok=False)
        self.assertIsNone(storm_check(b))


if __name__ == "__main__":
    unittest.main()
