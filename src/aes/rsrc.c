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
#include "aes/proc.h"
#include "sys/app.h"
#include "sys/cio.h"
#include "sys/farmem.h"

/* The running process's resource and the pool mark before it: see
 * src/aes/proc.h for why these belong to a process and not to this file.
 * rs_hdr stays the name the rest of the engine knows it by. */
#define rs_1        (*(RSHDR **)&rlr->p_rsc)
#define rs_1mark    (rlr->p_rscmark)
#define rs_2        (*(RSHDR **)&rlr->p_rsc2)
#define rs_2mark    (rlr->p_rscmark2)
/* The resource a call ACTS ON: the nested one while it is up, so that a
 * dialog loaded over a resident resource answers rsrc_gaddr and is what
 * rsrc_free takes away.  Read-only -- the two slots are assigned by
 * name. */
#define rs_hdr      (rs_2 ? rs_2 : rs_1)
#define rs_mark     (rlr->p_rscmark)

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

/* The sizes these records have IN THE FILE, written out rather than taken
 * from sizeof -- and the story behind that is worth the space, because it
 * cost an evening and very nearly cost a wrong "fix".
 *
 * sizeof IS RIGHT HERE.  An ICONBLK is 34 bytes in this compiler's
 * generated code, as the file gives it: a function returning
 * sizeof(ICONBLK) compiles to `lda ##34`, &arr[i] scales by 34, and
 * rs_cicons below steps the colour extension correctly.  MControl's own
 * resource -- 22 colour icons, a real ST file -- loads on the machine
 * with every record's geometry and both far addresses matching the file,
 * at a record stride of 50, which is 34 + 12 + 4 and could not be
 * 36 + 12 + 4.
 *
 * WHAT IS WRONG IS THE WAY THE STRUCT WAS MEASURED.  The negative-array
 * idiom -- char p[(sizeof(X)==N)?1:-1] -- answers 36 for this struct,
 * because the compiler's CONSTANT-EXPRESSION evaluator rounds a size up
 * to the alignment while its code generator does not (tools/ccbug, B7 -- whose rule,
 * written down since long before this, is the one this file broke).
 * On that answer the strides below were read as a live bug, a peer was
 * told the fix mattered to their port, and the gates passed either way
 * for the simple reason that the code was correct to begin with.
 *
 * So these four names buy no behaviour at all.  They buy the one
 * thing the
 * episode showed is worth having: a stride that does not depend on a
 * sizeof this compiler reports two different values for, with
 * tools/rsc.py naming the same numbers and tests/host/test_rsrc.py
 * holding the two lists against each other, against the ST's format, and
 * against the tables of the resources this tree builds.  rs_cicons below
 * is deliberately left on sizeof, because it is right and because the
 * two sides of its near record have to agree with each other. */
#define RSZ_OBJECT   24
#define RSZ_TEDINFO  28
#define RSZ_ICONBLK  34
#define RSZ_BITBLK   14

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
        offset = h->rsh_object;  size = RSZ_OBJECT;  break;
    case R_TEDINFO:
    case R_TEPTEXT:
        offset = h->rsh_tedinfo; size = RSZ_TEDINFO; break;
    case R_ICONBLK:
    case R_IBPMASK:
        offset = h->rsh_iconblk; size = RSZ_ICONBLK; break;
    case R_BITBLK:
    case R_BIPDATA:
        offset = h->rsh_bitblk;  size = RSZ_BITBLK;  break;
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
    swap_words(sub_of(h, 0, h->rsh_object, 0),  (UWORD)(h->rsh_nobs * (RSZ_OBJECT / 2)));
    swap_words(sub_of(h, 0, h->rsh_tedinfo, 0), (UWORD)(h->rsh_nted * (RSZ_TEDINFO / 2)));
    swap_words(sub_of(h, 0, h->rsh_iconblk, 0), (UWORD)(h->rsh_nib * (RSZ_ICONBLK / 2)));
    swap_words(sub_of(h, 0, h->rsh_bitblk, 0),  (UWORD)(h->rsh_nbb * (RSZ_BITBLK / 2)));
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
        case G_CICON:
            /* an INDEX into the colour-icon table, not an offset: it is
             * made an address by rs_cicons once the table is placed */
            break;
        default:
            fix_long(h, &obj->ob_spec);
            break;
        }
    }
}

/* THE IMAGE BITS GO TO FAR MEMORY, and the pool gets them back.
 *
 * A resource is loaded into the application pool -- 14 KB of bank $00 for
 * the desktop, its resource and everything resident beside it -- and
 * DESKTOP.RSC is 6,226 bytes of that.  A quarter of it is icon bitmaps,
 * which are the one part nothing in bank $00 has to reach: an ICONBLK
 * names its mask and its image in 32-bit fields, and gsx_blt has taken a
 * 32-bit address since phase 2, because that is what an MFDB holds.  So
 * the bits are copied up and the pool is wound back over them: 1,536
 * bytes, which is what a second desk accessory costs.
 *
 * tools/rsc.py puts the image block LAST in the file for this, so the
 * release is a wind-back of the tail and not a compaction of the middle
 * -- which would invalidate every offset rs_fixit has just fixed.
 *
 * IT ONLY MOVES WHAT IS PROVABLY REACHED THROUGH A 32-BIT FIELD.  A
 * resource with BITBLKs or free images keeps its bits in the pool, and
 * the reason is rs_gaddr: it answers an application with a NEAR address,
 * so a free image's bytes must be somewhere sixteen bits can name.  An
 * ICONBLK's cannot be asked for that way -- the application is given the
 * ICONBLK and reads the wide field itself.
 *
 * THE FAR BYTES COME BACK AT rs_free WHEN THEY ARE THE TOP OF THE HEAP.
 * The far heap is a bump allocator (src/sys/farmem.h), so a block can be
 * given back only while nothing has been taken above it -- which is the
 * common case, a resource loaded, used and freed with no Malloc between.
 * Then rs_free winds the heap back to where it stood before the load
 * (the mark BEFORE far_alloc, since far_alloc may have skipped to a bank
 * boundary), and a dialog resource loaded and freed per use costs
 * nothing that lasts.  When something IS above it the block stays, and
 * app_free reclaims it with the rest of the program's far memory: with
 * 14 MB that is a note rather than a leak.  The reclaim records are PER
 * SLOT -- a nested resource freed first must not take the outer one's
 * with it -- while rs_imbase/rs_cibase answer for the last load placed. */
typedef struct {
    uint32_t base, mark, top;   /* the block; the heap before and after it */
} FARBLK;
static uint32_t rs_imbase;      /* where they went, 0 if they stayed */
static uint16_t rs_imsize;
static FARBLK   rs_im[2];       /* by slot: [0] the resident, [1] the nested */

void rs_imaddr(uint32_t *base, uint16_t *len)
{
    *base = rs_imbase;
    *len = rs_imsize;
}

static void rs_imfar(uint8_t *mem, uint16_t im_off, uint16_t size)
{
    uint16_t im_len = (uint16_t)(size - im_off);
    uint16_t im_near;
    uint32_t base, mark;
    WORD i;

    if (!im_len || rs_hdr->rsh_nbb || rs_hdr->rsh_nimages)
        return;                         /* see the note: not provably safe --
                                         * and nothing moved, so a resource
                                         * NESTED over another leaves the
                                         * outer's record of its own alone */
    /* ...AND ONLY WHEN THE IMAGE BLOCK IS THE TAIL OF THE FILE.  The pool
     * is wound back over it, so anything the header places at or above
     * rsh_imdata would go with it.  tools/rsc.py and the desktop's
     * resource put the bits last; a resource written by another tool
     * need not -- HypView's has no images at all and 2,632 bytes of its
     * tables above the offset, and moving those "images" put its objects
     * in far memory and the colour-icon records on top of them.  A file
     * laid out that way keeps its bits in the pool. */
    {
        /* the header as eighteen words: (offset word, count word, size)
         * for each table, and the strings' offset with no count */
        static const uint8_t lay[8][3] = {
            {1, 10, RSZ_OBJECT}, {2, 12, RSZ_TEDINFO},
            {3, 13, RSZ_ICONBLK}, {4, 14, RSZ_BITBLK},
            {5, 15, 4}, {8, 16, 4}, {9, 11, 4}, {6, 10, 0}
        };
        const UWORD *hw = (const UWORD *)rs_hdr;
        for (i = 0; i < 8; i++)
            if ((uint16_t)(hw[lay[i][0]] + hw[lay[i][1]] * lay[i][2]) > im_off)
                return;
    }
    rs_imbase = 0;
    rs_imsize = 0;
    mark = farmem.brk;
    base = far_alloc(im_len);
    if (!base)
        return;                         /* no far memory: leave them be */
    {
        FARBLK *f = &rs_im[rs_2 ? 1 : 0];
        f->base = base;
        f->mark = mark;
        f->top = farmem.brk;
    }
    im_near = (uint16_t)((uint16_t)mem + im_off);
    far_put(base, (const uint8_t *)im_near, im_len);
    for (i = 0; i < rs_hdr->rsh_nib; i++) {
        ICONBLK *ib = addr_of(rs_hdr, R_ICONBLK, i);
        ib->ib_pmask = base + (ib->ib_pmask - im_near);
        ib->ib_pdata = base + (ib->ib_pdata - im_near);
    }
    pool_release(im_near);
    rs_imbase = base;
    rs_imsize = im_len;
}

/* COLOUR ICONS: THE EXTENSION GOES FAR, AND ITS MONO HEADERS COME NEAR.
 *
 * A new-format resource (rsh_vrsn bit 2) carries, after its rsh_rssize
 * bytes, an array of 68000 LONGs -- the file's true length, the offset of
 * the colour-icon table or 0/-1 for none, further extensions, a 0 -- and
 * at that offset one LONG per CICONBLK ending in -1, then the CICONBLKs:
 * each an ICONBLK, a LONG count of colour forms, the mono bits, the mono
 * mask, twelve bytes of text, and the colour forms (EmuTOS
 * aes/gemrslib.c, get_ciconblkptr and fixup_all_ciconblks).  A G_CICON
 * object's ob_spec is an INDEX into that table.
 *
 * MControl's extension is 34,752 bytes and HypView's 21,176, against a
 * pool of 14,336: it has no business there.  So it is streamed to far
 * memory through a slice of the pool, and only what the object library
 * reads through a near pointer comes back down -- one CICON_NEAR per
 * icon: the ICONBLK, its mask and bits named by far address exactly as a
 * mono icon's are once rs_imfar has moved them, its text beside it, and
 * where its colour forms are for the day objc_draw selects one.  They are
 * taken AFTER rs_imfar has wound the pool back, so they sit where the
 * mono images were and cost nothing extra when there were images to
 * move.  Like the image bits, the far block is app_free's to reclaim. */
#define CICON_TEXT  12
typedef struct {
    ICONBLK  ib;
    char     text[CICON_TEXT];
    uint32_t cicons;                    /* far: the first CICON, or 0 */
} CICON_NEAR;                           /* 50 bytes, and even */

#define CICON_MAX   256                 /* a table longer than this is not one */

uint32_t rs_cibase;                     /* where the extension went, 0 if none */
uint16_t rs_cisize;
static FARBLK   rs_ci[2];               /* by slot, as rs_im */

/* Give a slot's far blocks back, each while it is still the top of the
 * heap -- the extension was taken after the images, so it is asked first.
 * `slot` is 1 or 2, as rs_1/rs_2 are named. */
static void rs_farback(WORD slot)
{
    FARBLK *f = &rs_ci[slot - 1];
    if (f->base && farmem.brk == f->top) {
        far_release(f->mark);
        if (rs_cibase == f->base)
            rs_cibase = rs_cisize = 0;
        f->base = 0;
    }
    f = &rs_im[slot - 1];
    if (f->base && farmem.brk == f->top) {
        far_release(f->mark);
        f->base = 0;
    }
}

static uint32_t rd_long(uint32_t far)   /* a 68000 LONG, from far memory */
{
    uint8_t b[4];
    far_get(b, far, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
         | ((uint32_t)b[2] << 8) | b[3];
}

/* `fd` is positioned just past the rsh_rssize bytes already in `h`.
 * 1 on success; 0 and nothing of the pool kept on any failure. */
static WORD rs_cicons(int16_t fd, RSHDR *h, uint16_t size)
{
    uint8_t ext[8], st;
    uint16_t got, n, i, mark, slice, k, bytes;
    uint32_t true_len, tab, ext_len, base, p, remaining, at;
    CICON_NEAR *near;
    uint8_t *buf;

    if (cio_read(fd, ext, sizeof ext, &got) != CIO_OK || got != sizeof ext)
        return 0;
    true_len = ((uint32_t)ext[0] << 24) | ((uint32_t)ext[1] << 16) | ((uint32_t)ext[2] << 8) | ext[3];
    tab      = ((uint32_t)ext[4] << 24) | ((uint32_t)ext[5] << 16) | ((uint32_t)ext[6] << 8) | ext[7];
    if (tab == 0 || tab == 0xFFFFFFFFUL)
        return 1;                       /* new format, no colour icons: an
                                         * outer resource's record stands */
    if (true_len <= (uint32_t)size + sizeof ext || tab < size)
        return 0;
    ext_len = true_len - size;
    rs_cibase = 0;
    rs_cisize = 0;
    {
        FARBLK *f = &rs_ci[rs_2 ? 1 : 0];
        f->mark = farmem.brk;
        base = far_alloc(ext_len);
        if (!base)
            return 0;
        f->base = base;
        f->top = farmem.brk;
    }
    far_put(base, ext, sizeof ext);

    /* the rest of it, through a slice of what the pool has spare */
    mark = pool_mark();
    slice = pool_room();
    slice = slice > 2048 ? 2048 : (uint16_t)(slice & ~1);
    if (slice < 64)
        return 0;
    buf = pool_alloc(slice, 2);
    if (!buf)
        return 0;
    p = base + sizeof ext;
    remaining = ext_len - sizeof ext;
    while (remaining) {
        k = remaining > slice ? slice : (uint16_t)remaining;
        st = cio_read(fd, buf, k, &got);
        if ((st != CIO_OK && st != CIO_OK_EOF) || got == 0) {
            pool_release(mark);
            return 0;
        }
        far_put(p, buf, got);
        p += got;
        remaining -= got;
    }
    pool_release(mark);

    /* One running far address walks the whole extension: the table up to
     * its -1, then each CICONBLK's header, bits, mask, text and forms in
     * the order the file has them.  (A file offset is `at - base + size`;
     * nothing below needs one.) */
    at = base + (tab - size);
    for (n = 0; n < CICON_MAX && rd_long(at) != 0xFFFFFFFFUL; n++)
        at += 4;
    if (n == 0 || n == CICON_MAX)
        return 0;
    at += 4;                            /* past the -1: the first CICONBLK */
    near = pool_alloc((uint16_t)(n * sizeof(CICON_NEAR)), 2);
    if (!near)
        return 0;

    for (i = 0; i < n; i++) {
        CICON_NEAR *c = &near[i];
        uint32_t num;
        far_get((uint8_t *)&c->ib, at, sizeof(ICONBLK));
        swap_words((uint8_t *)&c->ib + 12, 11); /* the WORDs after the
                                                 * three (junk) LONGs */
        num = rd_long(at + sizeof(ICONBLK));
        bytes = (uint16_t)((c->ib.ib_wicon / 16) * c->ib.ib_hicon * 2);
        at += sizeof(ICONBLK) + 4;      /* the mono bits... */
        c->ib.ib_pdata = at;
        at += bytes;                    /* ...the mono mask... */
        c->ib.ib_pmask = at;
        at += bytes;                    /* ...the text, copied near... */
        far_get((uint8_t *)c->text, at, CICON_TEXT);
        c->text[CICON_TEXT - 1] = 0;
        c->ib.ib_ptext = (uint16_t)c->text;
        at += CICON_TEXT;               /* ...and the colour forms */
        c->cicons = num ? at : 0;
        for (k = 0; k < num; k++) {     /* step over each: planes is the
                                         * high word of the first LONG */
            uint16_t planes = (uint16_t)(rd_long(at) >> 16);
            uint32_t sel = rd_long(at + 10);
            at += 22 + (uint32_t)bytes * planes + bytes;
            if (sel)
                at += (uint32_t)bytes * planes + bytes;
        }
    }

    /* the objects: an index becomes the near record's address */
    for (i = 0; i < h->rsh_nobs; i++) {
        OBJECT *obj = addr_of(h, R_OBJECT, i);
        if ((obj->ob_type & 0xFF) == G_CICON) {
            if (obj->ob_spec >= n)
                return 0;
            obj->ob_spec = (uint16_t)&near[obj->ob_spec];
        }
    }
    rs_cibase = base;
    rs_cisize = (uint16_t)ext_len;
    return 1;
}

void rs_ciaddr(uint32_t *base, uint16_t *len)
{
    *base = rs_cibase;
    *len = rs_cisize;
}

WORD rs_load(const char *name)
{
    RSHDR hdr, raw;
    char cio[CIO_NAME_MAX + 1];
    uint16_t got, size, mark;
    int16_t fd;
    uint8_t *mem;
    WORD ok;

    if (rs_1 && rs_2)               /* one resident and one nested is all */
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
    if (size < sizeof hdr) {
        cio_close(fd);
        return 0;
    }
    mark = pool_mark();
    if (rs_1)
        rs_2mark = mark;
    else
        rs_1mark = mark;
    mem = pool_alloc(size, 2);
    if (!mem) {
        cio_close(fd);
        return 0;
    }
    memcpy(mem, &raw, sizeof raw);  /* rs_fixit takes the file as it is */
    {
        uint8_t st = cio_read(fd, mem + sizeof raw, (uint16_t)(size - sizeof raw), &got);
        if ((st != CIO_OK && st != CIO_OK_EOF) || got != size - sizeof raw) {
            cio_close(fd);
            pool_release(mark);
            return 0;
        }
    }
    if (rs_1)
        rs_2 = (RSHDR *)mem;    /* nested: rs_hdr now answers with it */
    else
        rs_1 = (RSHDR *)mem;
    rs_fixit(rs_hdr);
    rs_imfar(mem, hdr.rsh_imdata, size);
    /* the colour icons, if the file has the extension for them: the fd
     * is still positioned just past the rsh_rssize bytes */
    ok = (hdr.rsh_vrsn & NEW_FORMAT_RSC) ? rs_cicons(fd, rs_hdr, size) : 1;
    cio_close(fd);
    if (!ok) {                      /* refused whole: nothing half-loaded */
        WORD slot = rs_2 ? 2 : 1;
        if (rs_2)
            rs_2 = 0;
        else
            rs_1 = 0;
        rs_farback(slot);
        pool_release(mark);
        return 0;
    }
    return 1;
}

RSHDR *rs_loaded(void)
{
    return rs_hdr;
}

WORD rs_free(void)
{
    if (rs_2) {                     /* the nested one first: LIFO */
        pool_release(rs_2mark);
        rs_2 = 0;
        rs_farback(2);
        return 1;
    }
    if (!rs_1)
        return 0;
    pool_release(rs_1mark);
    rs_1 = 0;
    rs_farback(1);
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
