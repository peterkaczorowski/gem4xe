#!/usr/bin/env python3
"""Phase 4 gate: the AES object library.

Builds real GEM object trees, pokes them to the target, and compares both what
objc_draw() renders (pixel for pixel) and what objc_find() returns (hit for
hit) against tools/aesref.py.

The trees are the shapes GEM actually uses -- a dialog with a default button,
a menu bar with a dropped menu, nested boxes -- rather than synthetic ones, so
a failure means something a real .RSC would trip over.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from a8test.launcher import launch          # noqa: E402
import vbxeref, vdiref, aesref, symfile     # noqa: E402
from aesref import (Obj, NIL, G_BOX, G_IBOX, G_BUTTON, G_STRING, G_BOXTEXT,
                    G_BOXCHAR, G_TITLE, LASTOB, DEFAULT, SELECTABLE, EXIT,
                    SELECTED, DISABLED, SHADOWED, OUTLINED, CHECKED, HIDETREE)

DISK = os.path.abspath(os.path.join(ROOT, "build", "m3-boot.atr"))
SYMS = os.path.join(ROOT, "build", "m3.sym")
SHOTDIR = os.path.join(ROOT, "build", "shots")
STATUS, ST_GO, ST_DONE = 0x0600, 3, 4
OBJC_DRAW, OBJC_FIND = 1042, 1043


def strings_blob(base, items):
    """Lay strings out after the tree; returns {addr: text} and the bytes."""
    out, table, off = b"", {}, 0
    for s in items:
        table[s] = base + off
        out += s.encode("latin-1") + b"\0"
        off += len(s) + 1
    return table, out


def dialog():
    """A GEM alert-style dialog: shadowed outer box, text, three buttons."""
    names = ["Delete FLOPPY.TXT?", "OK", "Cancel", "Help"]
    return names, lambda S: [
        #    next head tail type      flags          state      spec        x   y   w   h
        Obj(NIL,  1,   4,  G_BOX,    LASTOB,        SHADOWED,  0x00021100, 160, 60, 320, 110),
        Obj(2,  NIL, NIL,  G_STRING, 0,             0,         S[names[0]],  16, 16, 288,  8),
        Obj(3,  NIL, NIL,  G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, S[names[1]],  24, 72,  72, 20),
        Obj(4,  NIL, NIL,  G_BUTTON, SELECTABLE | EXIT, 0,     S[names[2]], 120, 72,  72, 20),
        Obj(0,  NIL, NIL,  G_BUTTON, SELECTABLE | EXIT, DISABLED, S[names[3]], 216, 72,  72, 20),
    ]


def menubar():
    """A menu bar with one dropped menu and a selected item -- the shape the
    AES builds and the geometry RCS guarantees."""
    names = ["Desk", "File", "View", "  Open", "  Info...", "  Quit"]
    return names, lambda S: [
        Obj(NIL,  1,   6, G_BOX,    LASTOB, 0, 0x00001100,   0,  0, 640, 240),
        Obj(2,  NIL, NIL, G_TITLE,  0,      0, S[names[0]],   8,  3,  32,   8),
        Obj(3,  NIL, NIL, G_TITLE,  0,      0, S[names[1]],  56,  3,  32,   8),
        Obj(4,  NIL, NIL, G_TITLE,  0,      0, S[names[2]], 104,  3,  32,   8),
        Obj(5,  NIL, NIL, G_STRING, SELECTABLE, 0,        S[names[3]],  56, 20, 112,   8),
        Obj(6,  NIL, NIL, G_STRING, SELECTABLE, CHECKED,  S[names[4]],  56, 30, 112,   8),
        Obj(0,  NIL, NIL, G_STRING, SELECTABLE, SELECTED, S[names[5]],  56, 40, 112,   8),
    ]


def nested():
    """Nested boxes: children are positioned relative to their parent, which is
    the single easiest thing to get wrong in ob_offset()."""
    names = ["deep"]
    return names, lambda S: [
        Obj(NIL,  1,   1, G_BOX,     LASTOB, 0, 0x0002110F,  40, 40, 400, 160),
        Obj(0,    2,   3, G_IBOX,    0,      0, 0x00011100,  20, 20, 340, 120),
        Obj(3,  NIL, NIL, G_BOXCHAR, 0,      0, 0x41011108,  16, 16,  40,  40),
        Obj(1,    4,   4, G_BOX,     0,      OUTLINED, 0x00021100, 80, 30, 200,  60),
        Obj(3,  NIL, NIL, G_STRING,  0,      0, S[names[0]], 24, 26,  32,   8),
    ]


CASES = [
    ("dialog with default and disabled buttons", dialog, 0, 8,
     [(320, 82), (60, 82), (156, 82), (5, 5), (170, 70)]),
    ("menu bar with a dropped, selected item", menubar, 0, 8,
     [(70, 24), (70, 44), (20, 5), (600, 200)]),
    ("nested boxes and relative offsets", nested, 0, 8,
     [(60, 60), (140, 100), (300, 190), (10, 10)]),
    ("draw a subtree only", dialog, 2, 0, [(60, 82)]),
]


def main(argv):
    keep = "--shot" in argv
    os.makedirs(SHOTDIR, exist_ok=True)
    syms = symfile.load(SYMS)
    sa, sc = syms["vdi_script"], syms["vdi_scratch"]
    results_addr, count_addr = syms["vdi_results"], syms["vdi_result_count"]

    emu = launch(tag="m4", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    out = []
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(200)
        if bytes(b.memdump(STATUS, 3))[:2] != b"VD":
            print("FAIL: runner did not come up")
            return 1

        for idx, (name, maker, start, depth, hits) in enumerate(CASES):
            names, build = maker()
            tree_base = sc
            str_base = sc + 24 * 8            # strings after up to 8 objects
            S, blob = strings_blob(str_base, names)
            objs = build(S)
            b.memload(tree_base, aesref.pack_tree(objs))
            b.memload(str_base, blob)

            script = [(vdiref.V_CLRWK,),
                      (OBJC_DRAW, (0, 0, 640, 240), (start, depth), "tree")]
            script += [(OBJC_FIND, (mx, my), (0, 8), "tree") for mx, my in hits]

            ref_v = vdiref.VDI()
            ref_v.call(vdiref.V_CLRWK)
            ref_a = aesref.AES(ref_v, objs, {v: k for k, v in S.items()})
            ref_a.draw(start, depth, (0, 0, 640, 240))
            want_hits = [ref_a.find(0, 8, mx, my) for mx, my in hits]

            words = []
            for rec in script:
                op = rec[0]
                pts = list(rec[1]) if len(rec) > 1 else []
                ints = list(rec[2]) if len(rec) > 2 else []
                c7 = tree_base if len(rec) > 3 else 0
                words += [op, len(pts) // 2, len(ints), c7, 0, 0, 0] + pts + ints
            words.append(0)
            b.memload(sa, b"".join(struct.pack("<h", w if w < 32768 else w - 65536)
                                   for w in words))
            b.poke(STATUS + ST_DONE, 0)
            b.poke(STATUS + ST_GO, 1)
            ok = False
            for _ in range(300):
                if b.peek(STATUS + ST_DONE) == 0xA5:
                    ok = True
                    break
                b.frames(4)
            if not ok:
                out.append((idx, name, "timed out"))
                continue
            b.frames(4)

            err = None
            n = b.peek16(count_addr)
            got = b.memdump(results_addr, n * 16)
            got_hits = [int.from_bytes(bytes(got[i * 16 + 4:i * 16 + 6]),
                                       "little", signed=True)
                        for i in range(2, n)]
            if got_hits != want_hits:
                err = f"objc_find {got_hits} != {want_hits}"

            shot = os.path.join(SHOTDIR, f"m4-{idx:02d}.png")
            b.screenshot(shot)
            bad, shown = vbxeref.compare_to_shot(ref_v.to_rgb(), shot)
            if bad and not err:
                err = f"{bad} px differ; first {shown[:3]}"
            out.append((idx, name, err))
            if not err and not keep:
                os.remove(shot)
            print(f"  [{idx}] {name:<44s} {'ok' if not err else 'FAIL'}")
    finally:
        emu.stop()

    fails = [r for r in out if r[2]]
    print()
    for idx, name, e in fails:
        print(f"   FAIL [{idx}] {name}: {e}")
    print(f"gem4xe-m4: {len(out) - len(fails)}/{len(out)} AES cases passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
