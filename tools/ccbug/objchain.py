#!/usr/bin/env python3
"""Who keeps a function alive in a link -- from the OBJECT, not the map.

    python3 tools/ccbug/objchain.py build/app/gemlib.o clock [--hops N]

WHY THE MAP IS NOT ENOUGH.  ln65816's --list-file names a section's
referrers BY SYMBOL, and never prints a local label (`?L1466`) as one.
cc65816 at -O2 shares an identical tail across functions in one
translation unit and parks it inside one function's section, so other
functions reach that section through a local label -- a reference the
map cannot show.  Read the map alone and a function kept by such a hop
looks referrer-less; in gemlib.c, `clock` and `Frename` both do.  That
was read as a dead-section bug in the linker, twice on the same day, and
it was not one: `clock` was held by a live three-hop chain from
Fread/Fwrite/Fseek through Frename and Tgettimeofday -- and only under
--data-model=large; the small model shares no tails (tools/ccbug/
README.md, "Reading the map").  This walks the object's relocations,
which are the truth the map summarises.

HOW.  `readelf -SW` gives every section, and for a relocation section its
file offset and its Inf, the section it applies to.  Calypsi names every
relocation section `.relocations`, so the `-rW` blocks are matched to
their targets by that offset.  Each entry names a symbol; `-sW` gives the
symbol's section, local labels included.  That is the edge: the target
section references the symbol's section.  Walking the edges backwards
from the named symbol's section, hop by hop, names every section -- and
every global function -- whose presence in a link brings it in.

One more trap, met on the way: a `\\b` in a regex cannot match a symbol
that starts with '?', so grep for local labels without it.
"""
import re
import subprocess
import sys
from collections import defaultdict, deque


def readelf(flag, obj):
    return subprocess.run(["readelf", flag, obj], capture_output=True,
                          text=True, check=True).stdout


def sections(obj):
    """index -> (name, type, off, inf)"""
    out = {}
    for ln in readelf("-SW", obj).splitlines():
        m = re.match(r"\s*\[\s*(\d+)\]\s+(\S*)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                     r"\s+([0-9a-f]+)\s+(\S+)\s+(?:\S+\s+)?(\d+)\s+(\d+)\s+(\d+)\s*$", ln)
        if m:
            idx, name, typ, off, inf = (int(m.group(1)), m.group(2), m.group(3),
                                        int(m.group(5), 16), int(m.group(9)))
            out[idx] = (name, typ, off, inf)
    return out


def symbols(obj):
    """name -> section index; section index -> [global names]"""
    by_name, by_sec = {}, defaultdict(list)
    for ln in readelf("-sW", obj).splitlines():
        m = re.match(r"\s*\d+:\s+[0-9a-f]+\s+\d+\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)\s*$", ln)
        if not m:
            continue
        typ, bind, ndx, name = m.groups()
        if ndx in ("UND", "ABS"):
            continue
        by_name[name] = int(ndx)
        if bind == "GLOBAL":
            by_sec[int(ndx)].append(name)
    return by_name, by_sec


def edges(obj, secs, by_name):
    """referenced section -> {(referencing section, symbol)}"""
    by_off = {off: inf for idx, (name, typ, off, inf) in secs.items()
              if typ in ("RELA", "REL")}
    back = defaultdict(set)
    target = None
    for ln in readelf("-rW", obj).splitlines():
        m = re.match(r"Relocation section '.*' at offset 0x([0-9a-f]+)", ln)
        if m:
            target = by_off.get(int(m.group(1), 16))
            continue
        if target is None:
            continue
        m = re.match(r"\s*[0-9a-f]+\s+[0-9a-f]+\s+.*?\s+[0-9a-f]+\s+(\S+)\s*\+\s*-?\d+", ln)
        if m and m.group(1) in by_name:
            back[by_name[m.group(1)]].add((target, m.group(1)))
    return back


def main(argv):
    if len(argv) < 2:
        print(__doc__.split("\n", 3)[2].strip())
        return 2
    obj, sym = argv[0], argv[1]
    hops = int(argv[argv.index("--hops") + 1]) if "--hops" in argv else 4
    secs = sections(obj)
    by_name, by_sec = symbols(obj)
    if sym not in by_name:
        print(f"{sym}: not defined in {obj}")
        return 1
    back = edges(obj, secs, by_name)
    label = lambda s: ", ".join(by_sec.get(s, [])) or f"section {s}"

    start = by_name[sym]
    print(f"{sym} is in section {start} ({label(start)})")
    seen, frontier = {start: 0}, deque([start])
    reached = defaultdict(list)
    while frontier:
        s = frontier.popleft()
        if seen[s] >= hops:
            continue
        for t, via in sorted(back.get(s, ())):
            if t not in seen:
                seen[t] = seen[s] + 1
                frontier.append(t)
            if t != s:
                reached[seen[s] + 1].append((t, s, via))
    for hop in sorted(reached):
        print(f"\n  hop {hop}:")
        for t, s, via in reached[hop]:
            print(f"    section {t:3d} ({label(t)})  ->  section {s} via {via}")
    names = sorted(n for s, d in seen.items() if d > 0 for n in by_sec.get(s, []))
    print(f"\n{len(names)} global function(s) bring {sym} in within {hops} hop(s):")
    print("  " + " ".join(names))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
