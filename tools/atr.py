#!/usr/bin/env python3
"""Atari .atr disk images and the DOS 2.0/2.5 filesystem (single/enhanced density, 128-byte
sectors).  Used by the game extractors (they read DOS 2 files off the original disks) and by the
test harness (it writes a runtime .xex onto a DOS floppy to prove the loader path).

ATRImage is adapted from /home/jfergus/dev/a8-u4r/tools/atrlib.py.  Dos2 implements the standard
DOS 2 VTOC (sector 360), 8-entry directory (sectors 361-368) and the 3-byte per-sector file
links used by SD/ED disks.  It does NOT handle MyDOS "big" disks or double density.

Enhanced density is DOS 2.5's: 1040 sectors, the bitmap for sectors 720-1023 kept in a second
VTOC at sector 1024, and a file that uses any of them flagged $03 in the directory so that a
DOS 2.0 -- which cannot reach those sectors -- skips the entry instead of reading garbage.
The layout is taken from Altirra's disk explorer (src/ATIO/source/diskfsdos2.cpp: InitNew,
Flush, IsVisible, WriteFile), which reads and writes real DOS 2.5 disks, not from memory.
enhance() turns a single-density DOS 2 disk into one.
"""
import argparse
import struct
import sys

ATR_MAGIC = 0x0296
HEADER_SIZE = 16


class ATRError(Exception):
    pass


class ATRImage:
    def __init__(self, sector_size=128, sector_count=720, boot_sectors_128=True):
        if sector_size not in (128, 256):
            raise ATRError(f"unsupported sector size {sector_size}")
        self.sector_size = sector_size
        self.sector_count = sector_count
        self.boot_sectors_128 = boot_sectors_128 if sector_size == 256 else False
        self.data = bytearray(self._data_length())

    def _boot_run(self):
        if self.sector_size == 256 and self.boot_sectors_128:
            return (min(3, self.sector_count), 128)
        return (0, self.sector_size)

    def _data_length(self):
        n_short, short_size = self._boot_run()
        return n_short * short_size + (self.sector_count - n_short) * self.sector_size

    def sector_offset(self, n):
        if n < 1 or n > self.sector_count:
            raise ATRError(f"sector {n} out of range 1..{self.sector_count}")
        n_short, short_size = self._boot_run()
        if n <= n_short:
            return (n - 1) * short_size
        return n_short * short_size + (n - 1 - n_short) * self.sector_size

    def sector_len(self, n):
        n_short, short_size = self._boot_run()
        return short_size if n <= n_short else self.sector_size

    def read_sector(self, n):
        off = self.sector_offset(n)
        return bytes(self.data[off:off + self.sector_len(n)])

    def write_sector(self, n, payload):
        slen = self.sector_len(n)
        if len(payload) > slen:
            raise ATRError(f"sector {n}: {len(payload)} > {slen}")
        off = self.sector_offset(n)
        self.data[off:off + slen] = bytes(payload) + bytes(slen - len(payload))

    @classmethod
    def load(cls, path):
        blob = open(path, "rb").read()
        if len(blob) < HEADER_SIZE:
            raise ATRError("file too small")
        magic, plo, ssz, phi = struct.unpack_from("<HHHB", blob, 0)
        if magic != ATR_MAGIC:
            raise ATRError(f"bad magic 0x{magic:04x}")
        data = blob[HEADER_SIZE:]
        data_len = ((phi << 16) | plo) * 16 or len(data)
        data = data[:data_len]
        boot128 = True
        if ssz == 128:
            count, boot128 = data_len // 128, False
        elif data_len >= 384 and (data_len - 384) % 256 == 0:
            count = 3 + (data_len - 384) // 256
        elif data_len % 256 == 0:
            count, boot128 = data_len // 256, False
        else:
            raise ATRError("odd 256-byte image length")
        img = cls(ssz, count, boot128)
        img.data[:len(data)] = data
        return img

    def save(self, path):
        pars = self._data_length() // 16
        head = struct.pack("<HHHB", ATR_MAGIC, pars & 0xFFFF, self.sector_size, (pars >> 16) & 0xFF)
        with open(path, "wb") as f:
            f.write(head + b"\x00" * (HEADER_SIZE - len(head)))
            f.write(self.data)

    def __repr__(self):
        return f"ATRImage({self.sector_size}b x {self.sector_count})"


class DirEntry:
    def __init__(self, index, raw):
        self.index = index
        self.flag = raw[0]
        self.count = raw[1] | (raw[2] << 8)
        self.start = raw[3] | (raw[4] << 8)
        self.name = raw[5:13].decode("latin-1").rstrip()
        self.ext = raw[13:16].decode("latin-1").rstrip()

    @property
    def in_use(self):
        if self.flag & 0x80:
            return False
        # $03 is DOS 2.5's mark for a file reaching sectors 720-1023: bit 6
        # clear so DOS 2.0 ignores it, bit 0 (open for write) so nothing
        # else does either.  Altirra's IsVisible() accepts exactly this.
        if (self.flag & 0x43) == 0x03:
            return True
        return bool(self.flag & 0x40) and not (self.flag & 0x01)

    @property
    def filename(self):
        return self.name + ("." + self.ext if self.ext else "")

    def pack(self):
        nm = self.name.ljust(8)[:8].encode("latin-1")
        ex = self.ext.ljust(3)[:3].encode("latin-1")
        return bytes([self.flag, self.count & 0xFF, self.count >> 8,
                      self.start & 0xFF, self.start >> 8]) + nm + ex


class Dos2:
    VTOC = 360
    VTOC2 = 1024           # DOS 2.5: the bitmap for sectors 48-1023
    DIR0 = 361
    DIR_SECTORS = 8
    SD_SECTORS = 720
    ED_SECTORS = 1040
    HIGH = 720             # DOS 2.5: sectors from here up are the enhanced half

    def __init__(self, img):
        if img.sector_size != 128:
            raise ATRError("Dos2 handles 128-byte-sector disks only")
        self.img = img
        self.data_bytes = 125  # per sector (last 3 bytes are the link)
        # DOS 2.5 semantics, decided the way Altirra decides them: by the
        # geometry alone.
        self.dos25 = img.sector_count == self.ED_SECTORS

    # -- directory ---------------------------------------------------------
    def entries(self):
        out = []
        for i in range(64):
            sec = self.DIR0 + i // 8
            raw = self.img.read_sector(sec)[(i % 8) * 16:(i % 8) * 16 + 16]
            out.append(DirEntry(i, raw))
        return out

    def _write_entry(self, e):
        sec = self.DIR0 + e.index // 8
        raw = bytearray(self.img.read_sector(sec))
        raw[(e.index % 8) * 16:(e.index % 8) * 16 + 16] = e.pack()
        self.img.write_sector(sec, raw)

    def find(self, filename):
        filename = filename.upper()
        for e in self.entries():
            if e.in_use and e.filename.upper() == filename:
                return e
        return None

    def list(self):
        return [e.filename for e in self.entries() if e.in_use]

    # -- file contents -----------------------------------------------------
    def read(self, filename):
        e = self.find(filename)
        if not e:
            raise ATRError(f"{filename}: not found")
        out = bytearray()
        sec = e.start
        for _ in range(e.count + 1):
            if sec == 0:
                break
            raw = self.img.read_sector(sec)
            used = raw[127] & 0x7F
            out += raw[:used]
            sec = ((raw[125] & 0x03) << 8) | raw[126]
        return bytes(out)

    # -- VTOC / allocation -------------------------------------------------
    # The bitmap is kept as one bit per sector from 0 up, MSB first, as in
    # the VTOC.  A DOS 2 VTOC holds 90 bytes of it (sectors 0-719); DOS 2.5
    # keeps the rest in VTOC2, whose 122 bytes are the bits for 48-1023 --
    # a copy of VTOC1's 48-719 followed by 720-1023 -- and whose bytes 122-123
    # count the free sectors of the upper half.  VTOC1's own count (bytes 3-4)
    # covers the lower half only.  Sector 720 is unusable on both.
    def _bitmap(self):
        vtoc = bytearray(self.img.read_sector(self.VTOC))
        bits = bytearray(vtoc[10:100])
        if self.dos25:
            bits += self.img.read_sector(self.VTOC2)[84:122]
        return vtoc, bits

    @staticmethod
    def _bit(bits, s):
        return (bits[s // 8] >> (7 - s % 8)) & 1

    @staticmethod
    def _clear(bits, s):
        bits[s // 8] &= ~(1 << (7 - s % 8)) & 0xFF

    def _count_free(self, bits, lo, hi):
        return sum(self._bit(bits, s) for s in range(lo, hi))

    def _write_bitmap(self, vtoc, bits):
        vtoc[10:100] = bits[:90]
        n = self._count_free(bits, 0, self.HIGH)
        vtoc[3], vtoc[4] = n & 0xFF, n >> 8
        self.img.write_sector(self.VTOC, vtoc)
        if self.dos25:
            v2 = bytearray(self.img.read_sector(self.VTOC2))
            v2[0:122] = bits[6:128]
            n = self._count_free(bits, self.HIGH, 1024)
            v2[122], v2[123] = n & 0xFF, n >> 8
            self.img.write_sector(self.VTOC2, v2)

    def _free_sectors(self, bits):
        free = []
        for s in range(1, len(bits) * 8):
            if s == self.VTOC or self.DIR0 <= s < self.DIR0 + self.DIR_SECTORS:
                continue
            if self._bit(bits, s):
                free.append(s)
        return free

    def free_count(self):
        """What the DOS should report: both halves on an enhanced disk."""
        _, bits = self._bitmap()
        return len(self._free_sectors(bits))

    def add_file(self, filename, data, above=0):
        """Write `data` as `filename`.  `above` prefers sectors numbered higher
        than it, wrapping to the low ones when they run out: the harness uses
        it to put a file in the half of an enhanced-density disk that a DOS 2.0
        cannot reach, so a run proves the DOS reads that half."""
        name, _, ext = filename.upper().partition(".")
        vtoc, bits = self._bitmap()
        free = self._free_sectors(bits)
        free = [s for s in free if s > above] + [s for s in free if s <= above]
        nsec = max(1, (len(data) + self.data_bytes - 1) // self.data_bytes)
        if nsec > len(free):
            raise ATRError(f"not enough free sectors ({nsec} > {len(free)})")
        ent = None
        for e in self.entries():
            if not e.in_use and not (e.flag & 0x80 and e.count):
                ent = e
                break
            if e.flag == 0:
                ent = e
                break
        if ent is None:
            raise ATRError("directory full")
        secs = free[:nsec]
        fileno = ent.index
        for i, s in enumerate(secs):
            chunk = data[i * self.data_bytes:(i + 1) * self.data_bytes]
            nxt = secs[i + 1] if i + 1 < nsec else 0
            raw = bytearray(128)
            raw[:len(chunk)] = chunk
            raw[125] = (fileno << 2) | ((nxt >> 8) & 0x03)
            raw[126] = nxt & 0xFF
            raw[127] = len(chunk)
            self.img.write_sector(s, raw)
            self._clear(bits, s)
        self._write_bitmap(vtoc, bits)
        # in use, DOS 2 -- or DOS 2.5's mark for a file in the upper half
        ent.flag = 0x03 if self.dos25 and max(secs) >= self.HIGH else 0x42
        ent.count = nsec
        ent.start = secs[0]
        ent.name, ent.ext = name[:8], ext[:3]
        self._write_entry(ent)
        return ent


def enhance(img):
    """A DOS 2.5 enhanced-density copy of a single-density DOS 2 disk: the
    same boot sectors, DOS.SYS, directory and files, sectors 721-1023 free,
    720 unusable, and VTOC2 written the way DOS 2.5's own formatter leaves
    it.  Whether the DOS on the disk can USE the upper half is for the
    emulator to say -- that is what mkdisk's --high is for."""
    if img.sector_size != 128 or img.sector_count != Dos2.SD_SECTORS:
        raise ATRError(f"enhance: expected a 720 x 128 disk, got {img!r}")
    out = ATRImage(128, Dos2.ED_SECTORS)
    out.data[:len(img.data)] = img.data
    fs = Dos2(out)
    vtoc = bytearray(out.read_sector(Dos2.VTOC))
    if vtoc[0] != 2:
        raise ATRError(f"enhance: VTOC signature {vtoc[0]} is not DOS 2's")
    bits = bytearray(vtoc[10:100]) + bytearray(b"\xff" * 38)
    Dos2._clear(bits, Dos2.HIGH)
    vtoc[100:128] = bytes(28)
    out.write_sector(Dos2.VTOC2, bytes(128))
    fs._write_bitmap(vtoc, bits)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="inspect / modify DOS 2 .atr disks")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("ls"); p.add_argument("atr")
    p = sub.add_parser("cat"); p.add_argument("atr"); p.add_argument("name")
    p = sub.add_parser("add"); p.add_argument("atr"); p.add_argument("name"); p.add_argument("file")
    p.add_argument("-o", "--out")
    a = ap.parse_args(argv)
    img = ATRImage.load(a.atr)
    fs = Dos2(img)
    if a.cmd == "ls":
        for f in fs.list():
            e = fs.find(f)
            print(f"{f:14s} start={e.start:4d} sectors={e.count}")
    elif a.cmd == "cat":
        sys.stdout.buffer.write(fs.read(a.name))
    elif a.cmd == "add":
        e = fs.add_file(a.name, open(a.file, "rb").read())
        img.save(a.out or a.atr)
        print(f"added {a.name}: {e.count} sectors from {e.start} -> {a.out or a.atr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
