/* m24_antic.c -- the ANTIC surface milestone: bring up mode F with no
 * VBXE in the machine at all and draw a pattern the host can verify bit
 * for bit.
 *
 * The pattern is chosen for what it can catch, not for how it looks:
 *
 *   the border          the first and last lines, and the leftmost and
 *                       rightmost BYTES, where a run's edge masks are
 *   the wide bar        crosses line 96, which is where ANTIC's memory
 *                       counter wraps a 4 KB boundary -- if the second
 *                       LMS is wrong, the bottom of this bar is drawn
 *                       from the top of the screen and nothing else
 *                       goes wrong enough to notice
 *   the diagonal        one plot a line, so every bit position in a byte
 *                       is exercised and a reversed bit order shows as a
 *                       mirrored line rather than as nothing
 *   the thin bars       runs one pixel wide at odd x, which are the
 *                       single-byte case where both edge masks land in
 *                       the same byte
 *   the comb            runs that start and end inside one byte, at
 *                       every offset, which is the case a span routine
 *                       usually gets wrong at exactly one offset
 */
#include "antic/antic.h"

#define STATUS ((volatile unsigned char *) 0x0600)

__task void main(void)
{
    int16_t i;

    STATUS[0] = 'A';
    STATUS[1] = 'N';
    STATUS[2] = 0;

    antic_init(0x0E, 0x00);             /* white on black */
    antic_clear(0);

    /* the border, one pixel wide */
    antic_hline(0, AN_W - 1, 0, 1);
    antic_hline(0, AN_W - 1, AN_H - 1, 1);
    for (i = 0; i < AN_H; i++) {
        antic_plot(0, i, 1);
        antic_plot(AN_W - 1, i, 1);
    }

    /* the wide bar, across the 4 KB crossing at line 96 */
    antic_rect(40, 80, 279, 120, 1);
    /* ...with a hole punched in it, so the clearing path is exercised
     * over a run that is already set */
    antic_rect(100, 90, 219, 110, 0);

    /* the diagonal */
    for (i = 0; i < AN_H; i++)
        antic_plot(i, i, 1);

    /* thin bars: one pixel wide, every 16th column, down the middle */
    for (i = 8; i < AN_W; i += 16)
        antic_rect(i, 130, i, 150, 1);

    /* the comb: eight runs inside one byte each, one per offset */
    for (i = 0; i < 8; i++)
        antic_hline((int16_t)(200 + 16 * i), (int16_t)(200 + 16 * i + i),
                    (int16_t)(20 + i), 1);

    STATUS[2] = 'K';                    /* drawn */
    for (;;)
        ;
}
