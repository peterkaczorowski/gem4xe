#!/usr/bin/env python3
"""Phase 12 gate: alerts, icons and the pointer's shape.

Three things an application uses that the AES supplies out of its own
pocket, none of which needs a resource.

form_alert takes a string -- [1][Two lines|of message][Ok|Cancel] -- and
makes a dialog of it: the icon, the lines, the buttons, laid out in
character cells, converted to pixels by the same rs_obfix an
application's resource goes through, drawn over whatever was there, and
put back afterwards.  src/aes/alert.c builds it in the application pool
and releases it; the cases here check the button it returns, the pixels
it draws, and that the pool comes back to where it was.

graf_mouse sets the pointer's shape from the AES's own eight forms, from
37 words the caller supplies (USER_DEF), or from the one it saved
(M_SAVE/M_RESTORE) or the one before (M_PREVIOUS); M_OFF and M_ON hide
and show it.  Those live in far memory on the target and in
tools/gemdata.py on the host -- one script, both sides.

The icons themselves -- G_ICON, mask under image, character and label --
are drawn by objc_draw and are checked in the object gate (test-m4).
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import aesref, vbxeref, symfile, gemdata    # noqa: E402
from m7_form import (poke16, NOT_STARTED, STATUS, ST_GO, ST_DONE, DISK, SYMS,
                     F, M, B, K, CLICK, RETURN, compare)      # noqa: E402
from m4_aes import PRELUDE, SHOTDIR         # noqa: E402
from m12_file import Runner, run_planned    # noqa: E402

FORM_ALERT = aesref.FORM_ALERT
GRAF_MOUSE = 1078
V_SHOW_C, V_HIDE_C = 122, 123
ALLOC = 3011                                # the runner's pool report
STR_OFF = 0                                 # the alert string, in scratch
SHOT = ("shot", None)
# The target parses the string, builds ten objects, converts them to
# pixels, saves what is under the alert and draws it before it looks at
# the keyboard; the model does all of that in no time.  A settle covers
# the whole of it -- a press made and released before the AES reaches its
# wait is seen by nothing at all (docs/phase11.md).
SETTLE = 60

# graf_mouse's modes (src/aes/aes.h)
ARROW, TEXT_CRSR, HOURGLASS, POINT_HAND = 0, 1, 2, 3
FLAT_HAND, THIN_CROSS, THICK_CROSS, OUTLN_CROSS = 4, 5, 6, 7
USER_DEF = 255
M_OFF, M_ON, M_SAVE, M_RESTORE, M_PREVIOUS = 256, 257, 258, 259, 260

# A form of the caller's own: a hollow 16x16 square with a solid centre,
# hot spot in the middle.  Any 37 words will do; these are legible in a
# screenshot, which is the point.
USER_FORM = ([8, 8, 1, 0, 1]
             + [0xFFFF] + [0xFFFF] * 14 + [0xFFFF]
             + [0x0000] + [0x7FFE] * 3 + [0x7FFE] + [0x0180] * 6
             + [0x7FFE] * 4 + [0x0000])
assert len(USER_FORM) == gemdata.MFORM_WORDS


def alert_cases():
    """(title, defbut, string, plan, want) -- the button each case's plan
    presses, and what form_alert therefore returns."""
    # Where the buttons land is the model's business, not a constant here:
    # the plan clicks by name and fs_button_at() asks the model for the
    # rectangle after it has built the tree.
    return [
        ("two lines, two buttons, the note icon; RETURN takes the default",
         1, "[1][The disk is full.|Delete something?][Ok|Cancel]",
         [F(SETTLE), SHOT, K("RETURN", RETURN)], 1),
        ("the same alert, the second button clicked",
         1, "[1][The disk is full.|Delete something?][Ok|Cancel]",
         "click:2", 2),
        ("no icon, one line, one button",
         1, "[0][Nothing to report.][OK]",
         [F(SETTLE), SHOT, K("RETURN", RETURN)], 1),
        ("the question icon, three buttons, the third clicked",
         2, "[2][Replace the file?|It is already there.][Yes|No|Cancel]",
         "click:3", 3),
        ("the stop icon, five lines, a wide alert",
         1, "[3][Line one|Line two is longer|Line three|Line four|Line five]"
            "[Continue]",
         [F(SETTLE), SHOT, K("RETURN", RETURN)], 1),
        ("a literal | and ] in the text, as TOS takes them",
         1, "[1][a || b|c ]] d][Ok]",
         [F(SETTLE), SHOT, K("RETURN", RETURN)], 1),
    ]


def mouse_cases():
    """(title, [record, ...]) -- each sets a shape and shows the pointer;
    the screen afterwards is the check."""
    def gm(mode, form=()):
        return (GRAF_MOUSE, (), (mode,) + tuple(form))

    show = (V_SHOW_C, (), (0,))
    named = [("the arrow", ARROW), ("the text cursor", TEXT_CRSR),
             ("the hourglass", HOURGLASS), ("the pointing hand", POINT_HAND),
             ("the flat hand", FLAT_HAND), ("the thin cross", THIN_CROSS),
             ("the thick cross", THICK_CROSS), ("the outline cross", OUTLN_CROSS)]
    cases = [(f"graf_mouse({name})", [gm(mode), show]) for name, mode in named]
    cases += [
        ("graf_mouse(USER_DEF): the caller's own 37 words",
         [gm(USER_DEF, USER_FORM), show]),
        ("a mode that is neither a shape nor a command is the arrow",
         [gm(99), show]),
        ("M_OFF hides it, M_ON brings it back",
         [gm(HOURGLASS), show, (V_HIDE_C,), gm(M_ON)]),
        ("M_SAVE, another shape, M_RESTORE",
         [gm(THICK_CROSS), show, gm(M_SAVE), gm(ARROW), gm(M_RESTORE)]),
        ("M_PREVIOUS goes back one",
         [gm(THIN_CROSS), show, gm(HOURGLASS), gm(M_PREVIOUS)]),
    ]
    return cases


def main(argv):
    keep = "--shot" in argv
    syms = symfile.load(SYMS)
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    os.makedirs(SHOTDIR, exist_ok=True)
    emu = launch(tag="m13", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("L", "M", "3", "RETURN"):
            b.key(k)
            b.frames(10)
        b.frames(200)
        for _ in range(200):
            st = bytes(b.memdump(STATUS, 3))
            if st[:2] == b"VD" and st[2] == 1:
                break
            b.frames(4)
        if st[:2] != b"VD" or st[2] != 1:
            print("FAIL: runner did not come up")
            return 1
        r = Runner(b, syms)
        ptr = syms["ptr_state"]
        r.run(PRELUDE)
        mark, room = alloc(r)
        print(f"the pointer's shape: pool ${mark:04X}, {room} free")
        mouse_run(r, b, check, keep, mouse_cases())
        print("form_alert:")
        alert_run(r, b, ptr, check, keep, mark, room)
    finally:
        emu.stop()

    print(f"gem4xe-m13: {'PASS' if not fails else 'FAIL'} -- alerts, icons "
          f"and the pointer, {len(fails)} problem(s)")
    return 1 if fails else 0


def alloc(r):
    rec = r.run([(ALLOC, (), ())])[0][2:]
    return rec[6] & 0xFFFF, rec[7]


def mouse_run(r, b, check, keep, cases):
    """Each case is a whole script: the model runs it, the target runs it,
    and the screen is compared -- the pointer IS the output here."""
    for idx, (title, body) in enumerate(cases):
        script = PRELUDE + body
        pointer = (b.peek16(r.b and 0 or 0) if False else 0, 0)   # unused
        ptr_state = (b.peek16(r.syms["ptr_state"]),
                     b.peek16(r.syms["ptr_state"] + 2))
        ref_v, ref_a, want = aesref.run(script, [], {}, pointer=ptr_state)
        recs = r.run(script)
        err = compare(b, r.results, len(recs), script, want)
        if not err:
            p = os.path.join(SHOTDIR, f"m13-mouse-{idx:02d}.png")
            b.screenshot(p)
            bad, shown = vbxeref.compare_to_shot(ref_v.to_rgb(), p)
            if bad:
                err = f"{bad} px differ from the model; first {shown[:3]}"
            elif not keep:
                os.remove(p)
        check(not err, f"[{idx}] {title}: {err}")
        print(f"  [{idx}] {title:<58s} {'ok' if not err else 'FAIL'}")


def alert_run(r, b, ptr, check, keep, mark, room):
    for idx, (title, defbut, alstr, plan_or_click, want) in enumerate(alert_cases()):
        addr = r.stage(STR_OFF, alstr.encode("latin-1") + b"\0")
        script = PRELUDE + [(FORM_ALERT, (), (defbut,), addr)]
        buffers = {addr: alstr}
        # A "click:N" plan needs the button's rectangle, which only the
        # model knows once it has built the tree: run the model with a
        # placeholder plan to get the geometry, then again for real.
        if isinstance(plan_or_click, str):
            nth = int(plan_or_click.split(":")[1])
            rect = button_rect(script, addr, defbut, alstr, nth, mark)
            mid = (rect.x + rect.w // 2, rect.y + rect.h // 2)
            steps = [F(SETTLE), SHOT] + CLICK(mid)[:-1]
        else:
            steps = list(plan_or_click)
        plan = {len(PRELUDE): steps}
        shots = []

        def take(bridge, shots=shots, idx=idx):
            p = os.path.join(SHOTDIR, f"m13-alert-{idx:02d}-{len(shots)}.png")
            bridge.screenshot(p)
            shots.append(p)
        steps = [("shot", take) if st[0] == "shot" else st for st in steps]
        plan = {len(PRELUDE): steps}
        pointer = (b.peek16(ptr), b.peek16(ptr + 2))
        ref_v, ref_a, want_recs = aesref.run(
            script, [], {}, plan={k: list(v) for k, v in plan.items()},
            pointer=pointer, pool=mark, buffers=dict(buffers))
        got = want_recs[-1][2]
        err = None
        if got != want:
            err = f"the model returns {got}, the case wants {want}"
        else:
            err, recs = run_planned(r, ptr, script, plan)
            if not err:
                err = compare(b, r.results, len(recs), script, want_recs)
            if not err:
                after = alloc(r)
                if after != (mark, room):
                    err = (f"the pool after: mark ${after[0]:04X}, {after[1]} free; "
                           f"was ${mark:04X}, {room}")
            if not err:
                p = os.path.join(SHOTDIR, f"m13-alert-{idx:02d}.png")
                b.screenshot(p)
                shots.append(p)
                images = ref_a.shots + [ref_v.to_rgb()]
                if len(images) != len(shots):
                    err = f"{len(shots)} shots taken, the model has {len(images)}"
                for k, (rgb, sp) in enumerate(zip(images, shots)):
                    bad, shown = vbxeref.compare_to_shot(rgb, sp)
                    if bad and not err:
                        err = f"shot {k}: {bad} px differ; first {shown[:3]}"
        check(not err, f"[{idx}] {title}: {err}")
        print(f"  [{idx}] {title:<58s} {'ok' if not err else 'FAIL'}"
              + (f"  -> {want}" if not err else ""))
        if not err and not keep:
            for sp in shots:
                os.remove(sp)


def button_rect(script, addr, defbut, alstr, nth, mark):
    """The nth button's rectangle, from a model run of the same alert.
    The model is the specification for where the AES puts them."""
    steps = [F(SETTLE), K("RETURN", RETURN)]
    _, a, _ = aesref.run(script, [], {}, plan={len(PRELUDE): steps},
                         pool=mark, buffers={addr: alstr})
    return a.alert_buttons[nth - 1]


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
