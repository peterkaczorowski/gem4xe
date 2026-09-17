/* B18 -- sizeof is two different numbers, and the wrong one is in the
 * array bound.  See README.md.
 *
 * S is 34 bytes: three 32-bit fields and eleven 16-bit ones.  Its
 * alignment is 4, so 34 is not a multiple of it -- which is the whole
 * trigger.  Generated code uses 34 (a function returning sizeof(S)
 * compiles to `lda ##34`, and &arr[i] scales by 34), but the constant
 * expression in an array bound evaluates the same sizeof as 36.
 *
 * So this file must FAIL to compile while the bug is present: check.py
 * requires the refusal.  The day it compiles, the bug is fixed and the
 * entry can go.
 */
typedef struct { unsigned long a, b, c; short d[11]; } S;

unsigned long b18_in_an_expression(void)
{
    return sizeof(S);               /* 34 */
}

/* ...and this asks the constant-expression evaluator the same question. */
char b18_in_an_array_bound[(sizeof(S) == 34) ? 1 : -1];
