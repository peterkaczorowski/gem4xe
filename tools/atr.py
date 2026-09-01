#!/usr/bin/env python3
"""Atari .atr disk images and the DOS 2.0/2.5 filesystem (single/enhanced density, 128-byte
sectors).  Used by the game extractors (they read DOS 2 files off the original disks) and by the
test harness (it writes a runtime .xex onto a DOS floppy to prove the loader path).

ATRImage is adapted from /home/jfergus/dev/a8-u4r/tools/atrlib.py.  Dos2 implements the standard
DOS 2 VTOC (sector 360), 8-entry directory (sectors 361-368) and the 3-byte per-sector file
links used by SD/ED disks.  It does NOT handle MyDOS "big" disks or double density.
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
        return bool(self.flag & 0x40) and not (self.flag & 0x80)

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
    DIR0 = 361
    DIR_SECTORS = 8

    def __init__(self, img):
        if img.sector_size != 128:
            raise ATRError("Dos2 handles 128-byte-sector disks only")
        self.img = img
        self.data_bytes = 125  # per sector (last 3 bytes are the link)

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
    def _vtoc(self):
        return bytearray(self.img.read_sector(self.VTOC))

    def _free_sectors(self, vtoc):
        # bitmap starts at byte 10, bit for sector s = byte 10 + s//8, MSB = lowest sector
        free = []
        for s in range(1, self.img.sector_count + 1):
            if s in (self.VTOC,) or (self.DIR0 <= s < self.DIR0 + self.DIR_SECTORS):
                continue
            byte = 10 + s // 8
            if byte < len(vtoc) and (vtoc[byte] >> (7 - (s % 8))) & 1:
                free.append(s)
        return free

    def _alloc(self, vtoc, s):
        vtoc[10 + s // 8] &= ~(1 << (7 - (s % 8))) & 0xFF

    def add_file(self, filename, data):
        name, _, ext = filename.upper().partition(".")
        vtoc = self._vtoc()
        free = self._free_sectors(vtoc)
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
            self._alloc(vtoc, s)
        free_count = (vtoc[3] | (vtoc[4] << 8)) - nsec
        vtoc[3], vtoc[4] = free_count & 0xFF, free_count >> 8
        self.img.write_sector(self.VTOC, vtoc)
        ent.flag = 0x42                # in use, DOS 2
        ent.count = nsec - 1           # DOS 2 stores sectors-1... actually count = number of sectors
        ent.count = nsec
        ent.start = secs[0]
        ent.name, ent.ext = name[:8], ext[:3]
        self._write_entry(ent)
        return ent


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
