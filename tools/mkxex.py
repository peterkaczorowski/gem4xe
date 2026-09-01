#!/usr/bin/env python3
"""ELF -> Atari executable (.xex) packer for Calypsi output.

Calypsi's linker always emits ELF+DWARF and has no .xex output format, so this
walks the ELF program headers and writes the Atari segmented binary format:

    $FFFF                       file magic (once, at the start)
    <start> <end> <data...>     one per loadable segment, little-endian, end
                                is INCLUSIVE
    $02E0 $02E1 <addr>          the run vector -- DOS jumps here when the load
                                completes

Only PT_LOAD segments with a non-zero file size are emitted: a segment with
p_filesz == 0 and p_memsz > 0 is .bss, which occupies RAM but carries no bytes.
Calypsi's cstartup zeroes what needs zeroing through its data_init_table.

Every segment must live in bank $00 -- an Atari DOS loader has no concept of
65816 banks.  Code destined for Rapidus banks $01+ has to be copied up by the
program itself; that is a later milestone, and this refuses it rather than
silently truncating the address.

Usage: mkxex.py in.elf out.xex [--entry SYMBOL] [--syms out.sym]

--syms writes "NAME ADDR" lines for every symbol, so a test harness can find
buffers by name instead of hard-coding addresses that drift on every rebuild.
"""
import struct
import sys

PT_LOAD = 1


def read_elf(path):
    """Return (list of (vaddr, bytes), {symbol: value}) from a 32-bit LE ELF."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not an ELF file")
    if d[4] != 1 or d[5] != 1:
        raise SystemExit(f"{path}: expected a 32-bit little-endian ELF")

    e_shoff, = struct.unpack_from("<I", d, 0x20)
    e_phoff, = struct.unpack_from("<I", d, 0x1C)
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x2A)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)

    segs = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _pa, p_filesz, p_memsz = struct.unpack_from("<6I", d, o)
        if p_type == PT_LOAD and p_filesz > 0:
            segs.append((p_vaddr, d[p_offset:p_offset + p_filesz]))

    syms = {}
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sh_type, = struct.unpack_from("<I", d, o + 4)
        if sh_type != 2:                                  # SHT_SYMTAB
            continue
        sh_offset, sh_size, sh_link, _info, _align, sh_entsize = struct.unpack_from(
            "<6I", d, o + 16)
        stro = e_shoff + sh_link * e_shentsize
        str_off, str_size = struct.unpack_from("<II", d, stro + 16)
        strtab = d[str_off:str_off + str_size]
        for j in range(sh_size // sh_entsize):
            so = sh_offset + j * sh_entsize
            st_name, st_value = struct.unpack_from("<II", d, so)
            end = strtab.find(b"\0", st_name)
            name = strtab[st_name:end].decode("ascii", "replace")
            if name:
                syms.setdefault(name, st_value)
    return segs, syms


def build_xex(segs, entry):
    out = bytearray(b"\xff\xff")
    for vaddr, data in sorted(segs):
        end = vaddr + len(data) - 1
        if end > 0xFFFF:
            raise SystemExit(
                f"segment ${vaddr:06X}-${end:06X} is outside bank $00; an Atari DOS "
                f"loader cannot place it. Copy it up at runtime instead.")
        out += struct.pack("<HH", vaddr, end) + data
    out += struct.pack("<HHH", 0x02E0, 0x02E1, entry)     # run vector
    return bytes(out)


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    src, dst = argv[0], argv[1]
    want = "__program_start"
    if "--entry" in argv:
        want = argv[argv.index("--entry") + 1]

    segs, syms = read_elf(src)
    if want not in syms:
        raise SystemExit(f"{src}: entry symbol {want!r} not found")
    entry = syms[want] & 0xFFFF

    xex = build_xex(segs, entry)
    open(dst, "wb").write(xex)

    if "--syms" in argv:
        path = argv[argv.index("--syms") + 1]
        with open(path, "w") as f:
            for name in sorted(syms):
                f.write(f"{name} {syms[name]:06X}\n")
        print(f"    {path}: {len(syms)} symbols")

    total = sum(len(d) for _, d in segs)
    print(f"{dst}: {len(segs)} segment(s), {total} bytes of data, "
          f"{len(xex)} bytes total, run ${entry:04X} ({want})")
    for vaddr, data in sorted(segs):
        print(f"    ${vaddr:04X}-${vaddr + len(data) - 1:04X}  {len(data):5d} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
