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
from aesref import (G_BOX, G_IBOX, G_STRING, G_BUTTON, G_TITLE, G_FTEXT,  # noqa: E402
                    NONE, NORMAL, SELECTABLE, DEFAULT, EXIT, DISABLED,
                    EDITABLE, RBUTTON, OUTLINED, CHECKED, LASTOB)
import deskicons                            # noqa: E402

# -- trees ---------------------------------------------------------------------
ADMENU, ADDINFO, ADMKDBOX, ADDELDIA, ADFINFO = 0, 1, 2, 3, 4

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

# ADMKDBOX objects, the donor's names (desk_rsc.h)
MKBOX, MKTITLE, MKNAME, MKOK, MKCNCL = 0, 1, 2, 3, 4
NOBS_MKD = 5

# ADDELDIA objects: the donor's ADCPALER cut to what a delete shows
CDBOX, CDTITLE, CDFILES, CDFOLDS, CDOK, CDCNCL = 0, 1, 2, 3, 4, 5
NOBS_CDEL = 6

# ADFINFO objects: the donor's ADFFINFO (deskinf.c inf_file_folder) with
# its date and time in one field, its folder and file counts in another,
# and no Skip -- Skip is for a dialog shown once per item of a multiple
# selection, and this desktop selects one item at a time.  FIOK and
# FICNCL are adjacent because inf_what reads the two objects after OK.
FIBOX, FITITLE, FINAME, FISIZE, FIDATE = 0, 1, 2, 3, 4
FIFILES, FIFOLDS, FIATTBX = 5, 6, 7
FIRDWR, FIRONLY, FIOK, FICNCL = 8, 9, 10, 11
NOBS_FINF = 12

# Free strings: the icon labels, and every alert the desktop puts up.
# No string a person reads belongs in the C -- an alert written as a
# literal cannot be translated, and the donor does not write them that
# way either: EmuTOS's desktop keeps them in its resource and asks for
# them by index (fun_alert, desk/deskfun.c).  The names are the donor's
# where the donor has the same alert.  docs/shipping.md is the plan the
# rest of it belongs to; the one alert that cannot come from here is
# the one that says the resource is missing.
STDISK, STTRASH = 0, 1
(STNOMEM, STNOWIND, STDEFDIR, STDELFIL, STDELDIR, STFOFAIL, STFO8DEE,
 STDEEPPA, STCPYFIL, STDISKFU, STNOTHIN, STSAMEPL) = range(2, 14)
# The operation dialog's title, by operation: one dialog does the three,
# and which one it is is a string of this file rather than a word of C.
STDELTTL, STCPYTTL, STMOVTTL = 14, 15, 16
# Show info wears two of the same kind, and says so when a rename fails.
STFIINFO, STFOINFO, STRENAME = 17, 18, 19

# (index, name, text) in index order; the alerts as form_alert parses
# them -- [icon][the lines, | between][the buttons]
ALERTS = [
    (STNOMEM,  "STNOMEM",  "[3][There is no memory|for the windows.][ Quit ]"),
    (STNOWIND, "STNOWIND", "[1][There are no more|windows available.][ OK ]"),
    (STDEFDIR, "STDEFDIR", "[1][Failed to set default|directory.][ OK ]"),
    (STDELFIL, "STDELFIL", "[1][That file cannot be deleted.][ OK ]"),
    (STDELDIR, "STDELDIR", "[1][That folder cannot be deleted.][ OK ]"),
    (STFOFAIL, "STFOFAIL", "[1][You cannot create a folder|"
                           "with that name.][ OK ]"),
    (STFO8DEE, "STFO8DEE", "[3][You cannot delete a folder|this far down "
                           "the|directory path.][ OK ]"),
    (STDEEPPA, "STDEEPPA", "[3][A folder in here has|too long a path.][ OK ]"),
    (STCPYFIL, "STCPYFIL", "[1][That file cannot be copied.][ OK ]"),
    (STDISKFU, "STDISKFU", "[1][There is no room on|the disk.][ OK ]"),
    (STNOTHIN, "STNOTHIN", "[1][Nothing is selected|to copy or move.][ OK ]"),
    (STSAMEPL, "STSAMEPL", "[1][That is where it|already is.][ OK ]"),
]

# The three titles the operation dialog wears, in index order after the
# alerts.  Plain strings, not alerts: they go into the dialog's own
# G_STRING, so a translation moves with the rest of the resource.
TITLES = [
    (STDELTTL, "STDELTTL", "DELETE FILE(S)"),
    (STCPYTTL, "STCPYTTL", "COPY FILE(S)"),
    (STMOVTTL, "STMOVTTL", "MOVE FILE(S)"),
    (STFIINFO, "STFIINFO", "FILE INFORMATION"),
    (STFOINFO, "STFOINFO", "FOLDER INFORMATION"),
]

# What Show info says when the DOS will not take the new name: the
# donor's alert, whose second button is the one that gives up.
RENAME_ALERT = [
    (STRENAME, "STRENAME", "[1][That name cannot be used|for this item.]"
                           "[ Retry | Cancel ]"),
]

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
    ("ADMKDBOX", ADMKDBOX), ("MKNAME", MKNAME), ("MKOK", MKOK),
    ("MKCNCL", MKCNCL),
    ("ADDELDIA", ADDELDIA), ("CDTITLE", CDTITLE),
    ("CDFILES", CDFILES), ("CDFOLDS", CDFOLDS),
    ("CDOK", CDOK), ("CDCNCL", CDCNCL),
    ("ADFINFO", ADFINFO), ("FITITLE", FITITLE), ("FINAME", FINAME),
    ("FISIZE", FISIZE), ("FIDATE", FIDATE),
    ("FIFILES", FIFILES), ("FIFOLDS", FIFOLDS),
    ("FIRDWR", FIRDWR), ("FIRONLY", FIRONLY),
    ("FIOK", FIOK), ("FICNCL", FICNCL),
    ("STDISK", STDISK), ("STTRASH", STTRASH),
    *[(name, i) for i, name, _ in ALERTS],
    *[(name, i) for i, name, _ in TITLES],
    *[(name, i) for i, name, _ in RENAME_ALERT],
    ("IB_HARD", IB_HARD), ("IB_FLOPPY", IB_FLOPPY), ("IB_TRASH", IB_TRASH),
    ("IB_FOLDER", IB_FOLDER), ("IB_APPL", IB_APPL), ("IB_DOCU", IB_DOCU),
]

# The items the desktop does not do yet: disabled at start (menu_ienable),
# not in the file, so the file stays RCS-shaped.
NOT_YET = (FORMITEM, TEXTITEM, NAMEITEM, TYPEITEM, SIZEITEM, DATEITEM,
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

# The two dialogs the file operations put up, in the donor's shape
# (ADMKDBOX, and ADCPALER cut to what a delete shows).  The templates'
# underscores size the text buffers rsc.ted() makes, and each buffer
# holds that many spaces in the file, because the AES sets te_txtlen
# from the text's length at load (rsrc.c, the donor's fix_tedinfo_std)
# and an empty buffer would leave the field one character long.  "F" is
# the AES's filename class -- it admits no dot, which is why the donor's
# template has one of its own -- and "9" its digits.
MKD_W, MKD_H = 32, 7
MKD_TMPL, MKD_VALID = "Name: ________.___", "F"    # 11 places, 8 and 3
CDEL_W, CDEL_H = 34, 9
CDEL_FILES = "Number of files:   _____"
CDEL_FOLDS = "Number of folders: _____"
# Show info's fields.  The name is the only editable one, and it is the
# rename: what the dialog leaves in it is what the item is called
# afterwards.  Date and time share a field, and so do the two counts a
# folder has, because a field costs an object and an object costs the
# application pool.
FINF_W, FINF_H = 40, 14
FINF_NAME = "Name: ________.___"                    # 8 places and 3
FINF_SIZE = "Size: ________ bytes"
FINF_DATE = "Date: __-__-__  __:__"                 # DD-MM-YY  HH:MM
FINF_FILES = CDEL_FILES                             # what a folder holds,
FINF_FOLDS = CDEL_FOLDS                             # counted, in the same
                                                    # words the delete uses


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


def mkdir_tree(r):
    """ADMKDBOX: the name of a new folder, typed into an editable field.
    The donor's is 30 characters wide with the field indented one; ours
    is two wider so the template fits our 8-pixel cells."""
    objs = [
        (NIL, MKTITLE, MKCNCL, G_BOX, NONE, OUTLINED, 0x00021100,
         ch(0), ch(0), ch(MKD_W), ch(MKD_H)),
        (MKNAME, NIL, NIL, G_STRING, NONE, NORMAL, r.string("NEW FOLDER"),
         ch(11), ch(1), ch(10), ch(1)),
        (MKOK, NIL, NIL, G_FTEXT, EDITABLE, NORMAL,
         r.ted(" " * MKD_TMPL.count("_"), MKD_TMPL, MKD_VALID),
         ch(6), ch(3), ch(len(MKD_TMPL)), ch(1)),
        (MKCNCL, NIL, NIL, G_BUTTON, SELECTABLE | DEFAULT | EXIT, NORMAL,
         r.string("OK"), ch(5), ch(5), ch(9), ch(1)),
        (ROOT, NIL, NIL, G_BUTTON, SELECTABLE | EXIT | LASTOB, NORMAL,
         r.string("Cancel"), ch(18), ch(5), ch(9), ch(1)),
    ]
    assert len(objs) == NOBS_MKD, (len(objs), NOBS_MKD)
    return r.tree(objs)


def delete_tree(r):
    """ADDELDIA: what a delete is about to do, counted first -- the
    donor's copy/delete dialog with the fields it fills in for a
    delete, and its counts tick down as the walk goes."""
    objs = [
        (NIL, CDTITLE, CDCNCL, G_BOX, NONE, OUTLINED, 0x00021100,
         ch(0), ch(0), ch(CDEL_W), ch(CDEL_H)),
        (CDFILES, NIL, NIL, G_STRING, NONE, NORMAL, r.string("DELETE FILE(S)"),
         ch(10), ch(1), ch(14), ch(1)),        # replaced per operation
        (CDFOLDS, NIL, NIL, G_FTEXT, NONE, NORMAL,
         r.ted(" " * CDEL_FILES.count("_"), CDEL_FILES, "9"),
         ch(5), ch(3), ch(len(CDEL_FILES)), ch(1)),
        (CDOK, NIL, NIL, G_FTEXT, NONE, NORMAL,
         r.ted(" " * CDEL_FOLDS.count("_"), CDEL_FOLDS, "9"),
         ch(5), ch(4), ch(len(CDEL_FOLDS)), ch(1)),
        (CDCNCL, NIL, NIL, G_BUTTON, SELECTABLE | DEFAULT | EXIT, NORMAL,
         r.string("OK"), ch(6), ch(6), ch(9), ch(1)),
        (ROOT, NIL, NIL, G_BUTTON, SELECTABLE | EXIT | LASTOB, NORMAL,
         r.string("Cancel"), ch(19), ch(6), ch(9), ch(1)),
    ]
    assert len(objs) == NOBS_CDEL, (len(objs), NOBS_CDEL)
    return r.tree(objs)


def finfo_tree(r):
    """ADFINFO: what an item is, and the one field that changes it.
    The title is replaced per item (STFIINFO or STFOINFO) and centred
    then, the donor's align_title, so a translation of either length
    sits in the middle of the box.  A file has no counts and a folder
    no attributes, and each disables the fields the other uses."""
    objs = [
        (NIL, FITITLE, FICNCL, G_BOX, NONE, OUTLINED, 0x00021100,
         ch(0), ch(0), ch(FINF_W), ch(FINF_H)),
        (FINAME, NIL, NIL, G_STRING, NONE, NORMAL, r.string("FILE INFORMATION"),
         ch(11), ch(1), ch(18), ch(1)),         # replaced per item
        (FISIZE, NIL, NIL, G_FTEXT, EDITABLE, NORMAL,
         r.ted(" " * FINF_NAME.count("_"), FINF_NAME, "F"),
         ch(4), ch(3), ch(len(FINF_NAME)), ch(1)),
        (FIDATE, NIL, NIL, G_FTEXT, NONE, NORMAL,
         r.ted(" " * FINF_SIZE.count("_"), FINF_SIZE, "9"),
         ch(4), ch(4), ch(len(FINF_SIZE)), ch(1)),
        (FIFILES, NIL, NIL, G_FTEXT, NONE, NORMAL,
         r.ted(" " * FINF_DATE.count("_"), FINF_DATE, "9"),
         ch(4), ch(5), ch(len(FINF_DATE)), ch(1)),
        (FIFOLDS, NIL, NIL, G_FTEXT, NONE, NORMAL,
         r.ted(" " * FINF_FILES.count("_"), FINF_FILES, "9"),
         ch(8), ch(7), ch(len(FINF_FILES)), ch(1)),
        (FIATTBX, NIL, NIL, G_FTEXT, NONE, NORMAL,
         r.ted(" " * FINF_FOLDS.count("_"), FINF_FOLDS, "9"),
         ch(8), ch(8), ch(len(FINF_FOLDS)), ch(1)),
        # the two attributes are radio buttons of one parent: the AES
        # turns the other off by walking that parent's children
        # (src/aes/form.c fm_button), which is what the box is for
        (FIOK, FIRDWR, FIRONLY, G_IBOX, NONE, NORMAL, 0,
         ch(3), ch(10), ch(34), ch(1)),
        (FIRONLY, NIL, NIL, G_BUTTON, SELECTABLE | RBUTTON, NORMAL,
         r.string("Read/Write"), ch(0), ch(0), ch(16), ch(1)),
        (FIATTBX, NIL, NIL, G_BUTTON, SELECTABLE | RBUTTON, NORMAL,
         r.string("Read only"), ch(18), ch(0), ch(16), ch(1)),
        (FICNCL, NIL, NIL, G_BUTTON, SELECTABLE | DEFAULT | EXIT, NORMAL,
         r.string("OK"), ch(7), ch(12), ch(9), ch(1)),
        (ROOT, NIL, NIL, G_BUTTON, SELECTABLE | EXIT | LASTOB, NORMAL,
         r.string("Cancel"), ch(24), ch(12), ch(9), ch(1)),
    ]
    assert len(objs) == NOBS_FINF, (len(objs), NOBS_FINF)
    assert objs[FIOK][3] == G_BUTTON and objs[FICNCL][3] == G_BUTTON
    return r.tree(objs)


def build():
    r = rsc.Rsc()
    assert menu_tree(r) == ADMENU
    assert info_tree(r) == ADDINFO
    assert mkdir_tree(r) == ADMKDBOX
    assert delete_tree(r) == ADDELDIA
    assert finfo_tree(r) == ADFINFO
    assert r.free_string("DISK") == STDISK
    assert r.free_string("TRASH") == STTRASH
    for i, name, text in ALERTS:
        assert r.free_string(text) == i, (name, i)
    for i, name, text in TITLES:
        assert r.free_string(text) == i, (name, i)
    for i, name, text in RENAME_ALERT:
        assert r.free_string(text) == i, (name, i)
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
    print(f"{argv[1]}: {len(data)} bytes, "
          f"{NOBS_MENU} + {NOBS_INFO} + {NOBS_MKD} + {NOBS_CDEL} + "
          f"{NOBS_FINF} objects in five trees, {len(IB_TABLE)} icons; "
          f"{argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
