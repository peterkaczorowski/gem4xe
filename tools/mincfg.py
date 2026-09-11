#!/usr/bin/env python3
"""GEM4XE.CFG with the prose taken out, for the disk that has no room.

The double-density DOS 2 floppy has about 2 KB free once GEM's 122 KB and
the desktop are on it, and the shipped GEM4XE.CFG is 1,922 bytes of which
nearly all is documentation -- every setting in it is commented out.  The
file says so itself: "No file at all is the same as this one with
everything commented out, which is what it is."

So that disk gets this: every KEY=VALUE line the full file has (commented
out, as they are there), the one-line description of each, and a pointer
to where the prose lives.  Generated rather than written, so the keys
cannot drift from the file that documents them.
"""
import re
import sys

HEAD = """# GEM4XE.CFG -- what gem4xe should do before it has a screen.
#
# The short form.  This disk is the system and nothing else; the same
# file with its documentation is on the install disk, on the card, and in
# system/ of the release.  A line is KEY=VALUE; '#' or ';' is a comment;
# a key or a value gem4xe does not know is ignored, never refused.
#
"""


def main(argv):
    src, dst = argv[1], argv[2]
    text = open(src, "r", encoding="latin-1").read().replace("\r\n", "\n")
    out = [HEAD]
    # Every settable line the full file carries, with the VALUES its own
    # documentation lists, so this file says what may be typed.
    heads = [m for m in re.finditer(r"^# (\w+) -- (.*)$", text, re.M)]
    for i, m in enumerate(heads):
        key, what = m.group(1), m.group(2)
        # only as far as the NEXT key's heading, or the value lists run
        # into each other
        stop = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        vals = re.findall(r"^#   (\w+)\s", text[m.end():stop], re.M)
        out.append(f"# {key} -- {what}\n")
        if vals:
            out.append(f"#   {' '.join(vals)}\n")
        out.append(f"# {key}={vals[0] if vals else ''}\n#\n")
    blob = "".join(out).encode("latin-1")
    open(dst, "wb").write(blob.replace(b"\n", b"\x9b"))
    print(f"{dst}: {len(blob)} bytes, from {src} ({len(text)})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
