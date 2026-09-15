#!/usr/bin/env python3
"""GEM under Rapidus OS with SpartaDOS X's 65816.SYS loaded.

SpartaDOS X's Toolkit carries 65C816 drivers, and 65816.SYS is the one a Rapidus
OS machine loads first.  With it loaded, gem4xe 0.1.2 and 0.2 both stopped at the
desktop's first rsrc_load and said DESKTOP.RSC was not on the boot disk.  Nothing was
wrong with the disk or the DOS: proc_init() cleared a process record field by field,
and not the two resource slots added to the record later, so the desktop started out
holding two resources it had never loaded and rs_load refused a third.  The record is
taken from the bank-$00 pool, which is zero on a plain SpartaDOS X machine and holds
bytes of the driver once 65816.SYS has been loaded through it (docs/phase41.md).

The machine: Rapidus OS as the XL kernel (--os, from a private emulator profile, as
test-m11-os), the 65C816 selected before it runs, the SpartaDOS X cartridge (--cart),
and the SDX product disk's files (tools/mkfloppy.py) with a CONFIG.SYS that loads
65816.SYS (--driver) after SIO -- before SIO, SDX cannot read D1: to load it.

The gate: SpartaDOS X says the driver loaded, and the desktop reaches its first wait
with nothing refused and no interrupt fault.

  python3 tests/emu/sdx816.py --os=ROM --cart=CAR --driver=65816.SYS [--shot]
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import mkfloppy, symfile                    # noqa: E402
from m11_abi import os_profile              # noqa: E402
from m14_sparta import screen               # noqa: E402
from m4_aes import SHOTDIR                  # noqa: E402

BUILD = os.path.abspath(os.path.join(ROOT, "build"))
DISK = os.path.join(BUILD, "sdx816-boot.atr")
SYMS = os.path.join(BUILD, "gem.sym")
CONFIG = ["DEVICE SPARTA OSRAM", "DEVICE SIO", "DEVICE D1:65816",
          "DEVICE ATARIDOS", "DEVICE JIFFY"]
LOADED = "65816 v."                         # what 65816.SYS prints as it installs
FIRST_WAIT = 43                             # the desktop's calls to its first wait
LIMIT = 12000                               # frames


def arg(argv, name):
    return next((os.path.abspath(a.split("=", 1)[1]) for a in argv
                 if a.startswith(f"--{name}=")), None)


def main(argv):
    rom, cart, driver = arg(argv, "os"), arg(argv, "cart"), arg(argv, "driver")
    if not (rom and cart and driver):
        print("gem4xe-sdx816: needs --os=ROM --cart=CAR --driver=65816.SYS")
        return 2
    keep = "--shot" in argv
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    cfg = os.path.join(BUILD, "sdx816-config.sys")
    with open(cfg, "wb") as f:
        f.write(b"".join(ln.encode() + b"\x9b" for ln in CONFIG))
    if os.path.exists(DISK):
        os.remove(DISK)
    mkfloppy.build(DISK, [(driver, "65816.SYS"), (cfg, "CONFIG.SYS")],
                   root=os.path.abspath(ROOT))
    syms = symfile.load(SYMS)
    if not os_profile(rom):
        print("gem4xe-sdx816: no firmware entry for the XL ROM in "
              "~/.config/altirra/settings.ini to point at the OS")
        return 2
    print(f"the machine: {os.path.basename(rom)}, {os.path.basename(cart)}, 65816.SYS from D1:")
    os.makedirs(SHOTDIR, exist_ok=True)
    shot = os.path.join(SHOTDIR, "sdx816-desk.png")
    emu = launch(tag="sdx816", memsize="1088K", require_real_rom=False,
                 extra_args=["--disk", DISK, "--cart", cart])
    b = emu.bridge
    try:
        b.poke(0xD1FF, 0x01)                # the 65C816 before the OS runs
        b.poke(0xD191, 0x00)
        loaded, n, n_prev, still, f = False, 0, None, 0, 0
        for f in range(50, LIMIT, 50):
            b.frames(50)
            if not loaded:
                loaded = any(LOADED in ln for ln in screen(b))
            # gem4xe's variables mean nothing until it is running
            if b.peek(syms["irq_cio_swap"]) not in (1, 3):
                continue
            n = b.peek16(syms["app_calls"])
            if n > 0x1000:
                continue
            still = still + 1 if (n and n == n_prev) else 0
            n_prev = n
            if n >= FIRST_WAIT or still >= 100:
                break
        check(loaded, "SpartaDOS X never said 65816.SYS loaded: the gate is not testing it")
        print(f"  65816.SYS {'loaded' if loaded else 'NOT loaded'}; the desktop "
              f"{'reached its first wait' if n >= FIRST_WAIT else 'stopped'} "
              f"at call {n}, frame {f}")
        check(n >= FIRST_WAIT,
              f"the desktop stopped at call {n}, short of its first wait at {FIRST_WAIT} "
              f"(DESKTOP.RSC refused: see proc_init)")
        check(b.peek16(syms["gem_bad"]) == 0, f"{b.peek16(syms['gem_bad'])} ABI call(s) refused")
        check(b.peek(syms["irq_fault"]) == 0, f"irq_fault {b.peek(syms['irq_fault'])}")
        b.screenshot(shot)
        if not fails and not keep:
            os.remove(shot)
    finally:
        emu.stop()

    print(f"gem4xe-sdx816: {'PASS' if not fails else 'FAIL'} -- GEM with SpartaDOS X's "
          f"65816.SYS under Rapidus OS, {len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
