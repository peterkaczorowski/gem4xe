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
CALL = re.compile(r"^\s+(jsl|jsr)\s+(.*)$", re.I)
WIDE_IMM = re.compile(r"##")        # only assembles when A is 16-bit
FUNC = re.compile(r"^([A-Za-z_][\w.$]*):")
BRANCH = re.compile(r"^\s+(bra|beq|bne|bcc|bcs|bmi|bpl|bvc|bvs|brl|jmp)\s+"
                    r"`?([\w`?.$]+)`?", re.I)
ALWAYS = ("bra", "brl", "jmp")

NARROW, WIDE, UNKNOWN = "narrow", "wide", "unknown"


def meet(a, b):
    """Two paths into one label.  Agreement is knowledge; anything else
    is not, and this scan never reports what it does not know."""
    if a is None:
        return b
    if b is None:
        return a
    return a if a == b else UNKNOWN


def parse(path):
    """The listing as [(kind, text, label_or_target, line_no)]."""
    out = []
    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if line.lstrip().startswith(";") or not line.strip():
                continue
            m = LABEL.match(line)
            if m:
                out.append(("label", line, m.group(1).strip("`"), n))
                rest = line[m.end():]
                if not rest.strip():
                    continue
                line = rest          # a label with an instruction beside it
            if SEP.match(line):
                out.append(("sep", line, None, n))
            elif REP.match(line):
                out.append(("rep", line, None, n))
            elif CALL.match(line):
                out.append(("call", line, CALL.match(line).group(2).strip(), n))
            else:
                b = BRANCH.match(line)
                if b:
                    out.append(("branch", line, (b.group(1).lower(),
                                                 b.group(2).strip("`")), n))
                elif WIDE_IMM.search(line):
                    out.append(("wide", line, None, n))
                elif re.match(r"^\s+rtl\b|^\s+rts\b", line, re.I):
                    out.append(("ret", line, None, n))
    return out


def scan(path):
    """[(function, line, target)] for every call to a NAMED function that
    every path reaches with an 8-bit accumulator.

    A forward dataflow over the listing's labels, iterated to a fixed
    point.  A label's state is the MEET of its predecessors -- the
    fall-through and every branch that names it -- so a join whose paths
    all agree is known, which is the case a single pass has to give up on
    (RetroWP's linebreak.c reaches its failing call through exactly such
    a join).  Disagreement, or any unknown predecessor, stays unknown and
    is never reported.
    """
    items = parse(path)
    # entry state of each label
    state = {}
    for kind, _, lab, _ in items:
        if kind == "label":
            state[lab] = None
    for kind, _, lab, _ in items:
        if kind == "label" and FUNC.match(lab + ":") and not lab.startswith("?"):
            state[lab] = WIDE          # a function is entered 16-bit

    hits = []
    for _ in range(12):                # small listings converge at once
        changed = False
        cur, func = UNKNOWN, "?"
        hits = []
        for kind, _, arg, n in items:
            if kind == "label":
                if FUNC.match(arg + ":") and not arg.startswith("?"):
                    func = arg
                    cur = WIDE
                else:
                    new = meet(state.get(arg), cur if cur else None)
                    if new != state.get(arg):
                        state[arg] = new
                        changed = True
                    cur = state.get(arg) or UNKNOWN
            elif kind == "sep":
                cur = NARROW
            elif kind == "rep" or kind == "wide":
                cur = WIDE
            elif kind == "call":
                if cur == NARROW and not re.search(r"`\?L", arg):
                    hits.append((func, n, arg))
                cur = UNKNOWN          # the callee's exit width is its own
            elif kind == "branch":
                op, target = arg
                if target in state:
                    new = meet(state.get(target), cur)
                    if new != state.get(target):
                        state[target] = new
                        changed = True
                if op in ALWAYS:
                    cur = UNKNOWN      # nothing falls through
            elif kind == "ret":
                cur = UNKNOWN
        if not changed:
            break
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
