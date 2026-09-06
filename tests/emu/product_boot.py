#!/usr/bin/env python3
"""The product disks boot into the desktop, with nothing typed at all.

There are two of them and they come up the same way by different means:

  build/gem-sp.atr   SpartaDOS 3.2 on an SDFS volume, GEM.COM started by
                     the batch file the DOS runs at boot -- two of them,
                     STARTUP.BAT for SpartaDOS 3.2 and AUTOEXEC.BAT for
                     SpartaDOS X, because a disk cannot know which one
                     booted it (tools/mkspdisk.py --boot);
  build/gem-boot.atr a double-density DOS 2 disk, the system named
                     AUTORUN.SYS because that is what the DOS runs at
                     boot, with the DOS's own DUP.SYS still on it and
                     43 KB free for applications (tools/mkdisk.py
                     --sweep, and docs/shipping.md section 2 for why the
                     DOS 2 disk had to become double density).

This gate touches no key -- there is no b.key() in this file -- so what
comes up is what the disk itself started.  It runs the machine's real
sequence, which the Rapidus makes longer than it looks:

  1. the disk boots on the 6502 and the DOS starts GEM by itself, and
     the loader REFUSES the machine it is on -- "this needs a 65C816"
     through CIO, nothing written, back to the DOS (src/farload.s, and
     test-m6 for the same refusal reached by hand).  Reaching it from a
     start-up file is the new part: a boot that lands on a 6502 must be
     survivable;
  2. COLDST ($0244) is set and the CPU is switched.  Both halves are
     needed.  The switch resets the CPU, and the OS treats that reset as
     a WARM start -- which is exactly when a DOS does not run its
     start-up file, so without COLDST the machine comes back to a prompt
     and sits there.  On a real machine the U1MB Rapidus plugin sets the
     CPU over the M1 signal before the OS runs and the question does not
     arise; this is the emulator's version of a machine that powers on
     as a 65C816.  The write must also be made with the machine idle:
     mid-SIO it is lost and the DOS hangs (measured);
  3. the machine comes up cold as a 65C816, the DOS starts GEM again,
     and this time GEM keeps the machine: the desk, its drive icons and
     the trash under the menu bar.

What is checked: the disk's own files, read out of the image; the
refusal, on the screen, from a boot nobody drove; the far image, spot
checked against the linker's own output where a DOS that mangles the
staging would show (this gate found one that does -- MyDOS, section 2 of
docs/shipping.md); and the desk at the end, pixel for pixel against
tools/deskref.py, the desktop's own model run to its first wait.

Not checked here: G, the calls, the pool and the stacks.  GEM.COM has no
runner in it to answer a sys op, and the desktop's state is test-m17's
subject anyway; this gate is about the boot path.  The model is given the
pool base the linker gave the target (app_pool_lo, read out of the
machine) and the far heap's cursor as the target reports it, because the
model has to hand the desktop's Malloc a real address -- neither is on
the screen.

  python3 tests/emu/product_boot.py [--shot] [NAME]
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a8test.launcher import launch          # noqa: E402
import aesref, vbxeref, symfile, atr, mkxex  # noqa: E402
from deskref import Desktop                 # noqa: E402
from deskrsc import FILEMENU, QUITITEM      # noqa: E402
from m4_aes import PRELUDE, SHOTDIR         # noqa: E402
from m7_form import F                       # noqa: E402
from m14_sparta import screen               # noqa: E402
from m17_desktop import header, listing, menu, DESKTOP, DESK_SYM, SHOT  # noqa: E402

BUILD = os.path.join(ROOT, "build")
SYMS = os.path.join(BUILD, "gem.sym")
ELF = os.path.join(BUILD, "gem.elf")
BOOT_FILES = ("STARTUP.BAT", "AUTOEXEC.BAT")
BOOT_LINE = b"GEM\x9b"
COLDST = 0x0244                 # the OS: non-zero at RESET means come up cold
DRVBYT = 0x070A                 # DOS 2's drive map (src/sys/gemdos.c)
DOS_2 = 0                       # src/sys/dos.h
REFUSAL = "gem4xe:"             # src/farload.s msg_no816
FARMEM_BRK = 8                  # the cursor's offset in FARMEM (src/sys/farmem.h)
SEAM = 8                        # bytes checked either side of a chunk seam
STEP = 20                       # frames between screen reads while waiting for
                                # the refusal: see the poll in one()
HEAD = 64                       # and at the head of every chunk: where a DOS
                                # that loses part of one shows up

# (the image, what starts GEM on it, the echo its prompt shows when it
# does -- SpartaDOS prints the batch file's line, a DOS 2 menu prints
# nothing).
PRODUCTS = [
    ("gem-sp.atr", "GEM.COM", "SpartaDOS 3.2, STARTUP.BAT and AUTOEXEC.BAT", "D1:GEM"),
    ("gem-boot.atr", "AUTORUN.SYS", "a double-density DOS 2, AUTORUN.SYS", None),
]


def desk_model(mark, brk, pointer, drvmap, dirs):
    """The desktop against the model, up to its first wait: the same
    prelude and the same sh_main the desktop gates run (m17_desktop), and
    then one step producer, which photographs the desk and chooses
    File -> Quit so the model's session ends there."""
    v, a, _ = aesref.run(PRELUDE, [], {}, pointer=pointer, pool=mark)
    v.close_virtuals()
    a.wm_init()
    a.mn_init()
    a.ratinit()
    a.tree = a.W_TREE
    a.draw(0, 0, (0, 0, a.gl_width, a.gl_height))
    link_near, near_size, far_banks = header(DESKTOP)
    desk_len = (os.path.getsize(DESKTOP) + 3) & ~3
    a.dos_brk = ((brk + desk_len + 0xFFFF) & ~0xFFFF) + (far_banks << 16)
    a.dos_dirs = dirs
    g_link = symfile.load(DESK_SYM)["G"]

    def first_wait(d):
        return [F(3), SHOT, *menu(d, FILEMENU, QUITITEM, False)[1:]]

    d = Desktop(v, a, mark, link_near, near_size, g_link, drvmap, [first_wait])
    d.main()
    return v, a, d


def far_probes(far, chunk):
    """Where to read the far image back: the head of every chunk, and a
    few bytes either side of every seam.  The head is the part that
    matters -- a DOS that drops bytes out of a staged chunk drops them
    near its start, which is how MyDOS was caught."""
    want = set()
    for base, data in far:
        size = len(data)
        for dst, piece in mkxex.far_chunks(base, data, chunk):
            want.update(a for a in range(dst, dst + HEAD) if a < base + size)
            for edge in (dst, dst + len(piece) - 1):
                want.update(a for a in range(edge - SEAM, edge + SEAM + 1)
                            if base <= a < base + size)
        want.add(base + size - 1)
    return sorted(want)


def far_byte(far, addr):
    for base, data in far:
        if base <= addr < base + len(data):
            return data[addr - base]
    return None


def dos2_listing(fs):
    """A DOS 2 root, in the shape the model wants.  Nothing in this gate
    opens a window, so the desktop never asks for it -- it is here so the
    model has an answer if it ever does."""
    return {"A:\\": [(e.filename.upper(), 0, 0, 0, e.count * fs.data_bytes)
                     for e in fs.entries() if e.in_use and e.nameable]}


def one(name, progname, how, echo, keep, check):
    disk = os.path.abspath(os.path.join(BUILD, name))
    syms = symfile.load(SYMS)
    segs, _ = mkxex.read_elf(ELF)
    far = sorted((a, d) for a, d in segs if a > 0xFFFF)
    chunk = symfile.load(SYMS)["_fl_scr"] - symfile.load(SYMS)["_fl_buf"]

    # -- the disk, before anything boots it ---------------------------------
    img = atr.ATRImage.load(disk)
    fs = atr.open_fs(img)
    sdfs = isinstance(fs, atr.Sdfs)
    listed = ({e.filename.upper(): e.size for e in fs.entries("")} if sdfs else
              {e.filename.upper(): None for e in fs.entries() if e.in_use})
    print(f"{name}: {img!r}, {how}")
    print(f"  {', '.join(sorted(listed))}")
    want = {progname: "gem.xex", "DESKTOP.G4A": "desktop.g4a",
            "DESKTOP.RSC": "desktop.rsc"}
    for fname, built in want.items():
        check(fname in listed, f"{name}: {fname} is not on the disk")
        if sdfs and fname in listed:
            size = os.path.getsize(os.path.join(BUILD, built))
            check(listed[fname] == size,
                  f"{name}: {fname} is {listed[fname]} bytes, not build/{built}'s {size}")
    if sdfs:
        for batch in BOOT_FILES:
            check(batch in listed, f"{name}: {batch} is not on the disk")
            if batch in listed:
                check(fs.read(batch) == BOOT_LINE,
                      f"{name}: {batch} holds {fs.read(batch)!r}, not {BOOT_LINE!r}")
    else:
        check("DUP.SYS" in listed, f"{name}: no DUP.SYS -- nowhere to return to")
        free = fs.free_count() * fs.data_bytes
        print(f"  {fs.free_count()} sectors free, {free // 1024} KB for applications")
        check(fs.free_count() > 100, f"{name}: only {fs.free_count()} sectors free")

    emu = launch(tag="product", memsize="1088K", extra_args=["--disk", disk])
    b = emu.bridge
    shot = os.path.join(SHOTDIR, f"product-{name.split('.')[0]}.png")
    try:
        # -- 1. the 6502 pass, hands off ------------------------------------
        # A FINE poll, because the refusal is not on the screen for long:
        # GEM prints it, returns to the DOS, and a DOS 2 clears the screen
        # for its menu a moment later.  At a hundred frames a poll this
        # gate missed the window about one run in ten and reported that
        # the DOS never started GEM, with the menu on the screen as its
        # evidence -- which was true and not the point.  Twenty frames is
        # a third of a second and costs a screen read, not a frame.
        seen = None
        for t in range(0, 12000, STEP):
            b.frames(STEP)
            lines = [ln for ln in screen(b) if ln.strip()]
            if any(REFUSAL in ln for ln in lines):
                seen = lines
                break
        if seen is None:
            check(False, f"{name}: the DOS did not start GEM on the 6502")
            for ln in screen(b):
                if ln.strip():
                    print("   |" + ln)
            return
        lines = seen
        print(f"  the DOS started GEM and GEM refused, {t + STEP} frames in")
        if echo:
            check(any(ln.rstrip().endswith(echo) for ln in lines),
                  f"{name}: no {echo!r} on the screen: the DOS did not run the batch")
        # Let the DOS finish with it.  The refusal is printed early -- the
        # loader identifies the machine at the first staging chunk -- and
        # the rest of the 92 KB is read after it, in silence, so a screen
        # that has stopped changing is NOT the same as an idle DOS.  What
        # says the DOS has the machine back is the screen changing AGAIN:
        # a prompt on SpartaDOS, the menu on a DOS 2.  The switch has to
        # wait for it, because a write made while the DOS is mid-SIO is
        # lost and the DOS hangs.
        refused, last, same = lines, None, 0
        for t in range(0, 20000, 100):
            b.frames(100)
            now = [ln for ln in screen(b) if ln.strip()]
            same = same + 1 if now == last else 0
            last = now
            if now != refused and same >= 3:
                break
        else:
            check(False, f"{name}: the DOS never took the machine back")
            return
        print(f"  the DOS has it back, {t + 100} frames in: {last[-1][:40] if last else ''}")

        # -- 2. cold, and a 65C816 ------------------------------------------
        b.poke(COLDST, 0x01)
        check(b.peek(COLDST) == 0x01, f"{name}: COLDST did not take")
        b.poke(0xD1FF, 0x01)            # the PBI slot the Rapidus answers on
        b.poke(0xD191, 0x00)            # bit 6 clear: the 65C816
        for t in range(0, 3000, 50):
            b.frames(50)
            if not [ln for ln in screen(b) if ln.strip()]:
                break
        else:
            check(False, f"{name}: the machine did not restart after the switch")
            return
        print(f"  COLDST set, the CPU switched, the machine restarted "
              f"{t + 50} frames later")

        # -- 3. the desktop, and the model it must match --------------------
        calls = syms["gem_calls"]
        n, still = b.peek16(calls), 0
        for t in range(0, 20000, 250):
            b.frames(250)
            now = b.peek16(calls)
            still = still + 1 if now == n else 0
            n = now
            if still >= 2 and now:
                break
        else:
            check(False, f"{name}: GEM never settled ({n} calls)")
        print(f"  GEM settled after {t + 250} frames, {n} calls in")
        fault = b.peek(syms["irq_fault"])
        check(fault == 0, f"{name}: irq_fault {fault} (src/sys/irq.s)")
        if echo:
            check(any(ln.rstrip().endswith(echo) for ln in screen(b)),
                  f"{name}: the batch file did not run on the cold start")
        check(not any(REFUSAL in ln for ln in screen(b)),
              f"{name}: GEM refused the 65C816 as well: the switch did not take")

        # the far image, as the linker wrote it
        bad = []
        for a in far_probes(far, chunk):
            if b.cmd(f"EVAL db(${a:06x})").get("value") != far_byte(far, a):
                bad.append(a)
        check(not bad, f"{name}: the far image differs at {len(bad)} probed byte(s), "
                       f"first ${bad[0]:06X}" if bad else "")
        print(f"  the far image: {len(far_probes(far, chunk))} bytes probed, "
              f"{'all as the linker wrote them' if not bad else str(len(bad)) + ' wrong'}")

        kind = b.peek(syms["dos"])          # DOS_INFO.kind (src/sys/dos.h)
        drvmap = 0x03 if kind != DOS_2 else (b.peek(DRVBYT) or 1)
        mark = b.peek16(syms["app_pool_lo"])
        brk = int.from_bytes(bytes(b.memdump(syms["farmem"] + FARMEM_BRK, 4)), "little")
        pointer = (b.peek16(syms["ptr_state"]), b.peek16(syms["ptr_state"] + 2))
        print(f"  DOS kind {kind}, drive map {drvmap:#04x}, pool ${mark:04X}, "
              f"far brk ${brk:06X}, pointer {pointer}")
        dirs = listing(disk) if sdfs else dos2_listing(fs)
        ref_v, ref_a, d = desk_model(mark, brk, pointer, drvmap, dirs)
        print(f"  the model's desktop: {len(d.script)} calls to its first wait "
              f"at {d.waits[0]}, near ${d.near:04X}, G ${d.G:04X}")
        check(len(ref_a.shots) == 1, f"{name}: the model took {len(ref_a.shots)} shots")
        # The ABI's counter is the desktop's own: the shell and the AES do
        # not reach the machine through it, so the calls the target has
        # made are the calls the model's script has, and the wait it is
        # sitting in is the model's first (m17_desktop's read()).
        check((n - 1) & 0xFFFF == d.waits[0],
              f"{name}: the desktop is in call {(n - 1) & 0xFFFF}, not its first "
              f"wait {d.waits[0]}")

        b.screenshot(shot)
        bad_px, shown = vbxeref.compare_to_shot(ref_a.shots[0], shot)
        check(not bad_px, f"{name}: the desk, {bad_px} px differ from the model; "
                          f"first {shown[:3]}")
        print(f"  the desk {'ok' if not bad_px else 'FAIL'} against the model "
              f"({shot if bad_px or keep else 'not kept'})")
        if not bad_px and not keep:
            os.remove(shot)
    finally:
        emu.stop()


def main(argv):
    keep = "--shot" in argv
    only = [a for a in argv if not a.startswith("--")]
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print(f"  FAIL: {msg}")

    os.makedirs(SHOTDIR, exist_ok=True)
    for name, progname, how, echo in PRODUCTS:
        if only and not any(o in name for o in only):
            continue
        one(name, progname, how, echo, keep, check)
    print(f"{'FAIL' if fails else 'PASS'}: the product disks boot into the desktop "
          f"with nothing typed, {len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
