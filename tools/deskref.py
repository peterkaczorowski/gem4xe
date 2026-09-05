#!/usr/bin/env python3
"""The GEM Desktop, run against the AES model.

src/desk/desktop.c is a program, not a script: what it asks the AES
next depends on what the AES answered last -- the character cell from
graf_handle sizes the icons, the desk from wind_get places them, the
rectangle list drives the redraw, the drive map from Dsetdrv says how
many disks there are, and evnt_multi's answer chooses the branch.  So
the gate (tests/emu/m17_desktop.py) cannot hand the model a script the
way the earlier gates do; it hands the model the desktop, transcribed
here call for call, and the desktop asks the model as it goes.  What
comes out is the same thing the gates always had: the list of records
the desktop made (Desktop.script), the input each wait was given
(Desktop.plan, keyed the way m7_form.drive wants it), and the model's
screen at every ("shot",) step (AES.shots) -- to be compared with the
target, which runs the real DESKTOP.G4A under sh_main.

The addresses are the target's, derived the way src/sys/app.c and
src/aes/rsrc.c derive them from the application pool: the near region
at the first page boundary at or above the pool mark, the desktop's
globals G where the link put them relative to that, the resource at
the first even address after the near region.  The desktop's G4A header
gives the link addresses; build/desktop.sym gives G.  Nothing here is a
number read off a probe.

The screen tree is built as deskobj.c and desktop.c build it, object by
object, and Desktop.globes() packs G as the target lays it out (desk.h),
so the gate can read G back from the target and compare every byte.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aesref                               # noqa: E402
import vdiref                               # noqa: E402
import deskrsc                              # noqa: E402
from aesref import (Obj, Text, Iconblk, Rect,  # noqa: E402
                    G_BOX, G_IBOX, G_ICON, NONE, NORMAL, SELECTED,
                    NIL, ROOT, MAX_DEPTH, ARROW, HOURGLASS,
                    BEG_UPDATE, END_UPDATE, MU_KEYBD, MU_BUTTON, MU_MESAG,
                    MU_TIMER, FMD_START, FMD_FINISH, WF_WXYWH,
                    WF_FIRSTXYWH, WF_NEXTXYWH, WF_NEWDESK, MN_SELECTED,
                    APPL_INIT, APPL_EXIT, EVNT_MULTI, MENU_BAR,
                    MENU_IENABLE, MENU_TNORMAL, OBJC_DRAW, OBJC_FIND,
                    OBJC_CHANGE, FORM_DO, FORM_DIAL, FORM_CENTER,
                    GRAF_HANDLE, WIND_GET, WIND_SET, WIND_FIND, WIND_UPDATE,
                    RSRC_LOAD, RSRC_FREE, RSRC_GADDR, SHEL_WRITE,
                    DSETDRV, DGETDRV)
from rsc import R_TREE, R_ICONBLK, R_STRING, ICONBLK_SIZE  # noqa: E402
from deskrsc import (ADMENU, ADDINFO, DESKMENU, FILEMENU, ABOUITEM,  # noqa: E402
                     QUITITEM, DEVERSN, DEOK, STDISK, STTRASH,
                     IB_HARD, IB_FLOPPY, IB_TRASH, NOT_YET)

GRAF_MOUSE = 1078

# -- desk.h ----------------------------------------------------------------
DESKWH, DROOT, NUM_WNODES = 0, 1, 4
WOBS_START = DROOT + 1 + NUM_WNODES
NUM_ITEMS = 16
NUM_SOBS = WOBS_START + NUM_ITEMS
MAX_DRIVES = 8
MAX_ICONTEXT_WIDTH = 12
LABEL_LEN = MAX_ICONTEXT_WIDTH + 1
DESK_SPEC = 0x00001143
MIN_WINT, MIN_HINT = 4, 2
SHW_SHUTDOWN = 4                            # gem.h
AES_VERSION = 0x0140                        # abi.c: global[0]
SCREENINFO_SIZE = ICONBLK_SIZE + LABEL_LEN
OBJ_SIZE = aesref.OBJ_SIZE
RESULT_INTOUT = vdiref.RESULT_INTOUT

# GLOBES, field by field in the order desk.h declares them; sizes as
# cc65816 lays them out (WORD 2, a near pointer 2, GRECT 8, no padding).
GLOBES = [("a_menu", 2), ("a_info", 2), ("a_iblist", 2), ("g_handle", 2),
          ("g_wchar", 2), ("g_hchar", 2), ("g_wbox", 2), ("g_hbox", 2),
          ("g_desk", 8), ("g_wicon", 2), ("g_hicon", 2), ("g_icw", 2),
          ("g_ich", 2), ("g_screenfree", 2), ("g_rmsg", 16),
          ("g_screen", NUM_SOBS * OBJ_SIZE),
          ("g_screeninfo", NUM_ITEMS * SCREENINFO_SIZE)]
GLOBES_SIZE = sum(n for _, n in GLOBES)


def g_offset(name):
    off = 0
    for field, size in GLOBES:
        if field == name:
            return off
        off += size
    raise KeyError(name)


def w(x):
    """A WORD as the target stores it."""
    return (x & 0xFFFF).to_bytes(2, "little")


class NeedsInput(Exception):
    """The desktop is in a wait the gate gave no input for."""


class Desktop:
    """desktop.c against the model.  `inputs` is a list of step producers,
    one per wait that blocks for input (evnt_multi with MU_BUTTON, form_do)
    in the order the desktop enters them; each is called with the Desktop
    just before the wait and returns the plan steps for it -- so it can
    ask the model where things are at that moment."""

    def __init__(self, v, a, mark, link_near, near_size, g_link, drvmap, inputs):
        self.v, self.a = v, a
        # app_load: pool_alloc(near_size, 0x100), then rs_load's
        # pool_alloc(size, 2) (src/sys/app.c, src/aes/rsrc.c)
        self.near = (mark + 0xFF) & ~0xFF
        self.G = self.near + (g_link - link_near)
        self.rsc_base = (self.near + near_size + 1) & ~1
        self.g_screen_addr = self.G + g_offset("g_screen")
        self.g_screeninfo_addr = self.G + g_offset("g_screeninfo")
        a.dos_drvmap = drvmap
        self.inputs = list(inputs)
        self.script, self.plan, self.waits = [], {}, []
        # G as the program keeps it
        self.a_menu = self.a_info = self.a_iblist = 0
        self.handle = self.wchar = self.hchar = self.wbox = self.hbox = 0
        self.desk = Rect()
        self.wicon = self.hicon = self.icw = self.ich = 0
        self.screenfree = 0
        self.rmsg = [0] * 8
        self.screen = [Obj(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) for _ in range(NUM_SOBS)]
        self.info = [None] * NUM_ITEMS      # (Iconblk, Text) per item
        self.rsc = None

    # -- the ABI -----------------------------------------------------------
    def call(self, op, ints=(), pts=(), tree=None, steps=None):
        """One call: the record goes on the script, its input on the plan,
        the model answers now.  Returns (intout, ptsout)."""
        rec = (op, tuple(pts), tuple(ints))
        if tree is not None:
            rec += (tree,)
        i = len(self.script)
        self.script.append(rec)
        if steps:
            self.plan[i] = list(steps)
        res = aesref.resume(self.v, self.a, [rec],
                            {0: list(steps)} if steps else None)[0]
        return list(res[2:2 + RESULT_INTOUT]), list(res[2 + RESULT_INTOUT:])

    def wait(self, flags, clicks, mask, state, ms, blocking):
        """evnt_multi as desktop.c calls it: no mouse rectangles, the
        message into G.g_rmsg.  Returns (which, mx, my, button, kstate,
        kret, bret)."""
        steps = None
        if blocking:
            if not self.inputs:
                raise NeedsInput(f"call {len(self.script)}: evnt_multi "
                                 f"{flags:#x} with no input left")
            self.waits.append(len(self.script))
            steps = self.inputs.pop(0)(self)
        io, _ = self.call(EVNT_MULTI,
                          (flags, clicks, mask, state) + (0,) * 10
                          + (ms & 0xFFFF, ms >> 16), steps=steps)
        if io[0] & MU_MESAG:
            self.rmsg = list(io[7:15])
        return io[0:7]

    # -- dialogs -------------------------------------------------------------
    def start_dialog(self, tree):
        io, po = self.call(FORM_CENTER, tree=tree)
        self.dlg = Rect(io[0], io[1], io[2], po[0])
        d = self.dlg
        self.call(FORM_DIAL, (FMD_START, 0, 0, 0, 0, d.x, d.y, d.w, d.h))
        self.call(OBJC_DRAW, (ROOT, MAX_DEPTH), (d.x, d.y, d.w, d.h), tree=tree)

    def end_dialog(self):
        d = self.dlg
        self.call(FORM_DIAL, (FMD_FINISH, 0, 0, 0, 0, d.x, d.y, d.w, d.h))

    def busy(self, on):
        self.call(WIND_UPDATE, (BEG_UPDATE,))
        self.call(GRAF_MOUSE, (HOURGLASS if on else ARROW,))
        self.call(WIND_UPDATE, (END_UPDATE,))

    # -- the resource (rsrc.c on the target, tools/rsc.py here) -------------
    def rsrc_load(self):
        self.call(RSRC_LOAD)
        a = self.a
        r = deskrsc.build()
        image, trees, mem = r.expect(self.rsc_base, self.wchar, self.hchar,
                                     a.gl_width)
        self.rsc, self.rsc_image = r, image
        for t, objs in enumerate(trees):
            a.trees[r.addr(R_TREE, t, self.rsc_base)] = objs
        a.mem.update(mem)
        return 1

    def rsrc_gaddr(self, rtype, index):
        self.call(RSRC_GADDR, (rtype, index))
        return self.rsc.addr(rtype, index, self.rsc_base)

    def set_version(self):
        """AES_VERSION over the resource's "0.00", as global[0] has it."""
        spec = self.a.trees[self.a_info][DEVERSN].ob_spec
        old = self.a.mem[spec]
        g = AES_VERSION
        s = list(old.s)
        s[0] = "0123456789ABCDEF"[(g >> 8) & 15]
        s[2] = "0123456789ABCDEF"[(g >> 4) & 15]
        s[3] = "0123456789ABCDEF"[g & 15]
        self.a.mem[spec] = Text("".join(s), old.size)

    # -- the screen tree (deskobj.c) ---------------------------------------
    def r_set(self, obj, x, y, w_, h):
        o = self.screen[obj]
        o.ob_x, o.ob_y, o.ob_width, o.ob_height = x, y, w_, h

    def obj_add(self, parent, obj):
        pp = self.screen[parent]
        last = pp.ob_tail
        self.screen[obj].ob_next = parent
        if last == NIL:
            pp.ob_head = obj
        else:
            self.screen[last].ob_next = obj
        pp.ob_tail = obj

    def obj_init(self):
        for i in range(WOBS_START):
            o = self.screen[i]
            o.ob_head = o.ob_next = o.ob_tail = NIL
        for i in range(WOBS_START, NUM_SOBS - 1):
            self.screen[i].ob_next = i + 1
        self.screen[NUM_SOBS - 1].ob_next = NIL
        self.screenfree = WOBS_START
        self.screen[ROOT] = Obj(NIL, NIL, NIL, G_IBOX, NONE, NORMAL, 0, 0, 0, 0, 0)
        self.r_set(ROOT, 0, 0, self.desk.x + self.desk.w, self.desk.y + self.desk.h)
        for i in range(NUM_WNODES + 1):
            self.screen[DROOT + i] = Obj(NIL, NIL, NIL, G_BOX, NONE, NORMAL,
                                         DESK_SPEC, 0, 0, 0, 0)
            self.obj_add(ROOT, DROOT + i)
        self.a.trees[self.g_screen_addr] = self.screen

    def obj_wfree(self, obj, x, y, w_, h):
        win = self.screen[obj]
        self.r_set(obj, x, y, w_, h)
        if win.ob_head >= WOBS_START:
            oldfree = self.screenfree
            self.screenfree = win.ob_head
            i = win.ob_head
            while True:
                item = self.screen[i]
                if item.ob_next < WOBS_START:
                    item.ob_next = oldfree
                    break
                i = item.ob_next
        win.ob_head = win.ob_tail = NIL

    def obj_ialloc(self, wparent, x, y, w_, h):
        objnum = self.screenfree
        if objnum < WOBS_START:
            return 0
        o = self.screen[objnum]
        self.screenfree = o.ob_next
        o.ob_next = o.ob_head = o.ob_tail = NIL
        self.obj_add(wparent, objnum)
        self.r_set(objnum, x, y, w_, h)
        return objnum

    def info_addr(self, obj):
        return self.g_screeninfo_addr + (obj - WOBS_START) * SCREENINFO_SIZE

    # -- the desk (desktop.c) ------------------------------------------------
    def snap_icon(self, gx, gy):
        columns = self.desk.w // self.icw
        rows = self.desk.h // self.ich
        cx = min(gx, columns - 1)
        cy = min(gy, rows - 1)
        spare = self.desk.w - columns * self.icw
        px = cx * self.icw + spare // columns
        spare = self.desk.h - rows * self.ich
        py = cy * self.ich + spare // rows + self.desk.y
        return px, py

    def desk_icon(self, gx, gy, which, label, letter):
        x, y = self.snap_icon(gx, gy)
        obid = self.obj_ialloc(DROOT, x, y, self.wicon, self.hicon)
        if not obid:
            return 0
        o = self.screen[obid]
        o.ob_state, o.ob_flags, o.ob_type = NORMAL, NONE, G_ICON
        src = self.a.mem[self.a_iblist + which * ICONBLK_SIZE]
        addr = self.info_addr(obid)
        ib = Iconblk(src.pmask, src.pdata, src.ptext, src.char, src.xchar,
                     src.ychar, Rect(src.icon.x, src.icon.y, src.icon.w, src.icon.h),
                     Rect(src.text.x, src.text.y, src.text.w, src.text.h))
        o.ob_spec = addr
        ib.icon.x = (self.wicon - ib.icon.w) // 2
        ib.text.y = ib.icon.h
        ib.text.w = MAX_ICONTEXT_WIDTH * self.wchar
        ib.text.h = self.hchar + 2
        ib.char = (ib.char & 0xFF00) | letter
        text = Text(label[:LABEL_LEN - 1], LABEL_LEN)
        ib.ptext = addr + ICONBLK_SIZE
        self.info[obid - WOBS_START] = (ib, text)
        self.a.mem[addr] = ib
        self.a.mem[addr + ICONBLK_SIZE] = text
        return obid

    def desk_build(self):
        a = self.a
        ib0 = a.mem[self.a_iblist]
        self.wicon = MAX_ICONTEXT_WIDTH * self.wchar + 2 * ib0.text.x
        self.hicon = ib0.icon.h + self.hchar + 2
        xcnt = self.desk.w // (self.wicon + MIN_WINT)
        self.icw = self.desk.w // xcnt
        ycnt = self.desk.h // (self.hicon + MIN_HINT)
        self.ich = self.desk.h // ycnt
        self.obj_wfree(DROOT, 0, 0, self.desk.x + self.desk.w,
                       self.desk.y + self.desk.h)
        self.screen[DROOT].ob_spec = DESK_SPEC
        disk = a.mem[self.rsrc_gaddr(R_STRING, STDISK)].s
        trash = a.mem[self.rsrc_gaddr(R_STRING, STTRASH)].s
        io, _ = self.call(DGETDRV)
        io, _ = self.call(DSETDRV, (io[0],))
        drvmap = io[0] & 0xFFFF
        n = 0
        for drive in range(MAX_DRIVES):
            if not drvmap & (1 << drive):
                continue
            gx, gy = n % xcnt, n // xcnt
            label = disk[:LABEL_LEN - 3] + " " + chr(ord("A") + drive)
            self.desk_icon(gx, gy, IB_HARD if drive > 1 else IB_FLOPPY,
                           label, ord("A") + drive)
            n += 1
        gx, gy = 0, ycnt - 1
        if n and (n - 1) // xcnt >= gy:
            gx = xcnt - 1
        self.desk_icon(gx, gy, IB_TRASH, trash, 0)

    def desk_redraw(self, obj, pc):
        io, _ = self.call(WIND_GET, (DESKWH, WF_FIRSTXYWH))
        r = Rect(*io[1:5])
        while r.w and r.h:
            x0, y0 = max(r.x, pc.x), max(r.y, pc.y)
            x1 = min(r.x + r.w, pc.x + pc.w)
            y1 = min(r.y + r.h, pc.y + pc.h)
            if x0 < x1 and y0 < y1:
                self.call(OBJC_DRAW, (obj, MAX_DEPTH), (x0, y0, x1 - x0, y1 - y0),
                          tree=self.g_screen_addr)
            io, _ = self.call(WIND_GET, (DESKWH, WF_NEXTXYWH))
            r = Rect(*io[1:5])

    def desk_select(self, obj):
        d = self.desk
        i = self.screen[DROOT].ob_head
        while i >= WOBS_START:
            state = self.screen[i].ob_state & 0xFFFF
            want = (state | SELECTED) if i == obj else (state & ~SELECTED)
            if want != state:
                self.call(OBJC_CHANGE, (i, want, 1), (d.x, d.y, d.w, d.h),
                          tree=self.g_screen_addr)
            i = self.screen[i].ob_next

    # -- the menu ------------------------------------------------------------
    def do_deskmenu(self, item):
        if item == ABOUITEM:
            tree = self.a_info
            self.start_dialog(tree)
            self.waits.append(len(self.script))
            if not self.inputs:
                raise NeedsInput(f"call {len(self.script)}: form_do with no input left")
            steps = self.inputs.pop(0)(self)
            self.call(FORM_DO, (ROOT,), tree=tree, steps=steps)
            self.a.trees[tree][DEOK].ob_state = NORMAL
            self.end_dialog()
        return False

    def do_filemenu(self, item):
        if item == QUITITEM:
            self.call(SHEL_WRITE, (SHW_SHUTDOWN, 0, 0))
            return True
        return False

    def hndl_menu(self, title, item):
        done = False
        if title == DESKMENU:
            done = self.do_deskmenu(item)
        elif title == FILEMENU:
            done = self.do_filemenu(item)
        self.call(MENU_TNORMAL, (title, 1), tree=self.a_menu)
        return done

    # -- events --------------------------------------------------------------
    def hndl_button(self, clicks, mx, my):
        io, _ = self.call(WIND_FIND, (mx, my))
        if io[0] != DESKWH:
            return False
        io, _ = self.call(OBJC_FIND, (DROOT, MAX_DEPTH), (mx, my),
                          tree=self.g_screen_addr)
        obj = io[0]
        self.desk_select(obj if obj >= WOBS_START else 0)
        return False

    def hndl_msg(self):
        msg = self.rmsg
        if msg[0] == MN_SELECTED:
            return self.hndl_menu(msg[3], msg[4])
        return False

    def main(self):
        a = self.a
        self.call(APPL_INIT)
        io, _ = self.call(GRAF_HANDLE)
        self.handle, self.wchar, self.hchar, self.wbox, self.hbox = io[0:5]
        io, _ = self.call(WIND_GET, (DESKWH, WF_WXYWH))
        self.desk = Rect(*io[1:5])
        self.busy(True)
        self.rsrc_load()
        self.a_menu = self.rsrc_gaddr(R_TREE, ADMENU)
        self.a_info = self.rsrc_gaddr(R_TREE, ADDINFO)
        self.a_iblist = self.rsrc_gaddr(R_ICONBLK, 0)
        self.set_version()
        for item in NOT_YET:
            self.call(MENU_IENABLE, (item, 0), tree=self.a_menu)
        self.obj_init()
        self.desk_build()
        self.call(WIND_SET, (DESKWH, WF_NEWDESK, 0, self.g_screen_addr, DROOT, 0))
        self.call(WIND_UPDATE, (BEG_UPDATE,))
        self.desk_redraw(DROOT, self.desk)
        self.call(MENU_BAR, (1,), tree=self.a_menu)
        self.call(WIND_UPDATE, (END_UPDATE,))
        self.busy(False)

        done = False
        while not done:
            which, mx, my, button, kstate, kret, bret = self.wait(
                MU_BUTTON | MU_MESAG | MU_KEYBD, 2, 1, 1, 0, True)
            self.call(WIND_UPDATE, (BEG_UPDATE,))
            if which & MU_BUTTON:
                if self.hndl_button(bret, mx, my):
                    done = True
            while (which & MU_MESAG) and not done:
                if self.hndl_msg():
                    done = True
                which, mx, my, button, kstate, kret, bret = self.wait(
                    MU_MESAG | MU_TIMER, 2, 1, 1, 0, False)
            self.call(WIND_UPDATE, (END_UPDATE,))

        self.call(MENU_BAR, (0,), tree=self.a_menu)
        self.call(WIND_SET, (DESKWH, WF_NEWDESK, 0, 0, ROOT, 0))
        self.call(RSRC_FREE)
        self.call(APPL_EXIT)
        return 0

    # -- G, as the target lays it out ----------------------------------------
    def globes(self):
        """G packed as desk.h declares it: what a dump of the target's G
        should read at the same moment."""
        d = self.desk
        out = b"".join(w(x) for x in (
            self.a_menu, self.a_info, self.a_iblist, self.handle,
            self.wchar, self.hchar, self.wbox, self.hbox,
            d.x, d.y, d.w, d.h, self.wicon, self.hicon, self.icw, self.ich,
            self.screenfree)) + b"".join(w(x) for x in self.rmsg)
        assert len(out) == g_offset("g_screen"), len(out)
        out += b"".join(o.pack() for o in self.screen)
        for entry in self.info:
            if entry is None:
                out += bytes(SCREENINFO_SIZE)
            else:
                ib, text = entry
                out += ib.pack() + text.pack()
        assert len(out) == GLOBES_SIZE, len(out)
        return out

    # -- where things are, for the gate's plans ------------------------------
    def centre(self, tree, obj):
        a = self.a
        a.tree = a.trees[tree]
        r = a.ob_actxywh(obj)
        return (r.x + r.w // 2, r.y + r.h // 2)

    def pointer(self):
        return (self.v.ptr_x, self.v.ptr_y)


def describe(d):
    """One line per record, for a log."""
    for i, rec in enumerate(d.script):
        mark = "*" if i in d.plan else " "
        print(f"{mark}{i:3d} {rec[0]} {rec[2]}{' @' + hex(rec[3]) if len(rec) > 3 else ''}")
