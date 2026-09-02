# gem4xe -- GEM for the Atari 8-bit (VBXE + Rapidus + U1MB)
#
#   make            build/hello.xex and the bootable test disk
#   make test-emu   Phase 0 hardware gate (VBXE / Rapidus / MEMAC / CPU switch)
#   make test-m1    Milestone 1: Calypsi C running on the 65C816
#   make test-m6    the far code really is in, and running from, bank $01
#   make test-m7    evnt_* and form_do under host-driven input
#   make test-m8    the window manager: rectangle lists, moves, WM_REDRAW
#   make test-m9    menus: the bar, drop-downs, MN_SELECTED under host input
#   make check-cc   the compiler bugs we work around, in the vendor's simulator
#   make bench      GEMBench's tests on this machine, in milliseconds (docs/bench.md)
#   make test       all of them
#   make emu-stop   kill leftover emulators (never use pkill -f: it kills the shell)

CALYPSI  ?= $(HOME)/dev/toolchains/calypsi-65816
EMUTOS   ?= $(HOME)/dev/emutos
CC        = $(CALYPSI)/bin/cc65816
AS        = $(CALYPSI)/bin/as65816
LD        = $(CALYPSI)/bin/ln65816
LIB       = clib-lc-sd.a

# Large code puts every C function in `farcode`, which src/gem4xe.scm places in
# bank $01 and src/farload.s copies up as DOS loads the file.  Data stays small
# -- globals and constants are addressed through the data bank register, so
# they have to remain in bank $00 (see the linker script).
CFLAGS    = --code-model=large --data-model=small -O2
# --override lets src/sys/div16.o replace the library's _Div16/_Mod16, which
# leave the wrong flags for the compiler's own `beq` (see that file).
LDFLAGS   = --rtattr exit=simplified --override _Div16 --override _Mod16

SRC_DOS  ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['dos']['sd_dos2'])" 2>/dev/null)

HELLO_OBJS = build/crt_atari.o build/farload.o build/div16.o build/hello.o
M2_OBJS    = build/crt_atari.o build/farload.o build/div16.o build/m2_vbxe.o build/vbxe.o
M3_OBJS    = build/crt_atari.o build/farload.o build/div16.o build/m3_vdi.o build/vdi.o build/pointer.o build/objc.o build/graf.o build/event.o build/grlib.o build/form.o build/wind.o build/ctrl.o build/menu.o build/farmem.o build/rapidus.o build/font8x8.o build/fillpat.o build/vbxe.o

all: build/hello-boot.atr build/m2-boot.atr build/m3-boot.atr

build/%.o: src/%.s
	@mkdir -p build
	$(AS) -o $@ $<

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/vbxe.o: src/vbxe/vbxe.c src/vbxe/vbxe.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src/vbxe -o $@ $<

build/m2_vbxe.o: src/m2_vbxe.c src/vbxe/vbxe.h
build/m3_vdi.o:  src/m3_vdi.c  src/vbxe/vbxe.h src/vdi/vdi.h

build/vdi.o: src/vdi/vdi.c src/vdi/vdi.h src/vdi/pointer.h src/vbxe/vbxe.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/pointer.o: src/vdi/pointer.c src/vdi/pointer.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/objc.o: src/aes/objc.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/graf.o: src/aes/graf.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/event.o: src/aes/event.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/grlib.o: src/aes/grlib.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/form.o: src/aes/form.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/wind.o: src/aes/wind.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/ctrl.o: src/aes/ctrl.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/menu.o: src/aes/menu.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/div16.o: src/sys/div16.s
	@mkdir -p build
	$(AS) -o $@ $<

build/farmem.o: src/sys/farmem.c src/sys/farmem.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/rapidus.o: src/sys/rapidus.c src/sys/rapidus.h src/vbxe/vbxe.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# The GEM 8x8 system font, extracted from EmuTOS (GPL v2+) by fontconv.py.
# Checked in so the host reference reads the same bytes the target links.
build/font8x8.o: src/vdi/font8x8.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

src/vdi/font8x8.c:
	python3 tools/fontconv.py $(EMUTOS)/bios/fnt_st_8x8.c $@

# The standard fill patterns -- dithers, OEM patterns, hatches -- extracted
# from EmuTOS's vdi_fill.c by patconv.py, likewise checked in.
build/fillpat.o: src/vdi/fillpat.c src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

src/vdi/fillpat.c:
	python3 tools/patconv.py $(EMUTOS)/vdi/vdi_fill.c $@

build/hello.elf: $(HELLO_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(HELLO_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/hello.map

build/m2.elf: $(M2_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M2_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/m2.map

# _atari_entry, not __program_start: the stub must disable NMI/IRQ in emulation
# mode before the library startup's `xce`.  See src/crt_atari.s.
build/hello.xex: build/hello.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry

build/m3.elf: $(M3_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M3_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/m3.map

build/m2.xex: build/m2.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry

# --syms lets the conformance harness find vdi_script by name instead of
# hard-coding an address that moves on every rebuild.
build/m3.xex: build/m3.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry --syms build/m3.sym

build/m3-boot.atr: build/m3.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M3.COM

build/m2-boot.atr: build/m2.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M2.COM

build/hello-boot.atr: build/hello.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ HELLO.COM

test: test-host check-cc test-emu test-m1 test-m2 test-m3 test-m4 test-m5 test-m6 test-m7 test-m8 test-m9

# The cc65816 code generation bugs gem4xe works around, run in the vendor's
# own simulator: fails only if a workaround shape has stopped compiling
# right; a bug that has gone away is reported so its workaround can go.
check-cc:
	python3 tools/ccbug/check.py --calypsi $(CALYPSI)

test-host:
	python3 -m unittest discover -s tests/host -t .

test-emu: 
	python3 tests/emu/p0_probe.py

test-m1: build/hello-boot.atr
	python3 tests/emu/m1_toolchain.py

test-m2: build/m2-boot.atr
	python3 tests/emu/m2_vbxe.py

test-m3: build/m3-boot.atr
	python3 tests/emu/m3_vdi.py

test-m4: build/m3-boot.atr
	python3 tests/emu/m4_aes.py

test-m5: build/m3-boot.atr
	python3 tests/emu/m5_farmem.py

test-m6: build/m3-boot.atr
	python3 tests/emu/m6_farcode.py

# The event and form layer driven from the host: keys through POKEY, the
# pointer through ptr_state, frames counted -- and the reference walks the
# same input plan.
test-m7: build/m3-boot.atr
	python3 tests/emu/m7_form.py

# The window manager: wind_* against the reference's model of the rectangle
# lists, with the redraw messages the application would get and the pixels
# of every uncover, move and slider change.
test-m8: build/m3-boot.atr
	python3 tests/emu/m8_wind.py

# The menu library: menu_bar and the calls that change a menu tree, then
# hovers and presses through the bar while the application waits, with
# screenshots taken inside the wait for the drop-downs themselves.
test-m9: build/m3-boot.atr
	python3 tests/emu/m9_menu.py

# A GEM-style desktop drawn entirely through the 37 VDI opcodes, screenshotted
# and checked against the reference.  A demo that is also a regression test.
demo: build/m3-boot.atr
	python3 tests/emu/demo_desktop.py

# A session with the AES itself -- menu, dialog, a window opened, dragged,
# sized, fulled and closed -- driven through the emulator's input and
# captured a frame at a time into build/movie/gem4xe.mp4 and .gif.  Every
# result and screenshot is checked against the reference as the gates are.
# `--keep-frames` keeps the PNGs; `--dry` runs the model only.
movie: build/m3-boot.atr
	python3 tests/emu/demo_aes.py

# GEMBench's tests, shaped for this machine: the dialog, text, graphics,
# window, divide, float, RAM, ROM and blit rows timed to a VCOUNT tick
# and reported in milliseconds.  Not a gate; the baseline is docs/bench.md.
bench: build/m3-boot.atr
	python3 tests/emu/bench_gem.py

# NEVER `pkill -f AltirraSDL` here: the pattern matches this shell too and
# takes the session with it.  pgrep -x matches the process NAME only.
# An emulator halted at a debugger breakpoint ignores SIGTERM, so escalate
# rather than reporting a kill that did not happen.
emu-stop:
	@pgrep -x AltirraSDL | while read p; do kill $$p 2>/dev/null; done; true
	@sleep 1; pgrep -x AltirraSDL | while read p; do \
		kill -9 $$p 2>/dev/null && echo "emu-stop: SIGKILL $$p (was wedged)"; done; true
	@n=$$(pgrep -x AltirraSDL | wc -l); echo "emu-stop: $$n emulator(s) left"

clean:
	rm -rf build

.PHONY: all test test-host check-cc test-emu test-m1 test-m2 test-m3 test-m4 test-m5 test-m6 test-m7 test-m8 test-m9 demo movie bench emu-stop clean
