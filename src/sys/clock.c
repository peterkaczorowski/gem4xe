/* clock.c -- the Ultimate 1MB's DS1305, read a bit at a time.  See clock.h.
 *
 * THE WIRING is one register, $D3E2, and it is the U1MB's rather than the
 * Atari's: a write drives the chip's three input lines and a read gives
 * back its one output line.
 *
 *   write   bit 0   CE      the chip is selected while this is 1
 *           bit 1   SCLK    INVERTED: a 1 here is the clock LOW
 *           bit 2   DATA    the bit going in
 *   read    bit 3   the bit coming out
 *
 * THE PROTOCOL is the DS1305's SPI: raise CE, clock in eight address bits
 * most significant first, then clock out the data.  An address under $80
 * is a read and the chip advances it by itself, so seven clocked bytes
 * from address 0 are the seconds, minutes, hours, day, date, month and
 * year -- all BCD.
 *
 * Which edge does what is the part worth writing down, because it is not
 * symmetrical: the level the clock is at when CE goes up is the resting
 * level, a move AWAY from it presents the next output bit, and the move
 * BACK to it takes the next input bit.  (Altirra's rtcds1305.cpp is where
 * that was read; the DS1305 data sheet says the same in more words.)
 *
 * ON REAL HARDWARE the chip has hold times the Atari's own 1.79 MHz bus
 * satisfies by being slow.  $D3E2 is I/O, so every access here goes at
 * that speed however fast the CPU is -- but Altirra's manual warns that
 * an accelerator can still violate DS1305 timing, so each edge is held
 * for a few cycles rather than trusting that.  Nothing here has run on a
 * real Ultimate 1MB.
 */
#include "clock.h"

#define RTCOUT  0xD3E2
#define RTC_CE   0x01
#define RTC_LOW  0x02           /* the clock LOW: the bit is inverted */
#define RTC_DATA 0x04
#define RTC_IN   0x08           /* the bit coming back */

#define RTC_YEAR_PIVOT 80       /* 80..99 are 19xx, 00..79 are 20xx */

static volatile uint8_t *const rtc = (volatile uint8_t *)RTCOUT;

/* A moment, for a chip that was specified when a fast machine was 8 MHz.
 * Reading the register is a bus cycle the compiler cannot elide. */
/* A moment, and 16-BIT counters throughout this file: Calypsi puts two
 * byte locals in one slot often enough that tools/ccbug has a rule about
 * it, and here it cost four of the eight address bits -- the clock came
 * back four bits out of step, which is a hard thing to see and an easy
 * thing to avoid. */
static void hold(void)
{
    uint16_t i;
    for (i = 0; i < 4; i++)
        (void)*rtc;
}

static void out(uint16_t v)
{
    *rtc = (uint8_t)v;
    hold();
}

/* Eight bits in, most significant first: the address, or a command. */
static void send(uint16_t v)
{
    uint16_t i;

    for (i = 0; i < 8; i++) {
        uint16_t d = (v & 0x80) ? RTC_DATA : 0;
        v = (uint16_t)((v << 1) & 0xFF);
        out(RTC_CE | RTC_LOW | d);              /* the bit, clock at rest */
        out(RTC_CE | d);                        /* away from rest */
        out(RTC_CE | RTC_LOW | d);              /* and back: the chip takes it */
    }
}

/* Eight bits out, most significant first. */
static uint8_t recv(void)
{
    uint16_t i, v = 0;

    for (i = 0; i < 8; i++) {
        out(RTC_CE);                            /* away from rest: it presents */
        v = (uint16_t)((v << 1) | ((*rtc & RTC_IN) ? 1 : 0));
        out(RTC_CE | RTC_LOW);                  /* back to rest */
    }
    return (uint8_t)v;
}

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t plausible(const uint8_t *r)
{
    uint16_t i;

    for (i = 0; i < 7; i++)                     /* BCD, or no chip at all */
        if ((r[i] & 0x0F) > 9 || ((r[i] >> 4) & 0x0F) > 9)
            return 0;
    return (uint8_t)(bcd(r[0]) < 60 && bcd(r[1]) < 60 && bcd((uint8_t)(r[2] & 0x3F)) < 24
                     && bcd(r[4]) >= 1 && bcd(r[4]) <= 31
                     && bcd(r[5]) >= 1 && bcd(r[5]) <= 12);
}

uint8_t clock_read(CLOCK *c)
{
    uint8_t r[7];
    uint16_t i;

    c->second = c->minute = c->hour = 0;
    c->day = c->month = 1;
    c->year = 1980;
    c->present = 0;

    out(0);                                     /* CE low: the chip resets */
    out(RTC_CE | RTC_LOW);                      /* select, clock at rest */
    send(0x00);                                 /* read from the seconds */
    for (i = 0; i < 7; i++)
        r[i] = recv();
    out(0);

    if (!plausible(r))
        return 0;
    c->second = bcd(r[0]);
    c->minute = bcd(r[1]);
    c->hour = bcd((uint8_t)(r[2] & 0x3F));      /* 24-hour; bit 6 is the mode */
    c->day = bcd(r[4]);
    c->month = bcd(r[5]);
    c->year = (uint16_t)(bcd(r[6]) < RTC_YEAR_PIVOT ? 2000 + bcd(r[6])
                                                    : 1900 + bcd(r[6]));
    c->present = 1;
    return 1;
}
