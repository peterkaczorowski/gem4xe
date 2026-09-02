/* xem1_sim.c -- runs the XEM1 decoder over every pair of readings in the
 * compiler's simulator, for tests/host/test_pointer.py to compare against
 * tools/vdiref.py.  The adapter is not emulated by Altirra, so this is the
 * only place the target's decode is exercised. */
#include <stdint.h>
#include "vdi/pointer.h"

uint32_t r_hash;        /* over ptr_decode_xem1(ref, now), 64 <= ref, now < 192 */
uint32_t r_valid;       /* over ptr_xem1_valid(pot), 0 <= pot < 256 */
int16_t  r_wrap;        /* 190 -> 65: three counts across the wrap, halved */
int16_t  r_neg;         /* 128 -> 125 */

int main(void)
{
    uint32_t h = 0, v = 0;
    uint16_t ref, now;

    for (ref = 0; ref < 256; ref++)
        v = v * 3u + (uint32_t)ptr_xem1_valid((uint8_t)ref);
    for (ref = 64; ref < 192; ref++)
        for (now = 64; now < 192; now++)
            h = h * 31u + (uint8_t)ptr_decode_xem1((uint8_t)ref, (uint8_t)now);
    r_hash = h;
    r_valid = v;
    r_wrap = ptr_decode_xem1(190, 65);
    r_neg = ptr_decode_xem1(128, 125);
    return 0;
}
