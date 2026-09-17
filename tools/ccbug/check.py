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
    "r_b5_dec_bug": (4, "bug", "B5 spilled pointer, field - 1"),
    "r_b5_dec_fix": (4, "fix", "B5 length through a scalar"),
    "r_b7_bug": (801, "bug", "B7 sizeof(struct) as a constant expression"),
    "r_b7_fix": (801, "fix", "B7 the byte count written out"),
    "r_b8_bug": (84,  "bug", "B8 byte local narrowed on one path, stored"),
    "r_b8_fix": (84,  "fix", "B8 the character held in a WORD"),
    "r_b9_bug": (20,  "bug", "B9 got = c ? m : 0 beside a call taking &got"),
    "r_b9_fix": (20,  "fix", "B9 tested, then assigned plainly"),
    "r_b10_bug": (207, "bug", "B10 parameters clamped in place, inlined"),
    "r_b10_fix": (207, "fix", "B10 clamped into fresh locals"),
    "r_b12_bug": (112, "bug", "B12 signed 16-bit >> 3"),
    "r_b12_fix": (112, "fix", "B12 an unsigned copy shifted"),
    "r_b12_neg": (-113, "fix", "B12 asr(), negative"),
    "r_b13_bug": (48,  "bug", "B13 two elements in one expression"),
    "r_b13_fix": (48,  "fix", "B13 the far one through a scalar"),
    "r_b14_bug": (192, "bug", "B14 a negative index into an array"),
    "r_b14_fix": (192, "fix", "B14 indexed from the base instead"),
    "r_b15_bug": (164, "bug", "B15 p->a = p->b + k, spilled pointer"),
    "r_b15_fix": (164, "fix", "B15 the member through a scalar"),
    "r_b16_fix": (119, "fix", "B16 the byte read into a word, compared"),
}
B2 = ("r_b2_eq", "r_b2_lt", "r_b2_mod")
# file stem: note -- the shapes the compiler cannot get through at all
CRASHES = {
    "b6": "B6 indexed direct-page array",
    "b11": "B11 near <-> far struct copy over 8 bytes",
}
# file stem: note -- the shapes the compiler REFUSES, wrongly.  This is
# B7's SECOND test, independent of the one in bugs.c: the file asks for
# sizeof(S) == 34 in an array bound, which is the number the generated
# code uses and the number the constant-expression evaluator will not
# agree to, so the refusal IS the bug.
REFUSALS = {
    "b7_bound": "B7 sizeof in an array bound, padded (second test)",
}
# file stem: note -- the shapes that compile to something that cannot be run
LISTINGS = {
    "b16": "B16 byte spin loop, rep before its back edge",
}
BRANCHES = ("bcc", "bcs", "beq", "bne", "bmi", "bpl", "bvc", "bvs")


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


def loop_width_bug(listing):
    """True if the listing has a loop whose accumulator width is switched
    to 16 bits just before its backward branch with no switch back inside
    it: the loop's second pass then runs 8-bit code as 16-bit (B16)."""
    ins = []
    for line in open(listing):
        m = re.match(r"\s*\\ [0-9a-f]{6} \S*\s+(?:(`?\?L\d+`?|\w+):)?\s*(\S+)?\s*(.*)$",
                     line)
        if not m:
            continue
        label, op, arg = m.groups()
        if op is None and label is None:
            continue
        ins.append((label.strip("`") if label else None, op, arg.strip()))
    labels = {label: i for i, (label, _, _) in enumerate(ins) if label}
    for i, (_, op, arg) in enumerate(ins):
        if op not in BRANCHES or i == 0:
            continue
        tgt = labels.get(arg.strip("`"))
        if tgt is None or tgt > i:
            continue
        pop, parg = ins[i - 1][1], ins[i - 1][2]
        if pop == "rep" and "32" in parg and not any(
                o == "sep" and "32" in a for _, o, a in ins[tgt:i - 1]):
            return True
    return False


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

    # B6 and B11 are compiler crashes: compile each file alone and read
    # the outcome from the compiler
    crashes = {}
    for tag, note in CRASHES.items():
        r = subprocess.run([cc, "--code-model=large", "--data-model=small",
                            f"-O{a.O}", "-o", os.path.join(a.out, f"{tag}.o"),
                            os.path.join(ROOT, "tools", "ccbug", f"{tag}.c")],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        crashes[note] = r.returncode != 0 and "internal error" in r.stdout
    # A refusal the compiler should not make: the file compiles the day the
    # bug is fixed, so the bug is "it did not compile".
    for tag, note in REFUSALS.items():
        r = subprocess.run([cc, "--code-model=large", "--data-model=small",
                            f"-O{a.O}", "-o", os.path.join(a.out, f"{tag}.o"),
                            os.path.join(ROOT, "tools", "ccbug", f"{tag}.c")],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True)
        crashes[note] = r.returncode != 0 and "negative size" in r.stdout
    # B16 compiles, to code that derails on its second pass: compile the
    # file alone and read its listing for the shape
    for tag, note in LISTINGS.items():
        lst = os.path.join(a.out, f"{tag}.lst")
        run([cc, "--code-model=large", "--data-model=small", f"-O{a.O}",
             "-o", os.path.join(a.out, f"{tag}.o"), "--list-file", lst,
             os.path.join(ROOT, "tools", "ccbug", f"{tag}.c")])
        crashes[note] = loop_width_bug(lst)

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
    for note, here in crashes.items():
        print(f"  {note:42s} {'compiles':>16s}   "
              f"{'still present' if here else 'FIXED upstream'}")
        present += here
    print()
    if bad:
        print(f"check-cc: FAILED -- {bad} workaround shape(s) miscompile")
        return 1
    print(f"check-cc: PASSED -- every workaround shape is right; "
          f"{present} of {sum(1 for v in RESULTS.values() if v[1] == 'bug') + len(CRASHES) + len(LISTINGS) + len(REFUSALS)} "
          f"bug shapes still present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
