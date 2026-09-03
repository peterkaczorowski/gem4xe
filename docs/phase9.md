# Phase 9 — native-mode interrupts, and the way back to DOS

Status: **complete, in Altirra.** `make test-m10` 27/27, `make test-host`
38/38 (nine new), and the rest of the suite green after it — with two
gates changed because the world they were written in changed (below).
No Rapidus, no VBXE and no mouse has run any of this; the emulator's
device models are what every sign and rate here was checked against.

Since Phase 0 gem4xe had run with NMI and IRQ off. The 65C816 in native
mode takes its vectors from `$FFE4-$FFEF` — COP, BRK, ABORT, NMI, a
reserved slot, IRQ — and the Atari OS ROM fills only the emulation-mode set
at `$FFFA-$FFFF`; the XL OS has `$0000` at `$FFEA`, so the first vertical
blank after `clc; xce` would have jumped to page zero. `src/crt_atari.s`
switched NMIEN and IRQEN off before the `xce`, and everything above polled:
the keyboard from POKEY's latch, the pointer from PORTA or the pots, time by
watching VCOUNT wrap. That was enough for eight phases of gates and useless
for the device people actually plug in — a quadrature mouse loses counts
unless it is sampled thousands of times a second, which no main line can
promise — and it left no way back to DOS, since the OS's own VBI and
keyboard IRQ had been cut off under it. Phase 3a called the vectors "the
third thing waiting"; Phase 4 wrote down the recipe. This phase built it.

## Step 1 — the OS shadowed under itself (`src/sys/irq.c`)

The vectors are in ROM, so the ROM is copied into the RAM that sits under
it. On any XL/XE, PORTB bit 0 low swaps the OS out for RAM at `$C000-$CFFF`
and `$D800-$FFFF`, `$D000-$D7FF` staying hardware either way. The copy is
made a page at a time through a buffer on the stack: read with the ROM in,
write with it out, read back and compared before the next page. Then the
twelve bytes of native vectors are written — the six words the linker gave
`src/sys/irq.s`'s stubs, exported as `irq_vectab` so C never restates an
address — and read back too. The emulation-mode vectors at `$FFF0-$FFFF`
are left exactly as the OS had them, for the day CIO is called through
them. A mismatch anywhere puts the ROM back and leaves the machine in the
polled regime it was in, reported in `irq.how`/`irq.fail` rather than
assumed; with the interrupt sources still off the whole operation is
invisible to the OS.

On a Rapidus the same writes also land in the accelerator's SRAM. Altirra's
`rapidus.cpp` (`UpdateSRAMWindows`) models the semantics: with the OS ROM
enabled, or MCR bit 3 set, the SRAM window under `$C000-$FFFF` is not
selected at all, so the copy cannot be written straight into the SRAM — it
has to go through the RAM under the ROM with the window switched fast and
write-through on, each write reaching both. MCR bit 6 keeps `$D000-$D7FF`
as hardware and is never cleared. Afterwards the interrupt path never
touches the 1.79 MHz bus except for the registers it reads.

This is why `src/sys/rapidus.h`'s "`$C000-$FFFF` is left as found" now says
*by this file*: `rapidus_speedup()` still leaves window 3 alone, and
`irq_install()` — called after it, taking the window as it found it — is
what switches it, and switches it back.

## Step 2 — the handlers (`src/sys/irq.s`)

As little as possible. The vectors are 16-bit and bank `$00`, so five
4-byte `JML` stubs sit in `code`; the handlers are `farcode`, in fast SRAM,
where the timer handler runs some four thousand times a second and must not
share the slow window with MEMAC. Every handler saves what it uses, forces
DB to `$00` — an interrupt can land inside an `MVN` with DB pointing at a
far bank — addresses all state absolute, never through D (the direct page
is wherever the interrupted code put it), and `RTI`s.

- **NMI** is the vertical blank and nothing else (NMIEN enables only the
  VBI; RESET on an XL/XE is a real reset): one 16-bit count, `irq_frames`.
- **IRQ** is POKEY. Whatever is pending among the sources POKMSK enabled is
  acknowledged in one go, bit low then high in IRQEN as the OS does, with
  POKMSK kept consistent. Timer 1 counts `irq_timer`, then — if the pointer
  layer has said so — reads PORTA once and decodes both line pairs through
  three tables `ptr_init()` filled in: `irq_plo`/`irq_phi` pick each axis's
  two lines out of the nibble, `irq_qtab[(prev << 2) | now]` says what the
  transition is worth, and the result goes into two monotonic 16-bit
  counters, `irq_qlo`/`irq_qhi`. Which counter is x and which is y, and
  what the device is, the handler never knows. The keyboard puts KBCODE
  into an 8-deep ring; a full ring drops the newest key, the count still
  says it arrived.
- **COP, BRK, ABORT** record which in `irq_fault` and park with I set, so
  the STATUS block can still be read. ABORT is what a Rapidus raises for a
  hardware-protect violation and would re-execute forever if returned from;
  a BRK is a bug.

The state is declared in C (`src/sys/irq.c`) so its readers can call it
`volatile`, and every consumer runs from the main line and takes a
difference: the counters are never reset, only consumed.

The timer is AUDF1 = 15 on the 64 kHz clock — a sample every 250 µs,
~3.96 kHz PAL — a design constant reported in `irq.timer_div`, so the gate
derives the rate it should see from that and the frame length the target
measured rather than repeating the number: 312 lines of 114 cycles over
28 × 16 is 79.39 interrupts a frame, and 79.39 is what it counted over a
hundred frames.

## Step 3 — the consumers

`src/vdi/pointer.c` fills the handler's tables in `lines_select()`, seeding
the "previous pair" from PORTA as it is now so the first sample is not a
transition from zero, and switches `irq_ptr_on` only for the three relative
devices. `poll_relative()` then adds the counters' differences since it
last looked; off the interrupt regime it decodes one sample through the
same tables, so the two paths cannot disagree about what a transition
means. `ptr_sample()` copies the record and copies it again if anything
moved while it was being copied, since the writer is now an interrupt.

`vdi_key_poll()` drains the ring instead of reading POKEY. `vdi_input_poll()`
calls the timer vector once for every frame `irq_frames` advanced since it
last ran — catching up after a long draw rather than losing ticks — so
`evnt_timer` is accurate to the frame however irregular the polling. That
catch-up broke `make test-m7`'s timer case on the first full run: an
`evnt_timer(60)` completed at once, because `ev_wait_ticks()` sampled its
start after the frames the runner had spent idle and before the poll that
delivered them. `ev_multi` had always polled before taking its start;
`evnt_timer` now does the same, and the backlog goes where real elapsed
time belongs — the double-click delay — and not to the wait that had not
begun. Under the polled regime the most a first poll could hand over was
one tick, which is why the case had four frames of slack and never noticed.

`src/vdi/vdi.h` had said the application polls "because gem4xe runs with
interrupts off". It polls because that is where the AES consumes input;
the sampling that could not wait for it no longer does.

## Step 4 — the way back (`_sys_exit`, `rapidus_restore`, `vbxe_off`)

The runner's script can now ask for DOS (sys op 3001), and the way out is
the way in reversed: `irq_remove()` switches the sources off, waits for an
NMI ANTIC may already have asserted to land while the handlers are still
what the vectors point at, and puts the ROM back; `vbxe_off()` drops the
overlay and closes the MEMAC window; `rapidus_restore()` gives window 0 its
write-through back and copies it onto itself — reads still come from the
SRAM, so everything written since Phase 7's speed-up, the OS variables and
DOS's own state and the direct page and stack included, goes down to the
motherboard — then puts the MCR's speed bits back as found; `_sys_exit()`
sets D and DB to zero, drops into emulation mode, restores DOS's stack
pointer and POKMSK, re-enables the VBI, closes and reopens E: (the runner's
buffers sat over DOS's display list), and `RTS`es to DOS. `irq_remove()`
first, because CIOV runs from the ROM in emulation mode and the ROM has to
be in for that. It has been open in `docs/phase7.md` as "not done" since
the speed-up landed.

## Step 5 — the gate (`tests/emu/m10_irq.py`)

Each step is checked against something the target did not write:

- the shadow: `$C000-$CFFF` and `$D800-$FFE3` as the CPU now reads them
  equal the OS ROM image the emulator was booted with, byte for byte, and
  the two checksums the target reports equal the file's;
- the vectors: `$FFE4-$FFEF` hold the five stub addresses from the linker's
  symbol file, each a `JML`, and `$FFF0-$FFFF` are the ROM's;
- the VBI: `irq_frames` advances by the frames the emulator ran;
- the timer: within 3% of POKEY's rate for the divisor reported, derived
  from the frame length the target measured, not from a PAL assumption;
- the keyboard: one KEY is one entry in the handler's count, and the
  runner's idle loop drains the ring;
- the pointer: a CX80 trak-ball selected through the script, driven through
  a new bridge verb, `JOY`, while the target busy-waits with *nothing*
  polling — the handler counted +20 on x and +10 on y on its own — then
  read back through `vq_mouse` after one poll as (320, 110);
- the exit: RTCLOK advances under the OS's own VBI, page 6 is intact, the
  ROM is back, and a typed key is echoed at DOS's prompt — the OS keyboard
  IRQ running, seen as the display-code `$21` before the cursor in screen
  RAM, because DOS's `E:` GET RECORD takes the key out of `CH` again before
  the harness can look.

`make test-m6` compares the runner's report of the speed map with the
registers, and now expects window 3 fast when the runner says the vectors
took it: the gate had been written when nothing after `rapidus_speedup()`
touched the MCR.

## The phantom joystick

The first run of the trak-ball check counted y *backwards*: −10 where +10
was driven, and PORTA read `$FB` at idle — bit 2, the left line, low —
before the script had touched anything. Not the target: a fresh emulator
with no disk read the same.

AltirraSDL opens every joystick-class device SDL can see
(`input/joystick_sdl3.cpp`, `SDL_INIT_GAMEPAD`) and routes it to port 1
through its input maps; the Adaptive Input feature activates the arrow
key, numpad and gamepad maps on its own, and switching it off in a private
`XDG_CONFIG_HOME` left a map active anyway. The host has two candidates.
The mouse (a ROCCAT Tyon) has a joystick interface with centred axes;
blacklisting it changed nothing. The keyboard, a Keychron Q6 Max, exposes a
"System Control" HID interface that SDL enumerates as a joystick with one
axis and one hat — and the axis is evdev's `ABS_MISC`, reported with
min 1, max 183, value 0: below its own minimum, which SDL scales to full
negative, which AltirraSDL's `ConvertAnalogToDirectionMask` turns into
LEFT, held for the whole run. With both devices on
`SDL_JOYSTICK_BLACKLIST_DEVICES` PORTA reads `$FF` and the counts are what
the JOY verb drove.

The launcher now builds that list from sysfs — every input device's
VID/PID, not a configured pair, since whatever is plugged in today is what
SDL would open, and the list only governs what SDL opens *as a joystick*,
so a real keyboard's id on it costs nothing (`host_joystick_ids()` in
`tools/a8test/launcher.py`). The log then says SDL reports 0 joysticks.
The same lesson as the RAM probe and the far heap: the harness is part of
the machine, and it too has to be read rather than assumed.

## The host side (`tests/host/`)

The JOY verb writes the PIA's four switch lines active-low, nine states in
all, so on the trak-ball's direction-and-pulse lines it can make only
pulses with the direction line high: +x and +y. The ST and Amiga Gray codes
it cannot make at all. So `tests/host/quad_sim.c` walks Altirra's three
device models — the ST and Amiga tables and the trak-ball's direction bit
and pulse, read out of `Altirra/source/inputcontroller.cpp` — through the
target's own `ptr_init()`/`ptr_poll()` in the compiler's simulator, seven
steps in each direction on each axis, and a C copy of the handler's table
arithmetic alongside; `tests/host/irq_stub.c` supplies the handler's state
for a build with no handler, typed from `irq.h` so drift fails to compile.
Every device reads (±7, 0) and (0, ±7), and the handler's arithmetic agreed
with the polled path at every step. `tools/vdiref.py` gained `decode_tb()`,
the reference for a device that is not quadrature, and `test_pointer.py`
pins the `tbdec` table to it and `qdec` to the Gray-code reference as
before.

The signs are the emulator's. Altirra's tables are read from its source,
and a real ST mouse turned the other way would show up as a two-entry swap
in `pointer.c`, but nobody has turned one.

## Honesty

- Everything is Altirra. The Rapidus SRAM semantics the shadow relies on
  are as `rapidus.cpp` models them; Antonia is not emulated and has not
  been considered beyond "it has linear RAM".
- The trak-ball's −x/−y, and the two mice entirely, are proved in the
  simulator against the emulator's device models, not on the emulated
  machine — the bridge cannot drive them — and not on hardware.
- The headless rig still writes to the user's real
  `~/.config/altirra/settings.ini` on every run (that is where the firmware
  registrations live, so a private config loses the XL ROM); pre-existing,
  found while chasing the joystick, not fixed.
- A blitter-complete IRQ is now *possible* and not built: `irq_irq` finds
  nothing pending from anything but POKEY and returns.
- The VBI still does not drive `vdi_input_poll()`; the main line does, and
  the handlers keep what it would miss.

## Lessons

- **A gate assumes the world it was written in.** m6 compared the speed
  map with the registers, and m7 gave a timer four frames of slack; both
  encoded the polled regime without saying so, and both failed the first
  time the regime changed — correctly, and for reasons that took reading
  to see. The fix each time was to make the assumption a term the gate
  reads (`irq.fast`; a poll before the start), not to loosen it.
- **Read the harness the way the hardware is read.** A joystick nobody
  plugged in drove the port for as long as it took to look at what SDL
  had opened. The answer was in sysfs and Altirra's source, not in the
  target.
- **The handler decides nothing.** It reads one byte and indexes three
  tables; what the tables mean is the pointer layer's, and the same tables
  serve the polled path, so the simulator can prove the interrupt's
  arithmetic without an interrupt.

## Next

The application ABI — a `saveds` dispatcher, `contrl`/`intin` copied in
and out, `appl_init`/`appl_exit`, a loadable format — and the file layer
(`rsrc_load`, `fsel_input`, `shel_*`), which needs CIO through the OS and
so emulation-mode trampolines from native code; the emulation vectors were
kept intact for that. Then `form_alert`, icons, `graf_mouse`, and the
desktop.
