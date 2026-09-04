/* cio.c -- IOCBs filled in and CIO called.  See cio.h. */
#include "sys/cio.h"
#include "sys/irq.h"

uint16_t cio_calls;
uint8_t  cio_last;

/* The name CIO reads, in bank $00: "D:" + name + EOL at most. */
static char cio_name[CIO_NAME_MAX + 4];

static uint8_t call(int16_t iocb)
{
    uint8_t st;
    cio_calls++;
    st = (uint8_t)cio_call((uint16_t)iocb);
    irq_pokey_resync();             /* SIO had POKEY; the sampler wants it back */
    cio_last = st;
    return st;
}

static uint8_t is_digit(char c) { return (uint8_t)(c >= '0' && c <= '9'); }

/* A device spec is "X:" or "Xn:" -- a letter, an optional digit, a colon. */
static uint8_t has_device(const char *name)
{
    if (!name[0])
        return 0;
    if (name[1] == ':')
        return 1;
    return (uint8_t)(is_digit(name[1]) && name[2] == ':');
}

/* The name into cio_name, "D:" prefixed when it names no device. */
static void put_name(const char *name)
{
    uint8_t i = 0;
    if (!has_device(name)) {
        cio_name[i++] = 'D';
        cio_name[i++] = ':';
    }
    while (*name && i < CIO_NAME_MAX)
        cio_name[i++] = *name++;
    cio_name[i] = (char)CIO_EOL;
}

/* A free IOCB, or -1. */
static int16_t free_iocb(void)
{
    int16_t n;
    for (n = 1; n < CIO_IOCBS; n++)
        if (CIO_IOCB[n].ichid == 0xFF)
            return n;
    return -1;
}

int16_t cio_open(const char *name, uint8_t aux1, uint8_t aux2)
{
    volatile IOCB *io;
    int16_t n = free_iocb();

    if (n < 0)
        return -(int16_t)CIO_E_INUSE;
    put_name(name);
    io = &CIO_IOCB[n];
    io->iccom = CIO_OPEN;
    io->icbal = (uint16_t)cio_name;
    io->icbll = 0;
    io->icax1 = aux1;
    io->icax2 = aux2;
    {
        uint8_t st = call(n);
        if (st != CIO_OK) {
            /* A failed open leaves the IOCB claimed -- ICHID is the
             * handler's, not $FF -- until it is closed (seen: the next
             * open would have taken the next IOCB). */
            cio_close(n);
            return -(int16_t)st;
        }
    }
    return n;
}

uint8_t cio_close(int16_t iocb)
{
    CIO_IOCB[iocb].iccom = CIO_CLOSE;
    return call(iocb);
}

static uint8_t xfer(int16_t iocb, uint8_t cmd, const void *buf, uint16_t len,
                    uint16_t *got)
{
    volatile IOCB *io = &CIO_IOCB[iocb];
    uint8_t st;
    io->iccom = cmd;
    io->icbal = (uint16_t)buf;
    io->icbll = len;
    st = call(iocb);
    if (got)
        *got = io->icbll;
    return st;
}

uint8_t cio_read(int16_t iocb, void *buf, uint16_t len, uint16_t *got)
{
    return xfer(iocb, CIO_GETCHR, buf, len, got);
}

uint8_t cio_getrec(int16_t iocb, void *buf, uint16_t len, uint16_t *got)
{
    return xfer(iocb, CIO_GETREC, buf, len, got);
}

uint8_t cio_write(int16_t iocb, const void *buf, uint16_t len)
{
    return xfer(iocb, CIO_PUTCHR, buf, len, 0);
}

uint8_t cio_status(int16_t iocb)
{
    CIO_IOCB[iocb].iccom = CIO_STATUS;
    return call(iocb);
}

uint8_t cio_xio(uint8_t cmd, const char *name, uint8_t aux1, uint8_t aux2)
{
    volatile IOCB *io;
    int16_t n = free_iocb();
    uint8_t st;

    if (n < 0)
        return CIO_E_INUSE;
    put_name(name);
    io = &CIO_IOCB[n];
    io->iccom = cmd;
    io->icbal = (uint16_t)cio_name;
    io->icbll = 0;
    io->icax1 = aux1;
    io->icax2 = aux2;
    st = call(n);
    /* CIO serves a special command on a closed IOCB by opening it to the
     * device for the call; whether it is left claimed afterwards is the
     * handler's business, so it is closed here regardless -- closing a
     * free IOCB is a status, not a fault. */
    if (io->ichid != 0xFF)
        cio_close(n);
    return st;
}
