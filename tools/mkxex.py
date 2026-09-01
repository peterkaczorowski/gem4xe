#!/usr/bin/env python3
"""ELF -> Atari executable (.xex) packer for Calypsi output.

Calypsi's linker always emits ELF+DWARF and has no .xex output format, so this
walks the ELF program headers and writes the Atari segmented binary format:

    $FFFF                       file magic (once, at the start)
    <start> <end> <data...>     one per loadable segment, little-endian, end
                                is INCLUSIVE
    $02E2 $02E3 <addr>          INITAD -- DOS calls here after loading a segment
    $02E0 $02E1 <addr>          RUNAD  -- DOS jumps here when the load completes

Only PT_LOAD segments with a non-zero file size are emitted: a segment with
p_filesz == 0 and p_memsz > 0 is .bss, which occupies RAM but carries no bytes.
Calypsi's cstartup zeroes what needs zeroing through its data_init_table.

FAR SEGMENTS

A .xex segment header is two 16-bit addresses, so an Atari DOS loader cannot
place anything above $FFFF -- but gem4xe links its code into bank $01.  Those
segments therefore travel as CHUNKS: each is aimed at the staging buffer that
src/farload.s reserves in bank $00, followed by a two-byte segment that writes
INITAD, which makes DOS call the copier.  The copier moves the chunk to its
real home and returns, and by the time DOS reaches the run vector the far image
is assembled.

Writing INITAD after every chunk rather than once at the start is deliberate.
DOSes disagree about whether INITAD is called after EVERY segment or only after
one that writes to it; rewriting it per chunk is correct under both readings,
and the copier zeroes its own length field so a spurious extra call does
nothing.

A chunk is always a whole number of 256-byte pages, which is what lets the
copier be a flat page loop.  The tail of a segment is handled by sliding the
last chunk BACKWARDS to a page multiple -- recopying a few bytes already
placed -- rather than by padding forwards into whatever follows.

The staging layout is not repeated here: _fl_hdr, _fl_buf and _fl_scr come out
of the ELF symbol table, so src/farload.s and src/gem4xe.scm remain the only
places that decide where the buffer is and how big it is.

Usage: mkxex.py in.elf out.xex [--entry SYMBOL] [--syms out.sym]

--syms writes "NAME ADDR" lines for every symbol, so a test harness can find
buffers by name instead of hard-coding addresses that drift on every rebuild.
"""
import struct
import sys

PT_LOAD = 1


def read_elf(path):
    """Return (list of (vaddr, bytes), {symbol: value}) from a 32-bit LE ELF."""
    with open(path, "rb") as f:
        d = f.read()
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


RUNAD, INITAD = 0x02E0, 0x02E2


def seg(addr, data):
    """One .xex segment: start, INCLUSIVE end, bytes."""
    return struct.pack("<HH", addr, addr + len(data) - 1) + data


def far_chunks(vaddr, data, chunk):
    """Split a far segment into whole-page pieces inside the staging buffer.

    Every piece is a multiple of 256 bytes so the copier can be a flat page
    loop.  The tail is made whole by sliding the final piece BACKWARDS -- it
    recopies a few bytes that the previous piece already placed, which costs
    microseconds and cannot touch anything outside this segment.  Padding
    forwards would write past the end into whoever is next.
    """
    n = len(data)
    off = 0
    while off < n:
        rem = n - off
        if rem >= chunk:
            yield vaddr + off, data[off:off + chunk]
            off += chunk
            continue
        take = (rem + 255) & ~0xFF
        if take <= n:
            start = n - take
            yield vaddr + start, data[start:]
        else:
            # Shorter than one page in total, so there is nothing to slide
            # back into.  Pad, and let the caller police the overrun.
            yield vaddr + off, data[off:] + b"\x00" * (take - rem)
        off = n


def build_xex(segs, entry, syms):
    near, far = [], []
    for vaddr, data in sorted(segs):
        end = vaddr + len(data) - 1
        if end <= 0xFFFF:
            near.append((vaddr, data))
        elif vaddr > 0xFFFF:
            far.append((vaddr, data))
        else:
            raise SystemExit(
                f"segment ${vaddr:06X}-${end:06X} straddles the bank $00 boundary")

    out = bytearray(b"\xff\xff")
    for vaddr, data in near:
        out += seg(vaddr, data)
    if far:
        out += stage_far(far, syms)
    out += seg(RUNAD, struct.pack("<H", entry))
    return bytes(out), near, far


def stage_far(far, syms):
    """Chunk the far image into staging-buffer segments plus INITAD triggers."""
    need = ("_fl_hdr", "_fl_buf", "_fl_scr", "_fl_copy")
    missing = [n for n in need if n not in syms]
    if missing:
        raise SystemExit(
            f"far segments need src/farload.s linked in; missing {', '.join(missing)}")
    hdr, buf, scr, copier = (syms[n] for n in need)
    chunk = scr - buf
    if chunk <= 0 or chunk % 256:
        raise SystemExit(
            f"staging buffer is {chunk} bytes; it must be a positive multiple of 256")
    if buf != hdr + 4:
        raise SystemExit(
            f"_fl_buf (${buf:04X}) must follow _fl_hdr (${hdr:04X}) immediately, "
            f"so that a header and its payload are one .xex segment")

    starts = sorted(a for a, _ in far)
    out = bytearray()
    # Zero the length field while INITAD is still unset, so that the first
    # trigger cannot act on whatever the staging buffer happened to contain.
    out += seg(hdr, b"\x00" * 4)
    for vaddr, data in far:
        for dst, piece in far_chunks(vaddr, data, chunk):
            over = dst + len(piece)
            if over > vaddr + len(data):
                clash = [a for a in starts if vaddr + len(data) <= a < over]
                if clash:
                    raise SystemExit(
                        f"padding ${vaddr:06X} would overwrite ${clash[0]:06X}")
            out += seg(hdr, struct.pack("<HBB", dst & 0xFFFF, dst >> 16,
                                        len(piece) // 256) + piece)
            out += seg(INITAD, struct.pack("<H", copier))
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
    if syms[want] > 0xFFFF:
        raise SystemExit(
            f"{src}: entry symbol {want!r} is at ${syms[want]:06X}; DOS's run "
            f"vector is 16-bit, so the entry point has to be in bank $00")
    entry = syms[want]

    xex, near, far = build_xex(segs, entry, syms)
    open(dst, "wb").write(xex)

    if "--syms" in argv:
        path = argv[argv.index("--syms") + 1]
        with open(path, "w") as f:
            for name in sorted(syms):
                f.write(f"{name} {syms[name]:06X}\n")
        print(f"    {path}: {len(syms)} symbols")

    nearb = sum(len(d) for _, d in near)
    farb = sum(len(d) for _, d in far)
    print(f"{dst}: {len(xex)} bytes, run ${entry:04X} ({want})")
    for vaddr, data in near:
        print(f"    ${vaddr:04X}-${vaddr + len(data) - 1:04X}  {len(data):5d} bytes  bank $00")
    for vaddr, data in far:
        print(f"  ${vaddr:06X}-${vaddr + len(data) - 1:06X}  {len(data):5d} bytes  "
              f"staged through ${syms['_fl_buf']:04X}")
    if far:
        chunk = syms["_fl_scr"] - syms["_fl_buf"]
        print(f"    {nearb} bytes in bank $00, {farb} copied up in "
              f"{-(-farb // chunk)} chunk(s) of {chunk}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
