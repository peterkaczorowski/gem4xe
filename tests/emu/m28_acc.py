#!/usr/bin/env python3
"""Phase 36 gate: a desk accessory, resident beside the desktop.

test-m27 proved two contexts can take turns on one engine stack.  This is
the first time that buys anything: M28.ACC is a second PROGRAM, loaded
before the desktop, registering a name in the Desk menu and then sitting
in the message loop a desk accessory never leaves -- while the desktop
runs above it and answers the mouse.

WHAT IT CHECKS, and why each one is here rather than a picture:

  IT LOADED, AND BEFORE THE DESKTOP.  sh_naccs counts what started;
  sh_accfull counts what was found and had no room.  The order is what
  makes the pool's wind-back work in our favour -- an accessory taken
  before the first program is below the desktop's mark, so every return
  to the desktop leaves it standing (src/aes/shel.c).

  IT REGISTERED, AND THE AES KEPT THE POINTER AND NOT A COPY.  The gate
  derives the accessory's near base from gl_acctitle[0] -- the address
  the AES is holding -- and the offset acc_title has in the accessory's
  own link.  If the AES had copied the string, that arithmetic would land
  nowhere and every symbol read afterwards would be nonsense; the fact
  that acc_id and acc_menu then read as sensible values is the proof.

  THE DESK MENU GREW.  The tree is read out of the target through the
  AES's own gl_mntree: the Desk box must have exactly three children now
  (About, the separator, the name), the third one's ob_spec must BE the
  registered pointer, and the box must be three lines high.  That is
  exactly what menu_fixup() does, checked against the object tree rather
  than against a screenshot -- a picture would also pass with the height
  wrong by a pixel.

  IT IS ACTUALLY RUNNING.  The accessory counts its own timer waits.  The
  gate reads that counter, lets the machine run with the desktop up, and
  requires it to have ADVANCED.  A co-residency that only looked right in
  the menu would fail here, and this is the check the whole phase exists
  for.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import aesref, symfile                      # noqa: E402
from m7_form import poke16, NOT_STARTED, STATUS, ST_GO  # noqa: E402
from m4_aes import PRELUDE                  # noqa: E402
from m12_file import Runner                 # noqa: E402
from m14_sparta import boot, screen         # noqa: E402
from m16_shell import SHELL                 # noqa: E402
from m17_desktop import SYMS, header        # noqa: E402  (m3desk's symbols)

DISK = os.path.abspath(os.path.join(ROOT, "build", "m28-boot.atr"))
ACC = os.path.join(ROOT, "build", "m28_acc.g4a")
ACC_SYM = os.path.join(ROOT, "build", "m28_acc.sym")

OB_SIZE = 24                    # aesref.Object: "<hhhHHHIhhhh"
NIL = -1


def obj(b, tree, i):
    """One OBJECT out of the target, as the AES has it in memory."""
    d = bytes(b.memdump(tree + i * OB_SIZE, OB_SIZE))
    (nxt, head, tail, typ, flags, state, spec,
     x, y, w, h) = struct.unpack("<hhhHHHIhhhh", d)
    return dict(next=nxt, head=head, tail=tail, type=typ, flags=flags,
                state=state, spec=spec, x=x, y=y, w=w, h=h)


def main(argv):
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")
        return cond

    syms = symfile.load(SYMS)
    asym = symfile.load(ACC_SYM)
    link_near, _near_size, _ = header(ACC)

    emu = launch(tag="m28", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        t, st = boot(b)
        if st is None:
            print("FAIL: the runner did not come up")
            for ln in screen(b):
                if ln.strip():
                    print("   |" + ln)
            return 1
        print(f"  M3.COM loaded and running {t} frames after RETURN")
        r = Runner(b, syms)
        r.run(PRELUDE)

        # The shell: it loads the accessories, then the desktop.
        stage = PRELUDE + [(SHELL, (), ())]
        words = aesref.encode(stage, 0)
        b.memload(r.sa, b"".join(
            (x if x < 32768 else x - 65536).to_bytes(2, "little", signed=True)
            for x in words))
        poke16(b, r.count, NOT_STARTED)
        b.poke(STATUS + ST_GO, 1)

        # Wait for the desktop to be up: sh_runs counts the programs the
        # shell has started, and the desktop is the first.
        runs = syms["sh_runs"]
        for _ in range(0, 4000, 10):
            b.frames(10)
            if b.peek16(runs) >= 1 and b.peek16(syms["gl_mntree"]):
                break
        check(b.peek16(runs) >= 1, "the shell never started the desktop")
        b.frames(120)

        # 1. it loaded, and nothing was turned away
        naccs = b.peek16(syms["sh_naccs"])
        full = b.peek16(syms["sh_accfull"])
        check(naccs == 1, f"{naccs} accessories started, expected 1")
        check(full == 0, f"{full} accessory/ies found with no room for them")
        nproc = b.peek16(syms["proc_n"])
        check(nproc == 2, f"{nproc} processes, expected 2")
        print(f"  {naccs} accessory started, {nproc} processes")

        # 2. it registered, and the AES kept ITS pointer
        reg = b.peek16(syms["gl_accreg"])
        title = b.peek16(syms["gl_acctitle"])
        check(reg == 1, f"{reg} names registered, expected 1")
        if not check(title != 0, "the AES holds no title pointer"):
            return 1
        near = title - (asym["acc_title"] - link_near)
        acc = {n: asym[n] + near - link_near
               for n in ("acc_id", "acc_menu", "acc_ticks", "acc_msgs")}
        acc_id, acc_menu = b.peek16(acc["acc_id"]), b.peek16(acc["acc_menu"])
        check(acc_id == 1, f"the accessory's ap_id is {acc_id}, expected 1")
        check(acc_menu == 0, f"its menu id is {acc_menu}, expected slot 0")
        want = b"  Gate accessory\x00"
        got = bytes(b.memdump(title, len(want)))
        check(got == want, f"the title reads {got!r}, expected {want!r}")
        print(f"  registered '{want[:-1].decode().strip()}' as slot {acc_menu}, "
              f"ap_id {acc_id}; its near region is at ${near:04X}")

        # 3. the Desk drop-down grew by a separator and a name
        mntree = b.peek16(syms["gl_mntree"])
        if not check(mntree != 0, "the desktop has installed no menu bar"):
            return 1
        themenus = obj(b, mntree, 0)["tail"]
        dabox = obj(b, mntree, themenus)["head"]
        # GEM's tree is right-threaded: the LAST child's ob_next is the
        # parent, not NIL.  Walking for NIL runs on into the next box.
        kids = []
        i = obj(b, mntree, dabox)["head"]
        while i != NIL and i != dabox and len(kids) < 10:
            kids.append(i)
            i = obj(b, mntree, i)["next"]
        check(kids == [dabox + 1, dabox + 2, dabox + 3],
              f"the Desk box's children are {kids}, expected "
              f"{[dabox + 1, dabox + 2, dabox + 3]}")
        if len(kids) == 3:
            spec = obj(b, mntree, kids[2])["spec"]
            check(spec == title,
                  f"the accessory's item points at ${spec:06X}, not the "
                  f"registered ${title:04X}")
        hchar = b.peek16(syms["gl_hchar"])
        box_h = obj(b, mntree, dabox)["h"]
        check(box_h == 3 * hchar,
              f"the Desk box is {box_h} tall, expected 3 lines of {hchar}")
        print(f"  the Desk drop-down: {len(kids)} items, {box_h} px "
              f"({box_h // hchar} lines of {hchar})")

        # 4. it is running, not merely resident
        t0 = b.peek16(acc["acc_ticks"])
        b.frames(300)
        t1 = b.peek16(acc["acc_ticks"])
        check(t1 > t0,
              f"the accessory's timer did not advance in 300 frames "
              f"({t0} -> {t1}): it is resident but not running")
        print(f"  its own timer went {t0} -> {t1} over 300 frames with the "
              f"desktop up")
        print(f"  the scheduler gave {b.peek16(syms['proc_turns'])} turns away")
        check(b.peek16(syms["ctx_over"]) == 0,
              f"{b.peek16(syms['ctx_over'])} context park(s) refused")
        check(b.peek16(acc["acc_msgs"]) == 0,
              f"the accessory was sent {b.peek16(acc['acc_msgs'])} message(s) "
              f"and nothing has opened it yet")
    finally:
        emu.stop()

    print(f"gem4xe-m28: {'PASS' if not fails else 'FAIL'} -- an accessory "
          f"beside the desktop, {len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
