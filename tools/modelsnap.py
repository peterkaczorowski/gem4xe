#!/usr/bin/env python3
"""Every model screen and every returned record, hashed.

WHAT IT IS FOR.  tools/vdiref.py and tools/aesref.py are the
specification; a refactor of either is a refactor of the thing the
target is measured against, and "the emulator still agrees" takes eight
minutes and only says the two moved together.  This says they did not
move at all: it runs all 86 VDI conformance scripts and all 13 AES cases
through the models, hashes every screen and every returned record, and
prints a digest per case and one for the lot.

    8a637b7a48672f31  == ALL 99 CASES ==

Two and a half seconds.  Take it before a refactor, take it after, and
the digest either has not moved or names the case that changed.  It is
what made the device seam (docs/phase35.md) safe to cut.

It is not a gate -- `make test` does not run it, because a digest that
SHOULD change when the specification changes is a bad thing to have
failing a build.  It is a tool for the person doing the changing.

  python3 tools/modelsnap.py [> before.txt]
"""
import hashlib
import os
import sys
import zlib

ROOT = os.path.expanduser("~/dev/gem4xe")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "emu"))

import vdiref                                   # noqa: E402
import aesref                                   # noqa: E402
import m3_vdi                                   # noqa: E402
import m4_aes                                   # noqa: E402


def vdi_cases():
    save = vdiref.VramForm.save_buffer(0)
    forms = {"icon": (m3_vdi.ICON_BITS, m3_vdi.ICON_WDW),
             "to_save": (None, save), "from_save": (save, None)}
    out = []
    for idx, (name, script) in enumerate(m3_vdi.CASES):
        full = [(vdiref.V_OPNWK, (), vdiref.WORK_IN), (vdiref.V_CLRWK,)] + script
        resolved = [(r[0], r[1] if len(r) > 1 else (),
                     r[2] if len(r) > 2 else (),
                     forms[r[3]] if len(r) > 3 and r[3] is not None else None,
                     r[4] if len(r) > 4 else 0)
                    for r in full]
        ref = vdiref.VDI()
        ref.run(resolved)
        rgb = ref.to_rgb()
        h = hashlib.sha256()
        for row in rgb:
            for px in row:
                h.update(bytes(px))
        h.update(repr(ref.results).encode())
        out.append((f"m3[{idx:02d}] {name}", h.hexdigest()[:16]))
    return out


def digest_of(v, results, shots=()):
    h = hashlib.sha256()
    for row in v.to_rgb():
        for px in row:
            h.update(bytes(px))
    for shot in shots:
        for row in shot:
            for px in row:
                h.update(bytes(px))
    h.update(repr(results).encode())
    return h.hexdigest()[:16]


def aes_cases():
    from m4_aes import Layout
    out = []
    for idx, (name, build, body) in enumerate(m4_aes.CASES):
        script = m4_aes.PRELUDE + body
        L = Layout(0x6800)
        objs = build(L)
        v, a, want = aesref.run(script, objs, L.mem)
        out.append((f"m4[{idx:02d}] {name}",
                    digest_of(v, want, getattr(a, "shots", ()))))
    return out


def main():
    rows = vdi_cases() + aes_cases()
    for name, digest in rows:
        print(f"{digest}  {name}")
    total = hashlib.sha256("".join(d for _, d in rows).encode()).hexdigest()[:16]
    print(f"{total}  == ALL {len(rows)} CASES ==")


if __name__ == "__main__":
    main()
