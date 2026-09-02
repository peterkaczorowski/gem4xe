/* rapidus.c -- switch the Rapidus SRAM in over bank $00.  See rapidus.h. */
#include "rapidus.h"
#include "vbxe/vbxe.h"          /* MEMAC_WIN_ADDR, MEMAC_WIN_SIZE */

RAPIDUS rapidus;

/* The linker's direct page (src/gem4xe.scm, base-address _DirectPageStart);
 * the library startup loads D from it. */
extern char _DirectPageStart;

static uint8_t reg_read(uint32_t a)
{
    volatile uint8_t __far *p = (volatile uint8_t __far *)a;
    return *p;
}

static void reg_write(uint32_t a, uint8_t v)
{
    volatile uint8_t __far *p = (volatile uint8_t __far *)a;
    *p = v;
}

/* Bring a window's SRAM copy up to date from the motherboard.  Only valid
 * while the window is still slow and write-through is on: a read then comes
 * from the motherboard and a write lands in both.  Copying the window that
 * holds this function's own stack frame onto itself is fine -- each byte is
 * written back with the value just read, and nothing else runs. */
static void sync_window(uint8_t w)
{
    volatile uint8_t *p = (volatile uint8_t *)((uint16_t)w << 14);
    uint16_t n = 0x4000;

    do {
        *p = *p;
        p++;
    } while (--n);
}

void rapidus_speedup(void)
{
    uint8_t mcr, cmcr, keep_slow, fast, w, here;

    rapidus.present = 0;
    rapidus.synced = 0;
    if (reg_read(RAP_SIG) != '6' || reg_read(RAP_SIG + 1) != 'S')
        return;
    rapidus.present = 1;

    mcr  = reg_read(RAP_MCR);
    cmcr = reg_read(RAP_CMCR);
    rapidus.mcr_before = mcr;

    /* The windows that must not go fast: whatever the MEMAC window spans,
     * and the OS/hardware window, which is not ours to decide. */
    keep_slow = MCR_SLOW3;
    for (w = RAP_WIN(MEMAC_WIN_ADDR);
         w <= RAP_WIN(MEMAC_WIN_ADDR + MEMAC_WIN_SIZE - 1); w++)
        keep_slow |= (uint8_t)(1 << w);

    /* Windows about to change from slow to fast. */
    fast = (uint8_t)(mcr & MCR_SLOWALL & ~keep_slow);

    /* Without write-through nothing has kept the SRAM copies current. */
    if (!(mcr & MCR_WRTHRU)) {
        mcr |= MCR_WRTHRU;
        reg_write(RAP_MCR, mcr);
        for (w = 0; w < 4; w++) {
            if (fast & (1 << w)) {
                sync_window(w);
                rapidus.synced |= (uint8_t)(1 << w);
            }
        }
    }

    mcr = (uint8_t)((mcr & ~fast) | (keep_slow & ~MCR_SLOW3));
    reg_write(RAP_MCR, mcr);
    rapidus.mcr_after = mcr;

    /* Fast writes for $0000-$3FFF, if that is where the linker put what the
     * program writes: the direct page, the stack and the data.  If any of
     * them is elsewhere this is left alone -- slower, still correct. */
    if (!(mcr & MCR_SLOW0)
        && RAP_WIN(&_DirectPageStart) == 0
        && RAP_WIN(&here) == 0
        && RAP_WIN(&rapidus) == 0) {
        cmcr |= CMCR_FAST0;
        reg_write(RAP_CMCR, cmcr);
    }
    rapidus.cmcr_after = cmcr;
}
