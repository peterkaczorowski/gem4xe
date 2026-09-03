/* cio.h -- the Atari OS's CIO, from gem4xe.
 *
 * gem4xe has no file system of its own and wants none: DOS is resident,
 * and every disk it can read -- ATR, SIO2SD, a hard disk behind a PBI --
 * is behind CIO's D: device.  What is here is the thin layer over an
 * IOCB: pick a free one, fill it in, call, read the status back.  The
 * call itself is the round trip into emulation mode that the OS needs
 * (src/sys/cio.s), which is why every CIO call costs what a mode switch
 * costs and why the layer above -- rsrc_load, fsel_input, shel_find --
 * reads in big pieces rather than a byte at a time.
 *
 * Addresses are the OS's, as the Altirra source has them
 * (src/h/at/atcore/ksyms.h, cio.h): the IOCBs at $0340, sixteen bytes
 * each, eight of them; IOCB 0 is the OS's own, E:, and is left alone.
 *
 * Buffers and names must be in bank $00 -- CIO takes a 16-bit address.
 * The small data model's plain pointers are bank $00 by construction, so
 * the types say so.  Names are copied into a buffer here with the EOL the
 * OS wants appended; a name without a device ("TEST.RSC") gets "D:".
 *
 * Statuses are the OS's own: 1 is success, $88 end of file, $AA file
 * not found (cio.h in the Altirra source lists them all).  The functions
 * return the status; cio_open returns the IOCB number instead, or the
 * status negated, so that a caller can tell the two apart.
 */
#ifndef GEM4XE_CIO_H
#define GEM4XE_CIO_H

#include <stdint.h>

/* ICCOM */
#define CIO_OPEN     0x03
#define CIO_GETREC   0x05
#define CIO_GETCHR   0x07
#define CIO_PUTREC   0x09
#define CIO_PUTCHR   0x0B
#define CIO_CLOSE    0x0C
#define CIO_STATUS   0x0D

/* ICAX1 at open */
#define CIO_A_READ   0x04
#define CIO_A_DIR    0x06       /* the directory, as lines */
#define CIO_A_WRITE  0x08
#define CIO_A_APPEND 0x09
#define CIO_A_UPDATE 0x0C

/* ICSTA */
#define CIO_OK       0x01
#define CIO_OK_EOF   0x03       /* success, and the next read would not be */
#define CIO_E_BREAK  0x80
#define CIO_E_INUSE  0x81
#define CIO_E_NODEV  0x82
#define CIO_E_NOTOPEN 0x85
#define CIO_E_EOF    0x88
#define CIO_E_TRUNC  0x89       /* a record longer than the buffer */
#define CIO_E_NOTFOUND 0xAA

#define CIO_EOL      0x9B

#define CIO_NAME_MAX 31         /* the longest name the buffer holds:
                                   "D8:XXXXXXXX.XXX" is 15, and bank $00
                                   is short of room for more than a
                                   modest SpartaDOS path */

/* The IOCB the OS lays out at $0340 (ksyms.h). */
typedef struct {
    uint8_t  ichid;             /* $FF when free */
    uint8_t  icdno;
    uint8_t  iccom;
    uint8_t  icsta;
    uint16_t icbal;             /* buffer address */
    uint16_t icptl;
    uint16_t icbll;             /* buffer length; bytes moved on return */
    uint8_t  icax1, icax2, icax3, icax4, icax5, icax6;
} IOCB;

#define CIO_IOCBS   8
#define CIO_IOCB    ((volatile IOCB *)0x0340)

/* Open `name` (NUL-terminated, C style; a missing device becomes D:) with
 * ICAX1 = aux1, ICAX2 = aux2.  Returns the IOCB number, 1..7, or the
 * status negated: -CIO_E_INUSE when no IOCB is free. */
int16_t  cio_open(const char *name, uint8_t aux1, uint8_t aux2);
uint8_t  cio_close(int16_t iocb);

/* Up to `len` bytes into `buf`; *got is how many arrived, which is fewer
 * only at the end of the file (the status then says CIO_E_EOF, and the
 * bytes before it are good). */
uint8_t  cio_read(int16_t iocb, void *buf, uint16_t len, uint16_t *got);

/* One EOL-terminated record into `buf`, EOL included, at most `len`
 * bytes; *got as above. */
uint8_t  cio_getrec(int16_t iocb, void *buf, uint16_t len, uint16_t *got);

uint8_t  cio_write(int16_t iocb, const void *buf, uint16_t len);
uint8_t  cio_status(int16_t iocb);

/* The bare call: the IOCB is filled in already.  src/sys/cio.s. */
__attribute__((simple_call)) uint16_t cio_call(uint16_t iocb);

extern uint16_t cio_calls;      /* round trips made */
extern uint8_t  cio_last;       /* the last status */

#endif /* GEM4XE_CIO_H */
