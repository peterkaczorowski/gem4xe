#!/usr/bin/env python3
"""A loadable font: the character set comes off the disk.

The 8x8 face gem4xe links is the Atari ST's, whose high half is the
accented Latin letters of Western Europe.  Polish, Czech, Greek and
Cyrillic need a different set, so the strip is loadable: a GEM `.FNT`
whose cell is the same 8x8 replaces the 2 KB of glyphs
(src/vdi/font.c, docs/shipping.md section 5).  `make fonts` writes
Latin-2, Cyrillic, Greek and Turkish out of an EmuTOS checkout.

The gate uses neither of those, because a gate should not need a
checkout: it uses the system font INVERTED (tools/mkfnt.py --invert),
built from the strip that is checked in.  Every glyph differs from the
linked one, which is what makes "the file is being drawn from" visible
in a screenshot rather than a matter of trust.

Two disks and one script:

    m21-font.atr  SYSTEM.FNT beside the runner: the system loads it at
                  start-up, so the first thing drawn is already the
                  loaded face
    m21-none.atr  no font file: everything answers for the linked one

The script draws text, asks the two faces' names (vqt_name), switches
between them (vst_font), unloads (vst_unload_fonts) and loads again
(vst_load_fonts) -- GDOS's own calls, doing what GDOS does with the one
place this system has to look.  Every returned word and every screen is
compared with tools/vdiref.py, told what that disk's font file holds.

  python3 tests/emu/m21_font.py [--shot]
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import atr, mkfnt, symfile, vbxeref, vdiref  # noqa: E402
from vdiref import (V_CLRWK, V_GTEXT, VST_FONT, VST_LOAD_FONTS,   # noqa: E402
                    VST_UNLOAD_FONTS, VQT_NAME, FONT_ID_SYS,
                    FONT_SYSTEM, FONT_LOADED)
from m3_vdi import (SHOTDIR, STATUS, ST_GO, ST_DONE, SYMS,        # noqa: E402
                    poke_script, wait_done)

BUILD = os.path.join(ROOT, "build")
BASE = os.path.join(BUILD, "m3-boot.atr")
FONT_FILE = "SYSTEM.FNT"                    # src/vdi/font.h
INV = os.path.join(BUILD, "inv.fnt")        # the system font, inside out

# A line of the high half, where two character sets differ, and one of
# ASCII so the screen says which font drew it either way.
HIGH = [0x80 + i for i in range(16)]
TEXT = [ord(c) for c in "The quick brown fox"]


def script(loaded_id):
    """One script: draw, name the faces, switch, unload, load again."""
    return [
        (V_CLRWK,),
        (V_GTEXT, (16, 24), TEXT),
        (V_GTEXT, (16, 40), HIGH),
        (VQT_NAME, (), (FONT_SYSTEM,)),
        (VQT_NAME, (), (FONT_LOADED,)),
        (VST_FONT, (), (FONT_ID_SYS,)),         # the linked face
        (V_GTEXT, (16, 64), TEXT),
        (VST_FONT, (), (loaded_id,)),           # and back to the file's
        (V_GTEXT, (16, 80), TEXT),
        (VST_UNLOAD_FONTS, (), ()),
        (V_GTEXT, (16, 104), TEXT),
        (VST_LOAD_FONTS, (), (0,)),
        (V_GTEXT, (16, 120), TEXT),
    ]


def make_disk(tag, font):
    """A copy of the runner's disk, with SYSTEM.FNT on it or without."""
    out = os.path.join(BUILD, f"m21-{tag}.atr")
    img = atr.ATRImage.load(BASE)
    fs = atr.open_fs(img)
    if any(e.filename.upper() == FONT_FILE for e in fs.entries() if e.in_use):
        fs.delete(FONT_FILE)
    if font:
        with open(font, "rb") as f:
            fs.add_file(FONT_FILE, f.read())
    img.save(out)
    return out


def one(tag, font, keep, check):
    disk = make_disk(tag, font)
    strip = name = None
    font_id = FONT_ID_SYS
    if font:
        strip, name, hdr = mkfnt.read(font)
        font_id = hdr["font_id"]
    print(f"{os.path.basename(disk)}: "
          + (f'SYSTEM.FNT, "{name}", id {font_id}' if font else "no SYSTEM.FNT"))
    syms = symfile.load(SYMS)
    script_addr, scratch = syms["vdi_script"], syms["vdi_scratch"]
    results_addr, count_addr = syms["vdi_results"], syms["vdi_result_count"]
    script_room = min(a for a in syms.values() if a > script_addr) - script_addr

    emu = launch(tag=f"m21{tag}", memsize="1088K", extra_args=["--disk", disk])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("L", "M", "3", "RETURN"):
            b.key(k)
            b.frames(10)
        b.frames(200)
        st = b""
        for _ in range(200):
            st = bytes(b.memdump(STATUS, 3))
            if st[:2] == b"VD" and st[2] == 1:
                break
            b.frames(4)
        if st[:2] != b"VD" or st[2] != 1:
            check(False, f"{tag}: the runner did not come up")
            return

        body = script(font_id)
        full = [(vdiref.V_OPNWK, (), vdiref.WORK_IN)] + body
        resolved = [(r[0], r[1] if len(r) > 1 else (), r[2] if len(r) > 2 else (),
                     None) for r in full]
        ref = vdiref.VDI()
        if strip:
            ref.load_font(strip, name, font_id)
        ref.run(resolved)

        poke_script(b, script_addr, resolved, scratch, script_room)
        b.poke(STATUS + ST_DONE, 0)
        b.poke(STATUS + ST_GO, 1)
        if not wait_done(b):
            check(False, f"{tag}: the script did not finish")
            return
        b.frames(4)
        err = None
        n = b.peek16(count_addr)
        if n != len(ref.results):
            err = f"{n} calls recorded, expected {len(ref.results)}"
        else:
            got = vdiref.decode(b.memdump(results_addr,
                                          n * vdiref.RESULT_WORDS * 2), n)
            for i, rec in enumerate(got):
                if rec != ref.results[i]:
                    err = (f"call {i} (op {full[i][0]}) returned {rec}, "
                           f"expected {ref.results[i]}")
                    break
        shot = os.path.join(SHOTDIR, f"m21-{tag}.png")
        b.screenshot(shot)
        bad, shown = vbxeref.compare_to_shot(ref.to_rgb(), shot)
        if bad and not err:
            err = f"{bad} px differ from the model; first {shown[:3]}"
        if not err and not keep:
            os.remove(shot)
        check(not err, f"{tag}: {err}")
        print(f"  the script {'ok' if not err else 'FAIL'}: "
              f"{len(body)} calls, six lines of text, "
              f"{'the file' if font else 'the linked font'}")
    finally:
        emu.stop()


def main(argv):
    keep = "--shot" in argv
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    os.makedirs(SHOTDIR, exist_ok=True)
    if not os.path.exists(INV):
        print(f"gem4xe-m21: no {INV} -- run make build/inv.fnt")
        return 2
    one("font", INV, keep, check)
    one("none", None, keep, check)
    print(f"gem4xe-m21: {'PASS' if not fails else 'FAIL'} -- a loadable font, "
          f"{len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
