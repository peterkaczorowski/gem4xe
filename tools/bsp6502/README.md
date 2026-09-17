# Does Calypsi's 6502 banking trampoline a cross-bank call?

No.  `probe.py` asks the toolchain and asserts the three answers; the
design they imply is in `docs/6502.md`.

    python3 tools/bsp6502/probe.py

    banked.c           ~7 KB of code in a `bankedcode` section, reached
                       through two entry points so none of it is
                       garbage-collected
    main.c             resident bank-$00 code that calls both
    atari-banked.scm   a throwaway Atari map: PORTB window $4000-$7FFF,
                       resident $2000-$3FFF, banks scattered above

The probe shrinks the slot itself so more than one bank instance has to
be generated -- that is the case the interesting finding lives in, and
with the full 16 KB window this code fits in one bank and the collision
never appears.  It also links the SAME objects against Calypsi's own
`cx16-banked.scm` as a control, so a plain `jsr` cannot be blamed on the
Atari map being unsupported.

Set `CALYPSI6502` if the toolchain is not at `~/dev/toolchains/calypsi-6502`.

A failing run means the toolchain changed its behaviour and `docs/6502.md`
is out of date -- which is the point of asserting it rather than
remembering it.
