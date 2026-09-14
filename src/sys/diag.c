/* diag.c -- the marks GEMDIAG.COM makes; src/sys/diag.h says why. */
#include "sys/diag.h"
#include "sys/rapidus.h"

#define CONSOL  (*(volatile uint8_t *)0xD01F)
#define VCOUNT  (*(volatile uint8_t *)0xD40B)
#define AUDF4   (*(volatile uint8_t *)0xD206)
#define AUDC4   (*(volatile uint8_t *)0xD207)
#define SAVMSC  (*(volatile uint16_t *)0x0058)

uint8_t diag_keys;

/* Screen codes for 0-9, A-F: ATASCII less $20, inverse. */
static const uint8_t mark_code[16] = {
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6
};

void diag_init(void)
{
    uint16_t k = CONSOL;
    diag_keys = (uint8_t)(~k & 0x07);
}

/* n frames, by VCOUNT alone: it runs with or without ANTIC's DMA and with
 * or without an interrupt regime.  Read into a word, never spun on as a
 * byte -- cc65816 5.18 miscompiles the byte loop at -O2 (tools/ccbug B16). */
static void frames(uint8_t n)
{
    uint16_t v;
    while (n--) {
        do { v = VCOUNT; } while (v == 0);
        do { v = VCOUNT; } while (v != 0);
    }
}

/* ATASCII to a screen code, the OS's own arithmetic. */
static uint8_t screen_code(uint8_t c)
{
    uint8_t v = (uint8_t)(c & 0x7F);
    if (v < 0x20)
        v = (uint8_t)(v + 0x40);
    else if (v < 0x60)
        v = (uint8_t)(v - 0x20);
    return (uint8_t)(v | (c & 0x80));
}

static void hex_at(volatile uint8_t *at, uint8_t v)
{
    at[0] = mark_code[v >> 4];
    at[1] = mark_code[v & 0x0F];
}

#define DIAG_COL 18

void diag_rapidus(void)
{
    volatile uint8_t *top = (volatile uint8_t *)SAVMSC + DIAG_COL;
    uint16_t i;

    top[0] = screen_code(rapidus.present ? 'R' : '-');
    top[1] = 0;                                 /* a blank, over the DOS banner */
    for (i = 0; i < 8; i++)
        top[2 + i] = screen_code(rapidus_reg_read(RAP_SIG + i));
    if (rapidus.present) {
        hex_at(top + 11, rapidus.mcr_before);
        hex_at(top + 14, rapidus.cmcr_before);
    }
}

void diag_mark(uint8_t n)
{
    volatile uint8_t *top = (volatile uint8_t *)SAVMSC;
    uint16_t k;

    top[n & 0x0F] = mark_code[n & 0x0F];
    AUDF4 = (uint8_t)(200 - (n & 0x0F) * 12);     /* 159 Hz up to 1.5 kHz */
    AUDC4 = 0xA8;                                 /* pure tone, half volume */
    frames(6);
    AUDC4 = 0;
    if (diag_keys & DIAG_STEP) {
        do { k = CONSOL; } while ((k & 0x01) == 0);   /* START up ...  */
        do { k = CONSOL; } while (k & 0x01);          /* ... then down */
        frames(3);
    }
}
