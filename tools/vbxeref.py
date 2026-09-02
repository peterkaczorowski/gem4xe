#!/usr/bin/env python3
"""Host-side reference model of the VBXE HR surface and blitter.

This is the specification the on-target code must match. It exists so a test
can say "these exact pixels" rather than "it looked right in a screenshot" --
the pattern that made vbxetxtadv's conformance suite worth having, and the
thing Phase 2's VDI opcode tests will be built on.

Models only what gem4xe uses: a 4bpp HR overlay (2 pixels per byte, high
nibble = LEFT pixel) and the blitter's BCB semantics.
"""

SCR_W, SCR_H = 640, 240
STRIDE = SCR_W // 2                       # 320 bytes per row

# Where the overlay lands in an Altirra screenshot, measured, not assumed:
# a 672x240 shot with the 640-pixel overlay starting at column 16, 1:1.
SHOT_X0, SHOT_Y0, SHOT_W, SHOT_H = 16, 0, 640, 240


def dac(v):
    """Expand a written colour byte the way the VBXE DAC does.

    Only the top 7 bits are significant and the bit that comes back out is a
    copy of the top one, so $96 reads back as $97 and $7F as $7E.  Confirmed
    against a real screenshot on every one of 16 test colours.
    """
    return (v & 0xFE) | (v >> 7)


class Surface:
    """A VBXE VRAM region addressed as bytes; 4bpp pixels are packed 2/byte."""

    def __init__(self, size=0x80000):
        self.mem = bytearray(size)

    # -- blitter ---------------------------------------------------------
    def blit(self, src, sstride, dst, dstride, nbytes, rows,
             and_mask=0xFF, xor_mask=0x00, mode=0, sxstep=1, dxstep=1):
        """One BCB.  c = (source & and_mask) ^ xor_mask, then written per mode.

        and_mask == 0 is the constant-source fill: no source byte is read at
        all (and on hardware it costs 1 cycle/byte instead of 2).
        """
        if not 1 <= nbytes <= 512:
            raise ValueError(f"blit width {nbytes} outside the 9-bit field (1..512)")
        if not 1 <= rows <= 256:
            raise ValueError(f"blit height {rows} outside the 8-bit field (1..256)")
        for r in range(rows):
            sp = src + r * sstride
            dp = dst + r * dstride
            for i in range(nbytes):
                c = (0 if and_mask == 0 else self.mem[sp]) & and_mask
                c ^= xor_mask
                if mode == 0:
                    self.mem[dp] = c
                elif c:
                    d = self.mem[dp]
                    if mode == 1:
                        self.mem[dp] = c
                    elif mode == 2:
                        self.mem[dp] = (c + d) & 0xFF
                    elif mode == 3:
                        self.mem[dp] = c | d
                    elif mode == 4:
                        self.mem[dp] = c & d
                    elif mode == 5:
                        self.mem[dp] = c ^ d
                    elif mode == 6:                   # per-nibble stencil
                        hi, lo = c & 0xF0, c & 0x0F
                        self.mem[dp] = ((hi if hi else d & 0xF0) |
                                        (lo if lo else d & 0x0F))
                else:
                    # c == 0.  Every mode skips the write EXCEPT AND, which
                    # writes 0 -- so `x & 0` still clears, as it should.
                    # (vbxe.cpp BlitRow: the T_Mode == 4 branch of the else.)
                    if mode == 4:
                        self.mem[dp] = 0
                sp += sxstep
                dp += dxstep

    def fill(self, dst, stride, nbytes, rows, value):
        self.blit(0, 0, dst, stride, nbytes, rows, and_mask=0x00, xor_mask=value)

    def copy(self, src, sstride, dst, dstride, nbytes, rows):
        self.blit(src, sstride, dst, dstride, nbytes, rows, and_mask=0xFF)

    def move(self, src, sstride, dst, dstride, nbytes, rows):
        """An overlap-safe copy (vbxe.c blit_move): backwards, from the
        last byte of the last row, when the destination is higher."""
        if dst <= src:
            self.copy(src, sstride, dst, dstride, nbytes, rows)
            return
        self.blit(src + (rows - 1) * sstride + nbytes - 1, -sstride,
                  dst + (rows - 1) * dstride + nbytes - 1, -dstride,
                  nbytes, rows, and_mask=0xFF, sxstep=-1, dxstep=-1)

    def rmw(self, dst, stride, nbytes, rows, value, mode):
        """Constant-source read-modify-write: the 4bpp edge primitive."""
        self.blit(0, 0, dst, stride, nbytes, rows,
                  and_mask=0x00, xor_mask=value, mode=mode)

    # -- rendering -------------------------------------------------------
    def pixel(self, base, x, y):
        b = self.mem[base + y * STRIDE + x // 2]
        return (b >> 4) if (x & 1) == 0 else (b & 0x0F)

    def to_rgb(self, base, palette):
        """Return [[(r,g,b), ...] ...] as the DAC would drive it."""
        pal = [tuple(dac(c) for c in palette[i * 3:i * 3 + 3]) for i in range(16)]
        return [[pal[self.pixel(base, x, y)] for x in range(SCR_W)]
                for y in range(SCR_H)]


def compare_to_shot(expected_rgb, shot_path, max_report=8):
    """Compare the reference image against an Altirra screenshot.

    Returns (n_mismatches, [(x, y, expected, got), ...]).
    """
    from PIL import Image
    im = Image.open(shot_path).convert("RGB")
    px = im.load()
    bad, shown = 0, []
    for y in range(SCR_H):
        for x in range(SCR_W):
            got = px[SHOT_X0 + x, SHOT_Y0 + y]
            want = expected_rgb[y][x]
            if got != want:
                bad += 1
                if len(shown) < max_report:
                    shown.append((x, y, want, got))
    return bad, shown
