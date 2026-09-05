#!/usr/bin/env python3
"""Phase 14, milestone 4 gate: the GEM Desktop.

DESKTOP.G4A (src/desk) under sh_main on the SpartaDOS disk, with the
harness at the mouse: the desk comes up with its drive icons and the
trash under the menu bar; a click selects an icon; Desk -> About opens
the dialog, OK closes it; File -> Quit ends the session.  Four things
are checked --

  the screen, against the model, at every stop: the desk up, the Desk
  menu dropped, the About item under the pointer, the dialog with OK
  under the pointer, the File menu dropped, Quit under the pointer;

  the desktop's globals G, read out of the target while it waits for
  the first click and compared byte for byte with the model's: the
  screen tree as deskobj.c and desktop.c built it, the icons' ICONBLKs
  and labels, the geometry the AES answered;

  the calls: the ABI's counter says which call the desktop is inside,
  the harness feeds each wait its input while the target is in that
  call (m7_form.drive), and the sys op's record at the end says how
  many calls the desktop made in all, which must be the model's count
  and none refused;

  the pool and the far heap afterwards: back where they were, and the
  stack's low-water mark under the desktop.

The model is not a script but the desktop itself, transcribed against
the AES model (tools/deskref.py): it asks the model what the target asks
the AES, and the answers steer it the way the AES's steer the target.
Every address the desktop uses -- its near region, G, the resource --
is derived as the loader derives it; nothing is read off a probe.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import aesref, vdiref, vbxeref, symfile     # noqa: E402
import deskref                              # noqa: E402
from deskref import Desktop, DROOT, GLOBES_SIZE  # noqa: E402
from deskrsc import (DESKMENU, FILEMENU, ABOUITEM, QUITITEM, DEOK)  # noqa: E402
from m7_form import (poke16, NOT_STARTED, STATUS, ST_GO, ST_DONE, SYMS,  # noqa: E402
                     F, B, drive, compare)
from m4_aes import PRELUDE, SHOTDIR         # noqa: E402
from m12_file import Runner                 # noqa: E402
from m13_alert import ALLOC                 # noqa: E402
from m14_sparta import DISK as M14_DISK, boot, screen   # noqa: E402
from m16_shell import SHELL                 # noqa: E402
from demo_aes import path                   # noqa: E402

DISK = os.path.abspath(os.path.join(ROOT, "build", "m17-boot.atr"))
DESKTOP = os.path.join(ROOT, "build", "desktop.g4a")
DESK_SYM = os.path.join(ROOT, "build", "desktop.sym")
SHOT = ("shot", None)
DRVBYT = 0x070A                             # DOS 2's drive map (gemdos.c)
# the stops, in the order the plans take them
STOPS = ["desktop", "desk-menu", "about-item", "about", "file-menu", "quit-item"]


def header(path):
    """The G4A header's link addresses (src/sys/app.c app_load)."""
    d = open(path, "rb").read(20)
    assert d[:4] == b"G4A\x01", d[:4]
    link_near, near_size = struct.unpack("<HH", d[4:8])
    return link_near, near_size


def inputs(memo):
    """The step producers, one per wait the desktop blocks in.  `memo`
    collects what the gate reads back from the model at each: G as it
    stands when the first wait begins."""
    def icon_click(d):
        # the first evnt_multi: the desk is up, G is complete
        memo["globes"] = d.globes()
        memo["icon"] = d.screen[DROOT].ob_head
        icon = d.centre(d.g_screen_addr, memo["icon"])
        return [F(3), SHOT, *path(d.pointer(), icon), F(6),
                B(1), F(2), B(0), F(14)]

    def desk_about(d):
        title = d.centre(d.a_menu, DESKMENU)
        # straight down from the title into its drop-down: a slant would
        # cross into File's title first and drop that menu instead
        item = (title[0], d.centre(d.a_menu, ABOUITEM)[1])
        # the wait asks for two clicks, so the press is held through the
        # double-click delay before the menu sees it (m7_form.CLICK)
        return [F(3), *path(d.pointer(), title), F(8), SHOT,
                *path(title, item, speed=4), F(8), SHOT, B(1), F(14)]

    def about_ok(d):
        # form_do, entered with the button still down from the item
        ok = d.centre(d.a_info, DEOK)
        return [F(3), B(0), F(2), *path(d.pointer(), ok), F(10), SHOT,
                B(1), F(14), B(0)]

    def file_quit(d):
        title = d.centre(d.a_menu, FILEMENU)
        item = (title[0], d.centre(d.a_menu, QUITITEM)[1])
        return [F(3), *path(d.pointer(), title), F(8), SHOT,
                *path(title, item, speed=4), F(8), SHOT, B(1), F(14)]

    return [icon_click, desk_about, about_ok, file_quit]


def model(mark, pointer, drvmap):
    """The prelude and the desktop against the model: (v, a, want, d, memo)."""
    v, a, want = aesref.run(PRELUDE, [], {}, pointer=pointer, pool=mark)
    # sh_main before the desktop: the previous program's workstations
    # closed, the window manager, the menu and the pointer started, the
    # desk drawn (m16_shell.model_desk)
    v.close_virtuals()
    a.wm_init()
    a.mn_init()
    a.ratinit()
    a.tree = a.W_TREE
    a.draw(0, 0, (0, 0, a.gl_width, a.gl_height))
    link_near, near_size = header(DESKTOP)
    g_link = symfile.load(DESK_SYM)["G"]
    memo = {}
    d = Desktop(v, a, mark, link_near, near_size, g_link, drvmap, inputs(memo))
    d.main()
    return v, a, want, d, memo


def main(argv):
    keep = "--shot" in argv
    syms = symfile.load(SYMS)
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    calls, ptr = syms["gem_calls"], syms["ptr_state"]
    for addr in (calls, ptr, syms["sh_runs"]):
        assert not 0x4000 <= addr < 0x8000, hex(addr)
    desk_len = (os.path.getsize(DESKTOP) + 3) & ~3   # far_alloc's rounding
    os.makedirs(SHOTDIR, exist_ok=True)
    shots = []

    def shot(b, name):
        p = os.path.join(SHOTDIR, f"m17-{name}.png")
        b.screenshot(p)
        shots.append(p)
        return p

    stk = [ln for ln in open(os.path.join(ROOT, "build", "m3.map"))
           if ln.startswith("stack ")][0].split()
    stk_lo, stk_hi = (int(x, 16) for x in stk[1].split("-"))
    PAINT, MARGIN = 0xA5, 256

    def paint(b):
        b.memload(stk_lo, bytes([PAINT]) * (stk_hi - stk_lo + 1))

    def low_water(b):
        dmp = b.memdump(stk_lo, stk_hi - stk_lo + 1)
        for i, x in enumerate(dmp):
            if x != PAINT:
                return stk_lo + i
        return stk_hi + 1

    assert DISK != M14_DISK
    emu = launch(tag="m17", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        t, st = boot(b, prepare=paint)
        if st is None:
            print("FAIL: the runner did not come up")
            for ln in screen(b):
                if ln.strip():
                    print("   |" + ln)
            return 1
        print(f"  M3.COM loaded and running {t} frames after RETURN")
        kind = st[8]
        # GEMDOS's drive map from the DOS seam (src/sys/gemdos.c gd_drvmap)
        drvmap = 0x03 if kind else (b.peek(DRVBYT) or 1)
        r = Runner(b, syms)
        r.run(PRELUDE)
        rec = r.run([(ALLOC, (), ())])[0][2:]
        mark, room, brk = rec[6] & 0xFFFF, rec[7], (rec[8] & 0xFFFF) | (rec[9] << 16)
        print(f"before: pool ${mark:04X}, {room} free; far brk ${brk:06X}; "
              f"DOS kind {kind}, drive map {drvmap:#04x}")

        # The model, all the way through: the desktop's script, its plans
        # and its screens come out of it.
        pointer = (b.peek16(ptr), b.peek16(ptr + 2))
        ref_v, ref_a, want, d, memo = model(mark, pointer, drvmap)
        script = d.script
        print(f"  the model's desktop: {len(script)} calls, waits at {d.waits}, "
              f"near ${d.near:04X}, G ${d.G:04X}, resource ${d.rsc_base:04X}")
        check(len(ref_a.shots) == len(STOPS),
              f"the model took {len(ref_a.shots)} shots, not {len(STOPS)}")

        # The target: the prelude and the shell, staged by hand; the
        # ABI's call counter, zeroed by the sys op, says which of the
        # desktop's calls the target is inside.
        stage = PRELUDE + [(SHELL, (), ())]
        words = aesref.encode(stage, 0)
        b.memload(r.sa, b"".join(
            (x if x < 32768 else x - 65536).to_bytes(2, "little", signed=True)
            for x in words))
        b.poke(STATUS + ST_DONE, 0)
        poke16(b, r.count, NOT_STARTED)
        poke16(b, calls, 0)
        b.poke(STATUS + ST_GO, 1)

        def read():
            return (b.peek16(calls) - 1) & 0xFFFF

        # the load from disk is slower than drive() waits for a start
        first = d.waits[0]
        for t in range(0, 4000, 5):
            n = read()
            if n != NOT_STARTED and n >= first:
                break
            b.frames(5)
        else:
            check(False, f"the desktop did not reach its first wait (call {read()})")
            return 1
        check(n == first, f"the desktop is in call {n}, not its first wait {first}")
        print(f"  the desktop up {t} frames after GO, in call {n}")

        # G, while the desktop waits for the first click
        got = b.memdump(d.G, GLOBES_SIZE)
        if got != memo["globes"]:
            want_g = memo["globes"]
            bad = [i for i in range(GLOBES_SIZE) if got[i] != want_g[i]]
            field = [f for f, _ in deskref.GLOBES
                     if deskref.g_offset(f) <= bad[0]][-1]
            check(False, f"G differs at {len(bad)} byte(s), first at +{bad[0]} "
                         f"({field}): target {got[bad[0]:bad[0] + 8].hex()} "
                         f"model {want_g[bad[0]:bad[0] + 8].hex()}")
        print(f"  G at ${d.G:04X}: {GLOBES_SIZE} bytes "
              f"{'as the model has them' if got == memo['globes'] else 'DIFFER'}")

        # the plans, and a screenshot at each stop
        stops = iter(STOPS)

        def take(bb):
            shot(bb, next(stops))

        plan = {k: [("shot", take) if s == SHOT else s for s in v]
                for k, v in d.plan.items()}
        err = drive(b, None, ptr, plan, read=read)
        check(not err, f"driving the desktop: {err}")
        poke16(b, ptr + 4, 0)               # the button, held since Quit
        for name, rgb in zip(STOPS, ref_a.shots):
            p = os.path.join(SHOTDIR, f"m17-{name}.png")
            if not os.path.exists(p):
                check(False, f"no screenshot at {name}")
                continue
            bad, shown = vbxeref.compare_to_shot(rgb, p)
            check(not bad, f"{name}: {bad} px differ from the model; first {shown[:3]}")
            print(f"  {name:<58s} {'ok' if not bad else 'FAIL'}")

        # the sys op's return
        for _ in range(300):
            if b.peek(STATUS + ST_DONE) == 0xA5:
                break
            b.frames(4)
        else:
            check(False, f"after Quit: the runner did not finish (call {read()})")
            return 1
        b.frames(4)
        n = b.peek16(r.count)
        check(n == len(stage), f"{n} records, not {len(stage)}")
        recs = vdiref.decode(b.memdump(r.results, n * vdiref.RESULT_WORDS * 2), n)
        err = compare(b, r.results, len(PRELUDE), PRELUDE, want)
        check(not err, f"the prelude: {err}")
        rec = recs[-1][2:]
        ret, nruns, lret, lrc, ncalls, bad = rec[6:12]
        print(f"  sh_main returned {ret}: {nruns} run, last returned {lret}, "
              f"last load {lrc}; {ncalls} ABI calls, {bad} refused")
        check(ret == 1, f"sh_main returned {ret}, not the 1 program run")
        check(nruns == 1, f"sh_runs {nruns}, not 1")
        check(lret == 0, f"the desktop's main() returned {lret}, not 0")
        check(lrc == 0, f"the last load's status {lrc}, not 0")
        check(ncalls == len(script),
              f"the desktop made {ncalls} ABI calls; the model made {len(script)}")
        check(bad == 0, f"{bad} ABI calls refused")

        rec = r.run([(ALLOC, (), ())])[0][2:]
        mark2, room2 = rec[6] & 0xFFFF, rec[7]
        brk2 = (rec[8] & 0xFFFF) | (rec[9] << 16)
        print(f"after:  pool ${mark2:04X}, {room2} free; far brk ${brk2:06X}")
        check((mark2, room2) == (mark, room),
              f"the pool after: mark ${mark2:04X}, {room2} free; was ${mark:04X}, {room}")
        check(brk2 == brk + desk_len,
              f"far brk moved {brk2 - brk} bytes; the desktop's file is {desk_len}")
        lw = low_water(b)
        used, size = stk_hi + 1 - lw, stk_hi - stk_lo + 1
        print(f"stack:  {used} of {size} bytes used at the low-water mark (${lw:04X})")
        check(lw - stk_lo >= MARGIN,
              f"the stack came within {lw - stk_lo} bytes of its bottom ${stk_lo:04X}")
    finally:
        emu.stop()

    if not fails and not keep:
        for p in shots:
            os.remove(p)
    print(f"gem4xe-m17: {'PASS' if not fails else 'FAIL'} -- the desktop, "
          f"{len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    if "--model" in sys.argv:
        # a dry run on the host: the model alone, its script listed
        v, a, want, d, memo = model(0x4800, (0, 0), 0x03)
        deskref.describe(d)
        print(len(d.script), "calls; waits at", d.waits, "; shots", len(a.shots))
        sys.exit(0)
    sys.exit(main(sys.argv[1:]))
