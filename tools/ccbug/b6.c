/* B6 -- any variable-indexed access to a direct-page array is an internal
 * compiler error, not a miscompile, so this file is compiled on its own
 * and the outcome read from the compiler rather than the simulator:
 *
 *   internal error: Translator/Target/WDC65816/Compiler/CGHelpers.hs:
 *   (662,1)-(663,59): Non-exhaustive patterns in function mem8Reg
 *
 * Element type (uint8_t or uint16_t) and index source (a direct-page
 * scalar, a parameter, a plain global) make no difference; a constant
 * index compiles, and so does an ordinary array indexed by a direct-page
 * scalar, which is what the sources use (line_diag, src/vdi/vdi.c). */
#include <stdint.h>

static __attribute__((tiny)) uint8_t tbl[4];
static __attribute__((tiny)) int i;

uint8_t r_b6_bug(void)
{
    return tbl[i];
}
