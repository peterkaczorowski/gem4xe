/* m32_kid.c -- the child test-m32's program runs with Pexec (src/m32_con.c).
 *
 * It writes "kid " and the tail it was given, as shel_read hands it over,
 * to its handle 1 -- which its parent has forced onto a file -- and ends
 * with things still taken for its end to give back: a file open, a handle
 * duplicated, far memory allocated.  Pterm(5) is its answer; 9 is what
 * main() would return if Pterm came back.
 */
#include "portab.h"
#include "gem.h"

static char cmd[128], tail[128];

int main(void)
{
    shel_read(cmd, tail);
    Cconws("kid ");
    Fwrite(1, (LONG)(uint8_t)tail[0], tail + 1);
    Cconws("\r\n");
    Fopen("A:\\TEST.TXT", 0);
    Fdup(1);
    Malloc(4096L);
    Pterm(5);
    return 9;
}
