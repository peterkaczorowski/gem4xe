#!/usr/bin/env python3
"""Phase 10 gate: the application ABI.

The runner's sys op 3004 loads the gate application (src/m11_app.c: a
program linked on its own rules, src/app/gemapp.scm, against nothing of
gem4xe's) from the blob packed into the image, relocates it into the
bank-$00 pool and a far bank, and calls it.  The application makes the
calls a small GEM program makes -- appl_init, graf_handle, v_opnvwk,
drawing, an object tree, a window, a timer wait -- through COP, and
records what every call returned through the ABI's copy-out in its own
memory.

Three things are then compared.  The application's records, read out of
the pool by symbol (build/m11_app.sym, translated by where the loader
put the near part), against the reference's records for the same calls
-- the reference runs the runner's prelude and then the application's
sequence, with the tree decoded from the application's memory as the
loader and the application left it.  The runner's own record of op 3004:
the loader's status, the near base against the pool bounds the linker
reported, the far bank against where the image ends and the first
megabyte, and three counts that must agree -- what main() returned, the
records the application wrote, the COP calls the ABI took -- with none
refused.  And the screen, against the reference's.

Phase 14 added the third entry, GEMDOS (COP #$01): after appl_exit the
application asks the version, the drive, the boot disk's directory entry
by entry, a file that is not there, and how much memory is left, into
dosres[]; the directory count is checked against the image itself and
the calls are added to the COP reconciliation.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import vbxeref, vdiref, aesref, symfile, atr  # noqa: E402
from vdiref import (WORK_IN, V_OPNVWK, VSF_COLOR, VSF_INTERIOR, VR_RECFL,  # noqa: E402
                    VST_COLOR, V_GTEXT, VSL_COLOR, V_PLINE)
from aesref import (Obj, Text, APPL_INIT, APPL_EXIT, GRAF_HANDLE, OBJC_DRAW,  # noqa: E402
                    WIND_CREATE, WIND_OPEN, WIND_GET, WIND_CLOSE, WIND_DELETE,
                    EVNT_TIMER, NAME, CLOSER, MOVER, WF_WXYWH, OBJ_SIZE)
from m4_aes import PRELUDE                                          # noqa: E402
from m7_form import poke16, NOT_STARTED, STATUS, ST_GO, ST_DONE, DISK, SYMS, SHOTDIR  # noqa: E402

APP_SYMS = os.path.join(ROOT, "build", "m11_app.sym")
LOAD_RUN = 3004
APP_OK = 0
REC_WORDS = vdiref.RESULT_WORDS

# The application's sequence, as src/m11_app.c makes it -- one entry per
# record it writes: (script record, int_out words the AES binding asks
# for, the return the ABI gives where the runner's op gives none).  The
# two values the application got at run time and used in later calls --
# hbox from graf_handle, the window handle from wind_create -- are read
# from its records, so the reference is given the calls the application
# actually made; its own returns for them are compared like the rest.
TEXT = "gem4xe application"


def app_calls(hbox, wh):
    return [
        ((APPL_INIT,), 1, None),
        ((GRAF_HANDLE,), 5, None),
        ((V_OPNVWK, (), WORK_IN), None, None),
        ((VSF_COLOR, (), (3,)), None, None),
        ((VSF_INTERIOR, (), (1,)), None, None),
        ((VR_RECFL, (20, 20, 619, 219), ()), None, None),
        ((VST_COLOR, (), (1,)), None, None),
        ((V_GTEXT, (24, 32), tuple(ord(c) for c in TEXT)), None, None),
        ((VSL_COLOR, (), (2,)), None, None),
        ((V_PLINE, (30, 200, 300, 180, 600, 210), ()), None, None),
        ((OBJC_DRAW, (0, 0, 640, 240), (0, 8)), 1, 1),
        ((WIND_CREATE, (), (NAME | CLOSER | MOVER, 0, hbox, 640, 240 - hbox)), 1, None),
        ((WIND_OPEN, (), (wh, 320, 40, 280, 150)), 1, None),
        ((WIND_GET, (), (wh, WF_WXYWH)), 5, None),
        ((EVNT_TIMER, (), (40, 0)), 1, None),
        ((WIND_CLOSE, (), (wh,)), 1, None),
        ((WIND_DELETE, (), (wh,)), 1, None),
        ((APPL_EXIT,), 1, None),
    ]


TIMER_INDEX = 14        # evnt_timer's place in the list: the one that needs a plan


def abi_record(rec, nout, ret):
    """What the application records for an AES call: no points, control[2]
    int_out words -- the binding's count, not the runner's -- and the
    return the ABI gives where the runner's op gives none."""
    if nout is None:
        return rec
    io = list(rec[2:2 + vdiref.RESULT_INTOUT])
    if ret is not None:
        io[0] = ret
    return vdiref.record(0, nout, io, [0, 0, 0])


def read_tree(b, addr, n, mem):
    """The application's tree from the target's memory: n objects at addr,
    and the strings the spec words point at, as the model reads them."""
    raw = bytes(b.memdump(addr, OBJ_SIZE * n))
    objs = []
    for i in range(n):
        f = struct.unpack("<hhhHHHIhhhh", raw[i * OBJ_SIZE:(i + 1) * OBJ_SIZE])
        o = Obj(*f)
        if o.ob_type in (aesref.G_STRING, aesref.G_BUTTON, aesref.G_TITLE):
            # a far pointer to bank $00 data: the low word is the address
            sa = o.ob_spec & 0xFFFF
            s = bytes(b.memdump(sa, 64))
            s = s[:s.index(b"\0")].decode("latin-1")
            mem[o.ob_spec] = Text(s)
        objs.append(o)
    return objs


def main(argv):
    keep = "--shot" in argv
    os.makedirs(SHOTDIR, exist_ok=True)
    syms = symfile.load(SYMS)
    app = symfile.load(APP_SYMS)
    sa = syms["vdi_script"]
    results_addr, count_addr = syms["vdi_results"], syms["vdi_result_count"]
    script_room = min(a for a in syms.values() if a > sa) - sa
    link_near = min(a for a in app.values())      # the app's placeholder base
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    emu = launch(tag="m11", memsize="1088K", extra_args=["--disk", DISK])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(200)
        # "VD" says the runner is alive; STATUS[2] == 1 says it is
        # ready for scripts, which is what staging one needs.
        for _ in range(200):
            st = bytes(b.memdump(STATUS, 3))
            if st[:2] == b"VD" and st[2] == 1:
                break
            b.frames(4)
        if st[:2] != b"VD" or st[2] != 1:
            print("FAIL: runner did not come up")
            return 1

        script = PRELUDE + [(LOAD_RUN,)]
        words = aesref.encode(script, 0)
        assert len(words) * 2 <= script_room
        b.memload(sa, b"".join(struct.pack("<h", w if w < 32768 else w - 65536)
                               for w in words))
        b.poke(STATUS + ST_DONE, 0)
        poke16(b, count_addr, NOT_STARTED)
        b.poke(STATUS + ST_GO, 1)
        ok = False
        for _ in range(300):
            if b.peek(STATUS + ST_DONE) == 0xA5:
                ok = True
                break
            b.frames(4)
        if not ok:
            print("FAIL: the runner did not finish (the application may be wedged)")
            return 1
        b.frames(4)
        shot = os.path.join(SHOTDIR, "m11-00.png")
        b.screenshot(shot)

        # -- the runner's record of op 3004 -----------------------------------
        n = b.peek16(count_addr)
        check(n == len(script), f"{n} runner records, expected {len(script)}")
        got = vdiref.decode(b.memdump(results_addr, n * REC_WORDS * 2), n)
        run = got[-1]
        status, near_base, far_bank, main_ret, calls, bad = run[2 + 6:2 + 12]
        near_base &= 0xFFFF                 # decoded signed; it is an address
        print(f"  load status {status}, near base ${near_base:04X}, far bank "
              f"${far_bank:02X}, main() returned {main_ret}, {calls} COP calls, "
              f"{bad} refused")
        check(status == APP_OK, f"app_load returned {status}")
        pool_lo, pool_hi = b.peek16(syms["app_pool_lo"]), b.peek16(syms["app_pool_hi"])
        check(pool_lo <= near_base < pool_hi and near_base & 0xFF == 0,
              f"near base ${near_base:04X} is not page-aligned inside the pool "
              f"${pool_lo:04X}-${pool_hi:04X}")
        top = b.peek(syms["_fl_top"]) | (b.peek16(syms["_fl_top"] + 1) << 8)
        check(far_bank > (top - 1) >> 16, f"far bank ${far_bank:02X} is not above "
              f"the image's top ${top:06X}")
        check(far_bank < 0x10, f"far bank ${far_bank:02X} is outside the first megabyte")
        check(bad == 0, f"the ABI refused {bad} call(s)")

        # -- the application's own records --------------------------------------
        base = near_base - link_near
        ncalls = b.peek16(app["ncalls"] + base)
        seq = app_calls(0, 0)
        check(ncalls == len(seq), f"the application wrote {ncalls} records, expected {len(seq)}")
        ndos = b.peek16(app["ndos"] + base)
        check(main_ret == ncalls and calls == ncalls + ndos,
              f"main() returned {main_ret}, {ncalls} records, {ndos} GEMDOS calls, "
              f"{calls} COP calls: they should reconcile")

        # -- GEMDOS through COP #$01 ------------------------------------------
        dosres = list(struct.unpack("<8h", b.memdump(app["dosres"] + base, 16)))
        # What Fsfirst/Fsnext should count: the entries a DOS 2 could be
        # handed (atr.Entry.nameable, the rule src/sys/dos.c lists by); the
        # fixture disk's dashed dividers are in use and are not files.
        files = [e.filename for e in atr.Dos2(atr.ATRImage.load(DISK)).entries()
                 if e.in_use and e.nameable]
        print(f"  GEMDOS: version ${dosres[0] & 0xFFFF:04X}, drive {dosres[1]}, "
              f"{dosres[2]} entries (image {len(files)}), Fsnext {dosres[3]}, "
              f"Fopen(missing) {dosres[4]}, {dosres[5]} banks free, DTA {dosres[6]}")
        check(dosres[0] == 0x1500, f"Sversion answered ${dosres[0] & 0xFFFF:04X}, not $1500")
        check(dosres[1] == 0, f"Dgetdrv answered {dosres[1]}, not A")
        check(dosres[2] == len(files), f"Fsfirst/Fsnext saw {dosres[2]} entries; "
              f"the image has {len(files)}")
        check(dosres[3] == -49, f"the search ended with {dosres[3]}, not ENMFIL")
        check(dosres[4] == -33, f"Fopen of a missing file answered {dosres[4]}, not EFILNF")
        check(dosres[5] >= 1, f"Malloc(-1) reports {dosres[5]} whole banks free")
        check(dosres[6] == 1, "Fgetdta did not answer the DTA Fsetdta was given")
        arec = vdiref.decode(b.memdump(app["results"] + base, ncalls * REC_WORDS * 2), ncalls)
        # what the application got back and went on to use
        hbox = arec[1][2 + 4] if ncalls > 1 else 0
        wh = arec[11][2] if ncalls > 11 else 0
        seq = app_calls(hbox, wh)

        mem = {}
        tree = read_tree(b, app["tree"] + base, 3, mem)
        mscript = PRELUDE + [rec for rec, _, _ in seq]
        plan = {len(PRELUDE) + TIMER_INDEX: [("frames", 30)]}
        ref_v, ref_a, want = aesref.run(mscript, tree, mem, plan=plan)
        for i, rec in enumerate(got[:len(PRELUDE)]):
            check(rec == want[i], f"runner call {i} returned {rec}, expected {want[i]}")
        for i, (rec, nout, ret) in enumerate(seq):
            w = abi_record(want[len(PRELUDE) + i], nout, ret)
            if i < ncalls and arec[i] != w:
                check(False, f"application call {i} (op {rec[0]}) recorded {arec[i]}, "
                             f"expected {w}")

        bad_px, shown = vbxeref.compare_to_shot(ref_v.to_rgb(), shot)
        check(not bad_px, f"{bad_px} px differ from the reference; first {shown[:3]}")
        if not fails and not keep:
            os.remove(shot)
    finally:
        emu.stop()

    print(f"gem4xe-m11: {'PASS' if not fails else 'FAIL'} -- the application ABI, "
          f"{len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
