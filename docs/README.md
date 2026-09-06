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

## Across the phases

| | |
|---|---|
| [gacs.md](gacs.md) | The applications gem4xe exists for — GACS and RetroWP — and what a port actually needs. Carries the measurement: GACS's engine compiles for the 65C816 in both data models, runs in Calypsi's simulator against the tables it ships, computes a vehicle, and wants 84 bytes of bank $00. `make gacs-check` keeps asking. |
| [shipping.md](shipping.md) | How gem4xe boots, what it lives on and what language it speaks: the floppies and why one had to become double density, the APT/CF card and the three things about a U1MB machine that had to be measured before it would boot, the install layout, and `LANG.RSC`. |
| [spike-spartados.md](spike-spartados.md) | The reconnaissance behind Phase 13: what SpartaDOS does to memory, measured before any of it was ported. |
| [bench.md](bench.md) | GEMBench's tests on this machine, in milliseconds. Not a gate — a number to argue with. |
