#!/usr/bin/env python3
"""Atari .atr disk images, the DOS 2.0/2.5 filesystem and SpartaDOS's (SDFS).  Used by the
test harness: it writes the runtime .xex onto a DOS floppy to prove the loader path, and it
predicts what the file selector will list from the same image it booted.

ATRImage is adapted from /home/jfergus/dev/a8-u4r/tools/atrlib.py.  Dos2 implements the standard
DOS 2 VTOC (sector 360), 8-entry directory (sectors 361-368) and the 3-byte per-sector file
links used by SD/ED disks.  It does NOT handle MyDOS "big" disks or double density.

Enhanced density is DOS 2.5's: 1040 sectors, the bitmap for sectors 720-1023 kept in a second
VTOC at sector 1024, and a file that uses any of them flagged $03 in the directory so that a
DOS 2.0 -- which cannot reach those sectors -- skips the entry instead of reading garbage.
The layout is taken from Altirra's disk explorer (src/ATIO/source/diskfsdos2.cpp: InitNew,
Flush, IsVisible, WriteFile), which reads and writes real DOS 2.5 disks, not from memory.
enhance() turns a single-density DOS 2 disk into one.

Sdfs is SpartaDOS's file system, the one SpartaDOS 3.2 and SpartaDOS X format and boot from:
a superblock in sector 1, a free-sector bitmap, and every file -- directories included --
reached through a chain of sector-map sectors (next, previous, then data sector numbers)
so that files are random-access and 8 MB long, with 23-byte directory entries that carry a
size in bytes and a timestamp.  Again the layout is Altirra's (diskfssdx2.cpp: InitNew,
WriteEntry, SeekFile, GetFileInfo; diskfssdx2util.cpp for the blank boot sectors), which
reads and writes the real thing, checked against a SpartaDOS 3.2g disk's own sectors.
Sdfs.format() lays out a blank disk; Sdfs.boot_from() copies a SpartaDOS 3.2 disk's boot
code and DOS file onto it so it boots.  open_fs() picks the class by the disk's signature.
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

    @property
    def nameable(self):
        """Whether the DOS could be handed this name: letters, digits, _
        and @, a letter first -- the rule src/sys/dos.c lists by.  A disk
        can carry other entries (the dashed dividers a sector editor
        makes, in use and empty), and no DOS 2 can open, delete or rename
        one, so they are not files."""
        ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_@")
        return (bool(self.name) and self.name[0].isalpha()
                and all(c in ok for c in self.name + self.ext))

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


# ---------------------------------------------------------------------------
# SpartaDOS file system
# ---------------------------------------------------------------------------

class SdfsEntry:
    """One 23-byte directory entry.  Entry 0 of every directory is the
    header: status $28, the PARENT's sector-map sector, the directory's
    length in bytes (header included), and the directory's own name --
    'MAIN' for the root."""
    SIZE = 23
    IN_USE = 0x08
    DELETED = 0x10
    SUBDIR = 0x20
    OPEN_W = 0x80

    def __init__(self, index, raw):
        self.index = index
        self.status = raw[0]
        self.map = raw[1] | (raw[2] << 8)
        self.size = raw[3] | (raw[4] << 8) | (raw[5] << 16)
        self.name = raw[6:14].decode("latin-1").rstrip()
        self.ext = raw[14:17].decode("latin-1").rstrip()
        self.day, self.month, self.year = raw[17], raw[18], raw[19]
        self.hour, self.minute, self.second = raw[20], raw[21], raw[22]

    @property
    def in_use(self):
        # Altirra's rule: in use means bit 3 set; a deleted entry has bit 3
        # clear and bit 4 set; a zero status ends the directory.
        return bool(self.status & self.IN_USE)

    @property
    def is_dir(self):
        return bool(self.status & self.SUBDIR)

    @property
    def filename(self):
        return self.name + ("." + self.ext if self.ext else "")

    @property
    def stamp(self):
        # SDX Programming Guide 4.50, the `date` variable: 0-79 are
        # 2000-2079, 80-99 are 1980-1999 (src/sys/gemdos.c agrees)
        y = self.year + (1900 if self.year >= 80 else 2000)
        return f"{y:04d}-{self.month:02d}-{self.day:02d} {self.hour:02d}:{self.minute:02d}:{self.second:02d}"

    def pack(self):
        nm = self.name.ljust(8)[:8].encode("latin-1")
        ex = self.ext.ljust(3)[:3].encode("latin-1")
        return bytes([self.status, self.map & 0xFF, self.map >> 8,
                      self.size & 0xFF, (self.size >> 8) & 0xFF, self.size >> 16]) + nm + ex + \
            bytes([self.day, self.month, self.year, self.hour, self.minute, self.second])

    @classmethod
    def blank(cls, index=0):
        return cls(index, bytes(cls.SIZE))


# A fixed timestamp keeps the images reproducible byte for byte; the harness
# compares what the selector lists against what the image holds.
SDFS_STAMP = (4, 9, 26, 12, 0, 0)   # day, month, year (2026), h, m, s


class Sdfs:
    """SpartaDOS file system on an ATRImage.  Sector numbers are 1-based
    like the disk's own; 'path' arguments are 'DIR>SUB>NAME.EXT' or
    'DIR/SUB/NAME.EXT', case-insensitive, an optional leading separator."""
    SUPER = 1
    ENTRY = SdfsEntry.SIZE

    def __init__(self, img):
        self.img = img
        self.secsize = img.sector_size
        self.secshift = self.secsize.bit_length() - 1
        self.per_map = (self.secsize - 4) // 2          # data refs per map page
        sb = img.read_sector(self.SUPER)
        if sb[7] not in (0x80, 0x40):
            raise ATRError("not a SpartaDOS disk (sector 1 byte 7 is not $80/$40)")
        self.root_map = sb[9] | (sb[10] << 8)
        self.total = sb[11] | (sb[12] << 8)
        self.bm_count = sb[15]
        self.bm_start = sb[16] | (sb[17] << 8)
        self.volname = sb[22:30].decode("latin-1").rstrip()
        self.version = sb[32]

    # -- superblock --------------------------------------------------------
    def _superblock(self):
        return bytearray(self.img.read_sector(self.SUPER))

    def _write_superblock(self, sb):
        self.img.write_sector(self.SUPER, sb)

    @property
    def boot_file_map(self):
        sb = self._superblock()
        return sb[40] | (sb[41] << 8)

    # -- bitmap: bit SET means FREE, sector 0's bit unused --------------------
    def _bitmap(self):
        bits = bytearray()
        for i in range(self.bm_count):
            bits += self.img.read_sector(self.bm_start + i)
        return bits

    def _write_bitmap(self, bits):
        for i in range(self.bm_count):
            self.img.write_sector(self.bm_start + i, bits[i * self.secsize:(i + 1) * self.secsize])
        n = sum(self._is_free(bits, s) for s in range(1, self.total + 1))
        sb = self._superblock()
        sb[13], sb[14] = n & 0xFF, n >> 8
        # the next-free hints: any free sector will do, the DOS searches on
        # from it.  Altirra never touches them; SpartaDOS keeps them current.
        first = next((s for s in range(1, self.total + 1) if self._is_free(bits, s)), 0)
        sb[18], sb[19] = first & 0xFF, first >> 8
        sb[20], sb[21] = first & 0xFF, first >> 8
        self._write_superblock(sb)

    @staticmethod
    def _is_free(bits, s):
        return bool(bits[s >> 3] & (0x80 >> (s & 7)))

    @staticmethod
    def _mark_used(bits, s):
        bits[s >> 3] &= ~(0x80 >> (s & 7)) & 0xFF

    @staticmethod
    def _mark_free(bits, s):
        bits[s >> 3] |= 0x80 >> (s & 7)

    def free_count(self):
        bits = self._bitmap()
        return sum(self._is_free(bits, s) for s in range(1, self.total + 1))

    def _alloc(self, bits, n, start=1):
        """Take the first n free sectors at or above `start`; marks them used
        in `bits` (caller writes the bitmap back)."""
        out = []
        s = max(1, start)
        while len(out) < n and s <= self.total:
            if self._is_free(bits, s):
                out.append(s)
                self._mark_used(bits, s)
            s += 1
        if len(out) < n:
            raise ATRError(f"disk full: wanted {n} sectors, found {len(out)}")
        return out

    # -- sector maps ---------------------------------------------------------
    def _map_pages(self, first):
        """The sector-map sectors of a file, in chain order."""
        pages, s, seen = [], first, set()
        while s and s not in seen:
            seen.add(s)
            pages.append(s)
            raw = self.img.read_sector(s)
            s = raw[0] | (raw[1] << 8)
        return pages

    def _data_sectors(self, first):
        """Every data-sector reference of a file, in order, zeros included
        (a zero is a sparse block in a file, the end of a directory)."""
        out = []
        for p in self._map_pages(first):
            raw = self.img.read_sector(p)
            for i in range(self.per_map):
                out.append(raw[4 + 2 * i] | (raw[5 + 2 * i] << 8))
        return out

    def _read_chain(self, first, size):
        out = bytearray()
        for s in self._data_sectors(first):
            if len(out) >= size:
                break
            out += self.img.read_sector(s) if s else bytes(self.secsize)
        return bytes(out[:size])

    def _write_maps(self, pages, data_secs):
        """Write the chain `pages` of map sectors referencing `data_secs`."""
        for i, p in enumerate(pages):
            raw = bytearray(self.secsize)
            nxt = pages[i + 1] if i + 1 < len(pages) else 0
            prv = pages[i - 1] if i > 0 else 0
            raw[0], raw[1] = nxt & 0xFF, nxt >> 8
            raw[2], raw[3] = prv & 0xFF, prv >> 8
            for j, s in enumerate(data_secs[i * self.per_map:(i + 1) * self.per_map]):
                raw[4 + 2 * j], raw[5 + 2 * j] = s & 0xFF, s >> 8
            self.img.write_sector(p, raw)

    def _write_new(self, bits, data):
        """Lay a new file down: for each map page, the map sector then its
        data sectors -- the order SpartaDOS itself leaves on a disk.  Returns
        the first map sector."""
        nsec = (len(data) + self.secsize - 1) >> self.secshift
        pages, secs = [], []
        for start in range(0, max(nsec, 1), self.per_map):
            pages.append(self._alloc(bits, 1)[0])
            secs += self._alloc(bits, min(self.per_map, nsec - start))
        for i, s in enumerate(secs):
            self.img.write_sector(s, data[i * self.secsize:(i + 1) * self.secsize])
        self._write_maps(pages, secs)
        return pages[0]

    def _rewrite(self, bits, first, data):
        """Rewrite an existing file in place, growing its chain as needed:
        the first map sector never changes, so entries pointing at it stay
        valid.  Used for directories."""
        pages = self._map_pages(first)
        secs = [s for s in self._data_sectors(first) if s]
        nsec = (len(data) + self.secsize - 1) >> self.secshift
        if nsec > len(secs):
            secs += self._alloc(bits, nsec - len(secs))
        need = (len(secs) + self.per_map - 1) // self.per_map
        if need > len(pages):
            pages += self._alloc(bits, need - len(pages))
        for i, s in enumerate(secs):
            self.img.write_sector(s, data[i * self.secsize:(i + 1) * self.secsize])
        self._write_maps(pages, secs)

    def _free_chain(self, bits, first):
        for p in self._map_pages(first):
            for s in self._data_sectors(p)[:self.per_map]:
                if s:
                    self._mark_free(bits, s)
            self._mark_free(bits, p)

    # -- directories ---------------------------------------------------------
    @staticmethod
    def split(path):
        parts = [p for p in path.replace("/", ">").split(">") if p]
        return [p.upper() for p in parts]

    def _read_dir(self, dmap):
        """(header, entries) of the directory whose map sector is `dmap`."""
        head = SdfsEntry(0, self.img.read_sector(self._data_sectors(dmap)[0])[:self.ENTRY])
        raw = self._read_chain(dmap, head.size)
        ents = []
        for i in range(1, len(raw) // self.ENTRY):
            e = SdfsEntry(i, raw[i * self.ENTRY:(i + 1) * self.ENTRY])
            if e.status == 0:
                break
            ents.append(e)
        return head, ents

    def _lookup(self, dmap, name):
        name = name.upper()
        for e in self._read_dir(dmap)[1]:
            if e.in_use and e.filename == name:
                return e
        return None

    def _dir_map(self, parts):
        """The map sector of the directory named by `parts` (a list)."""
        dmap = self.root_map
        for p in parts:
            e = self._lookup(dmap, p)
            if e is None or not e.is_dir:
                raise ATRError(f"{'>'.join(parts)}: no such directory")
            dmap = e.map
        return dmap

    def entries(self, path=""):
        return [e for e in self._read_dir(self._dir_map(self.split(path)))[1] if e.in_use]

    def list(self, path=""):
        return [e.filename for e in self.entries(path)]

    def find(self, path):
        parts = self.split(path)
        if not parts:
            return None
        return self._lookup(self._dir_map(parts[:-1]), parts[-1])

    def read(self, path):
        e = self.find(path)
        if e is None:
            raise ATRError(f"{path}: not found")
        return self._read_chain(e.map, e.size)

    # -- writing -------------------------------------------------------------
    @staticmethod
    def check_name(filename):
        """SpartaDOS names: up to 8 of [A-Za-z0-9_], then optionally '.' and
        up to 3 alphanumerics -- Altirra's IsValidFileName, uppercased."""
        name, dot, ext = filename.partition(".")
        ok = lambda s, n: 0 < len(s) <= n and all(c.isalnum() or c == "_" for c in s)
        if not ok(name, 8) or (dot and not ok(ext, 3)) or (not dot and ext):
            raise ATRError(f"{filename}: not a SpartaDOS file name")
        return name.upper(), ext.upper()

    def _put_entry(self, bits, dmap, ent):
        """Store `ent` in directory `dmap`: the first free slot, else appended
        with the header's length bumped.  Returns the entry's index."""
        head, ents = self._read_dir(dmap)
        raw = bytearray(self._read_chain(dmap, head.size))
        slot = None
        for i in range(1, len(raw) // self.ENTRY):
            st = raw[i * self.ENTRY]
            if not st & SdfsEntry.IN_USE:
                slot = i
                break
            # (a zero status inside the length is a free slot too)
        if slot is None:
            slot = len(raw) // self.ENTRY
            raw += bytes(self.ENTRY)
            head.size = len(raw)
            raw[:self.ENTRY] = head.pack()
        ent.index = slot
        raw[slot * self.ENTRY:(slot + 1) * self.ENTRY] = ent.pack()
        self._rewrite(bits, dmap, raw)
        return slot

    def _stamp(self, ent, stamp):
        ent.day, ent.month, ent.year, ent.hour, ent.minute, ent.second = stamp

    def add_file(self, path, data, stamp=SDFS_STAMP):
        parts = self.split(path)
        name, ext = self.check_name(parts[-1])
        dmap = self._dir_map(parts[:-1])
        if self._lookup(dmap, parts[-1]):
            raise ATRError(f"{path}: exists")
        bits = self._bitmap()
        first = self._write_new(bits, data)
        e = SdfsEntry.blank()
        e.status, e.map, e.size, e.name, e.ext = SdfsEntry.IN_USE, first, len(data), name, ext
        self._stamp(e, stamp)
        self._put_entry(bits, dmap, e)
        self._write_bitmap(bits)
        return e

    def mkdir(self, path, stamp=SDFS_STAMP):
        parts = self.split(path)
        name, ext = self.check_name(parts[-1])
        dmap = self._dir_map(parts[:-1])
        if self._lookup(dmap, parts[-1]):
            raise ATRError(f"{path}: exists")
        # the new directory's header: status $28, the parent's map sector,
        # a length of one entry, and its own name
        head = SdfsEntry.blank()
        head.status = SdfsEntry.IN_USE | SdfsEntry.SUBDIR
        head.map, head.size, head.name, head.ext = dmap, self.ENTRY, name, ext
        bits = self._bitmap()
        first = self._write_new(bits, head.pack())
        e = SdfsEntry.blank()
        e.status, e.map, e.size, e.name, e.ext = head.status, first, self.ENTRY, name, ext
        self._stamp(e, stamp)
        self._put_entry(bits, dmap, e)
        self._write_bitmap(bits)
        return e

    def delete(self, path):
        parts = self.split(path)
        dmap = self._dir_map(parts[:-1])
        e = self._lookup(dmap, parts[-1])
        if e is None:
            raise ATRError(f"{path}: not found")
        if e.is_dir and self._read_dir(e.map)[1]:
            raise ATRError(f"{path}: directory not empty")
        bits = self._bitmap()
        self._free_chain(bits, e.map)
        e.status = (e.status & ~SdfsEntry.IN_USE) | SdfsEntry.DELETED
        head, _ = self._read_dir(dmap)
        raw = bytearray(self._read_chain(dmap, head.size))
        raw[e.index * self.ENTRY:(e.index + 1) * self.ENTRY] = e.pack()
        self._rewrite(bits, dmap, raw)
        self._write_bitmap(bits)

    # -- formatting ----------------------------------------------------------
    # Altirra's blank disk (diskfssdx2util.cpp: kATSDFSBootSector0/1), a
    # non-bootable stub: sector 1's code is one RTS, sectors 2-3 flag the
    # boot as failed and return.
    BOOT0 = (bytes([0x00, 0x03, 0x00, 0x30, 0x40, 0x07, 0x4C, 0x80, 0x30])   # +0..+8
             + bytes(13)                                                    # +9..+21 (filled in)
             + b"A       "                                                  # +22 volume name
             + bytes([0x01, 0x80, 0x21, 0x80, 0x00, 0x3E, 0x00, 0x01])      # +30 tracks, size, v2.1, size, refs/map, phys/log
             + bytes([0x00, 0xA5, 0x00, 0x00])                              # +38 sequence, random ID, boot file map
             + bytes(22) + bytes([0x60]))                                   # +42.., +64 RTS
    BOOT1 = bytes([0xA9, 0x60, 0x8D, 0x40, 0x07, 0x38, 0x60])

    @classmethod
    def format(cls, img, volname="GEM4XE"):
        """Lay a blank SDFS on `img`, the way Altirra's disk explorer does:
        three boot sectors, the bitmap from sector 4, then the root
        directory's map sector and its first data sector."""
        secsize = img.sector_size
        total = img.sector_count
        shift = secsize.bit_length() - 1
        bm_count = (total >> (shift + 3)) + 1
        bm_start = 4
        root_map = bm_start + bm_count
        root_data = root_map + 1
        sb = bytearray(cls.BOOT0) + bytes(secsize - len(cls.BOOT0))
        sb[9], sb[10] = root_map & 0xFF, root_map >> 8
        sb[11], sb[12] = total & 0xFF, total >> 8
        sb[15] = bm_count
        sb[16], sb[17] = bm_start & 0xFF, bm_start >> 8
        vol = "".join(c for c in volname.upper().partition(".")[0] if c.isalnum())[:8] or "NEWDISK"
        sb[22:30] = vol.ljust(8).encode("latin-1")
        sb[30] = 1
        sb[31] = secsize & 0xFF          # $80 = 128, 0 = 256
        sb[33], sb[34] = secsize & 0xFF, secsize >> 8
        sb[35], sb[36] = (secsize - 4) // 2, 0
        img.write_sector(1, sb[:img.sector_len(1)])
        img.write_sector(2, cls.BOOT1)
        img.write_sector(3, bytes())
        for i in range(bm_count):
            img.write_sector(bm_start + i, bytes())
        fs = cls(img)
        bits = fs._bitmap()
        for s in range(root_data + 1, total + 1):
            fs._mark_free(bits, s)
        # the root: one map page pointing at one data sector holding the header
        fs._write_maps([root_map], [root_data])
        head = SdfsEntry.blank()
        head.status, head.size, head.name = SdfsEntry.IN_USE | SdfsEntry.SUBDIR, cls.ENTRY, "MAIN"
        img.write_sector(root_data, head.pack())
        fs._write_bitmap(bits)
        return fs

    def boot_from(self, src):
        """Make this disk boot the SpartaDOS that `src` (an Sdfs) boots: copy
        its boot sectors over ours, keeping our own volume fields, and copy
        its DOS file -- the one its superblock names by map sector -- as a
        file here, pointed at from our superblock the same way."""
        ours = self._superblock()
        src_sb = src._superblock()
        boot_count = src_sb[1]
        for s in range(1, boot_count + 1):
            self.img.write_sector(s, src.img.read_sector(s))
        sb = self._superblock()
        # the file system's own fields stay ours; everything else -- the
        # boot code, its load/init addresses, the DOS's version word -- is
        # the donor's, verbatim
        sb[9:22] = ours[9:22]
        sb[22:31] = ours[22:31]
        sb[38:40] = ours[38:40]
        self._write_superblock(sb)
        dosmap = src_sb[40] | (src_sb[41] << 8)
        dos = next((e for e in src.entries() if e.map == dosmap), None)
        if dos is None:
            raise ATRError("boot_from: the source names no DOS file")
        e = self.add_file(dos.filename, src.read(dos.filename),
                          (dos.day, dos.month, dos.year, dos.hour, dos.minute, dos.second))
        sb = self._superblock()
        sb[40], sb[41] = e.map & 0xFF, e.map >> 8
        self._write_superblock(sb)
        return e


def open_fs(img):
    """The file system on `img`, by signature: SpartaDOS's byte 7 of sector 1
    ($80, or $40 for 512-byte sectors), else DOS 2's VTOC type byte."""
    if img.read_sector(1)[7] in (0x80, 0x40):
        return Sdfs(img)
    if img.sector_size == 128 and img.sector_count >= Dos2.VTOC and img.read_sector(Dos2.VTOC)[0] == 2:
        return Dos2(img)
    raise ATRError("neither a DOS 2 nor a SpartaDOS disk")


def main(argv=None):
    ap = argparse.ArgumentParser(description="inspect / modify DOS 2 and SpartaDOS .atr disks")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("ls"); p.add_argument("atr"); p.add_argument("path", nargs="?", default="")
    p = sub.add_parser("cat"); p.add_argument("atr"); p.add_argument("name")
    p = sub.add_parser("add"); p.add_argument("atr"); p.add_argument("name"); p.add_argument("file")
    p.add_argument("-o", "--out")
    p = sub.add_parser("mkdir"); p.add_argument("atr"); p.add_argument("path"); p.add_argument("-o", "--out")
    p = sub.add_parser("rm"); p.add_argument("atr"); p.add_argument("path"); p.add_argument("-o", "--out")
    p = sub.add_parser("format", help="a blank SpartaDOS disk")
    p.add_argument("atr"); p.add_argument("--sectors", type=int, default=720)
    p.add_argument("--size", type=int, default=128); p.add_argument("--name", default="GEM4XE")
    p.add_argument("--boot-from", help="a SpartaDOS 3.2 disk whose boot code and DOS file to copy")
    a = ap.parse_args(argv)
    if a.cmd == "format":
        img = ATRImage(a.size, a.sectors)
        fs = Sdfs.format(img, a.name)
        if a.boot_from:
            fs.boot_from(Sdfs(ATRImage.load(a.boot_from)))
        img.save(a.atr)
        print(f"{a.atr}: {a.sectors} x {a.size}, {fs.free_count()} free -> {a.atr}")
        return 0
    img = ATRImage.load(a.atr)
    fs = open_fs(img)
    if a.cmd == "ls":
        if isinstance(fs, Dos2):
            for f in fs.list():
                e = fs.find(f)
                print(f"{f:14s} start={e.start:4d} sectors={e.count}")
        else:
            for e in fs.entries(a.path):
                kind = "<DIR>" if e.is_dir else f"{e.size:6d}"
                print(f"{e.filename:14s} {kind:>6s} map={e.map:4d} {e.stamp}")
            print(f"{fs.free_count()} free sectors")
    elif a.cmd == "cat":
        sys.stdout.buffer.write(fs.read(a.name))
    elif a.cmd == "add":
        e = fs.add_file(a.name, open(a.file, "rb").read())
        img.save(a.out or a.atr)
        where = f"{e.count} sectors from {e.start}" if isinstance(fs, Dos2) else f"{e.size} bytes, map {e.map}"
        print(f"added {a.name}: {where} -> {a.out or a.atr}")
    elif a.cmd == "mkdir":
        e = fs.mkdir(a.path)
        img.save(a.out or a.atr)
        print(f"mkdir {a.path}: map {e.map} -> {a.out or a.atr}")
    elif a.cmd == "rm":
        fs.delete(a.path)
        img.save(a.out or a.atr)
        print(f"removed {a.path} -> {a.out or a.atr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
