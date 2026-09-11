#!/usr/bin/env python3
"""Where bank $00 has gone, and whether there is enough of it left.

WHY THIS EXISTS.  gem4xe's binding constraint is not the 14.5 MB of
linear RAM, it is the 64 KB bank the 65816 addresses DATA in: under the
small data model every C global is reached absolutely through the data
bank register, so `cdata`, `idata`, `data` and `zdata` are bank $00 by
requirement rather than by preference (src/gem4xe.scm).  On an Atari most
of that bank is spoken for before gem4xe starts -- the OS ROM, the
hardware, the DOS, the MEMAC window, a SpartaDOS X cartridge -- and what
is left is about 31 KB, of which the engine's data is 7.5 KB and the
application pool is 14 KB.

Running out of it is not a gentle failure.  It is a link that stops with
"Failed to place 1 section fragment(s)" in the middle of a change, or --
worse -- a pool that still holds the desktop but no longer leaves GEMDOS
a read slice worth having, which nothing fails on and everything gets
slower for.  So the budget is measured here and asserted, rather than
discovered.

WHAT IT READS.  The linker's own map for the regions, each .g4a's header
for what a program reserves, and each .RSC's header for what a resource
costs the pool once its icon bitmaps have gone to far memory
(src/aes/rsrc.c).  Nothing is written down that a build artefact already
knows, except the engine's own pool overhead, which is one constant with
its source named -- and test-boot asserts the LIVE figure on the real
machine, so a drift in that constant is caught rather than believed.
"""
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BUILD = os.path.join(ROOT, "build")

# The engine's own take from the pool, before any program: the process
# records (src/aes/proc.h, NUM_PROCS * sizeof(PROC)) and an eight-message
# queue for each accessory that loads.  test-boot checks the live number.
PROC_STORE = 4 * 52
ACC_QUEUE = 8 * 8 * 2

# What must be left, and why.
FLOORS = {
    "LoRAM": (256, "the engine's stack and every one of its globals; a "
                   "change that needs a new table has nowhere else to go"),
    "Near": (128, "near code and rodata"),
    "DirectPage": (32, "the compiler's register file travels with a "
                       "context switch (src/sys/ctx.s)"),
}
POOL_FLOOR = (2048, "GEMDOS reads files and directories through a slice of "
                    "whatever the pool has spare, capped at 2 KB and "
                    "falling back to 64 bytes of stack below 128 "
                    "(src/sys/gemdos.c)")


def regions(mapfile):
    """(name, size, used%, largest free) for each memory in a linker map."""
    out = {}
    for ln in open(mapfile):
        m = re.match(r"^(\w+)\s+([0-9a-f]{6})-([0-9a-f]{6})\s+([0-9a-f]+)\s+"
                     r"([\d.]+)%\s+\S+\s+(\S+)", ln)
        if m:
            free = 0 if m.group(6) == "none" else int(m.group(6), 16)
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(4), 16),
                               float(m.group(5)), free)
    return out


def g4a_near(path):
    """What a .G4A reserves in the pool: its whole near region."""
    d = open(path, "rb").read(20)
    assert d[:4] == b"G4A\x01", (path, d[:4])
    return struct.unpack("<H", d[6:8])[0]


def rsc_pool(path):
    """What a .RSC costs the pool -- the file, less the icon bitmaps when
    rs_load can move them to far memory (src/aes/rsrc.c)."""
    d = open(path, "rb").read(36)
    h = struct.unpack(">18H", d)
    imdata, nbb, nimages, rssize = h[7], h[14], h[16], h[17]
    if nbb or nimages:
        return rssize, 0
    return imdata, rssize - imdata


def main(argv):
    quiet = "--quiet" in argv
    bad = []
    r = regions(os.path.join(BUILD, "gem.map"))

    def say(*a):
        if not quiet:
            print(*a)

    say("bank $00, as the linker left it")
    say(f"  {'region':<12} {'size':>6} {'used':>6}  {'free':>6}")
    for name in ("DirectPage", "LoRAM", "Near", "Window", "AppPool", "Stage"):
        if name not in r:
            continue
        base, size, pct, free = r[name]
        say(f"  {name:<12} {size:6d} {pct:5.1f}%  {free:6d}   ${base:04X}")
        floor = FLOORS.get(name)
        if floor and free < floor[0]:
            bad.append(f"{name} has {free} bytes free, wanted {floor[0]} -- "
                       f"{floor[1]}")

    pool = r["AppPool"][1]
    desk = g4a_near(os.path.join(BUILD, "desktop.g4a"))
    desk_rsc, desk_far = rsc_pool(os.path.join(BUILD, "desktop.rsc"))
    accs = []
    for name, rsc in (("clockacc", "clock"),):
        p = os.path.join(BUILD, name + ".g4a")
        if os.path.exists(p):
            near = g4a_near(p)
            pr, _ = rsc_pool(os.path.join(BUILD, rsc + ".rsc"))
            accs.append((name.upper(), near, pr))

    # The bump allocator, in the order the machine runs it: proc_init,
    # then each accessory (queue, near region, resource), then the
    # desktop.  A PROGRAM's near region is page-aligned (app.c:
    # pool_alloc(near_size, 0x100)) and everything else is word-aligned,
    # so the arithmetic is a simulation of pool_alloc rather than a sum --
    # which is what makes it agree with the figure test-boot reads off the
    # live machine instead of being close to it.
    base = r["AppPool"][0]
    brk = base

    def take(n, align, what):
        nonlocal brk
        at = (brk + align - 1) & ~(align - 1)
        brk = at + n
        say(f"  {what:<34} {-(n + at - (brk - n)):6d}"
            if False else f"  {what:<34} {-n:6d}   ${at:04X}")

    say("")
    say("the application pool, with everything the product ships resident")
    say(f"  {'pool':<34} {pool:6d}   ${base:04X}")
    take(PROC_STORE, 2, "process records")
    for nm, near, pr in accs:
        take(ACC_QUEUE, 2, nm + " message queue")
        take(near, 0x100, nm + " near region")
        take(pr, 2, nm + " resource")
    # Where the shell marks the floor: everything permanent is below it
    # and no program's exit may wind back past it (src/sys/app.c).  The
    # desktop and its resource are above it, and come and go with it.
    say(f"  {'-- permanent below here':<34} {'':6}   ${brk:04X}")
    take(desk, 0x100, "the desktop")
    take(desk_rsc, 2, "its resource")
    free = base + pool - brk
    say(f"  {'= free':<34} {free:6d}   "
        f"({desk_far} bytes of icons are far, not here)")
    if free < POOL_FLOOR[0]:
        bad.append(f"the pool would have {free} bytes free, wanted "
                   f"{POOL_FLOOR[0]} -- {POOL_FLOOR[1]}")

    say("")
    if bad:
        for b in bad:
            print("FAIL:", b)
    say("memory: ok" if not bad else f"memory: {len(bad)} over budget")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
