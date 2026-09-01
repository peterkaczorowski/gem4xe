#!/usr/bin/env python3
"""Build a bootable Atari disk that auto-runs a .xex, for the emulator harness.

Copies a real DOS image (never modifies the original -- retroharness rule: the
fixture must be a COPY) and writes the .xex into it as AUTORUN.SYS, so booting
the disk runs the program with no keystroke driving at all.

That matters more here than it looks: the Rapidus cold-boots as a 6502 and
switching to the 65C816 resets the CPU, so a program cannot switch the CPU and
then keep running.  The machine has to already be a 65C816 when the program
loads -- which means the program must arrive via the boot path, after the
switch, not before it.

  python3 tools/mkdisk.py <source.atr> <program.xex> <out.atr> [NAME]

The default name is HELLO.COM: the fixture DOS is DOS II+/D 6.4, which boots to
a `D1:` command prompt rather than running AUTORUN.SYS, so the harness types the
name to launch it.  That is an advantage here -- it means the program is started
*after* the CPU switch, on the 65C816.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from atr import ATRImage, Dos2  # noqa: E402


def build(src_atr, xex, out_atr, name="HELLO.COM"):
    os.makedirs(os.path.dirname(os.path.abspath(out_atr)), exist_ok=True)
    shutil.copyfile(src_atr, out_atr)
    img = ATRImage.load(out_atr)
    dos = Dos2(img)
    if dos.find(name):
        raise SystemExit(f"{out_atr}: {name} already present in the source image")
    data = open(xex, "rb").read()
    ent = dos.add_file(name, data)
    img.save(out_atr)
    print(f"{out_atr}: {name} <- {xex} ({len(data)} bytes, {ent.count} sectors, "
          f"start sector {ent.start})")
    return out_atr


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    build(*sys.argv[1:5])
