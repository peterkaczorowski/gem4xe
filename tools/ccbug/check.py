#!/usr/bin/env python3
"""Run tools/ccbug/bugs.c in the vendor's simulator and report which cc65816
code generation bugs are still present, and whether the shapes gem4xe uses
instead still compile correctly.

Two links: one against the plain C library, one with src/sys/div16.s
overriding _Div16/_Mod16 the way the real build does.  The bug results are
informational -- a bug that has gone away means a workaround can go -- and
only a broken workaround shape fails the run, because that is what would
break gem4xe.

    python3 tools/ccbug/check.py [--calypsi DIR] [--out DIR] [-O2]
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")

# name: (want, kind, note)  -- kind is "bug" (informational) or "fix" (gate)
RESULTS = {
    "r_b1_bug": (476, "bug", "B1 stack array element + operand"),
    "r_b1_fix": (476, "fix", "B1 through a scalar"),
    "r_b2_eq":  (7,   "bug", "B2 (8 / 8) is true"),
    "r_b2_lt":  (0,   "bug", "B2 (7 / 8) is false"),
    "r_b2_mod": (0,   "bug", "B2 (16 % 8) is false"),
    "r_b3_bug": (7,   "bug", "B3 *out = c ? a : b, inlined static"),
    "r_b3_fix": (7,   "fix", "B3 returned instead"),
    "r_b4_bug": (-2,  "bug", "B4 (int8_t) of a 32-bit-derived value"),
    "r_b4_fix": (-2,  "fix", "B4 through an int8_t local"),
    "r_b5_bug": (120, "bug", "B5 spilled pointer, field * 2u"),
    "r_b5_fix": (120, "fix", "B5 field through a scalar"),
}
B2 = ("r_b2_eq", "r_b2_lt", "r_b2_mod")


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, **kw)
    if r.returncode:
        sys.exit(f"{' '.join(cmd)}\n{r.stdout}")
    return r.stdout


def simulate(db, elf, names):
    """db65816's -e commands race the program; drive it over stdin and wait
    for the stop before reading anything."""
    p = subprocess.Popen([db, "--nh", "--nx", "--exit-breakpoint", elf],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, bufsize=1)
    p.stdin.write("run\n")
    p.stdin.flush()
    while True:
        line = p.stdout.readline()
        if not line:
            sys.exit("simulator ended before the program stopped")
        if "SIGSTP" in line or "exit" in line.lower():
            break
    for n in names:
        p.stdin.write(f"print {n}\n")
    p.stdin.write("quit\n")
    p.stdin.flush()
    out, _ = p.communicate(timeout=60)
    vals = re.findall(r"\$\d+ = (-?\d+)", out)
    if len(vals) != len(names):
        sys.exit(f"expected {len(names)} values, got {len(vals)}:\n{out}")
    return dict(zip(names, map(int, vals)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--calypsi", default=os.environ.get(
        "CALYPSI", os.path.expanduser("~/dev/toolchains/calypsi-65816")))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "ccbug"))
    ap.add_argument("-O", default="2")
    a = ap.parse_args()
    cc, asm, ld, db = (os.path.join(a.calypsi, "bin", t)
                       for t in ("cc65816", "as65816", "ln65816", "db65816"))
    scm = os.path.join(a.calypsi, "example", "minimal", "linker.scm")
    os.makedirs(a.out, exist_ok=True)
    src = os.path.join(ROOT, "tools", "ccbug", "bugs.c")
    div = os.path.join(ROOT, "src", "sys", "div16.s")
    obj, divo = os.path.join(a.out, "bugs.o"), os.path.join(a.out, "div16.o")
    version = run([cc, "--version"]).strip().splitlines()[0]

    run([cc, "-g", "--code-model=large", "--data-model=small", f"-O{a.O}",
         "-o", obj, src])
    run([asm, "-o", divo, div])
    names = list(RESULTS)
    elfs = {}
    for tag, extra in (("lib", []),
                       ("ovr", [divo, "--override", "_Div16", "--override", "_Mod16"])):
        elf = os.path.join(a.out, f"bugs-{tag}.elf")
        run([ld, "-g", scm, obj] + [e for e in extra if e.endswith(".o")]
            + ["-o", elf, "clib-lc-sd.a", "--rtattr", "exit=simplified"]
            + [e for e in extra if not e.endswith(".o")])
        elfs[tag] = simulate(db, elf, names)

    print(f"{version}, -O{a.O}, {os.path.relpath(scm, a.calypsi)}")
    bad = 0
    present = 0
    for n, (want, kind, note) in RESULTS.items():
        got = elfs["lib"][n]
        ok = got == want
        if kind == "bug":
            state = "still present" if not ok else "FIXED upstream"
            present += not ok
        else:
            state = "ok" if ok else "BROKEN"
            bad += not ok
        print(f"  {note:42s} want {want:5d} got {got:5d}   {state}")
    # the divide override is the B2 workaround: it must make all three right
    for n in B2:
        got = elfs["ovr"][n]
        want = RESULTS[n][0]
        if got != want:
            bad += 1
        print(f"  {'B2 with src/sys/div16.s':42s} want {want:5d} got {got:5d}   "
              f"{'ok' if got == want else 'BROKEN'}")
    print()
    if bad:
        print(f"check-cc: FAILED -- {bad} workaround shape(s) miscompile")
        return 1
    print(f"check-cc: PASSED -- every workaround shape is right; "
          f"{present} of {sum(1 for v in RESULTS.values() if v[1] == 'bug')} "
          f"bug shapes still present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
