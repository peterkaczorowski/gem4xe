#!/usr/bin/env python3
"""DESKTOP.RSC: the GEM Desktop's resource, built on the host.

    tools/deskrsc.py build/desktop.rsc build/deskrsc.h

The desktop loads it with rsrc_load at start, the way every GEM program
loads its resource -- so the file is the real thing, not C data, and the
loader, the fixup and the pool are exercised every time the desktop
comes up.  `build()` is also what tools/deskref.py draws from, so the
gate compares the target against what this description means.

The shape is EmuTOS's desk/desk_rsc.c (the Caldera-GPL DRI desktop,
GPLv2), described in character units as RCS would have written them so
the one file serves any font the AES finds:

  ADMENU   the menu as RCS builds a menu (rcs/RCSMENU.C's fix_menu_bar,
           corpus): the bar gl_hchar+2 high, the titles gl_hchar+3, each
           drop-down 2 characters right of its title, the Desk box with
           EXACTLY eight children (About, a separator, six accessory
           slots -- the AES relinks dabox+1..dabox+8), separators as
           disabled strings of dashes the box's width, two leading spaces
           on every item for the check-mark column.  The items the
           desktop does not do yet are in the tree, and disabled by
           menu_ienable at start, so the menu keeps its shape from one
           milestone to the next.
  ADDINFO  the About dialog: a box, lines of text, OK.
  strings  STDISK/STTRASH, the icon labels; the alerts of later milestones
           will join them.
  icons    ICONBLKs from tools/deskicons.py (EmuTOS desk/icons.c through
           tools/iconconv.py): the drives, the trash -- the desktop copies
           them into its own tree with the label and the letter filled in
           (deskobj.c), which is why the file's text is "" and the char
           word only a colour.

Character positions are `ch(chars, px)` words; a width of 80 is "the
screen's width" to the AES (gemrslib.c fix_chpos) and a ROOT of 80x30
is the 640x240 screen at 8x8.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsc                                  # noqa: E402
from rsc import ch, NIL                     # noqa: E402
from aesref import (G_BOX, G_IBOX, G_STRING, G_BUTTON, G_TITLE, NONE,  # noqa: E402
                    NORMAL, SELECTABLE, DEFAULT, EXIT, DISABLED,
                    OUTLINED, CHECKED, LASTOB)
import deskicons                            # noqa: E402

# -- trees ---------------------------------------------------------------------
ADMENU, ADDINFO = 0, 1

# ADMENU objects, in the donor's names where the donor has the item
ROOT, THEBAR, THEACTIVE = 0, 1, 2
DESKMENU, FILEMENU, VIEWMENU, OPTNMENU = 3, 4, 5, 6
THEDROPS = 7
DESKBOX, ABOUITEM = 8, 9                    # 10 separator, 11..16 accessories
FILEBOX, OPENITEM, SHOWITEM = 17, 18, 19    # 20 separator
NFOLITEM, CLOSITEM, CLSWITEM = 21, 22, 23   # 24 separator
DELTITEM, FORMITEM = 25, 26                 # 27 separator
QUITITEM = 28
VIEWBOX, ICONITEM, TEXTITEM = 29, 30, 31    # 32 separator
NAMEITEM, TYPEITEM, SIZEITEM, DATEITEM, NSRTITEM = 33, 34, 35, 36, 37  # 38
FITITEM = 39
OPTNBOX, IICNITEM, IAPPITEM = 40, 41, 42    # 43 separator
PREFITEM = 44                               # 45 separator
READITEM, SAVEITEM = 46, 47
NOBS_MENU = 48

# ADDINFO objects
DEBOX, DETITLE, DEVERSN, DEOK = 0, 1, 5, 13
NOBS_INFO = 14

# free strings
STDISK, STTRASH = 0, 1

# ICONBLKs, in the order of the table; IG_* name them
IB_HARD, IB_FLOPPY, IB_TRASH, IB_FOLDER, IB_APPL, IB_DOCU = 0, 1, 2, 3, 4, 5
IB_TABLE = ((IB_HARD, deskicons.IG_HARD), (IB_FLOPPY, deskicons.IG_FLOPPY),
            (IB_TRASH, deskicons.IG_TRASH), (IB_FOLDER, deskicons.IG_FOLDER),
            (IB_APPL, deskicons.IG_APPLICATION), (IB_DOCU, deskicons.IG_DOCUMENT))

INDICES = [
    ("ADMENU", ADMENU), ("ADDINFO", ADDINFO),
    ("DESKMENU", DESKMENU), ("FILEMENU", FILEMENU), ("VIEWMENU", VIEWMENU),
    ("OPTNMENU", OPTNMENU), ("DESKBOX", DESKBOX), ("ABOUITEM", ABOUITEM),
    ("OPENITEM", OPENITEM), ("SHOWITEM", SHOWITEM), ("NFOLITEM", NFOLITEM),
    ("CLOSITEM", CLOSITEM), ("CLSWITEM", CLSWITEM), ("DELTITEM", DELTITEM),
    ("FORMITEM", FORMITEM), ("QUITITEM", QUITITEM), ("ICONITEM", ICONITEM),
    ("TEXTITEM", TEXTITEM), ("NAMEITEM", NAMEITEM), ("TYPEITEM", TYPEITEM),
    ("SIZEITEM", SIZEITEM), ("DATEITEM", DATEITEM), ("NSRTITEM", NSRTITEM),
    ("FITITEM", FITITEM), ("IICNITEM", IICNITEM), ("IAPPITEM", IAPPITEM),
    ("PREFITEM", PREFITEM), ("READITEM", READITEM), ("SAVEITEM", SAVEITEM),
    ("DEVERSN", DEVERSN), ("DEOK", DEOK),
    ("STDISK", STDISK), ("STTRASH", STTRASH),
    ("IB_HARD", IB_HARD), ("IB_FLOPPY", IB_FLOPPY), ("IB_TRASH", IB_TRASH),
    ("IB_FOLDER", IB_FOLDER), ("IB_APPL", IB_APPL), ("IB_DOCU", IB_DOCU),
]

# The items the desktop does not do yet: disabled at start (menu_ienable),
# not in the file, so the file stays RCS-shaped.
NOT_YET = (SHOWITEM, NFOLITEM, DELTITEM,
           FORMITEM, TEXTITEM, NAMEITEM, TYPEITEM, SIZEITEM, DATEITEM,
           NSRTITEM, FITITEM, IICNITEM, IAPPITEM, PREFITEM, READITEM,
           SAVEITEM)

# The menu, box by box: (title, box x, box width, items); an item is a
# string, "-" for a separator, and (string, state) for a state.
MENU = [
    (" Desk ", 2, 20, ["  About gem4xe...", "-", "1", "2", "3", "4", "5", "6"]),
    (" File ", 8, 19, ["  Open", "  Show info...", "-", "  New folder...",
                       "  Close folder", "  Close window", "-",
                       "  Delete...", "  Format...", "-", "  Quit"]),
    (" View ", 14, 17, [("  Show as icons", CHECKED), "  Show as text", "-",
                        "  Sort by name", "  Sort by type", "  Sort by size",
                        "  Sort by date", "  No sort", "-", "  Size to fit"]),
    (" Options ", 20, 25, ["  Install icon...", "  Install application...",
                           "-", "  Set preferences...", "-",
                           "  Read .INF file...", "  Save desktop..."]),
]
TITLE_X = [0, 6, 12, 18]                    # the titles, packed on the bar
ACTIVE_W = 27                               # " Desk " to " Options " inclusive

# The About dialog's lines: (text, x, y) in characters, centred by hand
# in a 40-column box the way RCS leaves them.
ABOUT = [
    ("gem4xe Desktop", 13, 1),
    ("GEM for the Atari XL/XE with VBXE,", 3, 2),
    ("Rapidus and Ultimate 1MB", 8, 3),
    ("AES version", 12, 5), ("0.00", 24, 5),           # DEVERSN, filled in
    ("Based on EmuTOS, 'GPLed' GEM sources", 2, 7),
    ("\xbd 1987 Digital Research, Inc.", 4, 8),
    ("\xbd 1999 Caldera Thin Clients, Inc.", 3, 9),
    ("\xbd 2001 Lineo, Inc.", 11, 10),
    ("The EmuTOS development team", 6, 11),
    ("gem4xe is distributed under the GPL", 2, 13),
    ("See COPYING for the details", 6, 14),
]
ABOUT_W, ABOUT_H = 40, 18


def menu_tree(r):
    """ADMENU as RCS lays it out: the bar and the drop-downs are the
    screen's width (80), the bar is hchar+2, the titles hchar+3, the
    drop-down box hangs at hchar+3."""
    objs = [
        (NIL, THEBAR, THEDROPS, G_IBOX, NONE, NORMAL, 0, ch(0), ch(0), ch(80), ch(30)),
        (THEDROPS, THEACTIVE, THEACTIVE, G_BOX, NONE, NORMAL, 0x00001100,
         ch(0), ch(0), ch(80), ch(1, 2)),
        (THEBAR, DESKMENU, OPTNMENU, G_IBOX, NONE, NORMAL, 0,
         ch(2), ch(0), ch(ACTIVE_W), ch(1, 3)),
    ]
    for i, (title, _, _, _) in enumerate(MENU):
        nxt = THEACTIVE if i == len(MENU) - 1 else DESKMENU + i + 1
        objs.append((nxt, NIL, NIL, G_TITLE, NONE, NORMAL, r.string(title),
                     ch(TITLE_X[i]), ch(0), ch(len(title)), ch(1, 3)))
    # the drop-downs' parent: its tail is the LAST BOX, not the last object
    objs.append((ROOT, DESKBOX, OPTNBOX, G_IBOX, NONE, NORMAL, 0,
                 ch(0), ch(1, 3), ch(80), ch(30 - 1)))
    box = THEDROPS + 1
    for i, (_, x, w, items) in enumerate(MENU):
        first, last = box + 1, box + len(items)
        nxt = THEDROPS if i == len(MENU) - 1 else last + 1
        objs.append((nxt, first, last, G_BOX, NONE, NORMAL, 0x00FF1100,
                     ch(x), ch(0), ch(w), ch(len(items))))
        for j, item in enumerate(items):
            state = NORMAL
            if isinstance(item, tuple):
                item, state = item
            if item == "-":
                item, state = "-" * w, DISABLED
            flags = NONE
            if i == len(MENU) - 1 and j == len(items) - 1:
                flags = LASTOB
            nxt = box if j == len(items) - 1 else first + j + 1
            objs.append((nxt, NIL, NIL, G_STRING, flags, state, r.string(item),
                         ch(0), ch(j), ch(w), ch(1)))
        box = last + 1
    assert len(objs) == NOBS_MENU, (len(objs), NOBS_MENU)
    assert objs[ABOUITEM][6].s.startswith("  About")
    assert objs[QUITITEM][6].s == "  Quit"
    assert objs[SAVEITEM][6].s == "  Save desktop..."
    assert objs[DESKBOX][2] - objs[DESKBOX][1] == 7    # eight children
    assert [i for i, o in enumerate(objs) if o[3] == G_BOX and i > THEDROPS] \
        == [DESKBOX, FILEBOX, VIEWBOX, OPTNBOX]
    return r.tree(objs)


def info_tree(r):
    objs = [(NIL, 1, DEOK, G_BOX, NONE, OUTLINED, 0x00021100,
             ch(0), ch(0), ch(ABOUT_W), ch(ABOUT_H))]
    for i, (text, x, y) in enumerate(ABOUT):
        objs.append((i + 2, NIL, NIL, G_STRING, NONE, NORMAL, r.string(text),
                     ch(x), ch(y), ch(len(text)), ch(1)))
    objs.append((ROOT, NIL, NIL, G_BUTTON, SELECTABLE | DEFAULT | EXIT | LASTOB,
                 NORMAL, r.string("OK"), ch(16), ch(ABOUT_H - 2), ch(8), ch(1)))
    assert len(objs) == NOBS_INFO, (len(objs), NOBS_INFO)
    assert objs[DEVERSN][6].s == "0.00"
    return r.tree(objs)


def build():
    r = rsc.Rsc()
    assert menu_tree(r) == ADMENU
    assert info_tree(r) == ADDINFO
    assert r.free_string("DISK") == STDISK
    assert r.free_string("TRASH") == STTRASH
    for ib, ig in IB_TABLE:
        (mask, data, char, xchar, ychar, xicon, yicon, wicon, hicon,
         xtext, ytext, wtext, htext) = deskicons.ICONS[ig]
        # the letter is the desktop's to fill in; the colour word stays
        it = r.iconblk(mask, data, "", wicon, hicon, char=0x1000,
                       xchar=xchar, ychar=ychar, xicon=xicon, yicon=yicon,
                       xtext=xtext, ytext=ytext, wtext=wtext, htext=htext)
        assert r.iconblks.index(it) == ib
    return r


def c_header(data):
    lines = [f"/* {os.path.basename(sys.argv[0])}: DESKTOP.RSC's indices "
             f"(the file is {len(data)} bytes).  Generated -- do not edit. */",
             "#ifndef GEM4XE_DESK_RSC_H", "#define GEM4XE_DESK_RSC_H",
             f"#define DESK_RSC_SIZE {len(data)}"]
    lines += [f"#define {name:<10s} {value}" for name, value in INDICES]
    lines.append("#define NOT_YET_ITEMS { " + ", ".join(
        str(i) for i in NOT_YET) + " }")
    lines.append(f"#define N_NOT_YET {len(NOT_YET)}")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    data = build().file()
    with open(argv[1], "wb") as f:
        f.write(data)
    with open(argv[2], "w") as f:
        f.write(c_header(data))
    print(f"{argv[1]}: {len(data)} bytes, {NOBS_MENU} + {NOBS_INFO} objects, "
          f"{len(IB_TABLE)} icons; {argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
