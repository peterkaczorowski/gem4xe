# tools/ccbug — the cc65816 bugs gem4xe works around

Sixteen defects in Calypsi cc65816 **5.18** — twelve in code generation,
two crashes, one in the front end's arithmetic and one in the run-time
library's division — each reproduced from a shape
lifted out of gem4xe or out of the vendor's own C library, each with the
shape the sources use instead. `make check-cc` builds `bugs.c` with the
vendor's minimal linker script and C library, runs it under `db65816`, and
reads the results back; `b6.c` and `b11.c`, which the compiler cannot get
through, are compiled on their own and the outcome read from the compiler,
and `b16.c`, whose output cannot be run, is compiled on its own and its
listing read:

    make check-cc
      B1 stack array element + operand           want   476 got   376   still present
      B1 through a scalar                        want   476 got   476   ok
      ...
      B6 indexed direct-page array                       compiles   still present
      B11 near <-> far struct copy over 8 bytes          compiles   still present
      B16 byte spin loop, rep before its back edge         compiles   still present
    check-cc: PASSED -- every workaround shape is right; 19 of 19 bug shapes still present

The run **fails only if a workaround shape stops compiling right**, because
that is what would break gem4xe. A bug that has gone away is reported as
`FIXED upstream`, which is the cue to remove its workaround. `make test`
includes it.

Every one of these was first seen as a wrong pixel or a wrong returned value
in the conformance suites, then proved by reading the emitted assembly and
finally by the simulator. Phase 6's lesson still applies: a bug that moves
with the code layout is self-corruption until proven otherwise, and every
bug below was proved.

## The rules

What the sources do, in one line each. The reasons follow.

1. **Never `stackarray[var] + operand` in one expression.** Read the element
   into a scalar first.
2. **Never trust the flags after `/` or `%`.** The build links
   `src/sys/div16.s` with `--override _Div16 --override _Mod16`; without it,
   `if (a / b)` and `a % b == 0` are wrong.
3. **Never `*out = c ? a : b` in a `static` function.** Return the value.
4. **Never `(int8_t)` an expression derived from a 32-bit value.** Go through
   a `WORD` (or `int8_t`) local.
5. **Never shift, double, increment or decrement a 16-bit load through a
   local pointer in the same expression.** `wd = p->field; stride = wd * 2;`,
   not `stride = p->field * 2;`; `len = p->len; n = len - 1;`, not
   `n = p->len - 1;`.
6. **Never index an array that lives in the direct page.** Direct-page
   scalars and direct-page *pointers* are fine (`__attribute__((tiny))`,
   not the `__tiny` keyword, on a pointer declarator); tables stay
   ordinary statics and are indexed by a direct-page scalar.
7. **Never `sizeof` a struct where an integer constant expression is
   required** — an enum, an array bound, `_Static_assert`. It is padded
   there and not in the code. Write the byte count out (`BCB_SIZE`).
8. **Never narrow arithmetic into a `char` local on one path of a
   conditional and store the local after the join.** Hold the character
   in a `WORD` and narrow it once, at the store.
9. **Never `got = c ? a : b` on a local whose address another path passes
   to a call.** Test the condition, `break` or return on it, then assign
   plainly.
10. **Never clamp a parameter back into itself in a `static` function.**
    `WORD cx = gx > n ? n : gx;` into a fresh local, then use `cx`.
11. **Never assign a struct of more than 8 bytes between a near object and
    a far one.** Copy it byte by byte (`fn_copy`, src/desk/deskwin.c), or
    keep both sides far — far-to-far copies of any size compile.
12. **Never right-shift a signed 16-bit value.** Shift an unsigned copy
    (`(UWORD)v >> n`) when it cannot be negative, and use `asr()`
    (src/vdi/vdi.c) when it can. A shift by one emits a plain `lsr`, which
    is right only for a non-negative value; by more than one the value is
    mangled outright, positive or negative.
13. **Never combine two elements of the same array in one expression**
    when either index is computed. `q = pt[j]; dx = pt[0] - q;`, not
    `dx = pt[0] - pt[i * inc * 2];` — B1 again, and it reaches pointer
    parameters, not only stack arrays.
14. **Never index an array from a pointer into its middle with a
    negative index.** Pass the base and an index, so that every subscript
    is non-negative: `draw_arrow(pt, n, (n-1)*2, -1)`, not
    `draw_arrow(&pt[(n-1)*2], n, -1)` with `pt[-2]` inside.
15. **Never write `P->a = P->b OP e` (or `P[i] = P[j] OP e`) through a
    near pointer.** `s = P->b; P->a = (WORD)(s OP e);` -- the member on
    the left of the operator goes through a scalar. Rules 1 and 13 are
    this rule's two earlier sightings; this is the exact trigger, and it
    is what breaks `fdopen` in the vendor's own C library.
16. **Never spin on a byte.** A poll such as `while (VCOUNT < line) ;`
    reads the byte into a word first -- `while (vcount() < line) ;` with
    `static uint16_t vcount(void) { return VCOUNT; }` -- so that the
    compare is a 16-bit one. At -O2 the byte compare's width switch can
    land before the loop's back edge, and the second pass runs 8-bit code
    as 16-bit.

## B1 — stack-array element plus operand compiles to a load

    x[d] = x[d-1] + tree[t].ob_x;       /* everyobj(), src/aes/objc.c */

emits `ldy ob_x; lda (&x[d-1]),y`: the operand becomes an *index* and the
element's address is *dereferenced* — a load from `&x[d-1] + ob_x`, not an
add. All `-O` levels. Only stack arrays; a global array or a scalar temporary
is fine.

    prev = x[d-1];  x[d] = prev + tree[t].ob_x;

Found by `make test-m4`: every child object was drawn at its parent's x plus
garbage.

## B2 — `_Div16` / `_Mod16` return with the wrong flags

Not the compiler alone: a mismatch between it and its library. At `-O1` and
above `if (a / b)` compiles to `jsl _Div16; beq`, relying on the callee to
leave N and Z from the result. The library's `_DivModSign16`
(`src/lib/lowlevel/integer.s`) ends `plx; bpl; rtl` on the non-negative path,
so the flags describe the **sign word** (dividend ^ divisor), not the
quotient. Hence `8 / 8` tests as zero and `7 / 8` as non-zero; `_Mod16` tests
the dividend, so `a % b == 0` is false for every non-zero `a`. Over 365
test pairs the library's flags are right 236 times; the override's, 365.

`src/sys/div16.s` is a replacement that computes the result and then loads it
(`tay; tya`) so the flags are its own. It is linked with
`ln65816 --override _Div16 --override _Mod16`, which treats the archive's
symbols as weak. `check-cc` links `bugs.c` both ways and requires the
override to make all three B2 rows right.

Found by `make test-m4`: `gsx_tcalc` decided no line of text fitted a field
exactly one line high, so no G_TEXT / G_FTEXT / G_BOXTEXT ever drew.

## B3 — `*out = c ? a : b` in an inlined static function stores nowhere

    static void callee(..., WORD *pn) { *pn = (n < m) ? n : m; }

once inlined at `-O1`+, stores the conditional into a dead stack slot; the
caller's variable is never written and reads whatever it held. If/else with
two plain stores is fine, an `extern` callee is fine, returning the value is
fine.

    static WORD callee(...) { return (n < m) ? n : m; }

Found by `make test-m4`, in the same `gsx_tcalc` as B2: the character count
came back as uninitialised stack.

## B4 — `(int8_t)` of a 32-bit-derived value does not sign-extend

    th = (WORD)(int8_t)((spec >> 16) & 0xFF);     /* ob_sst(), spec is uint32_t */

The cast is dropped — the byte is zero-extended — whenever the operand is
derived from a 32-bit value in the same expression: with or without the
`& 0xFF`, through `(uint16_t)` or `(WORD)`, from `int32_t` as well. All `-O`
levels. A `WORD` local in between restores it:

    WORD hi = (WORD)(spec >> 16);  int8_t sth = (int8_t)hi;  th = sth;

Found by `make test-m4`: the outward-border case, where a thickness of -2
became 254 and painted a black band the height of the object.

## B5 — a shifted load through a spilled pointer drops the load

    stride = (uint16_t)((uint16_t)src->fd_wdwidth * 2u);   /* vrt_cpyfm() */

with `src` a local that has been spilled to the stack (a call preceded it) and
is **dead after this line**: the compiler gives `stride` the pointer's own
slot and emits

    tsc ; clc ; adc ##slot ; tax ; asl 0,x

— the slot shifted in place. `stride` becomes `src << 1` and `fd_wdwidth` is
never read. Every shift-shaped operator does it (`* 2`, `<< 1`, `x + x`,
`* 4`, `/ 2u`, `>> 1`); `* 3` does not, a byte field does not, a global or
parameter pointer does not, and a pointer that is still live afterwards does
not. All `-O` levels.

    wd = src->fd_wdwidth;  stride = (uint16_t)((uint16_t)wd * 2u);

This is the Phase 2b "unexplained codegen difference": with `sy1 == 0` row 0
of the source read correctly (`0 * garbage`) and every later row read
unrelated memory. Switching to an incrementing pointer happened to change the
slot allocation so `stride` no longer landed on `src`. It is root-caused now
— `docs/phase2b.md` is updated — and `check-cc` pins it.

**A decrement does it too, and the same rule covers it.** Phase 14's
`inf_numset` wrote `WORD i = (WORD)(ted->te_txtlen - 1);` and got `dec 0,x`
on the pointer's slot: `i` became the TEDINFO's *address* less one, and the
loop that filled the field with spaces filled 25 KB of bank $00 instead —
through `$D0xx`, where any write soft-resets VBXE, and through POKEY's
`IRQEN`, which left an interrupt nothing could acknowledge. A whole machine
frozen by one dropped load. `len = ted->te_txtlen; i = (WORD)(len - 1);`
compiles right; `docs/phase14.md` has the account.

`- 1` does it too, as `dec 0,x` on the slot: `n = ted->te_txtlen - 1` in
`inf_sset()` (src/aes/fsel.c), with `ted` a call's return dead after the
line, made `n` the TEDINFO's address less one — negative in bank 0 above
`$8000` — so the loop copied nothing and every field of the file selector
came up empty.  `-O2`.  Found by `make test-m12`: the target's selector
showed templates with no text where the model showed the path and the
names; `check-cc` pins this shape as well (`B5 spilled pointer, field - 1`).

## B6 — an indexed direct-page array is an internal compiler error

    static __attribute__((tiny)) uint8_t tbl[4];
    static __attribute__((tiny)) int i;
    uint8_t f(void) { return tbl[i]; }

    internal error: Translator/Target/WDC65816/Compiler/CGHelpers.hs:
    (662,1)-(663,59): Non-exhaustive patterns in function mem8Reg

Any variable index — a direct-page scalar, a parameter, a plain global —
into an array placed in the direct page, with 8- or 16-bit elements, at
every `-O` level. A constant index compiles. Direct-page scalars and
direct-page pointers (`lda (.tiny p)`, `sta (.tiny p)`, `inc dp:.tiny p`)
are what the fast loops want anyway, and an ordinary array indexed by a
direct-page scalar is one instruction (`ldx dp:.tiny i; lda tbl,x`), so the
sources keep every table out of the direct page.

Two things next to it that are not bugs: the `__tiny` *keyword* on a
pointer declarator (`uint8_t * __tiny p;`) is rejected with
"expected identifier or '('", and the guide says to use
`__attribute__((tiny))` where the keyword form is refused; and
`--assembly-source` is the only way to see what a loop compiled to,
since there is no per-function optimisation pragma.

Found in Phase 8c while moving the line stepper's state into the direct
page (`docs/phase8c.md`).

## B7 — `sizeof` a struct is padded where a constant expression is required

    typedef struct { uint16_t a; uint8_t b; uint16_t c; } S;   /* laid out in 5 bytes */
    enum { STRIDE = sizeof(S) };                                /* 6 */

The code generator lays a struct out with no padding — a 16-bit member
sits at an odd offset if that is where it falls, the 65816 having no
alignment rule — and `sizeof(S)` in an ordinary expression is 5, `S x[2]`
is 10 bytes and `x[1].c` is read at offset 8. But where the language
requires an integer constant expression — an enum, an array bound,
`_Static_assert` — `sizeof(S)` is evaluated with 16-bit members aligned to
2 and is 6. `char buf[sizeof(S)]` merely over-allocates; a stride or an
offset taken from such a constant reads the wrong bytes, and
`_Static_assert(sizeof(S) == 5)` fails on a struct that *is* 5 bytes,
which is how this was found: the BCB overlay in `src/vbxe/vbxe.c` was
given exactly that assertion as a guard. All `-O` levels.

The sources write the byte count out where a constant is needed
(`BCB_SIZE`).  It is checked two independent ways, because one way is
what let this bug bite the project a second time: `check-cc` reads
element 1 of a three-element array through both strides -- a RUN, in the
simulator -- and compiles `b7_bound.c`, which asks for the true size in
an array bound and must be REFUSED.  The day that file compiles, the bug
is gone.

**It bit this project again on 2026-09-16, with rule 7 already written
above.**  `ICONBLK` is 34 bytes (three LONGs and eleven WORDs) and the
array-bound idiom answers 36, so the resource loader's table strides were
read as a live defect, a commit said so, and a peer was told the fix
mattered to their port.  It did not: the loader was correct, and every
gate passed either way for that reason.  What settled it was the target
-- MControl's own resource, 22 colour icons, loaded on the machine with
every record matching the file at a stride of 50, which is 34 + 12 + 4.
Note HOW the idiom fails, because that is what made it convincing: **it
refuses the TRUE value**, so it reads as a failed assertion about the
code rather than a broken instrument.

**Rule 7, in the form that would have prevented both:** measure a struct
by asking the GENERATED CODE -- a function that returns `sizeof(X)`, read
out of `--assembly-source` -- and never an array bound, an enum or a
static assertion.  (`&arr[1] - &arr[0]` is the other obvious way and is
not available: this compiler answers a pointer difference between struct
members with `internal error: ScaleIndex.hs`.)  `tests/host/test_sdk.py`
measures that way, and the tree carries no array-bound use of `sizeof` at
all -- the four floors that briefly guarded the resource loader were
removed, because a floor can only give a false pass and a rule-breaking
construct in the sources is one the next reader copies.

## B8 — a byte local narrowed on one path is stored from 8-bit mode

    char c = *name;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 0x20);       /* sh_cioname(), src/aes/shel.c */
    out[k] = c;

The taken path does the subtraction in 16 bits, drops to an 8-bit
accumulator to store the byte local, and falls through into the join with
the mode still 8-bit; the untaken path arrives in 16-bit mode. The join
loads the destination *pointer* for `out[k] = c` with `lda`, `tay` — an
8-bit load now, so Y gets half an address and the byte goes to page zero
while `out[k]` keeps whatever it held. `-O2` only; an `unsigned char`
local is the same shape and the same bug, and so is a `static char
to_upper(char)` once it is inlined into the store. Found by
`tests/emu/m12_file.py`: `sh_cioname("test.rsc")` gave `\xEEEST.RSC` —
the first lowercase letter of a name vanished, uppercase names were fine —
and the simulator reproduced it from the function alone.

The sources hold the character in a `WORD` and narrow it once, at the
store; `check-cc` folds `"test"` both ways and reads the first byte.

## The simulator recipe

Everything here is built and run the way `check.py` does it:

    cc65816 -g --code-model=large --data-model=small -O2 -o x.o x.c
    ln65816 -g $CALYPSI/example/minimal/linker.scm x.o -o x.elf clib-lc-sd.a \
            --rtattr exit=simplified [div16.o --override _Div16 --override _Mod16]
    db65816 --nh --nx --exit-breakpoint x.elf      # then, over STDIN:
        run
        ... wait for the line containing SIGSTP ...
        print r_b1_bug
        quit

Two things cost an afternoon each:

- **`db65816 -e run -e print x` races the program.** Batch commands are
  executed before `run` has finished, so `print` shows the pre-run value —
  zeros — and it gets worse with longer programs. Drive the debugger over
  stdin and read lines until `SIGSTP` before printing anything.
- **`cc65816 --assembly-source FILE` writes the assembly and then does not
  write the `.o`.** Compile twice when both are wanted.

Array variables print as `[i] = v` lines; scalars as `$n = v`. Merge stderr
into stdout before parsing, the two interleave.

## Reporting upstream

Each bug has a self-contained reproducer in `bugs.c`, and the characterisation
matrices above say what does and does not trigger it. They have not been sent
to Calypsi; that is the user's call. When a release fixes one, `check-cc`
says so, and the workaround — and for B2 `src/sys/div16.s` plus the two
`--override` flags in the Makefile — can go.

## B9 — a conditional assignment to an address-taken local is dropped

    if (write) { st = cio_write(iocb, p, m); got = ok ? m : 0; }
    else       { st = cio_read(iocb, p, m, &got); }     /* gd_xfer(), src/sys/gemdos.c */

The read path hands `&got` to a callee, so `got` has a stack slot.  The
write path's conditional is evaluated into a scratch slot (`sta 1,s`) and
never copied to that slot; instead the join loads `got` from an unrelated
slot (`lda 15,s; sta 26,s`), and the bytes-moved total that Fwrite returns
is whatever was there — 770 on the target, 259 in the simulator for a
20-byte write.  Sibling of B3 (the conditional store into a dead slot), on
a local rather than through a pointer.  -O2.  `gd_xfer` tests the status,
breaks on failure, and assigns `got = m` on the line after.

## B10 — parameters clamped in place are read from an unwritten slot

    static void snap_icon(WORD gx, WORD gy, WORD *px, WORD *py)
    {
        ...
        if (gx > columns - 1) gx = columns - 1;
        if (gy > rows - 1)    gy = rows - 1;
        *px = gx * icw + spare / columns;                /* src/desk/desktop.c */
        *py = gy * ich + spare / rows + desk.y;
    }

Inlined into its caller at `-O2`, the two parameters are parked at `1,s`
and `3,s` and the clamps store there — the second one to `1,s`, which is
the wrong parameter, though that is the smaller error — and then every
product that follows loads *both* parameters from `5,s`, a slot nothing in
the function ever wrote (`lda 5,s; ldx icw; jsl _Mul16`).  The result is
whatever the last call left on the stack: the first desk icon landed at
(0, 11) because that slot was zero, the second at (31744, 3595) and the
trash at (21844, -8027), off the screen.  Sibling of B3 and B9: a value
assigned on one path of a conditional, then fetched from the wrong slot at
the join.  Clamping into fresh locals (`cx`, `cy`) and leaving the
parameters alone compiles right.

Found by hand-running the desktop (milestone 4, `docs/phase14.md`) and
dumping its screen tree: two of three icons at impossible coordinates, the
first one right.

## B11 — a near ↔ far struct copy over 8 bytes is an internal compiler error

    FNODE __far *pf;  FNODE fn;          /* pn_active(), src/desk/deskwin.c */
    *pf = fn;

    internal error: labeling failed

Either direction — a near local, a near global or a near pointer target,
to or from a far pointer target — at every `-O` level and in both the small
and medium data models. A struct of up to 8 bytes is copied through the
registers and compiles; from 9 bytes up it is a block move the back end
cannot label. Near-to-near and far-to-far copies of any size compile, so
the listing's insertion sort slides FNODEs with `*pf = *(pf - 1)` (far to
far) and puts the new one in through a byte loop.

Found in Phase 14, milestone 5, as the first compile of the folder windows
failed at once; bisected to the one statement with a delta-minimiser (the
three-function "minimal failing set" it first reported was an artefact of
unused statics being dropped: the function alone, `static` and unreferenced,
compiles because it is never translated).


## B12 — a signed 16-bit `>>` is not an arithmetic shift

    UWORD i = angle >> 3;               /* Isin(), src/vdi/vdi.c */

emits three logical shifts and then a sign extension **from the wrong bit** --
`eor ##4 / and ##7 / sec / sbc ##4`, which keeps three bits and discards the
rest. So `900 >> 3` is 0 rather than 112, `900 >> 4` is -8 rather than 56,
and `-900 >> 3` is -1 rather than -113. A shift by ONE emits a plain `lsr`,
correct only when the value cannot be negative. 32-bit shifts, signed or
not, are right, and so are unsigned 16-bit ones.

The same compiler emits the correct `cmp ##-32768 / ror a` for the same
source shape elsewhere in the same file (`nf >> 2` in `raster_1bpp`), so
reading one listing proves nothing either way -- which is why this one is in
`bugs.c` and checked in the simulator.

    static WORD asr(WORD v, WORD n)     /* src/vdi/vdi.c */
    {
        if (v >= 0)
            return (WORD)((UWORD)v >> n);
        return (WORD)(-(WORD)(((UWORD)(-v) + (UWORD)((1u << n) - 1u)) >> n));
    }

Found by `make test-m3`: every GDP curve -- circle, ellipse, arc, pie --
drew nothing at all, because `Isin` returned `sin_tbl[0]` for every angle
and each point of the curve landed on its centre.  A sweep of the whole
tree's generated assembly for the broken idiom found no other site.


## B13 and B14 — an arrowhead's two ends

    dx = pt[0] - pt[i * inc * 2];       /* draw_arrow(), src/vdi/vdi.c */

reads the second element as **zero**. It is B1's shape reaching further
than B1 says: through a pointer parameter rather than a stack array, and
with a product of two variables as the index. Neither the index
arithmetic on its own nor the subtraction on its own is wrong -- lifted
into a small test program with the same types, the same expression
compiles correctly -- so this, like B12, is checked by running it rather
than by reading a listing.

    WORD j = (WORD)(i * inc * 2), q = pt[j];
    dx = (WORD)(pt[0] - q);

The head at the OTHER end of the line was wrong for a second reason.  The
donor reaches it with a pointer into the middle of the array and walks
backwards:

    draw_arrow(vwk, point+count-1, count, -1);      /* pt[-2] inside */

and compiled here `pt[-2]` reads neither point -- it came back as -8417
for a coordinate that is 48.  The sources pass the base and the tip's
INDEX, so every subscript inside is non-negative.

Neither shape reproduces on its own: lifted into a small function with
the same types, both compile correctly, and `bugs.c` therefore carries
the whole calculation -- the square root, the rounding and the loop -- to
get the same register pressure.  That is also why neither could have been
found by reading a listing.

Found by `make test-m3`: every arrowhead `vsl_ends` drew pointed at the
origin (B13), and the one at the far end of the line pointed off the
screen (B14).


## B15 — `p->a = p->b OP e` through a spilled pointer is `p->a OP= e`

    stream->fs_bufend = &stream->fs_bufstart[BUFSIZ];   /* __fs_fdopen(), clib */

with `stream` a near pointer that lives on the stack (a call preceded it)
compiles to

    ldy ##fs_bufend ; lda ##64 ; clc ; adc (slot,s),y ; sta (slot,s),y

— the load is done at the **destination's** offset. `fs_bufend` becomes
whatever it was plus 64 and `fs_bufstart` is never read, so any `fwrite`
or `fread` longer than 64 bytes runs off the end of the buffer and into
the heap. Found in the Calypsi-65816-Atari board support package, whose
`readwrite` test wrote 256 bytes and smashed the heap; it links
its own `fdopen.c` ahead of `clib-*.a` and `docs/cc65816-bug.md` there
carries the account.

The trigger is exact, from four characterisation matrices: the right-hand
side, after any casts and parentheses, is **one binary operation whose
left operand is another member or element of the same pointer**.

- Operators: `+ - & | ^ <<` and a signed `>>` (`ror n,x`); `+ 1` and
  `- 1` become `inc n,x` / `dec n,x` on the destination, `* 2` an
  `asl n,x`.
- Any element width — `char`, `WORD`, `long` (`adc ##64; sta 8,x; adc
  ##0; sta 10,x`).
- A variable index on either side is ignored: `p[n] = p[1] + 64` and
  `p[2] = p[n] + 64` are both wrong.
- The right operand can be a constant, a global, a parameter, another
  member or a `_Mul16` product; a `(WORD)` cast round the whole
  right-hand side changes nothing; two such statements in a row are both
  wrong.
- Not triggered: the member on the **right** of a non-commutative
  operator (`k - p->y`, `-p[1]`); more than one operator at the top level
  (`(p->y + k) + m`, `p->y * 2 + k`, `(p->y + k) >> 1`); a call or a
  `_Div16` between the load and the store (`p[1] - g()`, `p[1] / 2`,
  `p[1] * 3`); a cast on the left operand (`(UWORD)p->y >> 1`); a
  compound assignment (`p[2] += p[1]`, which is what the compiler thinks
  it was given); a global pointer, a `__far` pointer, a pointer that is
  still in X because no call spilled it, or an alias (`q = p; q->x =
  p->y + k`).

B1 (a stack array, `x[d] = x[d-1] + ...`), B13 (a pointer parameter with
a computed index) and B5 (the shift or decrement done in place on a dead
pointer's slot) are earlier faces of the same defect. A scan of gem4xe's
tree for the shape — every `P->a = P->b OP e` and `P[i] = P[j] OP e` whose
left operand's root differs from the destination's member — found no
instance left; the one candidate, a global struct in `src/desk/desktop.c`,
compiles right, and its listing was read to be sure.

`bugs.c` gets the spill with a callee that loops — a one-line callee is
inlined at -O2, the pointer never leaves X, and the statement compiles
correctly, which is also why a minimal reproducer so easily misses it.

## B16 — a byte spin loop's width switch lands before its back edge

    if (p) return 0;
    while (VCOUNT >= 19) ;      /* the frame's wrap */
    while (VCOUNT < 19) ;       /* the top of the logo */

with `VCOUNT` a volatile byte, at -O2, compiles the second loop to

    ?L11: lda VCOUNT ; cmp #19 ; rep #32 ; bcc ?L11

The first pass is right. The second runs `lda` as a word and `cmp #19` as
a three-byte instruction, which swallows the `rep`'s opcode `c2`, so the
next thing executed is the branch's own operand — `90 f7` and whatever
follows it. In `src/sys/bootinfo.c` what followed was `20 90 f7`, a `jsr`
into the middle of `farmem_probe`, whose `rtl` then landed in the OS ROM
and BRKed out of the boot screen's rainbow. -O0 and -O1 put the `rep`
after the loop; the first loop is untouched because its label sits before
the `sep`; and without the `if (p) return 0` both loops compile right,
which is why a minimal reproducer misses it. `do { vc++; } while (vc <
19);` is the same shape.

The shape cannot be run, so `b16.c` is compiled alone and `check.py`
reads its listing: a conditional branch backwards, immediately preceded
by `rep #32`, with no `sep #32` between the label and the branch. The
same reading over every gem4xe source with its own flags found only the
eight polls in `bootinfo.c`. The sources now read the byte into a word
through a helper and compare the word — `while (vcount() < line) ;`, with
`vcount()` a real call (`jsl`) that costs nothing at 20 MHz — and that
shape runs in `bugs.c` as `r_b16_fix`. Rule 16.

## Reading the map — not a bug, and it gave the wrong answer twice

`clock()` turned up in a program that never calls it, 275 bytes together
with `Tgettimeofday`, and `ln65816 --list-file` showed both as sections
with no referrer. That was read, twice on the same day, as a dead-section
bug in the linker: an unreachable cycle (`clock` calls `Tgettimeofday`;
`Tgettimeofday`'s section branches into `clock`'s) that the mark had
failed to drop. It was neither unreachable nor a bug.

**The map lists a section's referrers by symbol, and a local label is not
a symbol it prints.** At -O2 cc65816 shares an identical tail across the
functions of one translation unit — the `dl()`/`dos()` epilogue of the
GEMDOS bindings in `src/app/gemlib.c` — and parks it inside one
function's section, so every other function reaches it through a
`?Lnnnn` label. In the map every one of those references is invisible,
and `clock` and `Frename` both read as referrer-less while live. The
chain, from the object file:

    Fread, Fwrite, Fseek   ->  Frename's section        via ?L1473
    Frename, Fdatime       ->  Tgettimeofday's section  via ?L1466
    Tgettimeofday, Fforce, Pexec, Ptermres, Mshrink
                           ->  clock's section          via ?L1462

so any GEMDOS file binding brings `clock` in; twelve bindings reach it
within three hops.

**It happens only under `--data-model=large`.** The same `gemlib.c`
compiled `--data-model=small` — the kit's default, and every program in
this tree — shares no tails at all, and only `Tgettimeofday` reaches
`clock`. A large-model program pays the 275 bytes the moment it touches a
file; a small-model one never does.

The tool is `objchain.py`. It walks an object's relocations backwards
from a symbol's section — `readelf -SW` for the sections and, for each
`.relocations` section, its file offset and the section it applies to;
`-rW` for the entries, matched to their target by that offset; `-sW` for
every symbol's section, local labels included — and names every global
function that transitively keeps the symbol alive:

    python3 tools/ccbug/objchain.py build/appld/gemlib.o clock --hops 3
    12 global function(s) bring clock in within 3 hop(s):
      Fattrib Fdatime Fforce Fread Frename Fseek Fwrite Mshrink Mxalloc Pexec Ptermres Tgettimeofday

    python3 tools/ccbug/objchain.py build/app/gemlib.o clock --hops 3
    1 global function(s) bring clock in within 3 hop(s):
      Tgettimeofday

Three things to keep from it. The map is a summary of the relocations,
not the relocations: when the question is "what references this", read
the object. A `\b` in a regex cannot match a symbol that begins with `?`,
which is how the first scan for `?L1466` found nothing and confirmed the
wrong theory. And the tail-sharing is sound code — the observation is
only that parking a shared tail inside one function's section makes
dead-section elimination all-or-nothing for everything that branches in,
a size cost and not a correctness one. The evidence — both sessions'
maps, objects, readelf dumps, and the scripts that produced the wrong
answer and then the right one — is kept outside the tree at
`build/ccbug-clock-cycle/`, not committed.

**The cluster grows, and there is a switch.** The day `Psystem` went
into `gemlib.c` the GACS session measured it joining the cluster
unasked — 352 bytes now, `clock` 215 + `Tgettimeofday` 60 + `Psystem`
77, for any large-model program that touches a file binding, and one
member more each time a binding is added in that neighbourhood.
`cc65816 --no-interprocedural-cross-jump` is the sharing by name.
Compiling `gemlib.c` with it, in the large model, leaves the library 594
bytes bigger as a whole and every binding its own section, so a program
pays 3–13 bytes more for each binding it *calls* (Fread 39→52, Frename
43→46, Malloc 13→14) and nothing for the ones it does not; `objchain.py`
then finds no function that brings `clock` in. The tree's
`build/appld/gemlib.o` rule carries the flag, and the kit's README tells
a program that builds the bindings itself to do the same. Re-run
`objchain.py` after adding a binding rather than assuming a chain is
stable — that is what it is for.

Measured on GACS the same day, the flag shed **1,028 bytes** of a
138,190-byte image (far 114,210→113,470; bank fixups 7,025→6,929), not
the 352 the cluster accounts for: placed sections went 1,662→1,639, so
twenty-three passengers left, not three — the same mechanism had been
dragging others in. RetroWP, which calls more of the bindings, shed
1,075 bytes of 204,252 with `gemlib.c` byte-identical on both sides.
Neither saving exists under `--data-model=small`, whose bindings share
no tail to begin with. Two details a consumer will meet. The flag belongs
on `gemlib.c` **only**: `clib.c` compiled with it gave the same section
count and 15 bytes more, because a program uses nearly all of `clib.c`
and there is nothing for cross-jumping to waste — this is an
optimisation for a library a program uses a fraction of. And `make`
tracks a rule's sources, not its recipe: adding the flag to an existing
rule rebuilds nothing, the gate re-runs the old image and reports the
old number — delete the object or make the Makefile a prerequisite.
