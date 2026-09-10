# The notebooks

One document per phase, written as the work happened: what was built,
what was wrong on the way, and how it was found.  They are the reason a
bug that cost a day costs nobody a second day, so they record the false
trails as well as the fixes.  `../README.md` is the current state; these
are how it got there.

Read in order if you want the argument; jump if you want an answer.

## The phases

| | |
|---|---|
| [phase0.md](phase0.md) | The harness and the Calypsi board support. The emulator rig, the ELF→`.xex` packer, the two MEMAC experiments — and why `src/crt_atari.s` exists at all: `clc; xce` on an Atari is fatal, because native mode moves the interrupt vectors and the OS ROM fills only the emulation-mode ones. |
| [phase1.md](phase1.md) | The VBXE surface: the XDL, the palette upload, the MEMAC window, and the blitter **timed on target** — a full-screen fill in 0.45 of a PAL frame, a copy in 0.84. That measurement is why dirty rectangles are a requirement here and not an opinion. |
| [phase2.md](phase2.md) | The VDI: the dispatcher, the five-array parameter block, the two flat jump tables, and the MFDB whose `fd_addr` had to be a `uint32_t` — a native pointer is 16 bits under the small data model and shifted every later field by two bytes. |
| [phase2b.md](phase2b.md) | Text, and the raster masks. One glyph mask serves any ink colour through an AND/OR pair, which is also why mode 6's nibble stencil is unusable. Carries one bug worked around and **never root-caused** — a pointer plus a computed offset inside a loop. |
| [phase3a.md](phase3a.md) | The pointing-device seam: four devices reduced to one absolute position and a button mask, the cursor saved and restored as a blit of **nine** bytes at odd x, and why only the absolute devices worked before Phase 9. |
| [phase3b.md](phase3b.md) | Input plumbing — the `vex_*` vector exchanges, the keyboard polled from POKEY rather than the OS's `CH` — and the VDI complete: all 37 opcodes the AES and the desktop use. |
| [phase4.md](phase4.md) | The AES object library. Two GEM details that were wrong until the gate caught them: `ob_spec` packs its bytes as a 68000 LONG (colour is the low word), and a button's border thickness is computed, not stored. |
| [phase5.md](phase5.md) | Linear memory. `farmem_probe()` writes each bank's own number and reads them all back — and found more RAM than the documentation promises, because Rapidus's low SRAM is contiguous with the SDRAM above it. |
| [phase6.md](phase6.md) | Far code: the program moved to bank `$01`, staged in chunks through `INITAD`, and the bank-`$00` ceiling lifted. **The debugging lesson of the project is here**: three VDI cases failed, a different three per optimisation level, and it was not the compiler — a flag that changes the symptom is not an explanation. |
| [phase7.md](phase7.md) | `form_do` and the event layer — and the largest performance bug so far: the Rapidus resets with every 16 KB window of bank `$00` on the slow bus, so six phases had run all their data at 1.79 MHz without a gate noticing. |
| [phase8.md](phase8.md) | The window manager on dirty rectangles, the control manager nested in the application's wait, and the menu library — with one deliberate departure from the donor, and window x snapped to even so a move is one blit. |
| [phase8b.md](phase8b.md) | A whole AES session on film (`make movie`), and the cost of a pixel: where the time actually goes. |
| [phase8c.md](phase8c.md) | The per-pixel loops, read out of the compiler's own listing rather than guessed at. |
| [phase9.md](phase9.md) | Native-mode interrupts: the OS ROM shadowed into Rapidus SRAM, the 65816's native vectors filled, a POKEY timer at ~4 kHz for the quadrature mice — and the way back to DOS. |
| [phase10.md](phase10.md) | The application ABI. `COP #$73` is the call gate, so an application is a `.G4A` that never links against the system. |
| [phase11.md](phase11.md) | The file layer and the file selector, including the case that went the other way: every returned value agreed and only the screenshots disagreed. |
| [phase12.md](phase12.md) | Alerts, icons, and the pointer's shape. |
| [phase13.md](phase13.md) | SpartaGEM: gem4xe under SpartaDOS 3.2 and SpartaDOS X, where a DOS keeps its own RAM banked in behind the program. |
| [phase14.md](phase14.md) | GEMDOS and the desktop, in seven milestones — plus two Altirra 65C816 core bugs found and patched (`../tools/altirra/`), and the Ultimate 1MB, whose flash turned out to hold the disk driver as well as the DOS. |
| [phase15.md](phase15.md) | What the system says, and what it says it in: `LANG.RSC` far-resident with the English as its fallback, and a loadable 8x8 `.FNT` so a translation can bring its own alphabet — GDOS's four font calls, and none of the rest of GDOS. Also the reason a gate's disk is a fixture: adding one file to it broke two measurements in another gate. | GEMDOS and the desktop, in seven milestones — plus two Altirra 65C816 core bugs found and patched (`../tools/altirra/`), and the Ultimate 1MB, whose flash turned out to hold the disk driver as well as the DOS. |
| [phase16.md](phase16.md) | The GEMDOS gaps: `Dfree` reading the file system's own count through SIO rather than three characters of a directory listing, `Fseek` keeping the position GEMDOS counts in, `Fdatime` taking a stamp out of the directory without disturbing a search, and a clock — the Ultimate 1MB's DS1305, bit-banged. Carries two findings: a DOS 2 that hands back the last sector again when read past the end, and four bits of a clock lost to byte locals sharing a slot. |
| [phase17.md](phase17.md) | The opcodes an application uses and the AES never calls: text metrics, alignment and effects, colour, filled areas and their perimeters, markers, all ten GDPs, the attribute inquiries, `v_get_pixel`, arrowheads, and a paint bucket that discovers its region in far memory so that a patterned fill terminates. Carries the compiler defect that cost the day twice — a signed 16-bit `>>` is not an arithmetic shift — and the reason the runner's disk is double density now. |
| [phase18.md](phase18.md) | Files moved by hand: an item dragged out of a window is a copy, a move with SHIFT — which is the modifier because POKEY reports the shift key on a line of its own and CONTROL only inside the code of a key already down — and the trash is a delete. One dialog with three titles, all of them in the resource. |
| [phase19.md](phase19.md) | Show info, and the rename that lives inside it: one dialog for a file and for a folder, the counts a folder gets from the delete's own walk, and the field a folder does not get — because `XIO 32` renames a file, and answers "file not found" for a directory on both SpartaDOS 3.2 and SpartaDOS X. Measured in the GEMDOS gate rather than reasoned about. |
| [phase20.md](phase20.md) | The bindings an application actually links against: the engine served 130 VDI opcodes and sixty AES calls, and the library reached nine of the first. Every one of them has a name now, gated in the compiler's simulator by recording the parameter block each binding builds — which found a `vqt_width` that put its deltas in the wrong words, in a gap the conformance gate cannot see. |
| [phase21.md](phase21.md) | The kit: eleven files and no part of gem4xe itself, so that somebody who is not this repository can build a program that runs on it. Gated by building it out of a copy of itself in a directory of its own — and by rebuilding the Phase 10 gate application with it, byte for byte, so that what `test-m11` proves carries over. |
| [phase22.md](phase22.md) | What a tester is handed: `make dist`, and the fourteen bytes that were actually missing — a machine that can run gem4xe still cold-boots as a 6502, and until now only the test harness could switch it. The page is generated from the desktop's own menu and from the disk images, so the half that could go stale cannot. |
| [phase23.md](phase23.md) | The loader switches the machine: a Rapidus always cold-boots as a 6502, so gem4xe used to refuse a machine that could have run it. Thirty-five bytes now probe the eight PBI slots for the card — measured, both registers — set `COLDST` and switch it, and `test-boot` does nothing to the machine after power. Bank $00 has twelve bytes left. |
| [phase24.md](phase24.md) | The far allocator's bank boundary: thirty-five bytes of near code turned a gate red, dead padding did the same, and the fault was in far memory — `far_alloc` could hand out a block straddling a bank, and `__far` pointer arithmetic is 16 bits *within* one. The phase 6 corruption by another road, and master had been green by luck. |
| [phase25.md](phase25.md) | The desktop remembers: `Save desktop` and `Read .INF file`, and the start-up order that makes a layout outlive the machine being switched off — the shell buffer, then the file, then the default. Most of it was already there; what was missing was a file. |
| [phase26.md](phase26.md) | More than one thing at a time: shift-click and a rubber band, and the order a press has to be read in — the drag recognised before the click, because SHIFT means "move" to one and "add" to the other. Carries an anomaly nobody has explained: the desktop's stack had to grow, and the low-water mark says it did not need to. |

| [phase27.md](phase27.md) | The View menu's other item: `Show as text`, the same FNODEs the icon grid draws laid out one line each, in columns. |
| [phase28.md](phase28.md) | Two programs that are not tests: a calculator and a clock, written to the application ABI and run from the desktop. |
| [phase29.md](phase29.md) | Two bugs a person found in ten minutes that eight gates had not, both on one path: open a folder, launch the program in it, use it. |
| [phase30.md](phase30.md) | The clock was stopping the mouse. A question about which RTC gem4xe reads turned up the cause of "the mouse is slow as snot" and then "I can't click on anything" — a probe that wrote where it should only have read. |
| [phase31.md](phase31.md) | Size to fit: the View menu's last item, and the first thing in the desktop that scrolls sideways. |
| [phase32.md](phase32.md) | The ANTIC surface: the second display gem4xe can draw on, 320x168 in one bit, in the region the VBXE's MEMAC window would have had. Proof the driver seam is real. |
| [phase33.md](phase33.md) | The VDI on it — the same `vdi.c`, extracted onto a device seam in seven increments with the 86-case gate run after each — and Atari's condensed 6x6 face, because 320 pixels and an 8-wide cell is forty columns and forty columns is not a desktop. |
| [phase34.md](phase34.md) | One binary, two screens: the device chosen when the program starts, and `GEM4XE.CFG` to overrule it from a DOS prompt when the screen is the broken thing. Carries the build lesson — a hand-written prerequisite list is a list that can be wrong, and when it is wrong the compiler is not. |
| [phase35.md](phase35.md) | The model's side of the seam: `vdiref` stops being written to one surface, `devref` grows an ANTIC device beside the VBXE one, and `aesref` takes a `dev` argument — after which the object library and the whole GEM Desktop model run on 320x168 with nothing else edited. Carries the refactoring gate that made it safe: 99 model cases hashed in 2.6 seconds, one digest, unmoved throughout. |

## Across the phases

| | |
|---|---|
| [gacs.md](gacs.md) | The applications gem4xe exists for — GACS and RetroWP — and what a port actually needs. Carries the measurement: GACS's engine compiles for the 65C816 in both data models, runs in Calypsi's simulator against the tables it ships, computes a vehicle, and wants 84 bytes of bank $00. `make gacs-check` keeps asking. |
| [shipping.md](shipping.md) | How gem4xe boots, what it lives on and what language it speaks: the floppies and why one had to become double density, the APT/CF card and the three things about a U1MB machine that had to be measured before it would boot, the install layout, and `LANG.RSC`. |
| [spike-spartados.md](spike-spartados.md) | The reconnaissance behind Phase 13: what SpartaDOS does to memory, measured before any of it was ported. |
| [bench.md](bench.md) | GEMBench's tests on this machine, in milliseconds. Not a gate — a number to argue with. |
