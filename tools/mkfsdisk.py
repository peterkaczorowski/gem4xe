#!/usr/bin/env python3
"""A second disk for the file selector's gate: the fixture DOS disk
copied (never modified -- the fixture must be a copy) with enough small
files added that the selector's list scrolls, and names that exercise
its formatting -- no extension, the characters DOS 2 allows beyond
letters, names that sort around each other.

  python3 tools/mkfsdisk.py <source.atr> <out.atr>

`FILES` is what goes on; tests/emu/m12_file.py predicts the listing from
the image itself (tools/atr.py), not from this list.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from atr import ATRImage, Dos2  # noqa: E402

FILES = ["README", "ALPHA.TXT", "BETA.TXT", "GAMMA.DOC", "DELTA.C",
         "EPSILON.C", "ZETA.PRG", "ETA.PRG", "THETA.RSC", "IOTA.TXT",
         "KAPPA.DAT", "LAMBDA.BAS", "MU.OBJ", "NU_1.TXT", "XI@2.TXT"]


def build(src_atr, out_atr):
    os.makedirs(os.path.dirname(os.path.abspath(out_atr)), exist_ok=True)
    img = ATRImage.load(src_atr)
    dos = Dos2(img)
    for name in FILES:
        if dos.find(name):
            raise SystemExit(f"{out_atr}: {name} already present in the image")
        dos.add_file(name, (name + "\x9b").encode("latin-1"))
    print(f"{out_atr}: {len(FILES)} files added, {len(dos.list())} on the disk, "
          f"{dos.free_count()} sectors free")
    img.save(out_atr)
    return out_atr


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    build(sys.argv[1], sys.argv[2])
