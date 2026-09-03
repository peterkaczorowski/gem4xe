/* bugs.c -- the cc65816 5.18 code generation bugs gem4xe works around.
 *
 * Every bug is a pair of shapes: the one that miscompiles and the one the
 * sources use instead.  check.py builds this with the vendor's own minimal
 * linker script and C library, runs it in db65816's simulator, and reads the
 * results back through the volatile globals.  A bug that has gone away is
 * news (the workaround can go); a workaround shape that has stopped working
 * is a failure, because gem4xe is built on it.
 *
 * The shapes are lifted from where each bug was met: everyobj() in
 * src/aes/objc.c (B1), gsx_tcalc() in src/aes/graf.c (B2, B3), ob_sst()
 * in src/aes/objc.c (B4), vdi_vrt_cpyfm() in src/vdi/vdi.c (B5), the BCB
 * overlay in src/vbxe/vbxe.c (B7) and sh_cioname() in src/aes/shel.c (B8).
 * Keep them recognisable rather than minimal.
 */
#include <stdint.h>

typedef short WORD;

/* ---- B1: stack array element + operand loads instead of adds ---------- */

typedef struct { WORD pad[8]; WORD ob_x, ob_y; } OBJ;
OBJ tree[3];

/* `x[depth] = x[depth-1] + tree[this].ob_x` becomes `ldy ob_x; lda (&x),y`
 * -- a load from address &x[depth-1] + ob_x, not an add. */
static WORD b1_bug(OBJ *t, WORD this, WORD depth, WORD startx)
{
    WORD x[12];
    x[0] = startx;
    x[depth] = (WORD)(x[depth - 1] + t[this].ob_x);
    return x[depth];
}

/* The element through a scalar first. */
static WORD b1_fix(OBJ *t, WORD this, WORD depth, WORD startx)
{
    WORD x[12], prev;
    x[0] = startx;
    prev = x[depth - 1];
    x[depth] = (WORD)(prev + t[this].ob_x);
    return x[depth];
}

WORD b3_len(const char *s) { WORD n = 0; while (*s++) n++; return n; }

/* ---- B2: the flags after _Div16 / _Mod16 are not the result's ---------- */

/* At -O1 and above `if (a / b)` is `jsl _Div16; beq`.  The library leaves N
 * and Z from the sign word (dividend ^ divisor) on the non-negative path, so
 * 8/8 tests as zero and 7/8 as non-zero; _Mod16 likewise tests the dividend.
 * The fix is a replacement _Div16/_Mod16 (src/sys/div16.s, linked with
 * --override), so there is no source-level "fixed" shape: check.py links
 * this program both ways. */
volatile WORD b2_hc = 8;

WORD b2_div_truth(const char *s, WORD *ph)
{
    WORD n = b3_len(s);
    if (*ph / b2_hc)            /* gsx_tcalc: does one line of text fit? */
        return n;
    return 0;
}
WORD b2_mod_truth(const char *s, WORD *ph)
{
    WORD n = b3_len(s);
    if (*ph % b2_hc)
        return n;
    return 0;
}

/* ---- B3: `*out = c ? a : b` in an inlined static function ------------- */

/* The conditional store lands in a dead stack slot; *pn is never written. */
static void b3_bug_callee(const char *s, WORD *pw, WORD *pn)
{
    WORD n = b3_len(s), m = *pw;
    *pn = (n < m) ? n : m;
}
WORD b3_bug(const char *s, WORD *pw, WORD *ph)
{
    WORD num;
    (void)ph;
    b3_bug_callee(s, pw, &num);
    return num;
}

/* Return the value instead. */
static WORD b3_fix_callee(const char *s, WORD *pw)
{
    WORD n = b3_len(s), m = *pw;
    return (n < m) ? n : m;
}
WORD b3_fix(const char *s, WORD *pw, WORD *ph) { (void)ph; return b3_fix_callee(s, pw); }

/* ---- B4: (int8_t) of a 32-bit-derived value does not sign-extend ------- */

volatile uint32_t b4_spec = 0x00FE1100;   /* char 0x00, thickness -2, colour */

static WORD b4_bug(uint32_t spec) { return (WORD)(int8_t)((spec >> 16) & 0xFF); }

static WORD b4_fix(uint32_t spec)
{
    WORD hi = (WORD)(spec >> 16);
    int8_t th = (int8_t)hi;
    return th;
}

/* ---- B5: a shifted load through a spilled pointer drops the load ------- */

/* `stride = p->field * 2u` where p is a local that lives on the stack (a
 * call preceded it) and is dead afterwards: the compiler gives stride the
 * pointer's own slot and emits `tsc; adc #slot; tax; asl 0,x` -- the slot is
 * shifted in place, so stride = p << 1 and p->field is never read.  Any
 * shift-shaped operator does it (* 2, << 1, x + x, * 4, / 2u, >> 1); * 3
 * does not, a byte field does not, a global or parameter pointer does not,
 * and a pointer still live afterwards does not.  All -O levels.
 *
 * This was the Phase 2b "unexplained" wrong-row read in vrt_cpyfm. */
typedef struct { uint32_t fd_addr; WORD fd_w, fd_h, fd_wdwidth, fd_stand; } MFDB;
MFDB b5_mfdb;
uint8_t b5_form[32];
WORD b5_contrl[8], b5_ptsin[4];

void b5_order(WORD *a, WORD *b) { if (*a > *b) { WORD t = *a; *a = *b; *b = t; } }

static WORD b5_walk(const uint8_t *bits, WORD sy1, uint16_t stride)
{
    WORD row, sum = 0;
    for (row = 0; row < 4; row++)
        sum = (WORD)(sum + bits[(uint16_t)((sy1 + row) * stride)]);
    return sum;
}

WORD b5_bug(void)
{
    MFDB *src = (MFDB *)(uint16_t)b5_contrl[7];
    WORD sy1 = b5_ptsin[1], sy2 = b5_ptsin[3];
    const uint8_t *bits;
    uint16_t stride;
    if (!src) return -1;
    b5_order(&sy1, &sy2);                       /* spills src to the stack */
    bits   = (const uint8_t *)(uint16_t)src->fd_addr;
    stride = (uint16_t)((uint16_t)src->fd_wdwidth * 2u);   /* src dead here */
    return b5_walk(bits, sy1, stride);
}

/* The field through a scalar first. */
WORD b5_fix(void)
{
    MFDB *src = (MFDB *)(uint16_t)b5_contrl[7];
    WORD sy1 = b5_ptsin[1], sy2 = b5_ptsin[3], wd;
    const uint8_t *bits;
    uint16_t stride;
    if (!src) return -1;
    b5_order(&sy1, &sy2);
    bits   = (const uint8_t *)(uint16_t)src->fd_addr;
    wd     = src->fd_wdwidth;
    stride = (uint16_t)((uint16_t)wd * 2u);
    return b5_walk(bits, sy1, stride);
}

/* The same slot-sharing with `- 1` in place of the shift: `n = p->len - 1`
 * where p is a local pointer (a call's return, so it lives on the stack)
 * that is dead after the line compiles to `tsc; adc #slot; tax; dec 0,x`
 * -- the POINTER decremented in place, the field never read, and n is a
 * bank-0 address less one.  inf_sset() in src/aes/fsel.c: a positive
 * length became a negative n, so no field of the file selector ever
 * received its text. */
typedef struct {
    uint32_t te_ptext, te_ptmplt, te_pvalid;
    WORD te_font, te_fontid, te_just, te_color, te_fontsize, te_thickness;
    WORD te_txtlen, te_tmplen;
} B5_TED;
B5_TED b5_ted;
char b5_text1[16], b5_text2[16];

B5_TED *b5_ted_of(WORD obj) { return obj ? &b5_ted : 0; }

WORD b5_dec_bug(WORD obj, const char *pstr)
{
    B5_TED *ted = b5_ted_of(obj);
    char *text = (char *)(uint16_t)ted->te_ptext;
    WORD n = ted->te_txtlen - 1;                /* ted dead here */
    WORD k = 0;
    while (n > 0 && *pstr) {
        *text++ = *pstr++;
        n--;
        k++;
    }
    *text = 0;
    return k;
}

/* The field into a scalar, the arithmetic on the scalar. */
WORD b5_dec_fix(WORD obj, const char *pstr)
{
    B5_TED *ted = b5_ted_of(obj);
    char *text = (char *)(uint16_t)ted->te_ptext;
    WORD len = ted->te_txtlen;
    WORD n = (WORD)(len - 1);
    WORD k = 0;
    while (n > 0 && *pstr) {
        *text++ = *pstr++;
        n--;
        k++;
    }
    *text = 0;
    return k;
}

/* ---- B7: sizeof a struct is padded where a constant expression is needed - */

/* The code generator lays a struct out with no padding -- a 16-bit member
 * sits at an odd offset if that is where it falls, the 65816 having no
 * alignment rule -- and `sizeof` in an ordinary expression says so.  But
 * `sizeof` where an integer constant expression is required (an enum, an
 * array bound, _Static_assert) is evaluated with 16-bit members aligned to
 * 2 and comes out larger.  A stride taken from such a constant walks off
 * the elements.  Write the byte count out instead (BCB_SIZE, src/vbxe). */
typedef struct { uint16_t a; uint8_t b; uint16_t c; } B7_S;      /* 5 bytes */
enum { B7_STRIDE = sizeof(B7_S) };                                /* says 6 */
B7_S b7_arr[3];

static WORD b7_bug(void)
{
    const uint8_t *p = (const uint8_t *)b7_arr + B7_STRIDE;
    return (WORD)((const B7_S *)p)->c;
}

static WORD b7_fix(void)
{
    const uint8_t *p = (const uint8_t *)b7_arr + 5;
    return (WORD)((const B7_S *)p)->c;
}

/* ---- B8: a byte local narrowed on one path is stored from 8-bit mode ---- */

/* `if (c >= 'a' && c <= 'z') c = (char)(c - 0x20); out[k] = c;` -- the
 * folding in sh_cioname (src/aes/shel.c).  The taken path does the
 * subtraction in 16 bits, switches to 8-bit accumulator to store the byte
 * local, and falls into the join with the mode still 8-bit; the other
 * path arrives 16-bit.  The join then loads the destination POINTER for
 * the store with an 8-bit `lda`, so the first lowercase character of a
 * name is written to page zero and its slot is left untouched.  -O2 only
 * (the -O1 code stores the byte differently).  An `unsigned char` local
 * is the same shape and the same bug; the sources hold the character in
 * a WORD and narrow it once, at the store. */
void b8_bug(const char *name, char *out)      /* not static: inlined, the shape is gone */
{
    WORD k = 0;
    for (; *name; name++, k++) {
        char c = *name;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 0x20);
        out[k] = c;
    }
    out[k] = 0;
}

void b8_fix(const char *name, char *out)
{
    WORD k = 0;
    for (; *name; name++, k++) {
        WORD c = (uint8_t)*name;
        if (c >= 'a' && c <= 'z')
            c -= 0x20;
        out[k] = (char)c;
    }
    out[k] = 0;
}

char b8_out1[8], b8_out2[8];

/* ---- results ------------------------------------------------------------ */

volatile WORD r_b1_bug, r_b1_fix;                       /* want 476 */
volatile WORD r_b2_eq, r_b2_lt, r_b2_mod;               /* want 7, 0, 0 */
volatile WORD r_b3_bug, r_b3_fix;                       /* want 7 */
volatile WORD r_b4_bug, r_b4_fix;                       /* want -2 */
volatile WORD r_b5_bug, r_b5_fix;                       /* want 120 */
volatile WORD r_b5_dec_bug, r_b5_dec_fix;               /* want 4 */
volatile WORD r_b7_bug, r_b7_fix;                       /* want 801 */
volatile WORD r_b8_bug, r_b8_fix;                       /* want 'T' = 84 */

__task int main(void)
{
    WORD w = 200, h;

    tree[2].ob_x = 376;
    r_b1_bug = b1_bug(tree, 2, 1, 100);
    r_b1_fix = b1_fix(tree, 2, 1, 100);

    h = 8;  r_b2_eq  = b2_div_truth("centred", &h);     /* 8 / 8 = 1: 7 */
    h = 7;  r_b2_lt  = b2_div_truth("centred", &h);     /* 7 / 8 = 0: 0 */
    h = 16; r_b2_mod = b2_mod_truth("centred", &h);     /* 16 % 8 = 0: 0 */

    r_b3_bug = b3_bug("centred", &w, &h);
    r_b3_fix = b3_fix("centred", &w, &h);

    r_b4_bug = b4_bug(b4_spec);
    r_b4_fix = b4_fix(b4_spec);

    for (h = 0; h < 32; h++) b5_form[h] = (uint8_t)(h * 3);
    b5_mfdb.fd_addr = (uint16_t)b5_form;
    b5_mfdb.fd_wdwidth = 2;                     /* 4 bytes per row */
    b5_contrl[7] = (WORD)(uint16_t)&b5_mfdb;
    b5_ptsin[1] = 1; b5_ptsin[3] = 4;
    r_b5_bug = b5_bug();                        /* rows 1..4: 12+24+36+48 */
    r_b5_fix = b5_fix();
    b5_ted.te_ptext = (uint16_t)b5_text1;
    b5_ted.te_txtlen = 5;                       /* four characters and the NUL */
    r_b5_dec_bug = b5_dec_bug(1, "SAMPLE");    /* 4 copied */
    b5_ted.te_ptext = (uint16_t)b5_text2;
    r_b5_dec_fix = b5_dec_fix(1, "SAMPLE");

    b7_arr[1].a = 0x1111; b7_arr[1].b = 0x22; b7_arr[1].c = 801;
    b7_arr[2].a = 0x4444; b7_arr[2].b = 0x55; b7_arr[2].c = 0x6666;
    r_b7_bug = b7_bug();                        /* element 1's c, by stride */
    r_b7_fix = b7_fix();

    for (h = 0; h < 8; h++) b8_out1[h] = b8_out2[h] = (char)0xEE;
    b8_bug("test", b8_out1);                    /* out[0] must be 'T' */
    b8_fix("test", b8_out2);
    r_b8_bug = (uint8_t)b8_out1[0];
    r_b8_fix = (uint8_t)b8_out2[0];
    return 0;
}
