#!/usr/bin/env python3
"""The gem4xe CF card: an APT-partitioned image with the system on it.

This is the volume the desktop is for.  A floppy holds the system and
little else (docs/shipping.md, section 1); a card holds the system, the
applications and the documents, and SpartaDOS X mounts its partitions as
D1:, D2:, ... through the APT table tools/apt.py writes.

    D1:  the system      \\GEM\\GEM.COM       the VDI, AES, GEMDOS, shell
                         \\GEM\\DESKTOP.G4A   the desktop
                         \\GEM\\DESKTOP.RSC   its resource
                         \\APPS\\...          applications
                         AUTOEXEC.BAT       cd into \\GEM and run it
    D2:  documents       empty, and the reason the table has two entries

  python3 tools/mkcf.py <out.img> [--mb 16] [--system-mb 8]
                        [--add FILE PATH]... [--boot "CD >GEM"]

Every path is SpartaDOS's: `>` between the parts, no drive letter.  The
image is a plain file of 512-byte blocks -- what a card reader writes to
a card, and what the emulator's `harddisk` device reads.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apt                                       # noqa: E402
from atr import Sdfs                             # noqa: E402

MB = 1 << 20
EOL = 0x9B
# What the system partition carries, and where.  The names are the ones
# src/aes/shel.c looks for: the shell opens DESKTOP.G4A in the current
# directory, so AUTOEXEC.BAT changes into \GEM before it runs GEM.COM.
SYSTEM = [("build/gem.xex", "GEM>GEM.COM"),
          ("build/desktop.g4a", "GEM>DESKTOP.G4A"),
          ("build/desktop.rsc", "GEM>DESKTOP.RSC"),
          ("build/lang.rsc", "GEM>LANG.RSC"),
          ("build/gem4xe.cfg", "GEM>GEM4XE.CFG"),
          ("build/m11_app.g4a", "APPS>M11.G4A"),
          ("build/calc.g4a", "APPS>CALC.G4A"),
          ("build/calc.rsc", "APPS>CALC.RSC"),
          ("build/clock.g4a", "APPS>CLOCK.G4A"),
          ("build/clock.rsc", "APPS>CLOCK.RSC")]
DIRS = ["GEM", "APPS"]
BOOT = ["CD >GEM", "GEM"]


def build(out, mb=16, system_mb=8, adds=(), boot=BOOT, root=None):
    root = root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    img = apt.Image(mb * MB // apt.BLOCK)
    parts = apt.layout(img, [system_mb * MB // apt.BLOCK, 0])
    apt.write_table(img, parts)
    fs = Sdfs.format(parts[0], "GEM4XE")
    Sdfs.format(parts[1], "DOCS")
    for d in DIRS:
        fs.mkdir(d)
    for path, name in list(SYSTEM) + list(adds):
        with open(os.path.join(root, path), "rb") as f:
            data = f.read()
        fs.add_file(name, data)
        print(f"{out}: {name} <- {path} ({len(data)} bytes)")
    if boot:
        fs.add_file("AUTOEXEC.BAT",
                    b"".join(line.encode("ascii") + bytes([EOL]) for line in boot))
    img.save(out)
    free = fs.free_count() * apt.BLOCK
    print(f"{out}: {img!r}, {len(parts)} partitions; D1: {fs.volname} "
          f"{free // MB} MB free of {system_mb}, D2: the rest")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--mb", type=int, default=16, help="the whole card")
    ap.add_argument("--system-mb", type=int, default=8, help="the first partition")
    ap.add_argument("--add", nargs=2, action="append", default=[],
                    metavar=("FILE", "PATH"))
    a = ap.parse_args(argv)
    build(a.out, a.mb, a.system_mb, a.add)
    return 0


if __name__ == "__main__":
    sys.exit(main())
