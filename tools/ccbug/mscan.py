#!/usr/bin/env python3
"""Find a `jsl` the compiler reaches with an 8-BIT accumulator.

Calypsi's own assembly-interface documentation is explicit: "The 65816 is
used in native mode with 16 bit registers.  In some situations the runtime
needs to switch to 8 bit register mode ... This is done automatically and
the compiler will then switch back to 16 bits mode."  So a called function
is compiled assuming M=0, and reaching its `jsl` with M=1 means its first
16-bit immediate decodes short -- the operand's high byte is executed as
an opcode.  That is B17, and RetroWP met it as a BRK inside a callee.

THE SCAN IS DELIBERATELY CONSERVATIVE, because the alternative is a
wrong answer that reads as authoritative:

  * The accumulator is 16-bit at a function's entry label.
  * `sep #32` makes it 8-bit, `rep #32` makes it 16-bit.
  * A `##` immediate makes it 16-bit, because the assembler only takes
    that form when the accumulator is wide.  This is what caught the
    first version of this scan reporting nonsense: the compiler calls an
    outlined fragment while narrow and the fragment widens before
    returning, so the next instruction is a `##` and the call after it is
    perfectly safe.
  * ANY CALL makes it UNKNOWN afterwards, for the same reason -- the
    callee's exit width is not this scan's to guess.
  * ANY OTHER LABEL makes it UNKNOWN, because control can arrive there
    from a branch whose width this scan does not track.

Only a call to a NAMED function is ever reported.  A call to a `?Lnnnn`
fragment is the compiler talking to itself, and it knows what mode it
left.  An unknown state is never reported.  This will miss a case that
crosses a branch; it will not invent one.

    python3 tools/ccbug/mscan.py FILE.s [FILE.s ...]
    python3 tools/ccbug/mscan.py --tree        # every C source gem4xe builds
"""
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CALYPSI = os.environ.get("CALYPSI",
                         os.path.expanduser("~/dev/toolchains/calypsi-65816"))

LABEL = re.compile(r"^([A-Za-z_`?][\w`?.$]*):")
SEP = re.compile(r"^\s+sep\s+#(0x20|32)\b", re.I)
REP = re.compile(r"^\s+rep\s+#(0x20|32)\b", re.I)
JSL = re.compile(r"^\s+(jsl|jsr)\s+(.*)$", re.I)
WIDE_IMM = re.compile(r"##")        # only assembles when A is 16-bit
FUNC = re.compile(r"^([A-Za-z_][\w.$]*):")

NARROW, WIDE, UNKNOWN = "narrow", "wide", "unknown"


def scan(path):
    """[(function, line number, target)] for every call reached narrow."""
    hits, func, state = [], "?", UNKNOWN
    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if line.lstrip().startswith(";"):
                continue
            m = LABEL.match(line)
            if m:
                if FUNC.match(line) and not m.group(1).startswith("`"):
                    func, state = m.group(1), WIDE     # a function entry
                else:
                    state = UNKNOWN                    # a branch target
                continue
            if SEP.match(line):
                state = NARROW
                continue
            if REP.match(line):
                state = WIDE
                continue
            m = JSL.match(line)
            if m:
                tgt = m.group(2).strip()
                named = not re.search(r"`\?L", tgt)
                if state == NARROW and named:
                    hits.append((func, n, tgt))
                state = UNKNOWN        # the callee's exit width is its own
                continue
            if WIDE_IMM.search(line):
                state = WIDE
    return hits


def tree_sources():
    """The C the tree compiles, with the flags it compiles them with."""
    out = []
    for base, _, names in os.walk(os.path.join(ROOT, "src")):
        for name in sorted(names):
            if name.endswith(".c"):
                out.append(os.path.join(base, name))
    return out


def main(argv):
    if "--tree" not in argv:
        files = [a for a in argv if a.endswith(".s")]
        if not files:
            sys.exit(__doc__)
        total = 0
        for f in files:
            for func, n, tgt in scan(f):
                print(f"{f}:{n}: {func} calls {tgt} with an 8-bit accumulator")
                total += 1
        print(f"{total} call(s) reached narrow")
        return 1 if total else 0

    cc = os.path.join(CALYPSI, "bin", "cc65816")
    if not os.path.exists(cc):
        sys.exit("Calypsi not installed")
    out = os.path.join(ROOT, "build", "mscan")
    os.makedirs(out, exist_ok=True)
    total, skipped = 0, 0
    for src in tree_sources():
        stem = os.path.basename(src)[:-2]
        asm = os.path.join(out, stem + ".s")
        r = subprocess.run(
            [cc, "--code-model=large", "--data-model=small", "-O2",
             "-I", os.path.join(ROOT, "src"),
             "-I", os.path.join(ROOT, "src", "app"),
             "-I", os.path.join(ROOT, "build"),
             "--assembly-source", asm, "-c",
             "-o", os.path.join(out, stem + ".o"), src],
            capture_output=True, text=True)
        if r.returncode:
            skipped += 1           # a source this flag set cannot build alone
            continue
        for func, n, tgt in scan(asm):
            print(f"{os.path.relpath(src, ROOT)}: {func} calls {tgt} "
                  f"with an 8-bit accumulator ({os.path.relpath(asm, ROOT)}:{n})")
            total += 1
    print(f"mscan: {total} call(s) reached narrow; "
          f"{skipped} source(s) could not be compiled alone and were skipped")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
