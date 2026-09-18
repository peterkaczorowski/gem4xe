/* B18 -- a far-pointer byte loop the compiler never finishes compiling.
 *
 * cc65816 5.18.2, --code-model=large --data-model=small, -O1 or -O2: this
 * function does not compile.  The compiler runs until it is killed --
 * `make` was stopped twice by the host's memory guard before the file was
 * bisected -- and it never reports an error, because it never reports
 * anything.  Found 2026-09-18 giving expand_string() a FAR string for
 * docs/far-trees.md.
 *
 * THE TRIGGER IS THE COMBINATION, measured one axis at a time
 * (tools/ccbug/README.md has the matrix): a post-increment read through a
 * `__far` char pointer, narrowed with (uint8_t) IN THE SAME EXPRESSION,
 * inside a loop.  Remove any one and it compiles in under a second:
 *
 *   no cast:          dst[n++] = (WORD)*s++;                    compiles
 *   index, not ++:    dst[n] = (uint8_t)s[n]; n++;              compiles
 *   read to a local:  char c = *s++; dst[n++] = (uint8_t)c;     compiles
 *   recompute:        c = *(const uint8_t __far *)(a + n);      compiles
 *   -O0                                                          compiles
 *   --data-model=large                                           compiles
 *   -O1, -O2, --speed, --no-cross-call                           HANGS
 *
 * The loop's bound makes no difference: `while (*s)` hangs the same way.
 *
 * check.py compiles this file with a timeout and reports "still present"
 * when the compiler is killed by it, which is the only way a bug of this
 * shape can be seen. */
#include <stdint.h>
typedef short WORD;

WORD b18_bug(WORD *dst, const char __far *s)
{
    WORD n = 0;
    while (*s && n < 127)
        dst[n++] = (WORD)(uint8_t)*s++;
    dst[n] = 0;
    return n;
}
