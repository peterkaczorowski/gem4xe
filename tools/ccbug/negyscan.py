#!/usr/bin/env python3
"""Find a negative Y index used with long (24-bit) addressing.

The 65816 adds Y to a 24-bit base as an UNSIGNED 16-bit value, so

    ldy     ##-8
    lda     [(_Dp+8)],y

does not read eight bytes below the pointer: it reads 65,528 bytes above
it, one bank away.  Reads come back as garbage, writes vanish into
whatever lives there.

This is Calypsi #82, found by the MicroPython SNES port on 5.17 and fixed
by hth313 for 5.18 -- and it is the family gem4xe's own B13/B14 belong to,
where `pt[-2]` through a NEAR pointer "came back as neither point".  The
scan exists anyway, for three reasons: the shape is silent when it is
wrong, gem4xe addresses far memory constantly, and the author who found it
reports that WHICH FORM THE COMPILER PICKS IS REGISTER-PRESSURE ROULETTE
per compilation -- so testing cannot find it and only looking at the
output can.  The same argument mscan.py exists on.

The safe form the compiler emits instead adjusts the base and then uses a
non-indexed long access, which preserves the bank:

    sec
    lda     dp:.tiny _Dp
    sbc     ##4
    sta     dp:.tiny _Dp
    lda     [.tiny _Dp]

A NEGATIVE Y IS NOT ALWAYS WRONG.  With `lda addr,y` or `lda (dp),y` the
address is 16-bit and wrapping is what C's pointer arithmetic already
does.  Only the long forms -- `[dp],y` and `addr.l,y` -- carry a bank that
Y cannot borrow from, so only those are reported.

    python3 tools/ccbug/negyscan.py FILE.s [FILE.s ...]
    python3 tools/ccbug/negyscan.py --tree     # every C source gem4xe builds
"""
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CALYPSI = os.environ.get("CALYPSI",
                         os.path.expanduser("~/dev/toolchains/calypsi-65816"))

# A label and the first instruction of a function share a line in Calypsi's
# output (`below2:     sec`), so the instruction is not always indented.
# The first version of this scan required leading whitespace and therefore
# could not see a negative Y at any function's entry -- it reported one of
# the fixture's two sites and zero over the whole tree, which read as good
# news.  That is why the fixture asserts a COUNT.
INSN = r"^(?:[A-Za-z_`?][\w`?.$]*:)?\s+"
NEGY = re.compile(INSN + r"ldy\s+##\s*(-\d+|0x[fF][0-9a-fA-F]{3})\b", re.I)
# `[...],y` is direct-page indirect long indexed; `something.l,y` is
# absolute long indexed.  Both carry a bank byte.
LONGIDX = re.compile(r"(\[[^\]]*\]\s*,\s*y\b|\.l\s*,\s*y\b)", re.I)
# Anything that can change Y between the load and the use.
SETSY = re.compile(INSN + r"(ldy|tay|txy|iny|dey|ply|tsy)\b", re.I)

WINDOW = 8              # how far a use may sit from its `ldy`


def scan(path):
    """[(line number, the ldy, the long-indexed use)] for `path`."""
    lines = open(path, errors="ignore").read().splitlines()
    hits = []
    for i, ln in enumerate(lines):
        if not NEGY.match(ln):
            continue
        for j in range(i + 1, min(i + 1 + WINDOW, len(lines))):
            nxt = lines[j]
            if LONGIDX.search(nxt):
                hits.append((i + 1, ln.strip(), nxt.strip()))
                break
            if SETSY.match(nxt):        # Y is something else now
                break
    return hits


def tree_sources():
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
            for n, ldy, use in scan(f):
                print(f"{f}:{n}: {ldy!r} then {use!r}")
                total += 1
        print(f"{total} negative-Y long access(es)")
        return 1 if total else 0

    cc = os.path.join(CALYPSI, "bin", "cc65816")
    if not os.path.exists(cc):
        sys.exit("Calypsi not installed")
    out = os.path.join(ROOT, "build", "negyscan")
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
            skipped += 1
            continue
        for n, ldy, use in scan(asm):
            print(f"{os.path.relpath(src, ROOT)}: {ldy!r} then {use!r} "
                  f"({os.path.relpath(asm, ROOT)}:{n})")
            total += 1
    print(f"negyscan: {total} negative-Y long access(es); "
          f"{skipped} source(s) could not be compiled alone and were skipped")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
