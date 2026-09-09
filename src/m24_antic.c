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
    /* the same eight rows tools/anticref.py has */
    static const uint16_t patt[8] = {
        0xFF00, 0x8080, 0x8080, 0x8080, 0x0FF0, 0x0808, 0x0808, 0x0808
    };

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

    /* -- the writing modes, over a band -------------------------------
     * A solid source makes REPLACE and TRANS the same thing and ERASE
     * nothing at all, which is worth showing: it is what the VBXE
     * driver's paint_rect decides for itself, and the two drivers have
     * to agree about it. */
    antic_rect_mode(20, 32, 299, 44, 1, 1);         /* the band        */
    antic_rect_mode(30, 34, 60, 42, 1, 0);          /* REPLACE pen 0   */
    antic_rect_mode(70, 34, 100, 42, 3, 1);         /* XOR: inverted   */
    antic_rect_mode(110, 34, 140, 42, 4, 0);        /* ERASE: nothing  */
    antic_rect_mode(150, 34, 180, 42, 2, 0);        /* TRANS pen 0     */

    /* -- text, at every one of the eight shifts ------------------------ */
    for (i = 0; i < 8; i++)                         /* REPLACE on clear */
        antic_glyph((uint16_t)('A' + i), (int16_t)(20 + i * 9), 50, 1, 1);
    antic_rect_mode(20, 60, 200, 67, 1, 1);         /* a set ground     */
    for (i = 0; i < 8; i++)                         /* TRANS pen 0      */
        antic_glyph((uint16_t)('a' + i), (int16_t)(20 + i * 9), 60, 2, 0);
    for (i = 0; i < 8; i++)                         /* XOR on clear     */
        antic_glyph((uint16_t)('0' + i), (int16_t)(20 + i * 9), 70, 3, 1);

    /* -- patterned fills, and the styled lines that are the same thing -
     * Two runs side by side: the pattern is aligned to the SCREEN's
     * 16-pixel word, not to the run, so the two carry one continuous
     * pattern across the join at x=149 -- which is the property that
     * makes two adjacent window backgrounds look like one desk. */
    for (i = 0; i < 8; i++) {
        antic_patt_span(30, 148, (int16_t)(152 + i), patt[i], 1, 1);
        antic_patt_span(149, 269, (int16_t)(152 + i), patt[i], 1, 1);
    }
    antic_patt_span(30, 269, 162, 0xF0F0, 1, 1);
    antic_vline(24, 150, 165, 0xCCCC, 1, 1);
    antic_vline(275, 150, 165, 0xAAAA, 1, 1);

    /* -- v_get_pixel, which no picture can check ----------------------- */
    STATUS[4] = antic_get_pixel(0, 0);              /* border: set      */
    STATUS[5] = antic_get_pixel(5, 5);              /* on the diagonal  */
    STATUS[6] = antic_get_pixel(50, 100);           /* inside the bar   */
    STATUS[7] = antic_get_pixel(150, 100);          /* inside its hole  */
    STATUS[8] = antic_get_pixel(120, 38);           /* the ERASE rect   */
    STATUS[9] = antic_get_pixel(80, 38);            /* the XOR rect     */
    STATUS[10] = antic_get_pixel(-1, 5);            /* off the left     */
    STATUS[11] = antic_get_pixel(5, AN_H);          /* off the bottom   */

    STATUS[2] = 'K';                    /* drawn */
    for (;;)
        ;
}
