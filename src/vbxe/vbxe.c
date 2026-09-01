/* vbxe.c -- VBXE surface for gem4xe: detection, MEMAC A windowing, palette,
 * XDL and the blitter.
 *
 * Written in C rather than assembly on purpose.  Everything here is either a
 * handful of register pokes or a memory copy through a window; the hot path in
 * a VDI is the BLITTER, not the CPU, so there is nothing to hand-optimise yet.
 * Measure before moving any of this to as65816.
 */
#include "vbxe.h"

uint16_t vbxe_base = 0;

#define REG(off)  (*(volatile uint8_t *)(vbxe_base + (off)))
#define WIN       ((volatile uint8_t *)MEMAC_WIN_ADDR)

void vbxe_reg(uint8_t off, uint8_t val) { REG(off) = val; }
uint8_t vbxe_reg_read(uint8_t off)      { return REG(off); }

/* ---------------------------------------------------------------------- */
/* Detection                                                              */
/* ---------------------------------------------------------------------- */

/* Probe $D600 then $D700.  The base is not fixed -- U1MB selects it through
 * UAUX ($D381) D5:D4 and can disable VBXE entirely -- so it must be found,
 * never assumed.
 *
 * The test is (CORE_REVISION & $0F) == 0 for "full FX core" and the high
 * nibble for the major version.  Never compare the whole byte: Altirra's
 * manual says FX 1.26 reads $11, which contradicts its own bit table and is an
 * erratum -- real hardware and the emulator both read $10.
 */
uint8_t vbxe_detect(void)
{
    uint16_t bases[2];
    uint8_t i;

    bases[0] = 0xD600;
    bases[1] = 0xD700;

    for (i = 0; i < 2; i++) {
        uint8_t core, minor, bcd;
        vbxe_base = bases[i];
        core  = REG(FX_CORE_REVISION);
        minor = REG(FX_MINOR_REVISION);
        if (core == 0xFF || (core & 0x0F) != 0)
            continue;                       /* absent, or the GTIA-only core */
        bcd = (uint8_t)(((minor >> 4) & 7) * 10 + (minor & 0x0F));
        if ((core >> 4) == 1 && bcd >= 20)
            return 1;
    }
    vbxe_base = 0;
    return 0;
}

uint8_t vbxe_major(void)     { return (uint8_t)(REG(FX_CORE_REVISION) >> 4); }
uint8_t vbxe_minor_bcd(void)
{
    uint8_t m = REG(FX_MINOR_REVISION);
    return (uint8_t)(((m >> 4) & 7) * 10 + (m & 0x0F));
}

/* ---------------------------------------------------------------------- */
/* MEMAC A -- the CPU's only view of VRAM                                 */
/* ---------------------------------------------------------------------- */

static uint8_t memac_bank = 0xFF;           /* shadow: BANK_SEL is write-only
                                               in spirit and re-poking it on
                                               every byte would be wasteful */

void vram_map(uint32_t addr)
{
    uint8_t bank = (uint8_t)(addr >> 12);
    if (bank != memac_bank) {
        memac_bank = bank;
        REG(FX_MEMAC_CONTROL)  = MEMAC_CTL_4K_8000;
        REG(FX_MEMAC_BANK_SEL) = (uint8_t)(0x80 | bank);
    }
}

/* Force the next vram_map() to re-program the window.  Needed after anything
 * that could have changed MEMAC state behind our back. */
static void memac_invalidate(void) { memac_bank = 0xFF; }

volatile uint8_t *vram_win(uint32_t addr)
{
    vram_map(addr);
    return WIN + (addr & 0x0FFF);
}

void vram_write(uint32_t addr, const uint8_t *src, uint16_t len)
{
    while (len) {
        uint16_t off = (uint16_t)(addr & 0x0FFF);
        uint16_t n   = (uint16_t)(MEMAC_WIN_SIZE - off);
        uint16_t i;
        if (n > len)
            n = len;
        vram_map(addr);
        for (i = 0; i < n; i++)
            WIN[off + i] = src[i];
        addr += n;
        src  += n;
        len  = (uint16_t)(len - n);
    }
}

void vram_fill(uint32_t addr, uint8_t val, uint16_t len)
{
    while (len) {
        uint16_t off = (uint16_t)(addr & 0x0FFF);
        uint16_t n   = (uint16_t)(MEMAC_WIN_SIZE - off);
        uint16_t i;
        if (n > len)
            n = len;
        vram_map(addr);
        for (i = 0; i < n; i++)
            WIN[off + i] = val;
        addr += n;
        len  = (uint16_t)(len - n);
    }
}

uint8_t vram_read8(uint32_t addr)
{
    vram_map(addr);
    return WIN[addr & 0x0FFF];
}

/* ---------------------------------------------------------------------- */
/* Palette                                                                */
/* ---------------------------------------------------------------------- */

/* rgb is count*3 bytes.  Only D7:D1 of each component reach the DAC, and the
 * bit that comes back out is a copy of the top one -- so $96 reads back as
 * $97.  Any screenshot comparison must expand values the same way.
 *
 * Writing CB auto-increments CSEL, so a run of entries needs no CSEL reload.
 * Colours take effect immediately (unlike the XDL address, which is latched
 * at vertical sync), so upload during blanking or accept tearing.
 */
void vbxe_palette(uint8_t pal, uint8_t first, const uint8_t *rgb, uint16_t count)
{
    uint16_t i;
    REG(FX_PSEL) = (uint8_t)(pal & 3);
    REG(FX_CSEL) = first;
    for (i = 0; i < count; i++) {
        REG(FX_CR) = *rgb++;
        REG(FX_CG) = *rgb++;
        REG(FX_CB) = *rgb++;                /* bumps CSEL */
    }
}

/* ---------------------------------------------------------------------- */
/* XDL                                                                    */
/* ---------------------------------------------------------------------- */

/* A 640x240 4bpp HR overlay from one screen buffer.
 *
 * Two things about the XDL that are easy to get wrong and silent when wrong:
 *   - an entry is only as long as its control word says, so a "blanked"
 *     control word does not blank the bytes behind it: the hardware reads
 *     them as further entries.  Build the list exactly, never patch a word.
 *   - overlay/attribute-map ADDRESSING is not reset per frame (width,
 *     priority, scroll and palette selection are), so it must be set at the
 *     top of every XDL.
 */
void vbxe_xdl_hr(uint32_t screen)
{
    uint8_t xdl[16];
    uint16_t ctl = XDLC_GMON | XDLC_HR | XDLC_RPTL | XDLC_OVADR | XDLC_OVATT;
    uint8_t n = 0;

    xdl[n++] = (uint8_t)(ctl & 0xFF);
    xdl[n++] = (uint8_t)(ctl >> 8);
    xdl[n++] = (uint8_t)(SCR_H - 1);              /* repeat -> SCR_H lines  */
    xdl[n++] = (uint8_t)(screen);                 /* OVADR, 3 bytes         */
    xdl[n++] = (uint8_t)(screen >> 8);
    xdl[n++] = (uint8_t)(screen >> 16);
    xdl[n++] = (uint8_t)(SCR_STRIDE);             /* OVSTEP, 12 bits        */
    xdl[n++] = (uint8_t)(SCR_STRIDE >> 8);
    xdl[n++] = OVATT_OVPAL(1) | OVATT_WIDTH_NORMAL;
    xdl[n++] = OVATT_PRI_OVER_ALL;                /* $FF -- see vbxe.h      */
    xdl[n++] = (uint8_t)((XDLC_OVOFF | XDLC_END) & 0xFF);
    xdl[n++] = (uint8_t)((XDLC_OVOFF | XDLC_END) >> 8);

    vram_write(VR_XDL, xdl, n);

    REG(FX_XDL_ADR0) = (uint8_t)(VR_XDL);
    REG(FX_XDL_ADR1) = (uint8_t)(VR_XDL >> 8);
    REG(FX_XDL_ADR2) = (uint8_t)(VR_XDL >> 16);
    /* NO_TRANS so nibble 0 is a real colour: a GUI has to be able to paint
     * its background, not see through it. */
    REG(FX_VIDEO_CONTROL) = VC_XDL_ENABLE | VC_NO_TRANS;
}

/* VBXE generates no VBI of its own -- all timing still comes from ANTIC --
 * and crt_atari.s has switched ANTIC's interrupts off, so RTCLOK is not
 * ticking.  Poll VCOUNT ($D40B) for the top of the frame instead. */
void vbxe_wait_vbl(void)
{
    volatile uint8_t *vcount = (volatile uint8_t *)0xD40B;
    while (*vcount != 0)
        ;
}

/* ---------------------------------------------------------------------- */
/* Blitter                                                                */
/* ---------------------------------------------------------------------- */

/* Blit control blocks are staged in RAM and uploaded as one contiguous run,
 * because the blitter has NO jump instruction: the "Next" bit only advances
 * to the physically adjacent BCB. */
#define MAX_BCB 12
static uint8_t  bcb[MAX_BCB * BCB_SIZE];
static uint8_t  bcb_count = 0;

void blit_reset(void) { bcb_count = 0; }

static uint8_t *bcb_new(void)
{
    uint8_t *p;
    if (bcb_count >= MAX_BCB)
        return 0;
    p = &bcb[bcb_count * BCB_SIZE];
    bcb_count++;
    return p;
}

static void bcb_common(uint8_t *p, uint32_t src, uint16_t sstride,
                       uint32_t dst, uint16_t dstride,
                       uint16_t bytes, uint16_t rows)
{
    uint16_t w1 = (uint16_t)(bytes - 1);          /* NINE bits, 1..512      */
    uint8_t i;
    for (i = 0; i < BCB_SIZE; i++)
        p[i] = 0;
    p[0]  = (uint8_t)(src);
    p[1]  = (uint8_t)(src >> 8);
    p[2]  = (uint8_t)(src >> 16);
    p[3]  = (uint8_t)(sstride);
    p[4]  = (uint8_t)(sstride >> 8);
    p[5]  = 1;                                    /* source X step          */
    p[6]  = (uint8_t)(dst);
    p[7]  = (uint8_t)(dst >> 8);
    p[8]  = (uint8_t)(dst >> 16);
    p[9]  = (uint8_t)(dstride);
    p[10] = (uint8_t)(dstride >> 8);
    p[11] = 1;                                    /* dest X step            */
    p[12] = (uint8_t)(w1 & 0xFF);
    p[13] = (uint8_t)((w1 >> 8) & 0x01);          /* the 9th width bit      */
    p[14] = (uint8_t)(rows - 1);                  /* height, 1..256         */
    p[18] = 0x00;                                 /* zoom 1x1               */
}

/* Solid fill.  and_mask == 0 takes the blitter's constant-source path: it
 * fetches no source bytes at all and runs at 1 cycle/byte, half the cost of a
 * real copy.  This is the most common operation a GUI performs. */
void blit_fill(uint32_t dst, uint16_t stride, uint16_t bytes,
               uint16_t rows, uint8_t value)
{
    uint8_t *p = bcb_new();
    if (!p)
        return;
    bcb_common(p, 0, 0, dst, stride, bytes, rows);
    p[15] = 0x00;                                 /* AND mask: no source    */
    p[16] = value;                                /* XOR mask: the colour   */
    p[20] = BLT_MODE_COPY;
}

/* Constant-source read-modify-write.  and_mask == 0 means no source fetch, so
 * c is just the xor mask; modes 2-6 then combine c with the destination.
 * Note modes 1-5 SKIP a byte whose c is zero -- harmless for OR/XOR, and the
 * AND masks used for 4bpp edges ($F0 / $0F) are never zero. */
static void blit_rmw(uint32_t dst, uint16_t stride, uint16_t bytes,
                     uint16_t rows, uint8_t value, uint8_t mode)
{
    uint8_t *p = bcb_new();
    if (!p)
        return;
    bcb_common(p, 0, 0, dst, stride, bytes, rows);
    p[15] = 0x00;
    p[16] = value;
    p[20] = mode;
}

void blit_and(uint32_t d, uint16_t s, uint16_t b, uint16_t r, uint8_t m)
{ blit_rmw(d, s, b, r, m, BLT_MODE_AND); }

void blit_or(uint32_t d, uint16_t s, uint16_t b, uint16_t r, uint8_t v)
{ blit_rmw(d, s, b, r, v, BLT_MODE_OR); }

void blit_xor(uint32_t d, uint16_t s, uint16_t b, uint16_t r, uint8_t v)
{ blit_rmw(d, s, b, r, v, BLT_MODE_XOR); }

uint8_t blit_pending(void) { return bcb_count; }

void vram_write8(uint32_t addr, uint8_t v)
{
    vram_map(addr);
    WIN[addr & 0x0FFF] = v;
}

void blit_copy(uint32_t src, uint16_t sstride, uint32_t dst,
               uint16_t dstride, uint16_t bytes, uint16_t rows)
{
    uint8_t *p = bcb_new();
    if (!p)
        return;
    bcb_common(p, src, sstride, dst, dstride, bytes, rows);
    p[15] = 0xFF;                                 /* AND mask: pass source  */
    p[16] = 0x00;
    p[20] = BLT_MODE_COPY;
}

/* Upload the queued list and start it, without waiting. */
void blit_start(void)
{
    uint8_t i;
    if (!bcb_count)
        return;
    /* Chain every block but the last. */
    for (i = 0; i + 1 < bcb_count; i++)
        bcb[i * BCB_SIZE + 20] |= BLT_NEXT;
    bcb[(bcb_count - 1) * BCB_SIZE + 20] &= (uint8_t)~BLT_NEXT;

    vram_write(VR_BCB, bcb, (uint16_t)(bcb_count * BCB_SIZE));

    REG(FX_BL_ADR0) = (uint8_t)(VR_BCB);
    REG(FX_BL_ADR1) = (uint8_t)(VR_BCB >> 8);
    REG(FX_BL_ADR2) = (uint8_t)(VR_BCB >> 16);
    REG(FX_BLITTER_START) = 0;                    /* stop first: required if
                                                     a previous list may be
                                                     running */
    REG(FX_BLITTER_START) = 1;
    memac_invalidate();                           /* the blitter owns VRAM
                                                     while it runs          */
    bcb_count = 0;
}

uint8_t blit_busy(void) { return REG(FX_BLITTER_BUSY); }

void blit_mask(uint32_t src, uint16_t sstride, uint32_t dst, uint16_t dstride,
               uint16_t bytes, uint16_t rows, uint8_t and_mask,
               uint8_t xor_mask, uint8_t mode)
{
    uint8_t *p = bcb_new();
    if (!p)
        return;
    bcb_common(p, src, sstride, dst, dstride, bytes, rows);
    p[15] = and_mask;
    p[16] = xor_mask;
    p[20] = mode;
}

void blit_run(void)
{
    blit_start();
    while (REG(FX_BLITTER_BUSY))                  /* D1 BUSY | D0 BCB_LOAD  */
        ;
}

/* Run the queued list and return how long it took, in VCOUNT ticks (ANTIC's
 * line counter, one tick per two scanlines; a PAL frame is 156).
 *
 * Polling BLITTER_BUSY costs a resync to 1.79 MHz on every read -- which is
 * exactly why the real driver should use the blitter-complete IRQ instead --
 * but VCOUNT advances in real time regardless, so the measurement is honest.
 * It is coarse, not wrong. */
uint16_t blit_time(void)
{
    volatile uint8_t *vc = (volatile uint8_t *)0xD40B;
    uint16_t ticks = 0;
    uint8_t last, v;

    while (*vc != 0)                              /* align to top of frame  */
        ;
    blit_start();
    last = 0;
    while (REG(FX_BLITTER_BUSY)) {
        v = *vc;
        if (v < last)
            ticks = (uint16_t)(ticks + 156);      /* VCOUNT wrapped (PAL)   */
        last = v;
    }
    return (uint16_t)(ticks + last);
}
