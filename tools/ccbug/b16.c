/* B16 -- a spin loop that compares a volatile byte, when an early return
 * precedes it and 16-bit code follows it, gets its width switch BEFORE
 * its back edge:
 *
 *   ?L11: lda b16_vc ; cmp #19 ; rep #32 ; bcc ?L11
 *
 * The first pass is right.  The second runs `lda` as a word and `cmp
 * #19` as a three-byte instruction that swallows the `rep`'s opcode, so
 * the next thing executed is the branch's own operand -- `90 f7` and
 * whatever follows, on the Atari a `jsr` into the middle of another
 * function.  -O2 only; -O0 and -O1 put the `rep` after the loop.  The
 * first loop here is untouched (its label sits before the `sep`), and
 * without the `if (p) return 0` both compile right, which is why a
 * minimal reproducer misses it.  Met in src/sys/bootinfo.c's VCOUNT
 * polls, which BRKed out of the boot screen's rainbow.
 *
 * This file is compiled alone and its listing read: the shape cannot be
 * run.  The shape the sources use instead -- the byte read into a word
 * by a helper and the word compared -- runs in bugs.c. */
typedef unsigned short WORD;
typedef unsigned char BYTE;

volatile BYTE b16_vc;                       /* VCOUNT */

WORD b16_bug(WORD p)
{
    if (p)
        return 0;
    while (b16_vc >= 19) ;                  /* the frame's wrap */
    while (b16_vc < 19) ;                   /* the top of the logo */
    return 1;
}
