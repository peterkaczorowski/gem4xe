#!/usr/bin/env python3
"""Gate: the program's code really lives in, and runs from, the far banks.

An Atari DOS loader cannot place anything above $FFFF, so gem4xe's code
travels in the .xex as chunks aimed at a staging buffer and is copied up by
src/farload.s as DOS reads the file (tools/mkxex.py builds the chunks).  The
other gates would pass a build where that went subtly wrong -- a mangled byte
usually lands in a function nothing in the suite exercises -- so this one
checks the mechanism itself rather than its consequences:

  1. every byte of every far segment, at the chunk seams and across the whole
     span, matches what the linker emitted;
  2. the code is EXECUTING from the bank the linker put it in, not from a copy
     that fell back into bank $00 (which would silently defeat the whole
     exercise);
  3. the far heap starts above the far code -- see docs/phase6.md, this is
     the bug that made three VDI cases fail with a different three at every
     optimisation level -- and the loader's own record of how far it wrote
     (_fl_top), which is what the heap is derived from, agrees with the ELF;
  4. the staging buffer is inside the MEMAC A window, where it costs nothing;
  5. the bank $00 the data, stack and direct page live in is being served
     from the accelerator's SRAM, not the motherboard's 1.79 MHz bus -- the
     Rapidus resets with every 16 KB window slow, and six phases ran that way
     without a gate noticing (docs/phase7.md);
  6. and on a machine that CANNOT run it -- the same disk, booted without
     switching the CPU -- the loader says so and gives DOS its machine back
     rather than writing far RAM with an opcode the 6502 does not have.

The far code is linked into one memory per bank from $01 up, and spills into
the next bank only when the current one is full (src/gem4xe.scm).  The real
build fits in bank $01 today, which would leave the spill untested until the
day the code outgrows the bank -- so 1..5 are run on TWO images: the real one
and build/m6split, the same objects linked with bank $01 cut down to 16 KB,
which puts most of the program in bank $02.

Reading far RAM needs the debugger's `EVAL db($xxxxxx)`, one byte per call
and the only 24-bit path the bridge has, so the whole image is not swept: the
chunk seams are, densely, plus a spread over everything else.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkxex                            # noqa: E402
from a8test.launcher import launch      # noqa: E402

BUILD = os.path.abspath(os.path.join(ROOT, "build"))
IMAGES = (
    ("m3", "the real build"),
    ("m6split", "bank $01 cut to 16 KB, forcing the spill"),
)
STATUS = 0x0600
SEAM = 48           # bytes checked either side of every chunk boundary
SPREAD = 300        # additional probes spread over the whole image


def probe_addresses(far, chunk):
    """Chunk seams densely, plus a spread over the rest of every segment."""
    want = set()
    total = sum(len(d) for _, d in far)
    step = max(1, total // SPREAD)
    for base, data in far:
        size = len(data)
        for dst, piece in mkxex.far_chunks(base, data, chunk):
            for edge in (dst, dst + len(piece) - 1):
                for d in range(-SEAM, SEAM + 1):
                    if base <= edge + d < base + size:
                        want.add(edge + d)
        want.update(range(base, base + size, step))
        want.add(base + size - 1)
    return sorted(want)


def far_byte(far, addr):
    for base, data in far:
        if base <= addr < base + len(data):
            return data[addr - base]
    return None


def check_refuses_6502(disk, far):
    """Boot the same disk on a 6502 and require a clean refusal.

    On an NMOS 6502 the long store the copier needs ($9F) is an unstable
    undocumented opcode, so "it crashes" is not an acceptable answer: nothing
    may be written at all.  src/farload.s identifies the CPU before its first
    store and prints a line instead.
    """
    fails = []
    firsts = [base for base, _ in far]
    emu = launch(tag="m6no816", memsize="1088K", extra_args=["--disk", disk])
    b = emu.bridge
    try:
        b.frames(300)                       # no CPU switch: still a 6502
        before = [b.cmd(f"EVAL db(${a:06x})").get("value") for a in firsts]
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(250)
        mode = b.cmd("HWSTATE").get("cpu", {}).get("mode")
        after = [b.cmd(f"EVAL db(${a:06x})").get("value") for a in firsts]
        came_up = bytes(b.memdump(STATUS, 2)) == b"VD"

        # The message goes to E: through CIO, so it is in the text screen that
        # SAVMSC points at, in ATASCII.
        savmsc = b.peek(0x58) | (b.peek(0x59) << 8)
        screen = bytes(b.memdump(savmsc, 960)) if savmsc else b""
        said = b"gem4xe" in bytes((c + 32) & 0xFF if c < 64 else c for c in screen)
        touched = ", ".join(f"${a:06X} ${w:02X}->${g:02X}"
                            for a, w, g in zip(firsts, before, after))
        print(f"on a 6502  : CPU {mode}, far RAM {touched}, "
              f"runner {'STARTED' if came_up else 'did not start'}, "
              f"message {'on screen' if said else 'NOT FOUND'}")
        if mode != "6502":
            fails.append(f"the refusal check needs a 6502; the CPU is {mode}")
        else:
            for a, w, g in zip(firsts, before, after):
                if w != g:
                    fails.append(f"far RAM was written on a 6502: "
                                 f"${a:06X} ${w:02X} -> ${g:02X}")
        if came_up:
            fails.append("the runner started on a 6502; the guard did not fire")
        if not said:
            fails.append("no diagnostic reached the screen; the user is told nothing")
    finally:
        emu.stop()
    return fails


def check_image(name, why):
    """Boot one build on the 65C816 and run checks 1..5 on it."""
    disk = os.path.join(BUILD, f"{name}-boot.atr")
    elf = os.path.join(BUILD, f"{name}.elf")
    segs, syms = mkxex.read_elf(elf)
    far = sorted((a, d) for a, d in segs if a > 0xFFFF)
    print(f"== {name}: {why}")
    if not far:
        print("   FAIL: no far segments; is --code-model=large set?")
        return [f"{name}: no far segments"], far
    chunk = syms["_fl_scr"] - syms["_fl_buf"]
    probes = probe_addresses(far, chunk)
    end = max(a + len(d) for a, d in far)          # one past the image
    top_bank = (end - 1) >> 16
    banks = sorted({a >> 16 for a, _ in far} | {top_bank})
    for base, data in far:
        n = -(-len(data) // chunk)
        print(f"far image  : ${base:06X}-${base + len(data) - 1:06X}  "
              f"({len(data)} bytes, {n} chunks of {chunk})")
    print(f"             banks {', '.join(f'${b:02X}' for b in banks)}; "
          f"{len(probes)} bytes to probe")

    fails = []
    emu = launch(tag=f"m6-{name}", memsize="1088K", extra_args=["--disk", disk])
    b = emu.bridge
    try:
        b.frames(300)
        b.poke(0xD1FF, 0x01)
        b.poke(0xD191, 0x00)
        b.frames(500)
        for k in ("M", "3", "RETURN"):
            b.key(k)
            b.frames(6)
        b.frames(250)
        if bytes(b.memdump(STATUS, 2)) != b"VD":
            print("   FAIL: runner did not come up")
            return [f"{name}: runner did not come up"], far

        # 1. the image arrived intact, every segment of it
        bad = []
        for a in probes:
            r = b.cmd(f"EVAL db(${a:06x})")
            if r.get("value") != far_byte(far, a):
                bad.append((a, far_byte(far, a), r.get("value")))
        print(f"copy-up    : {len(probes) - len(bad)}/{len(probes)} probed bytes match")
        if bad:
            for a, w, g in bad[:6]:
                got = "??" if g is None else f"${g:02X}"
                print(f"   ${a:06X}: linker says ${w:02X}, target has {got}")
            fails.append(f"{name}: {len(bad)} of {len(probes)} probed bytes differ; "
                         f"first at ${bad[0][0]:06X}")

        # 2. it is running there.  The bridge reports a 16-bit PC and no K
        #    register, so the answer has to come from the target: a routine in
        #    `farcode` does phk and the runner publishes what it said.  The
        #    bank to expect is where the linker put THAT routine -- in the
        #    split build it is not the first far bank.
        ran = b.peek(STATUS + 24)
        want_bank = syms["_fl_running_bank"] >> 16
        mode = b.cmd("HWSTATE").get("cpu", {}).get("mode")
        print(f"execution  : far code reports bank ${ran:02X} "
              f"(_fl_running_bank linked at ${syms['_fl_running_bank']:06X}), "
              f"CPU {mode}")
        if mode != "65C816":
            fails.append(f"{name}: CPU is not the 65C816: {mode}")
        if ran != want_bank:
            fails.append(f"{name}: far code is executing in bank ${ran:02X}, not "
                         f"${want_bank:02X} -- the copy-up did not take effect")

        # 3. the far heap is above the far code.  farmem derives the first
        #    free bank from _fl_top, the loader's record of the highest
        #    address it wrote past; that record is checked against the ELF
        #    too (a padded sub-page tail may put it up to 255 bytes high).
        top = int.from_bytes(bytes(b.memdump(syms["_fl_top"], 3)), "little")
        heap_first = b.peek(STATUS + 17)
        want_first = top_bank + 1
        print(f"far heap   : loader wrote up to ${top:06X} (image ends ${end:06X}); "
              f"heap starts at bank ${heap_first:02X}, code reaches ${top_bank:02X}")
        if not end <= top < end + 256:
            fails.append(f"{name}: _fl_top is ${top:06X} but the image ends at "
                         f"${end:06X}")
        if heap_first < want_first:
            fails.append(f"{name}: far heap starts at bank ${heap_first:02X}: it "
                         f"overlaps the code, which reaches ${end - 1:06X}")

        # 4. staging cost nothing: it is inside the reserved MEMAC A window
        hdr, scr = syms["_fl_hdr"], syms["_fl_scr"]
        print(f"staging    : ${hdr:04X}-${scr + 15:04X} (MEMAC A window)")
        if not (0x8000 <= hdr and scr + 16 <= 0xA000):
            fails.append(f"{name}: staging buffer ${hdr:04X}-${scr + 15:04X} is "
                         f"outside the MEMAC A window, so it costs real bank $00 space")

        # 5. bank $00 is on the fast bus where the program lives.  The runner
        #    publishes what rapidus_speedup() found and did (STATUS+25..28);
        #    the registers are read back as well, so a report is not trusted
        #    over the hardware.  Windows are derived from the linker's
        #    placement, not restated: the direct page and a data symbol say
        #    where the program is, the staging buffer says where MEMAC is.
        present, mcr_before, mcr_said, cmcr_said = (b.peek(STATUS + 25 + i)
                                                    for i in range(4))
        irq_fast = b.peek(STATUS + 32)
        mcr = b.cmd("EVAL db($FF0080)").get("value")
        cmcr = b.cmd("EVAL db($FF0081)").get("value")
        win = lambda a: a >> 14
        program = {win(syms["_DirectPageStart"]), win(syms["rapidus"])}
        memac = win(hdr)
        vectors = win(0xFFE4)       # the OS window: irq_install() takes it
                                    # fast after rapidus_speedup() reported
        print(f"speed map  : Rapidus {'present' if present else 'ABSENT'}, "
              f"MCR ${mcr_before:02X} -> ${mcr:02X}, CMCR ${cmcr:02X}; "
              f"program in window(s) {sorted(program)}, MEMAC in {memac}, "
              f"vectors in {vectors} ({'fast' if irq_fast else 'as found'})")
        if not present:
            fails.append(f"{name}: rapidus_speedup() did not find the board signature")
        mcr_want = mcr_said & ~(1 << vectors) if irq_fast else mcr_said
        if (mcr, cmcr) != (mcr_want, cmcr_said):
            fails.append(f"{name}: runner reports MCR ${mcr_said:02X} CMCR "
                         f"${cmcr_said:02X} (irq.fast={irq_fast}) but the "
                         f"registers read ${mcr:02X} ${cmcr:02X}")
        for w in sorted(program):
            if mcr & (1 << w):
                fails.append(f"{name}: window {w} (${w << 14:04X}) holds the "
                             f"program's data and is still SLOW: MCR ${mcr:02X}")
        if not mcr & (1 << memac):
            fails.append(f"{name}: window {memac} (${memac << 14:04X}) holds the "
                         f"MEMAC window and is FAST, which hides VRAM: MCR ${mcr:02X}")
        if 0 in program and not cmcr & 0x40:
            fails.append(f"{name}: window 0 holds the direct page but CMCR "
                         f"${cmcr:02X} still writes it through at 1.79 MHz")
    finally:
        emu.stop()
    return fails, far


def main():
    fails = []
    ran = []
    for name, why in IMAGES:
        f, far = check_image(name, why)
        fails += f
        ran.append((name, far))
        print()

    # 6. the refusal, on the real build's disk
    disk = os.path.join(BUILD, f"{IMAGES[0][0]}-boot.atr")
    fails += check_refuses_6502(disk, ran[0][1])

    print()
    if fails:
        print("gem4xe-m6: FAILED")
        for f in fails:
            print("   FAIL:", f)
        return 1
    banks = {}
    for name, far in ran:
        banks[name] = sorted({a >> 16 for a, _ in far} |
                             {(a + len(d) - 1) >> 16 for a, d in far})
    where = "; ".join(f"{n} in bank{'s' if len(b) > 1 else ''} "
                      + ", ".join(f"${x:02X}" for x in b) for n, b in banks.items())
    print(f"gem4xe-m6: PASSED -- far code copied up and running: {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
