/* gacsrun.c -- GACS's engine, on the 65816, doing its real work.
 *
 * GACS (the GURPS Autoduel Construction Set, ~/dev/gacs) is one of the two
 * applications gem4xe exists for.  Its engine is strict C89 with no I/O and
 * no floating point, which is exactly the shape that ports; this is the
 * program that proves it: parse the six tables GACS ships, build a design
 * out of the first chassis, engine, tire and suspension they name, compute
 * it, and print the line GACS's own CLI prints.
 *
 * EVERYTHING THE ENGINE TOUCHES IS FAR.  ad_tables alone is 22 KB and the
 * sheet is another 33; a gem4xe application has 2 KB of bank $00 and asks
 * GEMDOS's Malloc for the rest, which answers with far memory.  So this
 * compiles --data-model=large, where a pointer is 24 bits, and puts the
 * working set in far RAM -- the arrangement an application would use.
 *
 * The tables travel with the program rather than through the debugger's
 * semi-hosted file system (tools/gacs/gacsdata.c, generated): one moving
 * part fewer, and it makes the run the same everywhere.
 *
 * Driven by tools/gacscheck.py; `make gacs-check`.
 */
#include <stdio.h>
#include <string.h>
#include "autoduel.h"

#define BUFCAP 6144        /* the biggest .DAT is 5,672 bytes */

static char __far  tables_mem[sizeof(ad_tables)];
static char __far  design_mem[sizeof(ad_design)];
static char __far  stats_mem[sizeof(ad_stats)];
static char __far text0[BUFCAP];
static char __far text1[BUFCAP];
static char __far text2[BUFCAP];
static char __far text3[BUFCAP];
static char __far text4[BUFCAP];
static char __far text5[BUFCAP];
static char __far *const text[6] = { text0, text1, text2, text3, text4, text5 };
static char        line[200];

static const char *files[6] = { "CHASSIS.DAT", "ENGINES.DAT", "TIRES.DAT",
                                "SUSPEN.DAT", "WEAPONS.DAT", "ACCESS.DAT" };
extern const char __far *const gacs_tables[6];
extern const unsigned gacs_sizes[6];
static const int kinds[6] = { AD_T_CHASSIS, AD_T_ENGINE, AD_T_TIRE,
                              AD_T_SUSPEN, AD_T_WEAPON, AD_T_ACCESS };

/* The tables travel with the program rather than through the debugger's
   semi-hosted file system, which is one moving part fewer.  Parsing works
   IN the buffer, so each one is copied into far RAM first. */
static int slurp(int which, char __far *buf, int cap)
{
    unsigned n = gacs_sizes[which], i;
    if ((int)n >= cap)
        return -1;
    for (i = 0; i < n; i++)
        buf[i] = gacs_tables[which][i];
    buf[n] = '\0';
    return (int)n;
}

int main(void)
{
    ad_tables *t = (ad_tables *)tables_mem;
    ad_design *d = (ad_design *)design_mem;
    ad_stats  *s = (ad_stats *)stats_mem;
    int i, n;

    ad_tables_init(t);
    for (i = 0; i < 6; i++) {
        n = slurp(i, text[i], BUFCAP);
        if (n < 0) {
            printf("cannot read %s\n", files[i]);
            return 1;
        }
        if (ad_tables_parse((char *)text[i], kinds[i], t) != AD_OK) {
            printf("malformed %s\n", files[i]);
            return 1;
        }
        printf("%-12s %5d bytes\n", files[i], n);
    }
    printf("tables: %d chassis, %d engines, %d tires, %d weapons\n",
           (int)t->nchassis, (int)t->nengine, (int)t->ntire, (int)t->nweapon);

    ad_design_init(d, 0);
    strcpy(d->name, "GEM4XE");
    d->body = &t->chassis[0];
    d->engine = &t->engine[0];
    d->tire_front = d->tire_rear = &t->tire[0];
    d->suspen = &t->suspen[0];
    d->crew = 1;
    printf("design: %s / %s / %s\n", d->body->id, d->engine->id,
           d->tire_front->id);
    printf("ad_compute -> %d\n", (int)ad_compute(d, s));
    ad_1e_format_line(d, s, line, (int)sizeof line);
    printf("%s\n", line);
    return 0;
}
