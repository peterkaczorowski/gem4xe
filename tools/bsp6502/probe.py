#!/usr/bin/env python3
"""What does Calypsi's 6502 banking actually DO to a cross-bank call?

The docs say "once properly linked, the compiler runtime handles the
bank system automatically", which reads like a call into another bank
gets a trampoline.  It does not.  This asks the toolchain directly and
asserts what it answers, so the answer stays true rather than
remembered:

  1. a call into banked code compiles to a PLAIN `jsr` at the slot's
     runtime address -- no bank switch, no veneer;
  2. that is not an Atari-target shortcoming: Calypsi's OWN cx16-banked
     map produces the same plain `jsr`;
  3. with more than one bank instance, two functions in DIFFERENT banks
     get the SAME runtime address, and the linker says nothing.

Together those mean the program owns the bank register, a wrong call
cannot be distinguished from a right one by the linked image alone, and
nothing in the toolchain will tell you when you got it wrong.

Run:  python3 tools/bsp6502/probe.py
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkxex                                # noqa: E402

CAL = os.environ.get("CALYPSI6502",
                     os.path.expanduser("~/dev/toolchains/calypsi-6502"))
CC = os.path.join(CAL, "bin", "cc6502")
LN = os.path.join(CAL, "bin", "ln6502")
CX16 = os.path.join(CAL, "linker-rules", "cx16-banked.scm")
OUT = os.path.join(ROOT, "build", "bsp6502")


def run(*args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"{args[0]} failed:\n{r.stdout}\n{r.stderr}")
    return r.stdout


def funcs(elf):
    """Every FUNC symbol, as {address: [names]}."""
    out = subprocess.run(["readelf", "-sW", elf], capture_output=True,
                         text=True).stdout
    by = {}
    for ln in out.splitlines():
        p = ln.split()
        if len(p) >= 8 and p[3] == "FUNC":
            by.setdefault(int(p[1], 16), []).append(p[-1])
    return by


def calls_from(elf, addr, count=64):
    """The jsr targets in `count` bytes at `addr`."""
    segs, _ = mkxex.read_elf(elf)
    for a, d in segs:
        if a <= addr < a + len(d):
            code = d[addr - a:addr - a + count]
            return [code[i + 1] | (code[i + 2] << 8)
                    for i in range(len(code) - 2) if code[i] == 0x20]
    return []


def main():
    os.makedirs(OUT, exist_ok=True)
    fails = []
    for src in ("main.c", "banked.c"):
        run(CC, "-O2", "-c", "-o", os.path.join(OUT, src[:-2] + ".o"),
            os.path.join(HERE, src))
    objs = [os.path.join(OUT, "main.o"), os.path.join(OUT, "banked.o")]

    # 1 + 3: the Atari map, with the slot shrunk so more than one bank
    # instance has to be generated.
    scm = open(os.path.join(HERE, "atari-banked.scm")).read()
    small = os.path.join(OUT, "atari-small.scm")
    open(small, "w").write(scm.replace("#x4000 . #x7fff", "#x4000 . #x4fff"))
    elf = os.path.join(OUT, "proto.elf")
    lst = os.path.join(OUT, "proto.lst")
    run(LN, small, *objs, "-o", elf, "--list-file", lst,
        "--rtattr", "exit=simplified")

    slots = len(re.findall(r"^BankSlot\s", open(lst).read(), re.M))
    print(f"bank instances generated        : {slots}")
    if slots < 2:
        fails.append("only one bank instance -- the interesting case never arose")

    by = funcs(elf)
    name = {n: a for a, ns in by.items() for n in ns}
    targets = calls_from(elf, name["main"])
    direct = [t for t in targets if t in (name["far_a"], name["far_b"])]
    print(f"main's calls into banked code   : "
          f"{', '.join(f'jsr ${t:04X}' for t in direct)}")
    if len(direct) != 2:
        fails.append("main does not reach the banked functions with a plain jsr "
                     "-- something DOES trampoline, and the design can change")

    shared = {a: ns for a, ns in by.items() if len(ns) > 1}
    print(f"runtime addresses shared by >1  : {len(shared)}")
    for a, ns in list(shared.items())[:3]:
        print(f"    ${a:04X}  {' and '.join(ns)}")
    if not shared:
        fails.append("no two functions share a runtime address -- banks may be "
                     "distinguishable after all, which would be good news")

    # 2: the control.  Calypsi's own supported banked target.
    cx = os.path.join(OUT, "cx16.elf")
    run(LN, CX16, *objs, "-o", cx, "--rtattr", "exit=simplified")
    cby = funcs(cx)
    cname = {n: a for a, ns in cby.items() for n in ns}
    ct = [t for t in calls_from(cx, cname["main"])
          if t in (cname["far_a"], cname["far_b"])]
    print(f"the same call on cx16-banked    : "
          f"{', '.join(f'jsr ${t:04X}' for t in ct)}")
    if len(ct) != 2:
        fails.append("cx16 does NOT emit a plain jsr, so Calypsi has a bank-call "
                     "mechanism the Atari map is simply missing -- find it")

    print()
    if fails:
        print("bsp6502: FINDINGS CHANGED -- re-read before trusting docs/6502.md")
        for f in fails:
            print("   ", f)
        return 1
    print("bsp6502: banking is PLACEMENT ONLY -- the program owns the bank "
          "register,\n         and nothing in the toolchain checks a "
          "cross-bank call")
    return 0


if __name__ == "__main__":
    sys.exit(main())
