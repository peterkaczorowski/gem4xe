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
place anything above $FFFF -- but gem4xe links its code into the banks above
it, one linker memory per bank from $01 up (src/gem4xe.scm), so a far
segment per bank the image reached.  Those segments therefore travel as
CHUNKS: each is aimed at the staging buffer that src/farload.s reserves in
bank $00, followed by a two-byte segment that writes INITAD, which makes DOS
call the loader.  The loader unpacks the chunk to its real home and returns,
and by the time DOS reaches the run vector the far image is assembled.

Writing INITAD after every chunk rather than once at the start is deliberate.
DOSes disagree about whether INITAD is called after EVERY segment or only after
one that writes to it; rewriting it per chunk is correct under both readings,
and the loader zeroes its own count field so a spurious extra call does
nothing.

THE CHUNKS ARE PACKED.  The far image is two thirds of a double-density
floppy and most of a minute of a 1050's reading, and it is code and tables,
which an LZ77 makes a third smaller.  Each far segment is packed on its own
as one stream of TOKENS, and the stream is cut into chunks at token
boundaries, so a chunk unpacks on its own given only where it goes -- what
a match reaches back into is output the loader already wrote, in the banks
above, and the segment before this one is never referenced.  A token is:

    byte       LLLL MMMM   L literal bytes follow; a match of M+4 bytes after
    bytes      if L == 15: added to L, one after another, until one is not 255
    L bytes    the literals
    word       the match's offset, little-endian, 1..65535: it copies from
               (the output so far) minus this, which may overlap what it
               writes -- that is how a run is encoded.  Or 0: NO match, the
               token was only its literals (M is 0 and nothing follows) --
               how a long stretch of incompressible bytes is carried, a
               thousand literals at a time, so that no token outgrows the
               staging buffer
    bytes      if M == 15: added to M as above

The last token of a chunk may stop after its literals, without even the
offset word.  Nothing marks that: the chunk's header carries how many bytes
come OUT of it, the loader stops when it has written that many, and the
packer cuts only at token boundaries, so the count runs out exactly where a
token ends.  The header is five bytes, a 24-bit destination and that 16-bit
count, and it sits immediately before the payload so that a chunk is ONE
.xex segment.

unpack() below is the loader written in Python, and the packer checks its
own output through it before writing a byte of the .xex: a format mistake
fails the build here, not a boot there.  tests/host/test_mkxex.py loads the
.xex the way a DOS does and requires the image back byte for byte.

The staging layout is not repeated here: _fl_hdr, _fl_buf and _fl_end come out
of the ELF symbol table, so src/farload.s and src/gem4xe.scm remain the only
places that decide where the buffer is and how big it is.

Usage: mkxex.py in.elf out.xex [--entry SYMBOL] [--syms out.sym]

--syms writes "NAME ADDR" lines for every symbol, so a test harness can find
buffers by name instead of hard-coding addresses that drift on every rebuild.
"""
import functools
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


MIN_MATCH = 4          # a match shorter than this costs more than its literals
MAX_OFFSET = 0xFFFF    # the offset is a word
MAX_LITERALS = 1024    # per token, so that no token outgrows a chunk
MAX_MATCH = 1024       # ...and no chunk's output outgrows its 16-bit count
HASH = 4               # bytes a candidate match is found by


@functools.lru_cache(maxsize=16)
def pack(data):
    """LZ77 the bytes into a list of tokens, each the bytes of one token.

    A hash of the next four bytes finds earlier places they occurred; the
    longest match among them wins, unless starting one byte later would find
    a longer one (the usual lazy step).  Simple and deterministic, which
    matters more than the last few percent: the gates recompute the chunking
    from the ELF and expect the same seams.
    """
    n = len(data)
    tokens = []
    heads = {}

    def note(i):
        if i + HASH <= n:
            heads.setdefault(data[i:i + HASH], []).append(i)

    def longest(i):
        if i + MIN_MATCH > n:
            return 0, 0
        best, off = 0, 0
        lim = min(n - i, MAX_MATCH)
        for p in reversed(heads.get(data[i:i + HASH], ())):
            if i - p > MAX_OFFSET:
                break
            k = HASH
            while k < lim and data[p + k] == data[i + k]:
                k += 1
            if k > best:
                best, off = k, i - p
                if k >= lim:
                    break
        return best, off

    def token(lits, mlen, off):
        L, M = len(lits), mlen - MIN_MATCH if mlen else 0
        out = bytearray([(min(L, 15) << 4) | min(M, 15)])
        if L >= 15:
            r = L - 15
            while r >= 255:
                out.append(255)
                r -= 255
            out.append(r)
        out += lits
        out += struct.pack("<H", off)          # 0: no match follows
        if mlen:
            if M >= 15:
                r = M - 15
                while r >= 255:
                    out.append(255)
                    r -= 255
                out.append(r)
        tokens.append(bytes(out))

    i = 0
    lits = bytearray()
    while i < n:
        mlen, off = longest(i)
        if mlen >= MIN_MATCH and i + 1 < n:
            later, _ = longest(i + 1)
            if later > mlen + 1:
                mlen = 0                       # take this byte; match next time
        if len(lits) >= MAX_LITERALS:
            token(lits, 0, 0)
            lits = bytearray()
        if mlen >= MIN_MATCH:
            token(lits, mlen, off)
            lits = bytearray()
            for k in range(i, i + mlen):
                note(k)
            i += mlen
        else:
            lits.append(data[i])
            note(i)
            i += 1
    if lits:
        token(lits, 0, 0)
    return tokens


def literals(tok):
    """A token's literal count, and where in it the offset word starts."""
    i = 1
    L = tok[0] >> 4
    if L == 15:
        while True:
            L += tok[i]
            i += 1
            if tok[i - 1] != 255:
                break
    return L, i + L


def literal_only(tok):
    """True if the token carries no match: it ends in the no-match word."""
    L, i = literals(tok)
    return i + 2 == len(tok) and tok[i] | tok[i + 1] == 0


def join(tokens):
    """The tokens as one chunk's payload.

    A chunk's last token stops after its literals if it has no match, so the
    no-match word that marks that mid-chunk is left off the end.
    """
    if tokens and literal_only(tokens[-1]):
        tokens = tokens[:-1] + [tokens[-1][:-2]]
    return b"".join(tokens)


def token_out(tok):
    """How many bytes a token writes: its literals plus its match."""
    L, i = literals(tok)
    if i >= len(tok) or tok[i] | tok[i + 1] == 0:
        return L
    i += 2
    M = (tok[0] & 15) + MIN_MATCH
    if (tok[0] & 15) == 15:
        while True:
            M += tok[i]
            i += 1
            if tok[i - 1] != 255:
                break
    return L + M


def unpack(packed, count, before=b""):
    """src/farload.s in Python: `count` bytes out of `packed`.

    `before` is the output the loader has already written ahead of this
    chunk's destination, which is what a match may reach back into.
    """
    out = bytearray(before)
    end = len(out) + count
    i = 0
    while len(out) < end:
        tok = packed[i]
        i += 1
        L = tok >> 4
        if L == 15:
            while True:
                L += packed[i]
                i += 1
                if packed[i - 1] != 255:
                    break
        out += packed[i:i + L]
        i += L
        if len(out) >= end:
            break
        off = packed[i] | (packed[i + 1] << 8)
        i += 2
        if off == 0:
            if tok & 15:
                raise ValueError(f"no-match token with a match length at {i - 2}")
            continue
        M = (tok & 15) + MIN_MATCH
        if (tok & 15) == 15:
            while True:
                M += packed[i]
                i += 1
                if packed[i - 1] != 255:
                    break
        if not 1 <= off <= len(out):
            raise ValueError(f"match reaches {off} bytes back at output {len(out)}")
        for _ in range(M):
            out.append(out[-off])
    if len(out) != end or i != len(packed):
        raise ValueError(f"chunk unpacked to {len(out) - len(before)} bytes of "
                         f"{count}, using {i} of {len(packed)}")
    return bytes(out[len(before):])


def far_chunks(vaddr, data, chunk):
    """Pack a far segment and cut it into chunks that fit the staging buffer.

    Yields (destination, the bytes the chunk unpacks to, the packed bytes).
    Cuts are at token boundaries only, and a chunk's output is kept under
    what its 16-bit count can say.  Every chunk is unpacked again here, on
    top of what came before it, and must give back the segment's own bytes.
    """
    tokens = pack(data)
    done = 0
    piece, out = [], 0

    def cut():
        packed = join(piece)
        got = unpack(packed, out, data[:done])
        if got != data[done:done + out]:
            raise SystemExit(f"packer bug: chunk at ${vaddr + done:06X} "
                             f"does not unpack to what went in")
        return vaddr + done, got, packed

    for tok in tokens:
        t_out = token_out(tok)
        if piece and (sum(map(len, piece)) + len(tok) > chunk
                      or out + t_out > 0xFFFF):
            yield cut()
            done += out
            piece, out = [], 0
        if len(tok) > chunk:
            raise SystemExit(f"a {len(tok)}-byte token cannot fit a "
                             f"{chunk}-byte staging buffer")
        piece.append(tok)
        out += t_out
    if piece:
        yield cut()
        done += out
    if done != len(data):
        raise SystemExit(f"packer bug: {done} of {len(data)} bytes chunked")


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
        staged, _ = stage_far(far, syms)
        out += staged
    out += seg(RUNAD, struct.pack("<H", entry))
    return bytes(out), near, far


def stage_far(far, syms):
    """Chunk the far image into staging-buffer segments plus INITAD triggers."""
    need = ("_fl_hdr", "_fl_buf", "_fl_end", "_fl_copy")
    missing = [n for n in need if n not in syms]
    if missing:
        raise SystemExit(
            f"far segments need src/farload.s linked in; missing {', '.join(missing)}")
    hdr, buf, scr, loader = (syms[n] for n in need)
    chunk = scr - buf
    if chunk <= 0:
        raise SystemExit(f"staging buffer is {chunk} bytes")
    if buf != hdr + HDR:
        raise SystemExit(
            f"_fl_buf (${buf:04X}) must follow the {HDR}-byte header at _fl_hdr "
            f"(${hdr:04X}) immediately, so that a chunk is one .xex segment")

    out = bytearray()
    # Zero the count while INITAD is still unset, so that the first trigger
    # cannot act on whatever the staging buffer happened to contain.
    out += seg(hdr, b"\x00" * HDR)
    stats = []
    for vaddr, data in far:
        n = 0
        for dst, plain, packed in far_chunks(vaddr, data, chunk):
            out += seg(hdr, struct.pack("<HBH", dst & 0xFFFF, dst >> 16, len(plain))
                       + packed)
            out += seg(INITAD, struct.pack("<H", loader))
            n += 1
        stats.append(n)
    return bytes(out), stats


HDR = 5    # the chunk header: a 24-bit destination and a 16-bit output count


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
        chunk = syms["_fl_end"] - syms["_fl_buf"]
        staged, chunks = stage_far(far, syms)
        packed = len(staged) - sum(chunks) * (4 + HDR + 6) - (4 + HDR)
        print(f"    {nearb} bytes in bank $00, {farb} far packed to {packed} "
              f"({100 * packed // farb}%) in {sum(chunks)} chunk(s) of up to {chunk}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
