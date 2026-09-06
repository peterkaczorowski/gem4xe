#!/usr/bin/env python3
"""The product CF card boots into the desktop, on the machine this
project is for: an Ultimate 1MB with SpartaDOS X and the PBI BIOS in its
flash, a SIDE 2 with the card on its IDE bus, VBXE and a Rapidus.

    build/gem-cf.img   16 MB, two APT partitions (tools/mkcf.py):
                       D1: GEM4XE, \\GEM\\GEM.COM and the desktop beside
                       it, AUTOEXEC.BAT changing into \\GEM and running
                       GEM; D2: DOCS, empty.

Nothing on the card is a driver.  The U1MB's own PBI BIOS reads the APT
table, mounts the mapping-slot partitions as D1: and D2: before any DOS
runs, and SpartaDOS X -- from the same flash -- then finds AUTOEXEC.BAT
on D1: exactly as it would on a floppy.  That is why the gate needs the
U1MB fixture ([u1mb].flash in fixtures.toml) and the patched emulator:
`--u1mbrom` and `KEYRAW` are ours (tools/altirra/).

Three things about this machine had to be measured, and each is a step
of the run below:

  * A fresh U1MB profile boots into the BIOS setup screen, and the
    settings that matter are off by default: PBI BIOS, and its Hard
    disk.  The gate walks that setup with KEYRAW -- RIGHT and LEFT step
    the pages along the icon row, UP and DOWN move the field cursor,
    RETURN changes the field under it, and page 8 saves and boots.  So
    the gate runs the machine's documented setup rather than a profile
    someone prepared by hand, and it starts from a config directory of
    its own (build/altirra-cf) so it is the same run every time and the
    user's own emulator profile is left alone.
  * The PBI device ID must not be 0.  Setting 0 is PBI bit 0, which is
    the Rapidus's, and the two would collide on real hardware
    (docs/phase14.md).
  * The PBI BIOS will not touch the disk while the cartridge port is
    claimed.  Its first act is a wait:

        $D803  LDA #$80 / STA $D5E4 / BIT $D384 / BVS $D803

    $D384 bit 6 is the U1MB's "external cart active" sense and the SIDE
    holds it while its own SDX module is mapped -- the state of a SIDE 2
    whose SDX switch is on.  A machine that runs SpartaDOS X from the
    U1MB has that switch off; AltirraSDL exposes the switch as a device
    button with no command line or bridge verb, so the gate unmaps the
    SDX bank the way the switch does, by writing $80 to the SIDE's bank
    register at $D5E1.  A reset puts the bank back, so it is written
    again through the run.

The rest is the boot the floppy gate runs (tests/emu/product_boot.py):
the card starts GEM on the 6502 and the loader refuses the machine, the
gate sets COLDST and switches the CPU, and the desktop comes up on the
restart and is compared with the model.

  python3 tests/emu/cf_boot.py [--shot]
"""
import os
import shutil
import sys
import tomllib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

BUILD = os.path.abspath(os.path.join(ROOT, "build"))
# The emulator keeps the U1MB's NVRAM in its profile, and a fresh NVRAM
# is what puts the BIOS into setup.  Set before the launcher is imported
# so the emulator it starts inherits it.
PROFILE = os.path.join(BUILD, "altirra-cf")
os.environ["XDG_CONFIG_HOME"] = PROFILE

from a8test.launcher import launch          # noqa: E402
import apt, atr, mkxex, symfile, vbxeref    # noqa: E402
from m4_aes import SHOTDIR                  # noqa: E402
from m14_sparta import screen               # noqa: E402
from m17_desktop import listing             # noqa: E402
from product_boot import (COLDST, FARMEM_BRK, REFUSAL, desk_model,   # noqa: E402
                          far_byte, far_probes)

CARD = os.path.join(BUILD, "gem-cf.img")
SYMS = os.path.join(BUILD, "gem.sym")
ELF = os.path.join(BUILD, "gem.elf")
FIXTURES = os.path.join(ROOT, "fixtures.toml")
BOOT_LINE = b"CD >GEM\x9bGEM\x9b"
WANT = {"GEM>GEM.COM": "gem.xex", "GEM>DESKTOP.G4A": "desktop.g4a",
        "GEM>DESKTOP.RSC": "desktop.rsc"}
PBI_BANNER = "Ultimate PBI"
SDX_BANK = 0xD5E1               # the SIDE's SDX bank register; $80 unmaps it
DRVMAP = 0x03                   # D1: and D2:, the card's two partitions

# The BIOS setup, from the page it opens on ("Memory and System").
SETUP = [("right", "the clock page"),
         ("right", "PBI BIOS Settings"),
         ("return", "PBI BIOS: Enabled"),
         ("down", "to PBI device ID"),
         ("return", "an ID that is not the Rapidus's bit 0"),
         ("down", "to Hard disk"),
         ("return", "Hard disk: Enabled"),
         ("right", "SIO"), ("right", "System Information"),
         ("right", "BIOS Settings"), ("right", "Device Control"),
         ("right", "Save and Exit"),
         ("b", "Save changes and boot")]


def flash():
    with open(FIXTURES, "rb") as f:
        return tomllib.load(f).get("u1mb", {}).get("flash")


def keep_switch(b, frames, step=50):
    """Frames, with the SIDE's SDX bank kept unmapped -- see the header."""
    for _ in range(0, frames, step):
        b.cmd(f"HWPOKE ${SDX_BANK:04X} $80")
        b.frames(step)


def card_checks(check):
    """The card as tools/mkcf.py wrote it, read back through the same
    table the firmware reads: two partitions in mapping slots 1 and 2, so
    they arrive as D1: and D2:, and the system where the batch file
    looks for it."""
    img = apt.Image.load(CARD)
    parts = apt.read_table(img)
    print(f"{os.path.basename(CARD)}: {img!r}, {len(parts)} partitions")
    check(len(parts) == 2, f"the card has {len(parts)} partitions, not 2")
    fs = atr.Sdfs(parts[0])
    listed = {e.filename.upper(): e.size for e in fs.entries("")}
    print(f"  D1: {fs.volname}, {', '.join(sorted(listed))}, "
          f"{fs.free_count()} sectors free")
    for path, built in WANT.items():
        name = path.split(">")[-1]
        where = path.split(">")[0] if ">" in path else ""
        sizes = {e.filename.upper(): e.size for e in fs.entries(where)}
        check(name in sizes, f"{path} is not on the card")
        if name in sizes:
            size = os.path.getsize(os.path.join(BUILD, built))
            check(sizes[name] == size,
                  f"{path} is {sizes[name]} bytes, not build/{built}'s {size}")
    check("AUTOEXEC.BAT" in listed, "no AUTOEXEC.BAT: nothing would start GEM")
    if "AUTOEXEC.BAT" in listed:
        check(fs.read("AUTOEXEC.BAT") == BOOT_LINE,
              f"AUTOEXEC.BAT holds {fs.read('AUTOEXEC.BAT')!r}, not {BOOT_LINE!r}")
    return fs


def main(argv):
    keep = "--shot" in argv
    rom = flash()
    if not rom or not os.path.exists(rom):
        print("gem4xe-cf: no U1MB fixture -- set [u1mb].flash in fixtures.toml")
        return 2
    if not os.path.exists(CARD):
        print(f"gem4xe-cf: no {CARD} -- run make")
        return 2
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    syms = symfile.load(SYMS)
    segs, _ = mkxex.read_elf(ELF)
    far = sorted((a, d) for a, d in segs if a > 0xFFFF)
    chunk = syms["_fl_scr"] - syms["_fl_buf"]
    fs = card_checks(check)

    # A profile of the gate's own, thrown away first: a fresh NVRAM is
    # what makes the BIOS open its setup screen.
    shutil.rmtree(PROFILE, ignore_errors=True)
    os.makedirs(PROFILE)
    os.makedirs(SHOTDIR, exist_ok=True)
    shot = os.path.join(SHOTDIR, "cf-desk.png")
    print(f"the machine: U1MB {os.path.basename(rom)}, SIDE 2, VBXE, Rapidus")
    emu = launch(tag="cf", memsize="1088K", require_real_rom=False,
                 extra_args=["--u1mbrom", rom, "--adddevice", "side2"])
    b = emu.bridge
    try:
        added = b.cmd(f"DEVICE_ADD harddisk parent=/side2/idebus path={CARD} "
                      f"write_enabled=1").get("ok")
        check(added, "the card would not attach to the SIDE 2's IDE bus")
        b.cmd("COLD_RESET")
        b.frames(300)
        check(b.has_keyraw(), "this emulator has no KEYRAW: see tools/altirra/")

        # -- 1. the BIOS setup, driven blind -------------------------------
        for key, why in SETUP:
            b.key_tap(key)
            b.frames(15)
        print(f"  the BIOS configured in {len(SETUP)} keys: "
              f"{SETUP[2][1]}, {SETUP[6][1]}")

        # -- 2. the card boots, on the 6502 --------------------------------
        for t in range(0, 12000, 100):
            keep_switch(b, 100)
            lines = [ln for ln in screen(b) if ln.strip()]
            if any(REFUSAL in ln for ln in lines):
                break
        else:
            check(False, "the card did not start GEM on the 6502")
            for ln in screen(b):
                if ln.strip():
                    print("   |" + ln)
            return 1
        check(any(PBI_BANNER in ln for ln in lines),
              "no PBI BIOS banner: the card was not mounted by it")
        print(f"  the PBI BIOS mounted the card and SpartaDOS X ran "
              f"AUTOEXEC.BAT; GEM refused the 6502, {t + 100} frames in")
        b.key("A")                       # the loader waits to be read
        keep_switch(b, 20)

        refused, last, same = lines, None, 0
        for t in range(0, 20000, 100):
            keep_switch(b, 100)
            now = [ln for ln in screen(b) if ln.strip()]
            same = same + 1 if now == last else 0
            last = now
            if now != refused and same >= 3:
                break
        else:
            check(False, "the DOS never took the machine back")
            return 1
        print(f"  the DOS has it back, {t + 100} frames in")

        # -- 3. cold, and a 65C816 -----------------------------------------
        b.poke(COLDST, 0x01)
        check(b.peek(COLDST) == 0x01, "COLDST did not take")
        b.poke(0xD1FF, 0x01)            # the PBI slot the Rapidus answers on
        b.poke(0xD191, 0x00)            # bit 6 clear: the 65C816
        for t in range(0, 6000, 50):
            keep_switch(b, 50)
            if not [ln for ln in screen(b) if ln.strip()]:
                break
        else:
            check(False, "the machine did not restart after the switch")
            return 1
        print(f"  COLDST set, the CPU switched, the machine restarted "
              f"{t + 50} frames later")

        # -- 4. the desktop ------------------------------------------------
        calls = syms["gem_calls"]
        n, still = b.peek16(calls), 0
        for t in range(0, 30000, 250):
            keep_switch(b, 250)
            now = b.peek16(calls)
            still = still + 1 if now == n else 0
            n = now
            if still >= 2 and now:
                break
        else:
            check(False, f"GEM never settled ({n} calls)")
        print(f"  GEM settled after {t + 250} frames, {n} calls in")
        fault = b.peek(syms["irq_fault"])
        check(fault == 0, f"irq_fault {fault} (src/sys/irq.s)")
        check(not any(REFUSAL in ln for ln in screen(b)),
              "GEM refused the 65C816 as well: the switch did not take")

        bad = []
        for a in far_probes(far, chunk):
            if b.cmd(f"EVAL db(${a:06x})").get("value") != far_byte(far, a):
                bad.append(a)
        check(not bad, f"the far image differs at {len(bad)} probed byte(s), "
                       f"first ${bad[0]:06X}" if bad else "")
        print(f"  the far image: {len(far_probes(far, chunk))} bytes probed, "
              f"{'all as the linker wrote them' if not bad else str(len(bad)) + ' wrong'}")

        mark = b.peek16(syms["app_pool_lo"])
        brk = int.from_bytes(bytes(b.memdump(syms["farmem"] + FARMEM_BRK, 4)), "little")
        pointer = (b.peek16(syms["ptr_state"]), b.peek16(syms["ptr_state"] + 2))
        print(f"  pool ${mark:04X}, far brk ${brk:06X}, pointer {pointer}")
        ref_v, ref_a, d = desk_model(mark, brk, pointer, DRVMAP, listing(fs))
        check(len(ref_a.shots) == 1, f"the model took {len(ref_a.shots)} shots")
        check((n - 1) & 0xFFFF == d.waits[0],
              f"the desktop is in call {(n - 1) & 0xFFFF}, not its first "
              f"wait {d.waits[0]}")
        b.screenshot(shot)
        bad_px, shown = vbxeref.compare_to_shot(ref_a.shots[0], shot)
        check(not bad_px, f"the desk, {bad_px} px differ from the model; "
                          f"first {shown[:3]}")
        print(f"  the desk {'ok' if not bad_px else 'FAIL'} against the model "
              f"({shot if bad_px or keep else 'not kept'})")
        if not bad_px and not keep:
            os.remove(shot)
    finally:
        emu.stop()

    print(f"gem4xe-cf: {'PASS' if not fails else 'FAIL'} -- the CF card, "
          f"{len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
