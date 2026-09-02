"""What the VDI's primitives cost on the target, and where the cycles go.

Not a gate: a tool for the per-pixel work Phase 8b left on the list.
Feeds the m3 runner one primitive N times and reports frames and
milliseconds per op; with --profile, Altirra's instruction profiler
runs across the op and the hottest addresses come back resolved to the
nearest symbol, with cycles per instruction -- the number that says
whether the CPU is fast (about 0.4 at 11x, with the code in SRAM) or
the code is long.  `docs/phase8b.md` has the readings this replaced.

  python3 tests/emu/bench_vdi.py [--profile] [--top N] [NAME ...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import m3_vdi as m                                                  # noqa: E402
import symfile                                                      # noqa: E402
import vdiref as V                                                  # noqa: E402
from m3_vdi import (STATUS, ST_GO, ST_DONE, launch, DISK, pack_mfdb,  # noqa: E402
                    ICON_W, ICON_H, ICON_WDW)

ONES = bytes([0xFF]) * 96
N = 16

ICON = lambda mode, x: (V.VRT_CPYFM, (0, 0, 31, 23, x, 40), (mode, 1, 0), "icon")
CLIP_CORNER = (V.VS_CLIP, (620, 230, 639, 239), (1,))
CLIP_OFF = (V.VS_CLIP, (0, 0, 639, 239), (0,))

# name: (script of N ops, ops counted, form bits)
CASES = {
    "pline-h":     ([(V.V_PLINE, (10, 10 + 8 * i, 610, 10 + 8 * i)) for i in range(N)], N),
    "pline-diag":  ([CLIP_OFF] + [(V.V_PLINE, (0, i, 100, 100 + i)) for i in range(N)], N),
    "pline-clipped": ([CLIP_CORNER] + [(V.V_PLINE, (0, i, 600, 200 + i)) for i in range(N)], N),
    "icon-trans":  ([ICON(V.MD_TRANS, 64 * (i & 3)) for i in range(N)], N),
    "icon-replace": ([ICON(V.MD_REPLACE, 64 * (i & 3)) for i in range(N)], N),
    "icon-xor":    ([ICON(V.MD_XOR, 64 * (i & 3)) for i in range(N)], N),
    "cursor":      ([(V.VSC_FORM, (), m.cursor_form())]
                    + [(V.V_SHOW_C, (), (0,)), (V.V_HIDE_C,)] * N, 2 * N),
    "vsc_form":    ([(V.VSC_FORM, (), m.cursor_form())] * N, N),
    "vswr_mode":   ([(V.VSWR_MODE, (), (1,))] * N, N),
}


def nearest(syms, addr):
    """The symbol at or below addr in bank $00 or $01 -- the profiler's
    dump drops the bank, and the runner's code is in $01."""
    best = None
    for name, a in syms.items():
        lo = a & 0xFFFF
        if lo <= addr and (best is None or lo > best[0]):
            best = (lo, name)
    return f"{best[1]}+{addr - best[0]:x}" if best else "?"


def run(b, syms, label, script, ops, profile, top):
    sa, sc = syms["vdi_script"], syms["vdi_scratch"]
    script_room = min(a for a in syms.values() if a > sa) - sa
    b.memload(sc, ONES)
    b.memload(sc + 512, pack_mfdb(sc, ICON_W, ICON_H, ICON_WDW))
    b.memload(sc + 532, pack_mfdb(0, 0, 0, 0))
    full = [(V.V_OPNWK, (), V.WORK_IN), (m.V_CLRWK,)] + script
    resolved = [(r[0], r[1] if len(r) > 1 else (), r[2] if len(r) > 2 else (),
                 (True if r[3] == "icon" else None) if len(r) > 3 else None)
                for r in full]
    m.poke_script(b, sa, resolved, sc + 512, script_room, screen_mfdb=sc + 532)
    b.poke(STATUS + ST_DONE, 0)
    if profile:
        b.ok("PROFILE_START mode=insns")
    b.poke(STATUS + ST_GO, 1)
    f = 0
    for _ in range(3000):
        b.frames(1)
        f += 1
        if b.peek(STATUS + ST_DONE) == 0xA5:
            break
    print(f"{label:14s} {ops:3d} ops in {f:3d} frames: {f / ops:5.2f} frames, "
          f"{f * 20 / ops:5.1f} ms per op")
    if profile:
        b.ok("PROFILE_STOP")
        r = b.ok(f"PROFILE_DUMP top={top}")
        tc, ti = r["total_cycles"], r["total_insns"]
        print(f"    {ti} insns in {tc} machine cycles: {tc / max(ti, 1):.2f} cycles/insn, "
              f"{ti / ops:.0f} insns/op")
        for h in r["hot"]:
            a = int(str(h["addr"]).lstrip("$"), 16)
            print(f"    {a:04x} {nearest(syms, a):30s} insns {h['insns']:7d}  "
                  f"cycles {h['cycles']:7d}  {h['cycles'] / max(h['insns'], 1):.2f}")


def main(argv):
    profile = "--profile" in argv
    top = 24
    names = []
    it = iter(a for a in argv if a != "--profile")
    for a in it:
        if a == "--top":
            top = int(next(it))
        else:
            names.append(a)
    names = names or list(CASES)
    for n in names:
        if n not in CASES:
            print(f"no such case: {n}; cases: {' '.join(CASES)}")
            return 2
    syms = symfile.load(m.SYMS)
    emu = launch(tag="bench", memsize="1088K", extra_args=["--disk", DISK])
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
        if bytes(b.memdump(STATUS, 3))[:2] != b"VD":
            print("FAIL: runner did not come up")
            return 1
        st = bytes(b.memdump(STATUS + 25, 5))
        print("rapidus: present %d  MCR %02x -> %02x  CMCR %02x  synced %02x" % tuple(st))
        for n in names:
            script, ops = CASES[n]
            run(b, syms, n, script, ops, profile, top)
    finally:
        emu.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
