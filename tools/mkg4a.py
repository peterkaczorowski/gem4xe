#!/usr/bin/env python3
"""ELF x3 -> gem4xe loadable application (.g4a).

A gem4xe application is linked at placeholder addresses (src/app/gemapp.scm)
and put wherever there is room at load time: its near region -- direct
page, stack, data -- somewhere page-aligned in gem4xe's bank-$00 pool, its
far region -- the code -- in some bank of the far heap.  Neither the
compiler nor the linker emits relocations for that, so this tool DERIVES
them: the same objects are linked three times, once at the placeholders,
once with the near region moved up a page and once with the far region
moved up a bank, and the bytes that changed are the fixups.

    near up a page:  every byte that changed is the HIGH byte of a near
                     address (a page moves it by exactly 1);
    far up a bank:   every byte that changed is the BANK byte of a far
                     address (a bank moves it by exactly 1).

Any byte that changed by other than +1, or changed under both shifts, is
address arithmetic the loader could not relocate -- a shifted or divided
address, a bank in a low byte -- and the tool refuses rather than emit a
program that would work at the placeholders and nowhere else.  Nothing
else about the layout is assumed: the shifts, the region bases and the
entry point are read from the ELFs.

    G4A file, little-endian:
      0  'G4A' v                    magic, format version (1 or 2)
      4  u16 near_base              where the near region was linked
      6  u16 near_size              its whole extent, a page multiple
      8  u16 far_off                the far region's offset in its bank
     10  u32 far_size
     14  u8  far_bank               the bank it was linked in
     15  u8  far_banks              banks the far region spans -- the image
                                    AND the far variables, which carry no
                                    bytes and so are nowhere in the file
     16  u16 entry_lo, u8 entry_bank, u8 0
     20  u16 x4: fixup counts -- near part: high-byte, bank; far part: high-byte, bank
     28  u32 0
     32  near bytes, far bytes, then the four fixup lists

    VERSION 1 and VERSION 2 differ in one thing: how wide a FAR fixup
    offset is.  v1 writes it as a u16, which caps the far image at 64 KB;
    v2 writes it as three bytes and has no such cap.  The near lists are
    u16 in both, and stay that way -- the near region is a page-aligned
    slice of gem4xe's bank-$00 pool and cannot be bigger than the bank.

    A file is written v1 whenever the far image fits in 64 KB, which is
    every program in this tree but one, so their bytes do not change and
    their disk images do not grow.  GACS's GEM shell is the exception and
    the reason v2 exists: 102,862 bytes of farcode and another 11 KB of
    constants, which is not close to a bank and cannot be made to fit one.
    The loader reads both (src/sys/app.c).

    The far IMAGE may now span banks.  It used to be refused here, on the
    ground that the program counter wraps inside a bank so code may not
    cross one -- which is true, and is not this tool's business: no single
    FUNCTION crosses a bank because each is its own linker fragment placed
    inside one memory, and src/app/gemapp.scm gives every code bank a pair
    of memories either side of the $D5 page for exactly that reason.  The
    packer was enforcing an invariant it does not own, and the cost was
    that no application could be larger than a bank.

Usage: mkg4a.py base.elf near-shifted.elf far-shifted.elf out.g4a
                [--syms out.sym] [--c-array out.c NAME]
"""
import re
import struct
import sys

from mkxex import read_elf

MAGIC_V1 = b"G4A\x01"
MAGIC_V2 = b"G4A\x02"


def read_elf_all(path):
    """Every PT_LOAD, bytes or not: (vaddr, filebytes, memsz), plus symbols."""
    with open(path, "rb") as f:
        d = f.read()
    e_phoff, = struct.unpack_from("<I", d, 0x1C)
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x2A)
    segs = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _pa, p_filesz, p_memsz = struct.unpack_from("<6I", d, o)
        if p_type == 1 and p_memsz > 0:
            segs.append((p_vaddr, d[p_offset:p_offset + p_filesz], p_memsz))
    _, syms = read_elf(path)
    return segs, syms


def extents(segs, syms):
    """(near_base, near_end, far_base, far_end) of a link: the near region
    starts at the direct page, which the linker script puts first, and runs
    to the end of its last memory -- a memory's whole size is what the ELF
    records, and the bss within it must be zeroed; the far region is code
    and ends with its last byte."""
    near = [s for s in segs if s[0] < 0x10000]
    # A far memory nothing was placed in still gets a PT_LOAD with no file
    # bytes (the second half of the bank, above the $D5 hole, in an
    # application smaller than the first); it carries nothing and must not
    # stretch the far region to it.
    far = [s for s in segs if s[0] >= 0x10000 and len(s[1])]
    if not near or not far:
        raise SystemExit("expected both a near and a far region")
    dp = syms["_DirectPageStart"]
    if dp & 0xFF:
        raise SystemExit(f"_DirectPageStart ${dp:04X} is not page aligned")
    near_end = max(a + m for a, _, m in near)
    far_base = min(a for a, _, _ in far)
    far_end = max(a + len(d) for a, d, _ in far)
    if min(a for a, _, _ in near) < dp:
        raise SystemExit("a near segment lies below the direct page")
    return dp, near_end, far_base, far_end


def far_span(mapfile, far_base):
    """How far the far region REACHES, from the linker's own list file.

    This is not the same as how far the IMAGE reaches, and the difference
    is a --data-model=large program's variables: `far` and `zfar`
    (src/app/gemapp.scm) carry no bytes, so they are nowhere in the image
    -- but the loader allocates whole banks from the count in the header,
    and a count taken from the image alone would leave them outside what
    it was given.  That is memory the far heap goes on to hand somebody
    else, written to by a program that believes it owns it, with nothing
    failing at the time.

    It has to come from the map because the ELF cannot say: the linker
    emits a PT_LOAD for every memory it was GIVEN, used or not, all with
    the memory's full size, so an empty AppFarBss looks exactly like a
    full one.  The map lists the sections it actually placed.
    """
    end = far_base
    try:
        f = open(mapfile)
    except OSError:
        return end
    with f:
        for ln in f:
            m = re.match(r"^(far|zfar|ifar|farcode|switch|cfar|libcode|code)\s+"
                         r"([0-9a-f]{6})-([0-9a-f]{6})\s", ln)
            if m and int(m.group(2), 16) >= far_base:
                end = max(end, int(m.group(3), 16) + 1)
    return end


def image(segs, base, end):
    """The bytes of [base, end): what the file carries, zero elsewhere, and
    a mask of which bytes the file carried."""
    buf = bytearray(end - base)
    mask = bytearray(end - base)
    for a, data, memsz in segs:
        if base <= a < end:
            o = a - base
            buf[o:o + len(data)] = data
            for i in range(len(data)):
                mask[o + i] = 1
    return bytes(buf), bytes(mask)


def diff(base_img, base_mask, moved_img, moved_mask, what):
    """Offsets of the bytes that moved by exactly +1; refuses anything else."""
    if base_mask != moved_mask:
        raise SystemExit(f"{what}: the shifted link laid its bytes out differently")
    out = []
    for i, (a, b) in enumerate(zip(base_img, moved_img)):
        if a != b:
            if ((a + 1) & 0xFF) != b:
                raise SystemExit(f"{what}: byte {i} changed from ${a:02X} to "
                                 f"${b:02X}, not by +1: address arithmetic "
                                 f"the loader cannot relocate")
            out.append(i)
    return out


def main(argv):
    if len(argv) < 4:
        raise SystemExit(__doc__)
    base_elf, near_elf, far_elf, out = argv[:4]
    syms_out = c_out = c_name = None
    i = 4
    while i < len(argv):
        if argv[i] == "--syms":
            syms_out = argv[i + 1]
            i += 2
        elif argv[i] == "--c-array":
            c_out, c_name = argv[i + 1], argv[i + 2]
            i += 3
        else:
            raise SystemExit(f"unknown option {argv[i]}")

    b_segs, b_syms = read_elf_all(base_elf)
    n_segs, n_syms = read_elf_all(near_elf)
    f_segs, f_syms = read_elf_all(far_elf)
    nb, ne, fb, fe = extents(b_segs, b_syms)
    nn, nne, nf, nfe = extents(n_segs, n_syms)
    fn, fne, ff, ffe = extents(f_segs, f_syms)
    fspan = far_span(base_elf[:-4] + ".map", fb)
    far_banks = ((max(fspan, fe) - 1) >> 16) - (fb >> 16) + 1

    # The shifts are whatever the links say they are; each must move one
    # region by a whole page / bank and leave the other alone.
    near_shift, far_shift = nn - nb, ff - fb
    if near_shift != 0x100 or nf != fb or (nne - nn) != (ne - nb) or (nfe - nf) != (fe - fb):
        raise SystemExit(f"the near-shifted link should move the near region "
                         f"up exactly one page: near ${nb:04X}->${nn:04X}, "
                         f"far ${fb:06X}->${nf:06X}")
    if far_shift != 0x10000 or fn != nb or (fne - fn) != (ne - nb) or (ffe - ff) != (fe - fb):
        raise SystemExit(f"the far-shifted link should move the far region "
                         f"up exactly one bank: near ${nb:04X}->${fn:04X}, "
                         f"far ${fb:06X}->${ff:06X}")

    near_size = (ne - nb + 0xFF) & ~0xFF
    far_size = fe - fb
    near_img, near_mask = image(b_segs, nb, nb + near_size)
    far_img, far_mask = image(b_segs, fb, fe)
    n_near_img, n_near_mask = image(n_segs, nn, nn + near_size)
    n_far_img, n_far_mask = image(n_segs, nf, nf + far_size)
    f_near_img, f_near_mask = image(f_segs, fn, fn + near_size)
    f_far_img, f_far_mask = image(f_segs, ff, ff + far_size)

    near_hi = diff(near_img, near_mask, n_near_img, n_near_mask, "near part, page shift")
    far_hi = diff(far_img, far_mask, n_far_img, n_far_mask, "far part, page shift")
    near_bank = diff(near_img, near_mask, f_near_img, f_near_mask, "near part, bank shift")
    far_bank = diff(far_img, far_mask, f_far_img, f_far_mask, "far part, bank shift")
    both = (set(near_hi) & set(near_bank)) | (set(far_hi) & set(far_bank))
    if both:
        raise SystemExit(f"bytes moved under both shifts: {sorted(both)[:8]}")

    entry = b_syms["__program_start"]
    if not (fb <= entry < fe):
        raise SystemExit(f"entry ${entry:06X} is not in the far region")

    # v1 unless the far image needs more room than its offsets have. Every
    # program in this tree but GACS's shell stays v1, byte for byte.
    v2 = far_size > 0x10000
    for name, lst in (("near", near_hi + near_bank), ("far", far_hi + far_bank)):
        if len(lst) > 0xFFFF:
            raise SystemExit(f"{len(lst)} {name} fixups: the header counts "
                             f"them in a u16")
    hdr = (MAGIC_V2 if v2 else MAGIC_V1) + struct.pack(
                              "<HHHIBBHBBHHHHI",
                              nb, near_size, fb & 0xFFFF, far_size, fb >> 16,
                              far_banks,
                              entry & 0xFFFF, entry >> 16, 0,
                              len(near_hi), len(near_bank), len(far_hi), len(far_bank), 0)
    assert len(hdr) == 32, len(hdr)
    body = near_img + far_img
    for lst in (near_hi, near_bank):
        body += b"".join(struct.pack("<H", o) for o in lst)
    for lst in (far_hi, far_bank):
        if v2:
            body += b"".join(struct.pack("<I", o)[:3] for o in lst)
        else:
            body += b"".join(struct.pack("<H", o) for o in lst)
    blob = hdr + body
    with open(out, "wb") as f:
        f.write(blob)

    if syms_out:
        with open(syms_out, "w") as f:
            for name, val in sorted(b_syms.items(), key=lambda kv: kv[1]):
                f.write(f"{name} {val:06X}\n")
    if c_out:
        with open(c_out, "w") as f:
            f.write(f"/* Generated by tools/mkg4a.py from {out} -- do not edit. */\n")
            f.write("#include <stdint.h>\n")
            f.write("#include \"portab.h\"\n")
            f.write(f"const uint32_t {c_name}_len = {len(blob)}UL;\n")
            f.write(f"const uint8_t FAR {c_name}[{len(blob)}] = {{\n")
            for o in range(0, len(blob), 16):
                f.write("    " + ",".join(f"{b}" for b in blob[o:o + 16]) + ",\n")
            f.write("};\n")

    print(f"{out}: near ${nb:04X}+{near_size} ({len(near_hi)} page, "
          f"{len(near_bank)} bank fixups), far ${fb:06X}+{far_size} "
          f"({len(far_hi)} page, {len(far_bank)} bank fixups), "
          f"entry ${entry:06X}, {len(blob)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
