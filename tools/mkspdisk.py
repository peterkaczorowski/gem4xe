#!/usr/bin/env python3
"""The SpartaDOS boot disk for the SpartaGEM gates: a fresh SDFS volume
that boots the SpartaDOS 3.2 on the fixture disk (its boot sectors and
DOS file copied over -- the fixture itself is never modified), with the
program, the file-layer fixtures and a small directory tree on it:

    M3.COM  TEST.TXT  TEST.RSC  OUT.TXT       what the DOS 2 disk carries
    SUB>ONE.TXT  SUB>TWO.DAT  SUB>DEEP>THREE.TXT   folders for the selector

Under SpartaDOS X the cartridge boots instead and this is just D1:, which
is why the tree, not the boot code, is the point of the disk.

  python3 tools/mkspdisk.py <sparta32.atr> <m3.xex> <out.atr> [--sectors N]
                            [--add FILE NAME]...

tests/emu/m14_sparta.py predicts the listing from the image itself
(tools/atr.py), not from TREE below.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from atr import ATRImage, Sdfs  # noqa: E402

# (path, contents); a path with no contents is a directory
TREE = [
    ("SUB", None),
    ("SUB>ONE.TXT", b"one\x9b"),
    ("SUB>TWO.DAT", b"two\x9b"),
    ("SUB>DEEP", None),
    ("SUB>DEEP>THREE.TXT", b"three\x9b"),
]


def build(src_atr, xex, out_atr, sectors=1040, adds=(), volname="GEM4XE"):
    os.makedirs(os.path.dirname(os.path.abspath(out_atr)), exist_ok=True)
    img = ATRImage(128, sectors)
    fs = Sdfs.format(img, volname)
    fs.boot_from(Sdfs(ATRImage.load(src_atr)))
    with open(xex, "rb") as f:
        fs.add_file("M3.COM", f.read())
    for path, name in adds:
        with open(path, "rb") as f:
            fs.add_file(name, f.read())
    for path, data in TREE:
        if data is None:
            fs.mkdir(path)
        else:
            fs.add_file(path, data)
    img.save(out_atr)
    print(f"{out_atr}: {sectors} x 128, {len(fs.list())} in MAIN, "
          f"{fs.free_count()} sectors free")
    return out_atr


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src", help="a SpartaDOS 3.2 boot disk (a copy)")
    ap.add_argument("xex", help="the program, stored as M3.COM")
    ap.add_argument("out")
    ap.add_argument("--sectors", type=int, default=1040)
    ap.add_argument("--add", nargs=2, action="append", default=[], metavar=("FILE", "NAME"))
    a = ap.parse_args(argv)
    build(a.src, a.xex, a.out, a.sectors, a.add)
    return 0


if __name__ == "__main__":
    sys.exit(main())
