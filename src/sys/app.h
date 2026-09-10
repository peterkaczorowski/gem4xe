/* app.h -- loading and running a gem4xe application.
 *
 * An application arrives as a .g4a (tools/mkg4a.py): a near part that
 * wants a page-aligned place in bank $00 -- its direct page, stack and
 * data -- a far part that wants whole banks -- its code -- and four lists
 * of the bytes to patch once both places are known.  app_load() takes the
 * near part from the pool src/gem4xe.scm sets aside (apppool; the bounds
 * come from the linker through src/sys/apppool.s) and the far part from
 * the far heap, copies, patches, and records where everything went.
 * app_exec() calls the application's entry as a far subroutine
 * (src/sys/abi.s) and returns what its main() returned.  app_free() gives
 * both regions back: the pool and the far heap are bump allocators, so an
 * application is released by winding them back to where they were --
 * which is also why applications are released in the reverse order of
 * their loading, or all together.
 *
 * The pool is also the AES's own bank-$00 allocator, for what it must
 * address near and cannot give a static home in a 7.9 KB bank $00: a
 * resource file's objects (src/aes/rsrc.c), the file selector's tree and
 * its work while it is up (src/aes/fsel.c).  Same bump discipline:
 * pool_mark(), take, pool_release() to the mark.
 */
#ifndef GEM4XE_APP_H
#define GEM4XE_APP_H

#include <stdint.h>

#define APP_OK        0
#define APP_E_MAGIC  -1     /* not a G4A, or a version this loader lacks */
#define APP_E_SHORT  -2     /* the blob ends before the header says */
#define APP_E_POOL   -3     /* no room in the bank-$00 pool */
#define APP_E_FAR    -4     /* no far bank */
#define APP_E_FIXUP  -5     /* a fixup offset outside its part */
#define APP_E_FILE   -6     /* the file would not open or read (app_load_file) */

typedef struct {
    uint16_t near_base;     /* where the near part landed, page aligned */
    uint16_t near_size;
    uint32_t far_addr;      /* where the far part landed */
    uint32_t far_size;
    uint32_t entry;         /* __program_start, relocated */
    uint16_t link_near;     /* the link-time bases, so a symbol from the */
    uint8_t  link_bank;     /* link's map translates: addr - link + base */
    uint16_t fixups;        /* patched bytes, all four lists */
    uint16_t pool_mark;     /* what app_free() winds back to */
    uint32_t far_mark;
} APP;

uint16_t pool_mark(void);                          /* the cursor         */
void    *pool_alloc(uint16_t size, uint16_t align); /* 0 when it will not fit */
void     pool_release(uint16_t mark);
uint16_t pool_room(void);

/* Where the last program app_load() placed its near region.  Only a
 * diagnostic -- nothing in the engine reads it -- but the gates need it
 * now that an accessory is loaded before the first program. */
extern uint16_t app_near;                          /* bytes left          */

int16_t app_load(const uint8_t __far *blob, uint32_t len, APP *app);
int16_t app_exec(const APP *app);
void    app_free(const APP *app);

/* A whole file, by its CIO name, into far memory: where it starts, with
 * its length in *len, or 0 when it would not open or read to its end --
 * and then nothing is kept.  It is taken from the far heap, so what
 * app_free() winds back to decides who owns it.  The file is read
 * through a slice of the pool, which is given back before returning. */
uint32_t far_read_file(const char *cioname, uint32_t *len);

/* app_load() on a file: the GEM name (X:\DIR\NAME.G4A, or a bare name on
 * the default drive) through the DOS seam, read whole, then loaded.  The
 * file's bytes are left in far memory below the application's and go
 * back with them at app_free(). */
int16_t app_load_file(const char *gemname, APP *app);

#endif /* GEM4XE_APP_H */
