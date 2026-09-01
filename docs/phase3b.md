# Phase 3b — input plumbing, and the VDI is complete

Status: **all gates green.** 9 host tests, 49/49 VDI cases.

> **The 37-opcode AES + GEM Desktop VDI surface is fully implemented.**
> Coverage is asserted mechanically at the end of `make test-m3`.

## What landed

| | |
|---|---|
| Input modes | `vsin_mode` (33), `vqin_mode` (115) |
| Vector exchange | `vex_butv` (125), `vex_motv` (126), `vex_curv` (127), `vex_timv` (118) |
| Keyboard | `v_string` (31), `vq_key_s` (128) |
| Text metrics | `vst_height` (12), `vqt_attributes` (38) |
| Misc | `v_escape` (5), `v_choice` (30), `vsl_udsty` (113) |

`v_clswk` (2) and `v_clsvwk` (101) remain no-ops — as they are in DRI's own
shipping driver.

## The keyboard is read from POKEY, not the OS

The OS's `CH` (`$02FC`) is filled by the keyboard **IRQ**, and gem4xe runs with
IRQs off. So `v_string` and `vq_key_s` read `SKSTAT` (`$D20F`) bit 2 and
`KBCODE` (`$D209`) directly, which polls fine with no interrupt at all.

That is the same shape as the pointer: **polling works today, interrupts would
be better.** Both input paths now sit behind one entry point, `vdi_input_poll()`,
so when native-mode vector stubs exist the VBI can call it and nothing above
notices.

## The vex_* ABI is a deliberate platform decision

GEM's 68000 convention passes the motion vector its x and y in `d0`/`d1`. That
has no meaning here, so gem4xe defines its own: **handlers take no arguments
and read `ptr_state`**, exactly like the VDI's own opcode handlers read the
`contrl`/`intin` globals. A documented platform convention beats a fabricated
one, and it matches the argument-free discipline the rest of the driver
inherited from `entry.a86`.

`vex()` returns the previous handler in `contrl[9..10]` per the VDI contract;
only `contrl[9]` carries address, since pointers are 16 bits here.

## The harness now checks what opcodes RETURN

This is the significant addition. Until now the suite compared pixels, which
made every non-rendering opcode invisible to it — and non-rendering is most of
what was left.

`src/m3_vdi.c` now records a fixed 8-word result per call
(`contrl[2]`, `contrl[4]`, `intout[0..2]`, `ptsout[0..2]`) into `vdi_results[]`,
and the harness compares it call-for-call against the reference. **Every case
checks both pixels and return values**, so a case costs nothing extra to cover
both.

### One subtlety that mattered

The first version recorded `intout[0..2]` unconditionally and every case
failed, because after `v_opnwk` the target still had `639, 239, …` sitting in
`intout` while the reference had zeros.

**The target was right.** The VDI contract is that `intout`/`ptsout` are valid
only up to `contrl[4]`/`contrl[2]`; anything beyond is leftover and a caller
must not read it. Recording it verbatim made the harness *stricter than the
contract*, which would fail correct code. Both recorders now mask to the
declared counts — the record expresses the contract rather than the memory.

Worth remembering as a general rule: when the reference and the target
disagree, the reference is authoritative about *behaviour*, but not about
*what the specification actually requires*.

## Coverage is checked, not claimed

`tests/emu/m3_vdi.py` ends by parsing the jump tables out of `src/vdi/vdi.c`
and asserting all 37 opcodes resolve to something other than `v_nop`. If an
opcode is ever regressed to a stub, the suite says so — the count cannot drift
away from the documentation.

## Where this leaves the project

The VDI is done to the level the AES needs. The remaining work is **the AES
itself** — object trees, `form_do`, the window manager, menus — which is where
the plan's two standing warnings come due:

- **Dirty rectangles are mandatory**, confirmed by measurement in Phase 1
  (a full-screen copy costs 0.88 of a frame).
- **Snap window x to even pixels**, from Phase 2: the blitter has no shifter,
  so an odd-parity move falls off the fast path entirely.

And one piece of infrastructure is now overdue rather than optional:
**native-mode interrupt vectors**, wanted by the blitter IRQ, by returning to
DOS, and — the one that cannot be worked around — by any quadrature pointing
device.
