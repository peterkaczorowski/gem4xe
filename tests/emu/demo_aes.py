#!/usr/bin/env python3
"""A session with the AES itself, on the emulated machine, one frame at a
time: build/movie/gem4xe.mp4 and .gif.

This is not the mock-up that `make demo` paints through the VDI.  The
menu bar, the drop-down, the About box, the window and its gadgets here
are drawn by gem4xe's AES running on the emulated Rapidus and VBXE, from
the pointer and button the harness feeds it -- the same way the gates
drive it -- and the session is checked as a gate is: every returned word
against tools/aesref.py, the screen at each ("shot") step, and the last
screen.  A frame that looks right but is not what the model draws fails
the run.

The session, as an application would write it: a desktop tree with two
icons is handed to the AES (WF_NEWDESK) and drawn, the menu bar shown;
the pointer walks to Desk and pulls About, which is answered with
form_dial/form_do on a dialog; a double-click on the floppy icon opens a
window whose contents are drawn through the shared VDI in reply to
WM_REDRAW; the window is dragged by its name, sized by its sizer, covered
by the About box again (form_dial(FMD_FINISH) sends the window a
WM_REDRAW -- the donor's shape), filled with the fuller and closed with
the closer.  Longer than the runner's buffers, so it is fed to the target
in chapters; the model runs it whole.

  python3 tests/emu/demo_aes.py [--dry] [--keep-frames] [--no-encode]

--dry runs only the model, and writes what it drew at each shot to
build/movie/dry-*.png: the look of the session before the emulator's time
is spent on it.
"""
import copy
import math
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from a8test.launcher import launch          # noqa: E402
import vbxeref, vdiref, aesref, symfile     # noqa: E402
from aesref import (Layout, Obj, NIL, G_BOX, G_IMAGE, G_STRING, G_BUTTON,  # noqa: E402
                    SELECTABLE, EXIT, DEFAULT, LASTOB, SELECTED,
                    MU_MESAG, MU_TIMER, MU_BUTTON, MAX_DEPTH,
                    FMD_START, FMD_GROW, FMD_SHRINK, FMD_FINISH,
                    WM_REDRAW, WM_MOVED, WM_SIZED, WM_FULLED, WM_CLOSED,
                    MN_SELECTED, WF_NAME, WF_INFO, WF_NEWDESK, WF_CXYWH,
                    WF_VSLSIZ, WF_HSLSIZ, W_NAME, W_SIZER, W_FULLER, W_CLOSER,
                    W_WORK, OBJC_DRAW, OBJC_FIND, OBJC_CHANGE, FORM_DO,
                    FORM_DIAL, GRAF_GROWBOX, GRAF_SHRINKBOX, EVNT_MESAG,
                    WIND_CLOSE, WIND_DELETE, WHITE, BLACK)
from vdiref import (V_HIDE_C, V_SHOW_C, VS_CLIP, VSWR_MODE, VSF_COLOR,  # noqa: E402
                    VSF_INTERIOR, VSF_STYLE, VST_COLOR, VSL_COLOR, VR_RECFL,
                    VRT_CPYFM, V_GTEXT, MD_REPLACE, FIS_SOLID, VSC_FORM)
from m4_aes import mem_diff, PRELUDE                                # noqa: E402
from m7_form import (F, M, B, multi, poke16, drive, compare, DCLICK,  # noqa: E402
                     NOT_STARTED, STATUS, ST_GO, ST_DONE, DISK, SYMS)
from m8_wind import (Script, RELEASE, create, wopen, wset, setaddr,  # noqa: E402
                     MESAG, ALL, FULL)
from m9_menu import (menu, bar, tnormal, N_OBJS, T_DESK, ABOUT)      # noqa: E402
from demo_desktop import ICON_BITS, ICON_W, ICON_H, ICON_WDW         # noqa: E402

MOVIEDIR = os.path.join(ROOT, "build", "movie")
FRAMEDIR = os.path.join(MOVIEDIR, "frames")
MAX_RECORDS = 48            # src/m3_vdi.c MAX_RESULTS: a chapter's records
FILES = ("AES", "VDI", "DOCS", "TESTS")     # what the floppy holds

# the GEM arrow (EmuTOS aes/mforms.c, GPLv2): hot spot, planes, mask and
# data colours, then the mask (drawn first) and the data over it
ARROW = ((0, 0, 1, 0, 1)
         + (0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80,
            0xFFC0, 0xFFE0, 0xFE00, 0xEF00, 0xCF00, 0x8780, 0x0780, 0x0380)
         + (0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
            0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000))


# -- the trees ----------------------------------------------------------------

# the desktop's objects, by index
D_FLOPPY, D_FLOPPY_ICON, D_TRASH = 1, 2, 4


def desktop(L):
    """The desktop: the patterned screen with two icons on it, each a
    white box holding a folder image and its name.  wind_set(WF_NEWDESK)
    hands it to the AES, which draws it under everything (the menu bar
    clips it)."""
    return [
        #   next  head  tail  type      flags   state  spec              x    y    w    h
        Obj(NIL,     1,    4, G_BOX,    0,      0,     0x00001148,       0,   0, 640, 240),
        Obj(4,       2,    3, G_BOX,    0,      0,     0x00001170,      32,  56,  48,  40),
        Obj(3,     NIL,  NIL, G_IMAGE,  0,      0,     L.bitblk(ICON_BITS, ICON_WDW * 2, ICON_H, color=BLACK),
            8, 2, ICON_W, ICON_H),
        Obj(1,     NIL,  NIL, G_STRING, 0,      0,     L.text("FLOPPY"),   0,  30,  48,   8),
        Obj(0,       5,    6, G_BOX,    0,      0,     0x00001170,      32, 166,  48,  40),
        Obj(6,     NIL,  NIL, G_IMAGE,  0,      0,     L.bitblk(ICON_BITS, ICON_WDW * 2, ICON_H, color=BLACK),
            8, 2, ICON_W, ICON_H),
        Obj(4,     NIL,  NIL, G_STRING, LASTOB, 0,     L.text("TRASH"),    4,  30,  40,   8),
    ]


A_OK = 4


def about(L):
    """The About box: a shadowed dialog with three lines and OK."""
    return [
        Obj(NIL,  1, A_OK, G_BOX,    0, 0, 0x00021100, 160, 60, 320, 120),
        Obj(2,  NIL, NIL, G_STRING, 0, 0, L.text("gem4xe  -  GEM for the Atari 8-bit"), 24, 16, 272, 8),
        Obj(3,  NIL, NIL, G_STRING, 0, 0, L.text("VDI and AES on a 65C816 with VBXE"), 24, 32, 264, 8),
        Obj(4,  NIL, NIL, G_STRING, 0, 0, L.text("GPLv2, from EmuTOS"), 24, 48, 144, 8),
        Obj(0,  NIL, NIL, G_BUTTON, SELECTABLE | EXIT | DEFAULT | LASTOB, 0,
            L.text("OK"), 120, 84, 80, 20),
    ]


# -- the session --------------------------------------------------------------

def path(src, dst, speed=10):
    """Pointer moves from src to dst, one a frame, eased at both ends and
    never more than `speed` pixels apart in the middle."""
    dx, dy = dst[0] - src[0], dst[1] - src[1]
    n = max(2, math.ceil(math.hypot(dx, dy) / speed * 1.5))
    out = []
    for i in range(1, n + 1):
        t = i / n
        t = t * t * (3 - 2 * t)
        out.append(M(round(src[0] + dx * t), round(src[1] + dy * t)))
    return out


def wait(ms):
    return multi(MU_MESAG | MU_TIMER, ms=ms)


def SHOT():
    return ("shot", None)


class Scratch:
    """The reference run over the session so far, on copies of the trees:
    where a title, an item, an icon or a gadget is, and what the last
    wait returned, are asked of the model rather than restated here."""

    def __init__(self, L, objs, L2, objs2, L3, objs3):
        self.L, self.objs, self.L2, self.objs2 = L, objs, L2, objs2
        self.L3, self.objs3 = L3, objs3
        self.mfdb = (ICON_BITS, ICON_WDW)

    def mem(self):
        mem = dict(self.L.mem)
        mem.update(self.L2.mem)
        mem.update(self.L3.mem)
        return mem

    def run(self, script, pointer=(0, 0), plan=None):
        objs, mem, objs2, objs3 = copy.deepcopy(
            (self.objs, self.mem(), self.objs2, self.objs3))
        if plan is None:
            plan = {k + len(PRELUDE): v for k, v in
                    getattr(script, "plan", {}).items()}
        return aesref.run(PRELUDE + list(script), objs, mem, plan=plan,
                          pointer=pointer,
                          trees={self.L2.base: objs2, self.L3.base: objs3})

    def aes(self, script):
        return self.run(script)[1]

    def centre(self, script, obj, tree=None):
        a = self.aes(script)
        if tree is not None:
            a.tree = a.trees[tree]
        r = a.ob_actxywh(obj)
        return (r.x + r.w // 2, r.y + r.h // 2)

    def item(self, script, title, obj):
        a = self.aes(script)
        a.tree = a.trees[self.L.base] if self.L.base in a.trees else a.home
        assert a.menu_sub(title) != NIL
        r = a.ob_actxywh(obj)
        return (r.x + r.w // 2, r.y + r.h // 2)

    def rect(self, script, wh, obj):
        a = self.aes(script)
        a.tree = a.W_ACTIVE
        a.w_bldactive(wh)
        return a.ob_actxywh(obj)

    def gadget(self, script, wh, obj):
        r = self.rect(script, wh, obj)
        return (r.x + r.w // 2, r.y + r.h // 2)

    def message(self, script):
        rec = self.run(script)[2][-1]
        return rec[2:10] if script[-1][0] == EVNT_MESAG else rec[9:17]

    def queue(self, script):
        return list(self.aes(script).gl_queue)


def contents(s, script, wh):
    """The window's contents, as an application draws them through the
    shared VDI in reply to WM_REDRAW: clipped to the work area, and the
    attributes the AES caches put back to what it believes they are."""
    a = s.aes(script)
    r = s.rect(script, wh, W_WORK)
    x2, y2 = r.x + r.w - 1, r.y + r.h - 1
    recs = [(V_HIDE_C,),
            (VS_CLIP, (r.x, r.y, x2, y2), (1,)),
            (VSWR_MODE, (), (MD_REPLACE,)),
            (VSF_INTERIOR, (), (FIS_SOLID,)),
            (VSF_COLOR, (), (WHITE,)),
            (VR_RECFL, (r.x, r.y, x2, y2), ()),
            (VST_COLOR, (), (BLACK,))]
    for i, name in enumerate(FILES):
        ix, iy = r.x + 24 + i * 80, r.y + 12
        recs.append((VRT_CPYFM,
                     (0, 0, ICON_W - 1, ICON_H - 1, ix, iy, ix + ICON_W - 1, iy + ICON_H - 1),
                     (MD_REPLACE, BLACK, WHITE), s.mfdb))
        tx = ix + (ICON_W - 8 * len(name)) // 2
        recs.append((V_GTEXT, (tx, iy + ICON_H + 4 + 7), tuple(name.encode())))
    for op, val in ((VSWR_MODE, a.gl_mode), (VST_COLOR, a.gl_tcolor),
                    (VSL_COLOR, a.gl_lcolor), (VSF_INTERIOR, a.gl_fis),
                    (VSF_STYLE, a.gl_patt)):
        if val != -1:
            recs.append((op, (), (val,)))
    recs.append((V_SHOW_C, (), (1,)))
    return recs


def answer(s, b, wh):
    """Take every message the queue holds, drawing the window's contents
    for each WM_REDRAW."""
    while s.queue(b):
        msg = s.queue(b)[0]
        b.append(MESAG)
        if msg[0] == WM_REDRAW:
            b.extend(contents(s, b, wh))


def about_box(s, b, cur, shots):
    """Desk -> About: the drop-down pulled by the pointer, the item
    pressed (MN_SELECTED at the press), the release handed back; then
    the dialog grown, drawn, run to OK, shrunk and finished."""
    L3 = s.L3
    title = s.centre(b, T_DESK)
    # straight down from the title into its drop-down: a slant would
    # cross into File's title first and drop that menu instead
    item = (title[0], s.item(b, T_DESK, ABOUT)[1])
    b.op(wait(8000),
         F(3), *path(cur, title), F(8), SHOT(),
         *path(title, item, speed=4), F(8), SHOT(), B(1))
    shots.extend(["desk-menu", "about-item"])
    msg = s.message(b)
    assert msg[:5] == (MN_SELECTED, 0, 0, T_DESK, ABOUT), msg
    b.append(tnormal(T_DESK, 1))
    b.op(multi(MU_BUTTON, 1, 1, 0), *RELEASE)
    big = (160, 60, 320, 120)
    small = (300, 100, 40, 40)
    b.append((FORM_DIAL, (), (FMD_START,) + small + big))
    b.append((FORM_DIAL, (), (FMD_GROW,) + small + big))
    b.append((OBJC_DRAW, (0, 0, 640, 240), (0, MAX_DEPTH), L3.base))
    ok = s.centre(b, A_OK, tree=L3.base)
    cur = item
    b.op((FORM_DO, (), (0,), L3.base),
         F(3), *path(cur, ok), F(10), SHOT(), B(1), F(14), B(0))
    shots.append("about")
    b.append((FORM_DIAL, (), (FMD_SHRINK,) + small + big))
    b.append((FORM_DIAL, (), (FMD_FINISH,) + small + big))
    # form_do leaves the exit button SELECTED; the application clears it
    # (no redraw -- the dialog is gone) or the next click would deselect
    b.append((OBJC_CHANGE, (0, 0, 640, 240), (A_OK, 0, 0), L3.base))
    return ok


def session(s):
    L, L2, L3 = s.L, s.L2, s.L3
    b = Script()
    shots = []
    b.extend([setaddr(0, WF_NEWDESK, L2.base),
              (OBJC_DRAW, (0, 0, 640, 240), (0, MAX_DEPTH), L2.base),
              bar(1),
              # the pointer: the VDI starts with it hidden, the AES hides
              # and shows around its drawing, so the application shows it
              (VSC_FORM, (), ARROW), (V_SHOW_C, (), (0,))])
    cur = (0, 0)

    # 1. About, from the Desk menu
    cur = about_box(s, b, cur, shots)

    # 2. a double-click on the floppy opens a window
    icon = s.centre(b, D_FLOPPY_ICON, tree=L2.base)
    b.op(multi(MU_MESAG | MU_BUTTON | MU_TIMER, 2, 1, 1, ms=8000),
         F(3), *path(cur, icon), F(10), *DCLICK(icon))
    cur = icon
    b.append((OBJC_FIND, icon, (0, MAX_DEPTH), L2.base))
    b.append((OBJC_CHANGE, (0, 0, 640, 240), (D_FLOPPY_ICON, SELECTED, 1), L2.base))
    a = s.aes(b)
    a.tree = a.trees[L2.base]
    ir = a.ob_actxywh(D_FLOPPY)
    win = (140, 40, 360, 130)
    b.append((GRAF_GROWBOX, (), (ir.x, ir.y, ir.w, ir.h) + win))
    b.append(create(ALL))
    wh = 1
    b.append(setaddr(wh, WF_NAME, L3.text(" FLOPPY DISK ")))
    b.append(setaddr(wh, WF_INFO, L3.text(f" {len(FILES)} folders ")))
    b.append(wset(wh, WF_VSLSIZ, 1000))
    b.append(wset(wh, WF_HSLSIZ, 1000))
    b.append(wopen(wh, *win))
    answer(s, b, wh)
    b.append((OBJC_CHANGE, (0, 0, 640, 240), (D_FLOPPY_ICON, 0, 1), L2.base))

    # 3. drag it by its name
    grip = s.gadget(b, wh, W_NAME)
    dest = (grip[0] + 90, grip[1] + 36)
    b.op(wait(8000),
         F(3), *path(cur, grip), F(6), B(1), F(3), *path(grip, dest, speed=8),
         F(6), SHOT(), B(0))
    shots.append("drag")
    cur = dest
    msg = s.message(b)
    assert msg[0] == WM_MOVED, msg
    b.append(wset(wh, WF_CXYWH, *msg[4:8]))
    answer(s, b, wh)

    # 4. size it by its sizer
    grip = s.gadget(b, wh, W_SIZER)
    dest = (grip[0] + 40, grip[1] + 30)
    b.op(wait(8000),
         F(3), *path(cur, grip), F(6), B(1), F(3), *path(grip, dest, speed=8),
         F(6), SHOT(), B(0))
    shots.append("size")
    cur = dest
    msg = s.message(b)
    assert msg[0] == WM_SIZED, msg
    b.append(wset(wh, WF_CXYWH, *msg[4:8]))
    answer(s, b, wh)

    # 5. About again, over the window: FMD_FINISH sends it a WM_REDRAW
    cur = about_box(s, b, cur, shots)
    answer(s, b, wh)

    # 6. the fuller
    grip = s.gadget(b, wh, W_FULLER)
    b.op(wait(8000), F(3), *path(cur, grip), F(6), B(1), F(6), B(0))
    cur = grip
    msg = s.message(b)
    assert msg[0] == WM_FULLED, msg
    b.append(wset(wh, WF_CXYWH, *FULL))
    answer(s, b, wh)

    # 7. the closer
    grip = s.gadget(b, wh, W_CLOSER)
    b.op(wait(8000), F(3), *path(cur, grip), F(6), B(1), F(6), B(0))
    cur = grip
    msg = s.message(b)
    assert msg[0] == WM_CLOSED, msg
    a = s.aes(b)
    a.tree = a.trees[L2.base]
    ir = a.ob_actxywh(D_FLOPPY)
    b.append((GRAF_SHRINKBOX, (), (ir.x, ir.y, ir.w, ir.h) + tuple(FULL)))
    b.append((WIND_CLOSE, (), (wh,)))
    b.append((WIND_DELETE, (), (wh,)))
    assert not s.queue(b), s.queue(b)

    # 8. a last look: a timer wait long enough for the whole plan, so the
    # plan ends it (the tick is 20 ms in PAL, and a step is one frame)
    steps = (F(3), *path(cur, (320, 130)), F(40))
    ticks = sum(st[1] if st[0] == "frames" else 1 for st in steps)
    b.op(wait(ticks * 20), *steps)
    return b, shots


# -- chapters -----------------------------------------------------------------

def chapters(script, tree_base, mfdb_addr, script_room):
    """Split the session where the runner's buffers require: at most
    MAX_RECORDS records and script_room bytes of encoded words a chapter,
    the first carrying the prelude.  Yields (start, records)."""
    start, cur = 0, []
    for i, rec in enumerate(script):
        trial = cur + [rec]
        # the first chapter is run with the prelude in front of it
        full = (PRELUDE if start == 0 else []) + trial
        words = aesref.encode(full, tree_base, mfdb_addr)
        if len(full) > MAX_RECORDS or len(words) * 2 > script_room:
            assert cur, f"record {i} does not fit a chapter by itself"
            yield start, cur
            start, cur = i, [rec]
        else:
            cur = trial
    if cur:
        yield start, cur


class Camera:
    """Every frame the harness runs, screenshotted: b.frames becomes one
    FRAME and one SCREENSHOT per frame."""

    def __init__(self, b, outdir):
        self.b, self.dir, self.n = b, outdir, 0
        self.inner = b.frames

    def frames(self, n):
        r = None
        for _ in range(n):
            r = self.inner(1)
            self.b.screenshot(os.path.join(self.dir, f"f{self.n:05d}.png"))
            self.n += 1
        return r


def save_rgb(rgb, path):
    from PIL import Image
    im = Image.new("RGB", (len(rgb[0]), len(rgb)))
    im.putdata([px for row in rgb for px in row])
    im.save(path)


def encode_movie(framedir, n_frames):
    """ffmpeg: the overlay's 640 pixels out of the 672-wide shot, doubled
    vertically to square-ish pixels; the mp4 at the frame rate the
    machine ran at, the gif at half of it."""
    src = os.path.join(framedir, "f%05d.png")
    crop = f"crop={vbxeref.SHOT_W}:{vbxeref.SHOT_H}:{vbxeref.SHOT_X0}:{vbxeref.SHOT_Y0}"
    mp4 = os.path.join(MOVIEDIR, "gem4xe.mp4")
    gif = os.path.join(MOVIEDIR, "gem4xe.gif")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "50",
                    "-i", src, "-vf", f"{crop},scale=1280:960:flags=neighbor,format=yuv420p",
                    "-c:v", "libx264", "-crf", "18", "-preset", "slow", mp4], check=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "50",
                    "-i", src, "-vf",
                    f"fps=25,{crop},scale=640:480:flags=neighbor,"
                    "split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none",
                    gif], check=True)
    return mp4, gif


def main(argv):
    dry = "--dry" in argv
    keep = "--keep-frames" in argv
    encode = "--no-encode" not in argv
    os.makedirs(MOVIEDIR, exist_ok=True)
    syms = symfile.load(SYMS)
    sa, sc = syms["vdi_script"], syms["vdi_scratch"]
    results_addr, count_addr = syms["vdi_results"], syms["vdi_result_count"]
    ptr = syms["ptr_state"]
    scratch_room = min(a for a in syms.values() if a > sc) - sc
    script_room = min(a for a in syms.values() if a > sa) - sa

    L = Layout(sc, max_objs=N_OBJS)
    objs = menu(L)
    L2 = Layout(L.next, max_objs=7)
    objs2 = desktop(L2)
    L3 = Layout(L2.next, max_objs=5)
    objs3 = about(L3)
    s = Scratch(L, objs, L2, objs2, L3, objs3)
    script, shot_names = session(s)
    # the VDI's form for the icons: the bits the desk tree's first image
    # already holds, described by an MFDB after the last layout
    bits_addr = L2.mem[objs2[D_FLOPPY_ICON].ob_spec].pdata
    mfdb_addr = L3.next
    mfdb = vdiref.pack_mfdb(bits_addr, ICON_W, ICON_H, ICON_WDW)
    assert mfdb_addr + len(mfdb) - sc <= scratch_room, (mfdb_addr + len(mfdb) - sc, scratch_room)
    layouts = [(L, objs, L.pack(objs)), (L2, objs2, L2.pack(objs2)), (L3, objs3, L3.pack(objs3))]

    chaps = list(chapters(script, L.base, mfdb_addr, script_room))
    plan = {k + len(PRELUDE): v for k, v in script.plan.items()}
    full = PRELUDE + list(script)
    frames = sum(sum(st[1] for st in steps if st[0] == "frames")
                 + sum(1 for st in steps if st[0] in ("move", "button", "key"))
                 for steps in plan.values())
    print(f"session: {len(script)} records in {len(chaps)} chapters, "
          f"{len(plan)} planned waits, {len(shot_names)} shots, "
          f"at most {frames} planned frames")

    if dry:
        for k, steps in plan.items():
            for i, st in enumerate(steps):
                if st[0] == "shot":
                    steps[i] = ("shot", None)
        pointer = (0, 0)
        ref_v, ref_a, want = s.run(script, pointer=pointer, plan=dict(plan))
        for name, rgb in zip(shot_names, ref_a.shots):
            save_rgb(rgb, os.path.join(MOVIEDIR, f"dry-{name}.png"))
        save_rgb(ref_v.to_rgb(), os.path.join(MOVIEDIR, "dry-final.png"))
        print(f"model ok: {len(want)} results; dry-*.png in {MOVIEDIR}")
        return 0

    shots = []

    def take(bridge):
        p = os.path.join(MOVIEDIR, f"shot-{len(shots):02d}-{shot_names[len(shots)]}.png")
        bridge.screenshot(p)
        shots.append(p)
    for steps in plan.values():
        for i, st in enumerate(steps):
            if st[0] == "shot":
                steps[i] = ("shot", take)

    if os.path.isdir(FRAMEDIR):
        shutil.rmtree(FRAMEDIR)
    os.makedirs(FRAMEDIR)

    emu = launch(tag="movie", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    err = None
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("L", "M", "3", "RETURN"):
            b.key(k)
            b.frames(10)
        b.frames(200)
        if bytes(b.memdump(STATUS, 3))[:2] != b"VD":
            print("FAIL: runner did not come up")
            return 1
        for lay, _, img in layouts:
            b.memload(lay.base, img)
        b.memload(mfdb_addr, mfdb)
        pointer = (b.peek16(ptr), b.peek16(ptr + 2))

        ref_v, ref_a, want = s.run(script, pointer=pointer, plan=dict(plan))
        # the trees as the model left them: menu_bar rebuilds the Desk box,
        # and the model ran on copies of the layouts' objects
        ended = [(L, ref_a.home), (L2, ref_a.trees[L2.base]), (L3, ref_a.trees[L3.base])]

        cam = Camera(b, FRAMEDIR)
        b.frames = cam.frames
        b.frames(25)
        for ci, (start, recs) in enumerate(chaps):
            first = ci == 0
            chap = (PRELUDE if first else []) + recs
            base = start + (0 if first else len(PRELUDE))    # index in `full`
            # the chapter's plan, keyed by its own record indices
            cplan = {k - base: v for k, v in plan.items() if base <= k < base + len(chap)}
            words = aesref.encode(chap, L.base, mfdb_addr)
            assert len(words) * 2 <= script_room
            b.memload(sa, b"".join(struct.pack("<h", w if w < 32768 else w - 65536)
                                   for w in words))
            b.poke(STATUS + ST_DONE, 0)
            poke16(b, count_addr, NOT_STARTED)
            b.poke(STATUS + ST_GO, 1)
            cwant = want[base:base + len(chap)]
            err = drive(b, count_addr, ptr, cplan)
            if err:
                n = b.peek16(count_addr)
                err = (f"chapter {ci} (records {base}..{base + len(chap) - 1}): {err}; "
                       + (compare(b, results_addr, n, chap, cwant)
                          or f"the {n} records so far match"))
                break
            ok = False
            for _ in range(300):
                if b.peek(STATUS + ST_DONE) == 0xA5:
                    ok = True
                    break
                b.frames(4)
            if not ok:
                err = f"chapter {ci}: timed out"
                break
            n = b.peek16(count_addr)
            if n != len(chap):
                err = f"chapter {ci}: {n} calls recorded, expected {len(chap)}"
                break
            err = compare(b, results_addr, n, chap, cwant)
            if err:
                err = f"chapter {ci}: {err}"
                break
            print(f"  chapter {ci}: {len(chap)} records ok, {cam.n} frames so far")
        b.frames(25)
        if not err:
            for lay, lobjs in ended:
                err = mem_diff(lay, lobjs, bytes(b.memdump(lay.base, len(lay.pack(lobjs)))))
                if err:
                    break
        final = os.path.join(MOVIEDIR, "final.png")
        b.screenshot(final)
        shots.append(final)
        images = ref_a.shots + [ref_v.to_rgb()]
        if len(images) != len(shots):
            err = err or f"{len(shots)} shots taken, reference has {len(images)}"
        for k, (rgb, p) in enumerate(zip(images, shots)):
            bad, shown = vbxeref.compare_to_shot(rgb, p)
            if bad and not err:
                err = f"shot {k} ({os.path.basename(p)}): {bad} px differ; first {shown[:3]}"
        n_frames = cam.n
    finally:
        emu.stop()

    if err:
        print(f"FAIL: {err}")
        print(f"frames kept in {FRAMEDIR}")
        return 1
    print(f"{n_frames} frames, every result and every shot as the model has it")
    if encode:
        mp4, gif = encode_movie(FRAMEDIR, n_frames)
        print(f"{mp4}\n{gif}")
        if not keep:
            shutil.rmtree(FRAMEDIR)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
