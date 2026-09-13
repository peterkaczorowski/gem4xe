#!/usr/bin/env python3
"""The gem4xe floppy: an SDFS disk with the system on it and no DOS.

The two floppies `make dist` builds each boot a DOS that is not gem4xe's
to give away (fixtures.toml.example), so the public release has had
none -- only the card image and the loose files.  This one it can carry:
a double-sided double-density disk (1440 sectors of 256 bytes, 360 KB)
formatted SDFS and holding exactly what the card's system partition
holds, in the same directories, with the same AUTOEXEC.BAT, and NO
boot code beyond the stub every blank SDFS disk has.

It boots under SpartaDOS X -- from a cartridge or from Ultimate 1MB
flash -- which is a DOS that lives in the machine rather than on the
disk: SDX comes up, changes to the disk in D1: and runs its AUTOEXEC.BAT,
and that is GEM.  On a machine without SDX it is an install disk: any
SpartaDOS can read it, and `\\GEM\\` and `\\APPS\\` copy across as
directories (docs/media.md).

Why 360 KB: the system is 195 KB, which is more than a 180 KB
double-density floppy holds and comfortably less than a double-sided
one; the XF551, every SIO emulator and every FAT loader read the
geometry, and SDX mounts it.

  python3 tools/mkfloppy.py <out.atr> [--add FILE PATH]...

The file table and the boot lines are tools/mkcf.py's own, so the floppy
IS the card's layout on a smaller volume rather than a second list that
could drift from it.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkcf                                      # noqa: E402
from atr import ATRImage, Sdfs                   # noqa: E402

SECTOR = 256
SECTORS = 1440                                   # DSDD, as an XF551 writes
EOL = 0x9B


def build(out, adds=(), boot=mkcf.BOOT, root=None):
    root = root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    img = ATRImage(SECTOR, SECTORS)
    fs = Sdfs.format(img, "GEM4XE")
    for d in mkcf.DIRS:
        fs.mkdir(d)
    for path, name in list(mkcf.SYSTEM) + list(adds):
        with open(os.path.join(root, path), "rb") as f:
            data = f.read()
        fs.add_file(name, data)
        print(f"{out}: {name} <- {path} ({len(data)} bytes)")
    if boot:
        fs.add_file("AUTOEXEC.BAT",
                    b"".join(line.encode("ascii") + bytes([EOL]) for line in boot))
    img.save(out)
    free = fs.free_count()
    print(f"{out}: {img!r}, {fs.volname}, no DOS; {free} sectors free, "
          f"{free * (SECTOR - 2) // 1024} KB")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--add", nargs=2, action="append", default=[],
                    metavar=("FILE", "PATH"))
    a = ap.parse_args(argv)
    build(a.out, a.add)
    return 0


if __name__ == "__main__":
    sys.exit(main())
