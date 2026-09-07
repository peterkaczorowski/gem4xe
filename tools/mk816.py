#!/usr/bin/env python3
"""816.COM: put a Rapidus into 65C816 mode, from the DOS prompt.

    tools/mk816.py build/816.com

The Rapidus ALWAYS cold-boots as a 6502 -- Altirra's own device does it
in ColdReset() ("reset FPGA, force boot on 6502"), and the card does the
same -- so a machine that has just powered on cannot run gem4xe, and
gem4xe says so and leaves it alone (src/farload.s).  Something has to
make the switch, and on a machine without an Ultimate 1MB (whose Rapidus
plugin sets the CPU over the M1 signal before the OS runs) there is
nothing on the disk that can.  This is that something, and it is the
sequence tests/emu/product_boot.py proved:

    COLDST ($0244) = 1   the switch RESETS the CPU and the OS treats
                         that reset as a WARM start -- which is exactly
                         when a DOS does not run its start-up file.
                         Without this the machine comes back to a prompt
                         and sits there instead of starting GEM.
    $D1FF = $01          the PBI slot the Rapidus answers on
    $D191 = $00          bit 6 clear: the 65C816.  The CPU resets here
                         and this program never reaches its RTS; if it
                         does, nothing answered and the machine has no
                         Rapidus (or is already a 65C816, which gem4xe
                         will find out for itself).

Fourteen bytes at $0600 -- page 6, which no DOS allocates -- so it loads
under any of them and needs no MEMLO.
"""
import sys

ORG = 0x0600
RUNAD = 0x02E0
COLDST = 0x0244

# (mnemonic, bytes) -- the program, so the listing IS the source
PROGRAM = [
    ("lda #$01",     [0xA9, 0x01]),
    ("sta COLDST",   [0x8D, COLDST & 0xFF, COLDST >> 8]),
    ("sta $D1FF",    [0x8D, 0xFF, 0xD1]),      # A is still 1
    ("lda #$00",     [0xA9, 0x00]),
    ("sta $D191",    [0x8D, 0x91, 0xD1]),      # ...and the CPU resets
    ("rts",          [0x60]),
]


def code():
    return bytes(b for _, bs in PROGRAM for b in bs)


def xex():
    """An Atari binary: the header, one segment, and a run address."""
    body = code()
    end = ORG + len(body) - 1
    out = bytearray(b"\xff\xff")
    out += bytes([ORG & 0xFF, ORG >> 8, end & 0xFF, end >> 8])
    out += body
    out += bytes([RUNAD & 0xFF, RUNAD >> 8,
                  (RUNAD + 1) & 0xFF, (RUNAD + 1) >> 8,
                  ORG & 0xFF, ORG >> 8])
    return bytes(out)


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    data = xex()
    with open(argv[1], "wb") as f:
        f.write(data)
    print(f"{argv[1]}: {len(data)} bytes, {len(code())} of program at "
          f"${ORG:04X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
