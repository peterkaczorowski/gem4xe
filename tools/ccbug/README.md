# tools/ccbug — the cc65816 bugs gem4xe works around

Seven defects in Calypsi cc65816 **5.18** — five in code generation, one
crash and one in the front end's arithmetic — each reproduced from a shape
lifted out of gem4xe, each with the shape the sources use instead. `make check-cc` builds `bugs.c` with the
vendor's minimal linker script and C library, runs it under `db65816`, and
reads the results back; `b6.c`, which the compiler cannot get through, is
compiled on its own and the outcome read from the compiler:

    make check-cc
      B1 stack array element + operand           want   476 got   376   still present
      B1 through a scalar                        want   476 got   476   ok
      ...
      B6 indexed direct-page array                       compiles   still present
    check-cc: PASSED -- every workaround shape is right; 9 of 9 bug shapes still present

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
5. **Never shift or double a 16-bit load through a local pointer in the same
   expression.** `wd = p->field; stride = wd * 2;`, not
   `stride = p->field * 2;`.
6. **Never index an array that lives in the direct page.** Direct-page
   scalars and direct-page *pointers* are fine (`__attribute__((tiny))`,
   not the `__tiny` keyword, on a pointer declarator); tables stay
   ordinary statics and are indexed by a direct-page scalar.
7. **Never `sizeof` a struct where an integer constant expression is
   required** — an enum, an array bound, `_Static_assert`. It is padded
   there and not in the code. Write the byte count out (`BCB_SIZE`).

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
(`BCB_SIZE`) and `check-cc` reads element 1 of a three-element array
through both strides.

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
