/* gemdos.h -- GEMDOS, for the applications: the ST's trap #1 on CIO.
 *
 * GEM on the ST is VDI and AES over GEMDOS, and a GEM application --
 * the Desktop first of all -- asks GEMDOS for its files: Fsfirst and
 * Fsnext to list a directory, Fopen/Fread/Fwrite/Fclose, Dsetpath,
 * Dcreate, Frename, Malloc.  gem4xe has CIO behind it (src/sys/cio.h)
 * and one of three DOSes behind that (src/sys/dos.h), so this is the
 * third face of the ABI (src/sys/abi.h): COP #$01 -- the ST's trap
 * number -- with X:C at a block laid out as the ST's stack frame is:
 *
 *     +0  LONG  the result, written back
 *     +4  WORD  the function number
 *     +6  ...   the arguments, in the ST's order and sizes (WORD 2,
 *               LONG 4), so that an ST binding's stack picture IS the
 *               block's picture, four bytes along
 *
 * Handles, paths, the DTA and the error numbers are the ST's, so that
 * the donor's desktop code reads unchanged.  What differs is written
 * down, function by function, in gemdos.c: what CIO has no way to do
 * says EINVFN rather than pretending.
 *
 * PATHS.  The application speaks GEM -- A:\DIR\NAME.EXT, or a name
 * relative to a per-drive current directory GEMDOS keeps -- and the
 * DOS seam turns the composed path into what CIO opens.  A drive is a
 * D: unit: A = D1: .. H = D8:.  Where the DOS has no directories, a
 * path with one is EPTHNF.
 *
 * SEARCHES.  Fsfirst opens the directory once and reads as much of it
 * ahead as a bank-$00 buffer allows -- a CIO call costs six frames on
 * a SpartaDOS whether it moves one entry or forty (measured, Phase
 * 14) -- into a slot in far memory keyed by the DTA; Fsnext walks the
 * slot.  Seven slots; an eighth search takes the oldest.  The DTA is
 * the ST's 44 bytes, in the application's memory wherever it says.
 *
 * MEMORY.  Malloc is the far heap (src/sys/farmem.h): the bump
 * allocator, wound back when the application exits, so Mfree is a
 * no-op and a leak lasts one run.  Malloc(-1) says how much is left.
 */
#ifndef GEM4XE_GEMDOS_H
#define GEM4XE_GEMDOS_H

#include <stdint.h>

/* Function numbers: the ST's. */
#define GD_DSETDRV   0x0E
#define GD_DGETDRV   0x19
#define GD_FSETDTA   0x1A
#define GD_TGETDATE  0x2A
#define GD_TGETTIME  0x2C
#define GD_FGETDTA   0x2F
#define GD_SVERSION  0x30
#define GD_DFREE     0x36
#define GD_DCREATE   0x39
#define GD_DDELETE   0x3A
#define GD_DSETPATH  0x3B
#define GD_FCREATE   0x3C
#define GD_FOPEN     0x3D
#define GD_FCLOSE    0x3E
#define GD_FREAD     0x3F
#define GD_FWRITE    0x40
#define GD_FDELETE   0x41
#define GD_FSEEK     0x42
#define GD_FATTRIB   0x43
#define GD_DGETPATH  0x47
#define GD_MALLOC    0x48
#define GD_MFREE     0x49
#define GD_FSFIRST   0x4E
#define GD_FSNEXT    0x4F
#define GD_FRENAME   0x56
#define GD_FDATIME   0x57

/* Errors: the ST's. */
#define GD_EINVFN   -32L        /* no such function here */
#define GD_EFILNF   -33L        /* file not found */
#define GD_EPTHNF   -34L        /* path not found */
#define GD_ENHNDL   -35L        /* no handle left */
#define GD_EACCDN   -36L        /* access denied: locked, full, exists */
#define GD_EIHNDL   -37L        /* not a handle of ours */
#define GD_ENSMEM   -39L        /* no memory */
#define GD_EDRIVE   -46L        /* no such drive */
#define GD_ENMFIL   -49L        /* no more files */
#define GD_ERANGE   -64L        /* a seek past the end of the file */

/* File attributes, in Fsfirst's mask and the DTA. */
#define FA_RDONLY   0x01
#define FA_HIDDEN   0x02
#define FA_SYSTEM   0x04
#define FA_VOLUME   0x08
#define FA_SUBDIR   0x10
#define FA_ARCHIVE  0x20

/* The DTA: 44 bytes, the ST's layout, the numbers little-endian as the
 * application reads them.  The reserved bytes are GEMDOS's own. */
#define DTA_SIZE    44
#define DTA_ATTRIB  21          /* BYTE */
#define DTA_TIME    22          /* UWORD: h << 11 | m << 5 | s / 2 */
#define DTA_DATE    24          /* UWORD: (y - 1980) << 9 | mo << 5 | d */
#define DTA_LENGTH  26          /* ULONG */
#define DTA_FNAME   30          /* char[14], NAME.EXT */

/* Fopen's modes. */
#define GD_O_READ   0
#define GD_O_WRITE  1
#define GD_O_RDWR   2

/* A handle is its IOCB's number plus this; 0..5 are the ST's standard
 * handles, which gem4xe does not hand out. */
#define GD_HANDLE_BASE 6

/* The call block's size, the largest argument list being Fread's. */
#define GD_PB_SIZE  16

/* The largest sector gd_dfree will read into the pool: SDFS on a CF card
 * (tools/apt.py) is 512, and nothing gem4xe mounts is bigger. */
#define GD_SECMAX   512

/* Once, after dos_ident and farmem_probe and before any application is
 * loaded: the search slots and the per-drive directories are far
 * allocations that must not be in the region an application's exit
 * winds back. */
void gemdos_init(void);

/* One call: the block at `pb`, the result written into it. */
void gemdos_call(uint32_t pb);

/* The application has exited: its handles closed, its searches freed,
 * the DTA back to the default. */
void gemdos_release(void);
/* A name the AES opens, resolved through GEMDOS's current
 * directory when there is one (src/sys/gemdos.c). */
void gd_cioname(const char *name, char *cio);

extern uint16_t gemdos_calls;   /* calls made */
extern uint16_t gemdos_bad;     /* of which EINVFN */

#endif /* GEM4XE_GEMDOS_H */
