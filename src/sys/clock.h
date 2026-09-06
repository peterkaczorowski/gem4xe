/* clock.h -- the machine's clock, for GEMDOS's Tgetdate and Tgettime.
 *
 * The Atari has no clock of its own.  The one this machine has is on the
 * Ultimate 1MB: a DS1305, bit-banged through a single register at $D3E2
 * (docs/phase15.md).  A machine without one says so and GEMDOS answers
 * the ST's epoch, 1 January 1980, which is what a TOS with a dead clock
 * answers as well.
 */
#ifndef GEM4XE_CLOCK_H
#define GEM4XE_CLOCK_H

#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour;       /* 0-59, 0-59, 0-23 */
    uint8_t day, month;                 /* 1-31, 1-12 */
    uint16_t year;                      /* 1980..2079 */
    uint8_t present;                    /* 0: no clock answered */
} CLOCK;

/* Read it.  Answers 0 and leaves `c` at the epoch when no clock is
 * there -- which is a read of a register no hardware is driving, so what
 * comes back is $FF and fails the check that it is a plausible time. */
uint8_t clock_read(CLOCK *c);

#endif /* GEM4XE_CLOCK_H */
