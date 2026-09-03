/* app.c -- the G4A loader.  See app.h; the format is tools/mkg4a.py's. */
#include <string.h>
#include "sys/app.h"
#include "sys/abi.h"
#include "sys/farmem.h"

/* The pool's bounds, from the linker (src/sys/apppool.s). */
extern const uint16_t app_pool_lo, app_pool_hi;

static uint16_t pool_brk;       /* 0 until the first take: then the cursor */

uint16_t pool_mark(void)
{
    if (!pool_brk)
        pool_brk = app_pool_lo;
    return pool_brk;
}

void *pool_alloc(uint16_t size, uint16_t align)
{
    uint16_t base = pool_mark();
    uint16_t mask = (uint16_t)(align - 1);
    base = (uint16_t)((base + mask) & ~mask);
    if (base < pool_brk || (uint32_t)base + size > app_pool_hi)
        return 0;
    pool_brk = (uint16_t)(base + size);
    return (void *)base;
}

void pool_release(uint16_t mark)
{
    pool_brk = mark;
}

uint16_t pool_room(void)
{
    return (uint16_t)(app_pool_hi - pool_mark());
}

#define HDR_SIZE 32

static uint16_t rd16(const uint8_t __far *p, uint16_t off)
{
    return (uint16_t)(p[off] | ((uint16_t)p[off + 1] << 8));
}

static uint32_t rd32(const uint8_t __far *p, uint16_t off)
{
    return (uint32_t)rd16(p, off) | ((uint32_t)rd16(p, off + 2) << 16);
}

int16_t app_load(const uint8_t __far *blob, uint32_t len, APP *app)
{
    uint16_t near_size, far_off, n_nhi, n_nbank, n_fhi, n_fbank, k;
    uint32_t far_size, need, lists, entry;
    uint8_t  far_banks, dpage, dbank;
    uint16_t bank;
    uint8_t *near;
    uint8_t __far *far;

    memset(app, 0, sizeof *app);     /* a failed load reports zeros */
    if (len < HDR_SIZE)
        return APP_E_SHORT;
    if (blob[0] != 'G' || blob[1] != '4' || blob[2] != 'A' || blob[3] != 1)
        return APP_E_MAGIC;
    app->link_near = rd16(blob, 4);
    near_size      = rd16(blob, 6);
    far_off        = rd16(blob, 8);
    far_size       = rd32(blob, 10);
    app->link_bank = blob[14];
    far_banks      = blob[15];
    entry          = rd32(blob, 16) & 0xFFFFFFUL;
    n_nhi   = rd16(blob, 20);
    n_nbank = rd16(blob, 22);
    n_fhi   = rd16(blob, 24);
    n_fbank = rd16(blob, 26);
    lists = HDR_SIZE + near_size + far_size;
    need = lists + 2UL * ((uint32_t)n_nhi + n_nbank + n_fhi + n_fbank);
    if (need > len)
        return APP_E_SHORT;

    /* A place for each part.  Both allocators are marked first so a
     * failure part-way leaves nothing taken. */
    app->pool_mark = pool_mark();
    app->far_mark = farmem.brk;
    near = pool_alloc(near_size, 0x100);
    if (!near)
        return APP_E_POOL;
    app->near_base = (uint16_t)near;
    app->near_size = near_size;
    bank = far_alloc_banks(far_banks);
    if (!bank) {
        pool_release(app->pool_mark);
        return APP_E_FAR;
    }
    app->far_addr = ((uint32_t)bank << 16) | far_off;
    app->far_size = far_size;

    /* The bytes, then the patches.  A near address moves by whole pages
     * and a far one by whole banks, so each fixup is one byte plus a
     * constant: the high byte of a near address by the page difference,
     * the bank byte of a far address by the bank difference. */
    near = (uint8_t *)app->near_base;
    far = (uint8_t __far *)app->far_addr;
    for (k = 0; k < near_size; k++)
        near[k] = blob[HDR_SIZE + k];
    __memcpy_far(far, blob + HDR_SIZE + near_size, (size_t)far_size);

    dpage = (uint8_t)((app->near_base - app->link_near) >> 8);
    dbank = (uint8_t)(bank - app->link_bank);
    app->fixups = 0;
    {
        const uint8_t __far *l = blob + lists;
        for (k = 0; k < n_nhi; k++, l += 2) {
            uint16_t o = rd16(l, 0);
            if (o >= near_size)
                return APP_E_FIXUP;
            near[o] += dpage;
        }
        for (k = 0; k < n_nbank; k++, l += 2) {
            uint16_t o = rd16(l, 0);
            if (o >= near_size)
                return APP_E_FIXUP;
            near[o] += dbank;
        }
        for (k = 0; k < n_fhi; k++, l += 2) {
            uint16_t o = rd16(l, 0);
            if (o >= far_size)
                return APP_E_FIXUP;
            far[o] += dpage;
        }
        for (k = 0; k < n_fbank; k++, l += 2) {
            uint16_t o = rd16(l, 0);
            if (o >= far_size)
                return APP_E_FIXUP;
            far[o] += dbank;
        }
        app->fixups = (uint16_t)(n_nhi + n_nbank + n_fhi + n_fbank);
    }
    app->entry = entry + ((uint32_t)dbank << 16);
    return APP_OK;
}

int16_t app_exec(const APP *app)
{
    return app_run(app->entry);
}

void app_free(const APP *app)
{
    pool_release(app->pool_mark);
    farmem.brk = app->far_mark;
}
