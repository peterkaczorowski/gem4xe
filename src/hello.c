/* gem4xe milestone 1: prove the Calypsi toolchain reaches the Atari.
   Writes a signature and a computed value to a fixed address the test
   harness reads back over the Altirra bridge.  No libc, no stdio. */

#define SIG ((volatile unsigned char *) 0x0600)  /* page 6: free on the Atari and outside every linker region */

/* Something the compiler cannot constant-fold away into the signature:
   a real 16-bit multiply-accumulate loop. */
static unsigned int checksum(void)
{
    unsigned int i, acc = 0;
    for (i = 1; i <= 100; i++)
        acc += i * i;               /* sum i^2, i=1..100 = 338350; low 16 bits = 10670 */
    return acc;
}

__task void main(void)
{
    unsigned int c = checksum();

    SIG[0] = 'G';  SIG[1] = 'E';  SIG[2] = 'M';  SIG[3] = '4';
    SIG[4] = (unsigned char)(c & 0xFF);
    SIG[5] = (unsigned char)(c >> 8);
    SIG[6] = sizeof(int);           /* expect 2 -- Calypsi int is 16-bit */
    SIG[7] = sizeof(void *);        /* expect 2 in the small data model  */

    for (;;)                        /* park: DOS has nothing to return to */
        ;
}
