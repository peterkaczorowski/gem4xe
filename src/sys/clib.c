/* clib.c -- the eight C library functions gem4xe uses, so that it uses
 * nobody else's.
 *
 * WHY THIS EXISTS, AND IT IS NOT ABOUT CODE.
 *
 * Calypsi's C library carries these as `libs/libc/string/lib_*.c`, taken
 * from Apache NuttX and licensed Apache 2.0 -- which is INCOMPATIBLE
 * WITH GPLv2, the licence this tree is under and the one it inherits
 * from EmuTOS.  Linking them put Apache-2.0 object code inside GEM.COM,
 * and there are only two ways out of that: distribute the result under
 * GPLv3, which Apache 2.0 is compatible with and which the donor's AES
 * files permit only by an argument about what silence means; or link
 * none of it.  This is linking none of it.  It is also what the project
 * plan recommended on size grounds before the licence was looked at.
 *
 * WHAT IS LEFT AFTER THIS, so that nobody reads a clean link as a clean
 * licence: the compiler's own runtime -- _Dp, _Mul16, _UDivMod16,
 * _JmpIndLong, _ValueSwitch16, _MoveLongNear, __memcpy_far, _FillDP2,
 * __initialize_sections and the startup -- is still the library's, 815
 * bytes of it, under "Permission to use with the Calypsi tool chain is
 * hereby granted".  That is a smaller problem than this one was and of a
 * different kind: the tool chain's licence expressly permits producing
 * software for retro machines, so the intent is plain, and what does not
 * close is the GPL's own requirement that the whole work be
 * GPL-licensable -- the gap GCC's Runtime Library Exception fills.
 * docs/licence.md has it in full.
 *
 * These are the ISO C functions, written to the standard's own wording
 * and nothing else's.  They are all cold: six strlen, five strcpy, two
 * memset, one each of the rest across the whole engine, and none in a
 * drawing path -- so a byte at a time is the right shape, and they go in
 * `farcode` with the rest of the C, costing bank $00 nothing.
 */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;

    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    char *d = (char *)dst;

    while (n--)
        *d++ = (char)c;
    return dst;
}

size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;

    while ((*d++ = *src++) != 0)
        ;
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;

    while (*d)
        d++;
    while ((*d++ = *src++) != 0)
        ;
    return dst;
}

/* The comparisons are on UNSIGNED chars, which the standard requires and
 * which matters here: a filename from a DOS can hold $80-$FF, and a
 * signed compare would order those before every letter. */
int strcmp(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (*p && *p == *q) {
        p++;
        q++;
    }
    return (int)*p - (int)*q;
}

int strncmp(const char *a, const char *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (n && *p && *p == *q) {
        p++;
        q++;
        n--;
    }
    return n ? (int)*p - (int)*q : 0;
}

/* The terminator counts as part of the string, so strchr(s, 0) finds it. */
char *strchr(const char *s, int c)
{
    char want = (char)c;

    for (;;) {
        if (*s == want)
            return (char *)s;
        if (!*s)
            return 0;
        s++;
    }
}
