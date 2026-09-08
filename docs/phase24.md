# Phase 24 — the far allocator's bank boundary

A thirty-five byte addition to the loader turned `test-m14x` red.  The
addition was innocent — thirty-five bytes of *dead padding* in the same
place failed identically — and so, in the end, was the near memory it
grew.  What it actually did was move the **far** image, and the fault
was there.

## The symptom, and why it was so hard to read

    padding   1  16  24  32   ->  PASS   (32 twice: not flaky)
    padding  28  35           ->  FAIL

Not a threshold and not noise: deterministic per layout, with no clean
boundary.  The failing case was always in the file selector, and it
failed by *completing early* — the harness would find the record count
at some nonsense value while the screenshot showed a perfectly healthy
`ITEM SELECTOR` with the right file highlighted.

The false trails, each ruled out by measurement rather than argument:

- **the DOS mangling the image.**  Bank $00 was read back on the SDX
  machine and compared with the linker's own output: identical, but for
  the six bytes the loader itself writes.
- **the application pool.**  8192 bytes free; the selector's tree is
  1516.
- **the harness's staging rooms**, which it computes from symbol
  adjacency and which a layout change could have broken: all three came
  out right.
- **the stack.**  Another 256 bytes changed nothing.
- **the struct that grew.**  Two dead bytes added to `Vwk` — the same
  growth the real fix caused, moving everything after it in the banked
  window — passed.

## What cracked it

Making room in bank $00 by moving the 608 bytes of fill patterns to
`cfar` — a change to the **far** image, not the near one — reproduced
the *same* failure on **SpartaDOS 3.2**, which had been passing all
along.  One symptom, two very different pushes, and the only thing they
had in common was that both moved where far memory begins.

`src/sys/farmem.c`:

    uint32_t far_alloc(uint32_t bytes)
    {
        uint32_t base = farmem.brk;
        ...
        farmem.brk = base + bytes;
        return base;                    /* ...anywhere at all */
    }

A bump allocator, four-byte aligned, and **nothing stopping a block
from straddling a bank boundary**.

That is fatal here, and the reason is a Calypsi semantic rather than a
bug: **`__far` pointer arithmetic is 16 bits WITHIN a bank** — carrying
into the bank byte is what `__huge` is for.  So a buffer that straddles
a boundary wraps round to the bottom of its own bank the moment it is
indexed past the edge.  And the bottom of a far bank is the far code
image.

The file selector allocates `MAX_FILES * LEN_FSNAME` — about 900 bytes
— for the names it lists, and indexes it by slot.  Whether that block
straddled depended on where the far image ended, which is why *any*
change to the program's size was a coin toss, and why the victim moved
about: sometimes three bytes of code nobody was executing, sometimes a
word the test harness was polling.

**This is the phase 6 corruption reached by another road**, and
`farmem.c`'s own header comment describes that one:

> `farmem_probe()` wrote a bank number into `$010100` and `far_alloc()`
> returned `$010000`, and the program corrupted three bytes of its own
> text.  The symptom was three unrelated VDI conformance failures that
> MOVED with the optimisation level.

The lesson was drawn then and the guard was put on the *start* of the
heap.  The same reasoning had to be applied to every block in it, and
was not.

## The fix

Eight lines: a request that will not fit in what is left of the current
bank starts the next one, and a request bigger than a bank is refused
outright, because no such block could be indexed anyway.  The gap is
lost — at most 64 KB of the fifteen megabytes this machine has.

## The gate

`tests/host/test_farmem.py` runs the allocator in the compiler's own
simulator against the cursors that used to break it -- 256 bytes left
in a bank, four bytes left, exactly the rest of a bank, one byte more
than that, a block bigger than a bank, and the last bank's edge -- and
asserts that **no block straddles**, that each lands where the contract
says, and that a request no `__far` pointer could index is refused
rather than handed out.  Two seconds on the host, no emulator.

## What it proves, and what it cost

- the loader's shim (`phase23.md`) plus the guard: `test-m14x` **PASS**;
- the fill patterns moved far, plus the guard: `test-m14`
  and `test-m14x` **PASS**, all six selector cases on each;
- the whole suite green with all three.

And **master was green by luck**.  Every far allocation this program
has ever made could have straddled a bank; the ones that did happened
to land on something nobody looked at.

## The room, and the wrong way to make it

The reason any of this came up was that bank $00 was full: with the
loader's shim in it, `Near` had **four** bytes free and `LoRAM` seven.
Disarming the tripwire does not help with that; the memory is still
gone.

The obvious 608 bytes are the fill patterns, which are 23% of all the
near memory gem4xe has and are read a row at a time when a pattern
changes rather than per pixel.  `cfar`, with the far code, is where
they belong.

**The first attempt was wrong, and instructively so.**  Moving the
tables makes the pointer that names them far, so `Vwk.patptr` became
`const UWORD __far *` -- and `ud_patrn`, the user's own pattern, is a
near array inside that very struct.  Two lines then compare the two:

    if (vwk.patptr == vwk.ud_patrn)     vdi.c, vwk_select and vwk_to_phys

Those are on the workstation switch, which only an application reaches.
`test-m3` passed 86/86 and the desktop crashed before its first wait --
a reminder that the conformance suite drives the PHYSICAL workstation
and never goes near that code.

**The right shape has no pointer at all.**  The workstation holds a
*source* and a *first row*:

    WORD patsrc;    /* PAT_DITHER, PAT_OEM, PAT_HATCH0/1, PAT_USER,
                     * PAT_SOLID, PAT_HOLLOW */
    WORD patidx;    /* where that pattern starts in its table */

and `pat_bits(y)` switches on the source to read either a far table or
the near `ud_patrn`.  There is no near/far question anywhere, the two
comparisons above become `patsrc == PAT_USER`, and the two one-word
tables the pointer used to name for solid and hollow are gone -- their
rows are constants in the switch.  The expansion cache keys on the
source and the index instead of the pointer.

    before   LoRAM  7 free   Near   4 free
    after    LoRAM 259       Near 360

The boundary moved from $3580 to $3680 as well, so that both sides of
bank $00 have a few hundred bytes rather than a few.
