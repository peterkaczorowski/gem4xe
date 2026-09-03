/* rsrc.c -- the resource library: rsrc_load, rsrc_free, rsrc_gaddr,
 * rsrc_saddr, rsrc_obfix.  EmuTOS aes/gemrslib.c, less the colour icons.
 *
 * A .RSC is a dump of OBJECT, TEDINFO, ICONBLK and BITBLK arrays with
 * every pointer an offset from the start of the file and every word in
 * the 68000's byte order.  gem4xe's structures are the same bytes in the
 * 65816's order, so loading is: read the file whole into the bank-$00
 * pool, swap the words of the header and of every table the header
 * counts, then add the pool address to every offset -- the donor's
 * fix_long, with the base a 16-bit address in bank $00 rather than a
 * 68000 pointer.  Strings and image data are bytes and are left alone:
 * a 1-plane image is MSB-first in both worlds, and the VDI's vrt_cpyfm
 * reads it that way.
 *
 * The objects' rectangles are stored as (pixel offset << 8 | character
 * position) and made pixels from the workstation's character cell
 * (fix_chpos); a width of 80 characters means the whole screen.  The
 * donor does that fixup separately (rs_fixit) because its own resource
 * is read before the workstation is open; here the workstation always
 * is, so one call does everything.
 *
 * The pool is why there is one resource at a time: rsrc_load takes from
 * the bump allocator and rsrc_free winds it back, which is only right
 * while nothing else was taken above it -- the file selector's tree,
 * which is taken and released inside one call, is the only other user.
 * The selector's tree is a .RSC too, built into the far image
 * (tools/fselrsc.py), so the fixup takes the header it works on as a
 * parameter, rs_fixit(), rather than the one resource the library holds.
 */
#include <string.h>
#include "aes/aes.h"
#include "sys/app.h"
#include "sys/cio.h"

RSHDR *rs_hdr;
static uint16_t rs_mark;            /* the pool before the load */

static void swap_words(void *p, uint16_t n)
{
    uint8_t *b = p;
    while (n--) {
        uint8_t t = b[0];
        b[0] = b[1];
        b[1] = t;
        b += 2;
    }
}

/* A word of the file made a pixel: HIBYTE the pixel offset (signed),
 * LOBYTE the position in characters. */
static void fix_chpos(WORD *pfix, WORD which)
{
    WORD coffset = (WORD)((UWORD)*pfix >> 8);
    WORD cpos = (WORD)(*pfix & 0xFF);

    switch (which) {
    case 0:
        cpos = (WORD)(cpos * gl_wchar);
        break;
    case 1:
        cpos = (WORD)(cpos * gl_hchar);
        break;
    case 2:
        cpos = (WORD)(cpos == 80 ? gl_width : cpos * gl_wchar);
        break;
    default:
        cpos = (WORD)(cpos * gl_hchar);
        break;
    }
    cpos = (WORD)(cpos + (coffset > 128 ? coffset - 256 : coffset));
    *pfix = cpos;
}

void rs_obfix(OBJECT *tree, WORD obj)
{
    WORD *p = &tree[obj].ob_x;
    WORD k;
    for (k = 0; k < 4; k++)
        fix_chpos(p + k, k);
}

/* An offset from the start of the file made an address; -1 stays -1. */
static WORD fix_long(const RSHDR *h, uint32_t *p)
{
    if (*p == 0xFFFFFFFFUL)
        return 0;
    *p += (uint16_t)h;
    return 1;
}

static void *sub_of(const RSHDR *h, UWORD index, UWORD offset, UWORD size)
{
    return (uint8_t *)h + offset + size * index;
}

/* The donor's get_addr: (void *)-1 for a type it does not know.  Every
 * address it hands out is bank $00, so a 16-bit one. */
static void *addr_of(const RSHDR *h, UWORD rtype, UWORD rindex)
{
    UWORD offset, size;

    switch (rtype) {
    case R_TREE:
        return (void *)(uint16_t)*(uint32_t *)sub_of(h, rindex, h->rsh_trindex, 4);
    case R_OBJECT:
        offset = h->rsh_object;  size = sizeof(OBJECT);  break;
    case R_TEDINFO:
    case R_TEPTEXT:
        offset = h->rsh_tedinfo; size = sizeof(TEDINFO); break;
    case R_ICONBLK:
    case R_IBPMASK:
        offset = h->rsh_iconblk; size = sizeof(ICONBLK); break;
    case R_BITBLK:
    case R_BIPDATA:
        offset = h->rsh_bitblk;  size = sizeof(BITBLK);  break;
    case R_OBSPEC:
        return &((OBJECT *)addr_of(h, R_OBJECT, rindex))->ob_spec;
    case R_TEPTMPLT:
        return &((TEDINFO *)addr_of(h, R_TEDINFO, rindex))->te_ptmplt;
    case R_TEPVALID:
        return &((TEDINFO *)addr_of(h, R_TEDINFO, rindex))->te_pvalid;
    case R_IBPDATA:
        return &((ICONBLK *)addr_of(h, R_ICONBLK, rindex))->ib_pdata;
    case R_IBPTEXT:
        return &((ICONBLK *)addr_of(h, R_ICONBLK, rindex))->ib_ptext;
    case R_STRING:
        return (void *)(uint16_t)*(uint32_t *)sub_of(h, rindex, h->rsh_frstr, 4);
    case R_IMAGEDATA:
        return (void *)(uint16_t)*(uint32_t *)sub_of(h, rindex, h->rsh_frimg, 4);
    case R_FRSTR:
        offset = h->rsh_frstr;   size = 4; break;
    case R_FRIMG:
        offset = h->rsh_frimg;   size = 4; break;
    default:
        return (void *)0xFFFF;
    }
    return sub_of(h, rindex, offset, size);
}

static void fix_nptrs(const RSHDR *h, WORD cnt, WORD type)
{
    WORD i;
    for (i = 0; i < cnt; i++)
        fix_long(h, addr_of(h, type, i));
}

/* Everything the donor does in rs_readit and rs_fixit: `h` is the file's
 * bytes, whole and as the file has them, at the address they will be
 * used from.  The header's words are swapped first, then every table it
 * counts. */
void rs_fixit(RSHDR *h)
{
    WORD i;

    swap_words(h, sizeof *h / 2);
    swap_words(sub_of(h, 0, h->rsh_object, 0),  (UWORD)(h->rsh_nobs * (sizeof(OBJECT) / 2)));
    swap_words(sub_of(h, 0, h->rsh_tedinfo, 0), (UWORD)(h->rsh_nted * (sizeof(TEDINFO) / 2)));
    swap_words(sub_of(h, 0, h->rsh_iconblk, 0), (UWORD)(h->rsh_nib * (sizeof(ICONBLK) / 2)));
    swap_words(sub_of(h, 0, h->rsh_bitblk, 0),  (UWORD)(h->rsh_nbb * (sizeof(BITBLK) / 2)));
    swap_words(sub_of(h, 0, h->rsh_frstr, 0),   (UWORD)(h->rsh_nstring * 2));
    swap_words(sub_of(h, 0, h->rsh_frimg, 0),   (UWORD)(h->rsh_nimages * 2));
    swap_words(sub_of(h, 0, h->rsh_trindex, 0), (UWORD)(h->rsh_ntree * 2));
    /* A 68000 LONG is its two words swapped as well as each word */
    for (i = 0; i < h->rsh_ntree; i++) {
        uint16_t *p = sub_of(h, i, h->rsh_trindex, 4);
        uint16_t t = p[0]; p[0] = p[1]; p[1] = t;
    }
    for (i = 0; i < h->rsh_nstring; i++) {
        uint16_t *p = sub_of(h, i, h->rsh_frstr, 4);
        uint16_t t = p[0]; p[0] = p[1]; p[1] = t;
    }
    for (i = 0; i < h->rsh_nimages; i++) {
        uint16_t *p = sub_of(h, i, h->rsh_frimg, 4);
        uint16_t t = p[0]; p[0] = p[1]; p[1] = t;
    }
    for (i = 0; i < h->rsh_nobs; i++) {
        uint16_t *p = (uint16_t *)&((OBJECT *)addr_of(h, R_OBJECT, i))->ob_spec;
        uint16_t t = p[0]; p[0] = p[1]; p[1] = t;
    }
    for (i = 0; i < h->rsh_nted; i++) {
        uint16_t *p = (uint16_t *)addr_of(h, R_TEDINFO, i);
        uint16_t t;
        t = p[0]; p[0] = p[1]; p[1] = t;
        t = p[2]; p[2] = p[3]; p[3] = t;
        t = p[4]; p[4] = p[5]; p[5] = t;
    }
    for (i = 0; i < h->rsh_nib; i++) {
        uint16_t *p = (uint16_t *)addr_of(h, R_ICONBLK, i);
        uint16_t t;
        t = p[0]; p[0] = p[1]; p[1] = t;
        t = p[2]; p[2] = p[3]; p[3] = t;
        t = p[4]; p[4] = p[5]; p[5] = t;
    }
    for (i = 0; i < h->rsh_nbb; i++) {
        uint16_t *p = (uint16_t *)addr_of(h, R_BITBLK, i);
        uint16_t t = p[0]; p[0] = p[1]; p[1] = t;
    }

    /* fix_trindex */
    for (i = 0; i < h->rsh_ntree; i++)
        fix_long(h, sub_of(h, i, h->rsh_trindex, 4));
    /* fix_tedinfo_std */
    for (i = 0; i < h->rsh_nted; i++) {
        TEDINFO *ted = addr_of(h, R_TEDINFO, i);
        if (fix_long(h, &ted->te_ptext))
            ted->te_txtlen = (WORD)(strlen((const char *)(uint16_t)ted->te_ptext) + 1);
        if (fix_long(h, &ted->te_ptmplt))
            ted->te_tmplen = (WORD)(strlen((const char *)(uint16_t)ted->te_ptmplt) + 1);
        fix_long(h, &ted->te_pvalid);
    }
    fix_nptrs(h, h->rsh_nib, R_IBPMASK);
    fix_nptrs(h, h->rsh_nib, R_IBPDATA);
    fix_nptrs(h, h->rsh_nib, R_IBPTEXT);
    fix_nptrs(h, h->rsh_nbb, R_BIPDATA);
    fix_nptrs(h, h->rsh_nstring, R_FRSTR);
    fix_nptrs(h, h->rsh_nimages, R_FRIMG);
    /* fix_objects */
    for (i = 0; i < h->rsh_nobs; i++) {
        OBJECT *obj = addr_of(h, R_OBJECT, i);
        rs_obfix(obj, 0);
        switch (obj->ob_type & 0xFF) {
        case G_BOX:
        case G_IBOX:
        case G_BOXCHAR:
            break;
        default:
            fix_long(h, &obj->ob_spec);
            break;
        }
    }
}

WORD rs_load(const char *name)
{
    RSHDR hdr, raw;
    char cio[CIO_NAME_MAX + 1];
    uint16_t got, size;
    int16_t fd;
    uint8_t *mem;

    if (rs_hdr)                     /* one at a time: free it first */
        return 0;
    sh_cioname(name, cio);          /* A:\X.RSC -> D1:X.RSC */
    fd = cio_open(cio, CIO_A_READ, 0);
    if (fd < 0)
        return 0;
    if (cio_read(fd, &raw, sizeof raw, &got) != CIO_OK || got != sizeof raw) {
        cio_close(fd);
        return 0;
    }
    hdr = raw;                      /* the file's header, read for its size */
    swap_words(&hdr, sizeof hdr / 2);
    size = hdr.rsh_rssize;
    if ((hdr.rsh_vrsn & NEW_FORMAT_RSC) || size < sizeof hdr) {
        cio_close(fd);
        return 0;
    }
    rs_mark = pool_mark();
    mem = pool_alloc(size, 2);
    if (!mem) {
        cio_close(fd);
        return 0;
    }
    memcpy(mem, &raw, sizeof raw);  /* rs_fixit takes the file as it is */
    {
        uint8_t st = cio_read(fd, mem + sizeof raw, (uint16_t)(size - sizeof raw), &got);
        cio_close(fd);
        if ((st != CIO_OK && st != CIO_OK_EOF) || got != size - sizeof raw) {
            pool_release(rs_mark);
            return 0;
        }
    }
    rs_hdr = (RSHDR *)mem;
    rs_fixit(rs_hdr);
    return 1;
}

WORD rs_free(void)
{
    if (!rs_hdr)
        return 0;
    pool_release(rs_mark);
    rs_hdr = 0;
    return 1;
}

WORD rs_gaddr(UWORD rtype, UWORD rindex, uint32_t *paddr)
{
    void *p;
    if (!rs_hdr)
        return 0;
    p = addr_of(rs_hdr, rtype, rindex);
    *paddr = (uint16_t)p;
    return (uint16_t)p != 0xFFFF;
}

WORD rs_saddr(UWORD rtype, UWORD rindex, uint32_t addr)
{
    uint32_t *p;
    if (!rs_hdr)
        return 0;
    p = addr_of(rs_hdr, rtype, rindex);
    if ((uint16_t)p == 0xFFFF)
        return 0;
    *p = addr;
    return 1;
}
