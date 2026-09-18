#!/usr/bin/env python3
"""FARRSC.RSC: a resource too big for the application pool, on purpose.

    tools/farrsc.py build/farrsc.rsc build/farrsc.h

The pool is 14 KB of bank $00 and every resource this tree ships fits it
with room to spare -- the desktop's is 6 KB, GACS's 2.8.  QED's is 34 KB
with 682 objects, which is why docs/far-trees.md exists.  This file is
the smallest thing that has the same problem: **702 objects across 28
trees**, 16,848 bytes of OBJECT records before a single string, so that
rsrc_load cannot put it in the pool and must either take the far path
(a program that says it can take a far address) or refuse (one that has
not).  test-m33 loads it both ways.

Tree 0 is the one the program draws and hit-tests: a dialog of twenty
rows, two editable fields and three buttons.  Trees 1..27 are filler --
a box and twenty-four strings each -- there to be counted, not drawn,
though every one is fixed up and every string is reached through a
32-bit address the AES can now follow.  No icons, no images, no
BITBLKs: nothing here is moved by rs_imfar, so where the file lands is
where all of it stays.
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsc                                  # noqa: E402
from rsc import ch, NIL                     # noqa: E402
from aesref import (G_BOX, G_STRING, G_BUTTON, G_FTEXT,  # noqa: E402
                    NONE, NORMAL, SELECTABLE, DEFAULT, EXIT, EDITABLE, LASTOB,
                    TE_LEFT, IBM)

# -- tree 0, the dialog ------------------------------------------------------
FR_DIALOG = 0                               # the tree index
ROWS = 20                                   # "Row 01".."Row 20"
(FR_ROOT, FR_TITLE) = (0, 1)
FR_ROW0 = 2                                 # rows are FR_ROW0 .. FR_ROW0+ROWS-1
FR_FIELD1 = FR_ROW0 + ROWS                  # 22
FR_FIELD2 = FR_FIELD1 + 1                   # 23
FR_OK = FR_FIELD2 + 1                       # 24
FR_CANCEL = FR_OK + 1                       # 25
FR_HELP = FR_CANCEL + 1                     # 26
NOBS_DIALOG = FR_HELP + 1                   # 27
W, H = 40, ROWS + 8                         # cells
TITLE = "A RESOURCE FROM FAR MEMORY"
FREE0 = "FREE STRING ZERO"                  # free string 0: the program reads its first byte
FR_FREE0_FIRST = ord(FREE0[0])            # ...and this is what it must read: 'F'

# -- the filler --------------------------------------------------------------
FILLER_TREES = 27
FILLER_ROWS = 24
NOBS_FILLER = 1 + FILLER_ROWS               # a box and its strings
NOBS_TOTAL = NOBS_DIALOG + FILLER_TREES * NOBS_FILLER      # 702


def dialog(r):
    objs = [
        (NIL, FR_TITLE, FR_HELP, G_BOX, NONE, NORMAL, 0x00021100,
         ch(0), ch(0), ch(W), ch(H)),
        (FR_ROW0, NIL, NIL, G_STRING, NONE, NORMAL, r.string(TITLE),
         ch((W - len(TITLE)) // 2), ch(1), ch(len(TITLE)), ch(1)),
    ]
    for i in range(ROWS):
        s = f"Row {i + 1:02d}: far tree, object {FR_ROW0 + i:3d}"
        objs.append((FR_ROW0 + i + 1, NIL, NIL, G_STRING, NONE, NORMAL,
                     r.string(s), ch(2), ch(3 + i), ch(len(s)), ch(1)))
    objs.append((FR_FIELD2, NIL, NIL, G_FTEXT, EDITABLE, NORMAL,
                 r.ted("", "Name: ________", "X", font=IBM, just=TE_LEFT),
                 ch(2), ch(3 + ROWS + 1), ch(14), ch(1)))
    objs.append((FR_OK, NIL, NIL, G_FTEXT, EDITABLE, NORMAL,
                 r.ted("", "Code: ____", "9", font=IBM, just=TE_LEFT),
                 ch(20), ch(3 + ROWS + 1), ch(10), ch(1)))
    objs.append((FR_CANCEL, NIL, NIL, G_BUTTON, SELECTABLE | DEFAULT | EXIT,
                 NORMAL, r.string("OK"), ch(2), ch(H - 3), ch(8), ch(2)))
    objs.append((FR_HELP, NIL, NIL, G_BUTTON, SELECTABLE | EXIT, NORMAL,
                 r.string("Cancel"), ch(14), ch(H - 3), ch(8), ch(2)))
    objs.append((FR_ROOT, NIL, NIL, G_BUTTON, SELECTABLE | LASTOB, NORMAL,
                 r.string("Help"), ch(26), ch(H - 3), ch(8), ch(2)))
    assert len(objs) == NOBS_DIALOG, (len(objs), NOBS_DIALOG)
    return r.tree(objs)


def filler(r, n):
    objs = [(NIL, 1, FILLER_ROWS, G_BOX, NONE, NORMAL, 0x00021100,
             ch(0), ch(0), ch(W), ch(FILLER_ROWS + 2))]
    for i in range(FILLER_ROWS):
        s = f"T{n:02d}/{i:02d} filler text so the file is big"
        last = i == FILLER_ROWS - 1
        objs.append((0 if last else i + 2, NIL, NIL, G_STRING,
                     LASTOB if last else NONE, NORMAL, r.string(s),
                     ch(1), ch(1 + i), ch(len(s)), ch(1)))
    assert len(objs) == NOBS_FILLER
    return r.tree(objs)


def build(r):
    """The whole description into `r`; returns the dialog's tree index."""
    t0 = dialog(r)
    assert t0 == FR_DIALOG
    for n in range(FILLER_TREES):
        filler(r, n + 1)
    r.free_string(FREE0)
    assert len(r.objects) == NOBS_TOTAL, (len(r.objects), NOBS_TOTAL)
    return t0


INDICES = [("FR_DIALOG", FR_DIALOG), ("FR_ROOT", FR_ROOT), ("FR_TITLE", FR_TITLE),
           ("FR_ROW0", FR_ROW0), ("FR_FIELD1", FR_FIELD1), ("FR_FIELD2", FR_FIELD2),
           ("FR_OK", FR_OK), ("FR_CANCEL", FR_CANCEL), ("FR_HELP", FR_HELP),
           ("NOBS_DIALOG", NOBS_DIALOG), ("NOBS_TOTAL", NOBS_TOTAL)]


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    r = rsc.Rsc()
    build(r)
    data = r.file()
    with open(argv[0], "wb") as f:
        f.write(data)
    with open(argv[1], "w") as f:
        f.write("/* generated by tools/farrsc.py -- the indices test-m33's program uses */\n")
        for name, val in INDICES:
            f.write(f"#define {name} {val}\n")
        f.write(f"#define FR_FREE0_FIRST {FR_FREE0_FIRST}   /* '{FREE0[0]}' */\n")
    print(f"{argv[0]}: {len(data)} bytes, {NOBS_TOTAL} objects in "
          f"{1 + FILLER_TREES} trees, {len(r.strings)} strings"
          f" -- {'does not fit' if len(data) > 14336 else 'FITS'} a 14 KB pool")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
