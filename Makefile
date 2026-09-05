# gem4xe -- GEM for the Atari 8-bit (VBXE + Rapidus + U1MB)
#
#   make            build/hello.xex and the bootable test disk
#   make test-emu   Phase 0 hardware gate (VBXE / Rapidus / MEMAC / CPU switch)
#   make test-m1    Milestone 1: Calypsi C running on the 65C816
#   make test-m6    the far code really is in, and running from, bank $01
#   make test-m7    evnt_* and form_do under host-driven input
#   make test-m8    the window manager: rectangle lists, moves, WM_REDRAW
#   make test-m9    menus: the bar, drop-downs, MN_SELECTED under host input
#   make test-m10   native-mode interrupts: the ROM shadow, VBI, timer, keys,
#                   a trak-ball counted in the handler, and the way back to DOS
#   make test-m11   the application ABI: a separately linked program loaded,
#                   relocated and run, calling GEM through COP
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

# Large code puts every C function in `farcode`, which src/gem4xe.scm places
# in banks $01 upwards -- one linker memory per bank, filled in order, so no
# function straddles a bank -- and src/farload.s copies up as DOS loads the
# file.  Data stays small -- globals and constants are addressed through the
# data bank register, so they have to remain in bank $00 (see the linker
# script).
CFLAGS    = --code-model=large --data-model=small -O2
# --override lets src/sys/div16.o replace the library's _Div16/_Mod16, which
# leave the wrong flags for the compiler's own `beq` (see that file).
LDFLAGS   = --rtattr exit=simplified --override _Div16 --override _Mod16

SRC_DOS  ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['dos']['sd_dos2'])" 2>/dev/null)
# SpartaDOS 3.2 boot disk and the SpartaDOS X cartridge, [spartados] in
# fixtures.toml -- the SpartaGEM gate (docs/phase13.md) boots the one and
# then the other, with the same program.
SRC_SP32 ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['spartados']['disk_32'])" 2>/dev/null)
SRC_SDX  ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['spartados']['sdx_cart'])" 2>/dev/null)
# An Ultimate 1MB flash image carrying SpartaDOS X (test-m14u, test-m15u):
# needs the patched emulator's --u1mbrom (tools/altirra/), ALTIRRASDL=...
SRC_U1MB ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['u1mb']['flash'])" 2>/dev/null)

HELLO_OBJS = build/crt_atari.o build/farload.o build/div16.o build/hello.o
M2_OBJS    = build/crt_atari.o build/farload.o build/div16.o build/m2_vbxe.o build/vbxe.o
M3_OBJS    = build/crt_atari.o build/farload.o build/div16.o build/m3_vdi.o build/vdi.o build/pointer.o build/objc.o build/graf.o build/event.o build/grlib.o build/form.o build/alert.o build/wind.o build/ctrl.o build/menu.o build/farmem.o build/rapidus.o build/irq.o build/irqs.o build/abi.o build/abis.o build/app.o build/apppool.o build/cio.o build/cios.o build/dos.o build/gemdos.o build/rsrc.o build/shel.o build/app_blob.o build/font8x8.o build/fillpat.o build/vbxe.o build/fsel.o build/fsel_rsc.o build/gemdata.o

# GEM.COM, the product (src/gem.c): the runner's objects with the runner
# itself and its compiled-in test application taken out, linked on the
# same rules with the application pool given the whole of $4000-$7FFF
# (src/gem4xe.scm, `layout`).
GEM_OBJS   = $(filter-out build/m3_vdi.o build/app_blob.o,$(M3_OBJS)) build/gem.o

# A gem4xe application: its own C startup and bindings (src/app), linked
# against nothing of gem4xe's, on the application's own linker rules.
APP_OBJS   = build/app/crt_gemapp.o build/app/gemabi.o build/app/gemlib.o build/app/m11_app.o
# Its near budget (src/app/gemapp.scm): the stack and data, then the
# constants; and the stack's share of the first.  Decimal, because a `#`
# in a make variable starts a comment.
APP_BSS    = 2048
APP_BITS   = 256
APP_STACK  = 256

# Everything a gate boots, and the product: a plain `make` leaves no disk
# behind its sources (a gate run by hand, rather than through its test-m*
# target, otherwise boots a stale image and compares it against a fresh
# linker map).
all: build/hello-boot.atr build/m2-boot.atr build/m3-boot.atr build/m6split-boot.atr \
     build/m12-d2.atr build/m14-boot.atr build/m17-boot.atr build/gem-boot.atr build/gem-sp.atr

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
build/m3_vdi.o:  src/m3_vdi.c  src/vbxe/vbxe.h src/vdi/vdi.h src/sys/irq.h src/sys/abi.h src/sys/app.h src/sys/cio.h src/sys/dos.h

build/vdi.o: src/vdi/vdi.c src/vdi/vdi.h src/vdi/pointer.h src/vbxe/vbxe.h src/sys/irq.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/pointer.o: src/vdi/pointer.c src/vdi/pointer.h src/vdi/vdi.h src/sys/irq.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/objc.o: src/aes/objc.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/graf.o: src/aes/graf.c src/aes/aes.h src/vdi/vdi.h build/gemdata.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

build/event.o: src/aes/event.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/grlib.o: src/aes/grlib.c src/aes/aes.h src/vdi/vdi.h build/gemdata.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

build/alert.o: src/aes/alert.c src/aes/aes.h src/sys/app.h build/gemdata.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

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

# The interrupt regime: the C side installs and removes it, the assembly
# side is the handlers and the bank-$00 stubs the vectors point at.
build/irq.o: src/sys/irq.c src/sys/irq.h src/sys/rapidus.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/irqs.o: src/sys/irq.s
	@mkdir -p build
	$(AS) -o $@ $<

# The application ABI: the COP handler and the far-call trampoline in
# assembly, the parameter-block copy-in/out and the AES's crysbind in C,
# then the loader and the linker-reported bounds of its bank-$00 pool.
build/abi.o: src/sys/abi.c src/sys/abi.h src/vdi/vdi.h src/aes/aes.h src/sys/gemdos.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/abis.o: src/sys/abi.s
	@mkdir -p build
	$(AS) -o $@ $<

build/app.o: src/sys/app.c src/sys/app.h src/sys/abi.h src/sys/cio.h src/sys/dos.h src/sys/farmem.h src/sys/gemdos.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/apppool.o: src/sys/apppool.s
	@mkdir -p build
	$(AS) -o $@ $<

# The file layer's floor: CIO through the OS, the IOCB side in C and the
# round trip into emulation mode in assembly.
build/cio.o: src/sys/cio.c src/sys/cio.h src/sys/irq.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/cios.o: src/sys/cio.s
	@mkdir -p build
	$(AS) -o $@ $<

# The DOS seam: which DOS booted, its path syntax, its listing's marks.
build/dos.o: src/sys/dos.c src/sys/dos.h src/sys/cio.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# GEMDOS for the applications: the ST's trap #1 on CIO, through the seam.
build/gemdos.o: src/sys/gemdos.c src/sys/gemdos.h src/sys/dos.h src/sys/cio.h src/sys/farmem.h src/sys/app.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# The file layer proper: the resource loader and the shell library.
build/rsrc.o: src/aes/rsrc.c src/aes/aes.h src/sys/app.h src/sys/cio.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# The AES's own artwork -- eight mouse forms, three alert icons -- from the
# one script the reference reads them from too.
build/gemdata.c build/gemdata.h: tools/gemdata.py
	@mkdir -p build
	python3 tools/gemdata.py build/gemdata.c build/gemdata.h
build/gemdata.o: build/gemdata.c src/aes/aes.h
	$(CC) $(CFLAGS) -I src -o $@ $<

build/fsel_rsc.c build/fsel_rsc.h: tools/fselrsc.py tools/rsc.py tools/aesref.py
	@mkdir -p build
	python3 tools/fselrsc.py build/fsel_rsc.c build/fsel_rsc.h
build/fsel_rsc.o: build/fsel_rsc.c
	$(CC) $(CFLAGS) -o $@ $<
build/fsel.o: src/aes/fsel.c src/aes/aes.h src/sys/app.h src/sys/cio.h src/sys/dos.h src/sys/farmem.h build/fsel_rsc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<
build/shel.o: src/aes/shel.c src/aes/aes.h src/sys/app.h src/sys/cio.h src/sys/dos.h src/sys/farmem.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# A .G4A program (src/app/*: the startup and the bindings, plus its own
# body) is linked three times -- at its placeholder addresses, with the
# near region up a page, with the far region up a bank -- so that
# tools/mkg4a.py can find every byte that depends on where it is loaded.
# $(call g4a,name,objects,near bss,near bits,stack,more mkg4a args,more targets)
# The stack is part of the near bss (src/app/gemapp.scm); its size is the
# linker's --stack-size, so the map shows what each application asked for.
G4A_LIB = build/app/crt_gemapp.o build/app/gemabi.o build/app/gemlib.o
define g4a
build/$(1).elf: $(2) src/app/gemapp.scm
	$$(LD) src/app/gemapp.scm $(2) $$(LIB) --rtattr exit=simplified --cstartup gemapp -o $$@ \
	    --list-file build/$(1).map --stack-size $(5) \
	    --memories-expression "(app-layout #x1000 #x020000 $(3) $(4))"
build/$(1)-near.elf: $(2) src/app/gemapp.scm
	$$(LD) src/app/gemapp.scm $(2) $$(LIB) --rtattr exit=simplified --cstartup gemapp -o $$@ \
	    --stack-size $(5) --memories-expression "(app-layout #x1100 #x020000 $(3) $(4))"
build/$(1)-far.elf: $(2) src/app/gemapp.scm
	$$(LD) src/app/gemapp.scm $(2) $$(LIB) --rtattr exit=simplified --cstartup gemapp -o $$@ \
	    --stack-size $(5) --memories-expression "(app-layout #x1000 #x030000 $(3) $(4))"
build/$(1).g4a build/$(1).sym $(7): build/$(1).elf build/$(1)-near.elf build/$(1)-far.elf tools/mkg4a.py
	python3 tools/mkg4a.py build/$(1).elf build/$(1)-near.elf build/$(1)-far.elf \
	        build/$(1).g4a --syms build/$(1).sym $(6)
endef

build/app/%.o: src/app/%.s
	@mkdir -p build/app
	$(AS) -o $@ $<

build/app/%.o: src/app/%.c src/app/gem.h
	@mkdir -p build/app
	$(CC) $(CFLAGS) -I src/app -o $@ $<

build/app/%.o: src/%.c src/app/gem.h
	@mkdir -p build/app
	$(CC) $(CFLAGS) -I src/app -o $@ $<

# The gate application (src/m11_app.c).  The .g4a is what a loader reads
# from disk; the C array is the same bytes for the runner to load from
# the image, there being no file layer yet.
$(eval $(call g4a,m11_app,$(APP_OBJS),$(APP_BSS),$(APP_BITS),$(APP_STACK),--c-array build/app_blob.c app_blob,build/app_blob.c))

build/app_blob.o: build/app_blob.c
	$(CC) $(CFLAGS) -o $@ $<

# The desktop (src/desk): a bigger near region than the gate application's,
# for the object trees a desktop keeps in bank $00, and DESKTOP.RSC beside
# it on the disk (tools/deskrsc.py; the icons from EmuTOS desk/icons.c
# through tools/iconconv.py, checked in like the font).  The milestone-3
# desktop (src/m16_desk.c: a line of help, R/X/Q) stays as the stand-in
# test-m16 drives the shell loop with.
# The near region is 16 pages: the direct page, the bss, the constants.
# The bss holds the desktop's globals (GLOBES, src/desk/desk.h: the screen
# tree, the window nodes, the icon records, ~2 KB) and its stack, which
# is the gate application's 256 bytes and more: the folder window's open
# -- the button, do_open, do_dopen, do_wopen, a call to the AES on top --
# ran 256 bytes out (phase 14, milestone 5).  It shares the pool with
# DESKTOP.RSC (4652 bytes) and with GEMDOS's work area (src/sys/gemdos.c),
# which is why the desktop gates run a runner whose staging leaves the
# pool the room GEM.COM leaves it (build/m3desk.xex, above).
DESK_OBJS  = $(G4A_LIB) build/desk/desktop.o build/desk/deskobj.o build/desk/deskwin.o \
             build/desk/deskfun.o
DESK_BSS   = 2944
DESK_BITS  = 768
DESK_STACK = 640
DESK_H     = src/app/gem.h src/desk/desk.h build/deskrsc.h

build/desk/%.o: src/desk/%.c $(DESK_H)
	@mkdir -p build/desk
	$(CC) $(CFLAGS) -I src/app -I build -o $@ $<

$(eval $(call g4a,desktop,$(DESK_OBJS),$(DESK_BSS),$(DESK_BITS),$(DESK_STACK)))
$(eval $(call g4a,m16_desk,$(G4A_LIB) build/app/m16_desk.o,$(APP_BSS),$(APP_BITS),$(APP_STACK)))

tools/deskicons.py: tools/iconconv.py
	python3 tools/iconconv.py $(EMUTOS)/desk/icons.c $@

build/desktop.rsc build/deskrsc.h: tools/deskrsc.py tools/rsc.py tools/aesref.py tools/deskicons.py
	@mkdir -p build
	python3 tools/deskrsc.py build/desktop.rsc build/deskrsc.h

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

build/gem.elf: $(GEM_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(GEM_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/gem.map \
	      --memories-expression "(layout #x010000 #x7fff)"

build/gem.xex: build/gem.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry --syms build/gem.sym

# --syms lets the conformance harness find vdi_script by name instead of
# hard-coding an address that moves on every rebuild.
build/m3.xex: build/m3.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry --syms build/m3.sym

# The desktop gates' runner: the same code with the staging buffers cut
# to the few calls they stage (the prelude, the shell op, the pool
# probes) and the pool run up to where that staging starts, so the
# desktop and DESKTOP.RSC are loaded into a pool the size GEM.COM gives
# them rather than the conformance runner's half of the window.  One
# object differs, so the link is the same list with it swapped in.
M3DESK_SIZES = -D SCRIPT_WORDS=128 -D SCRATCH_BYTES=512 -D MAX_RESULTS=24
M3DESK_OBJS  = $(patsubst build/m3_vdi.o,build/m3desk_vdi.o,$(M3_OBJS))

build/m3desk_vdi.o: src/m3_vdi.c src/vbxe/vbxe.h src/vdi/vdi.h src/sys/irq.h src/sys/abi.h src/sys/app.h src/sys/cio.h src/sys/dos.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(M3DESK_SIZES) -I src -o $@ $<

build/m3desk.elf: $(M3DESK_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M3DESK_OBJS) -o $@ $(LIB) $(LDFLAGS) \
	      --list-file build/m3desk.map \
	      --memories-expression "(layout #x010000 #x78ff)"

build/m3desk.xex: build/m3desk.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry --syms build/m3desk.sym

# The runner outgrew a single-density disk (620 free sectors; the .xex
# alone wants more), so the fixture is converted to DOS 2.5 enhanced density
# on the way (tools/atr.py enhance()), and the program goes into the upper
# half FIRST -- the sectors a DOS 2.0 cannot reach -- so that every boot
# proves the fixture DOS reads them.  The same disk build serves the small
# programs too: one disk format, not one that works until the file grows.
DISK_DENSITY = --enhanced --high

# The disk also carries what the file layer's gate reads back through CIO
# (tests/emu/m12_file.py): a text fixture and a resource file built on
# the host (tools/mkrsc.py), so rsrc_load has something to load.  OUT.TXT
# is on it because the gate WRITES that name: the emulator's disk changes
# under the run while the image on the host does not, and the selector's
# listing is predicted from the image.  A file that is already there is
# overwritten, so the directory the selector reads is the one the host
# read -- with the name on both sides rather than neither.
DISK_FILES = --add tests/fixtures/test.txt TEST.TXT --add build/test.rsc TEST.RSC \
             --add tests/fixtures/out.txt OUT.TXT

# The applications the shell loop runs (docs/phase14.md, milestone 3): the
# desktop, and the gate application as the program the desktop launches.
# On the runner's disks for test-m16, on the product's because that is the
# product.
SHELL_FILES = --add build/m16_desk.g4a DESKTOP.G4A --add build/m11_app.g4a M11.G4A
SHELL_DEPS  = build/m16_desk.g4a build/m11_app.g4a
DESK_FILES  = --add build/desktop.g4a DESKTOP.G4A --add build/desktop.rsc DESKTOP.RSC \
              --add build/m11_app.g4a M11.G4A
DESK_DEPS   = build/desktop.g4a build/desktop.rsc build/m11_app.g4a

build/test.rsc: tools/mkrsc.py tools/rsc.py tools/aesref.py
	@mkdir -p build
	python3 tools/mkrsc.py $@

# The selector's second drive: the fixture DOS disk with fifteen small
# files on it, D2: when test-m12 boots (tools/mkfsdisk.py).
build/m12-d2.atr: tools/mkfsdisk.py tools/atr.py
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkfsdisk.py "$(SRC_DOS)" $@

# The runner's DOS 2 disk has no room for the shell's applications: DOS
# II+/D's 1040 sectors hold M3.COM's 820 and the fixtures, and 35 more.
# The shell gate (test-m16) runs on the SpartaDOS disk, whose size is
# ours to choose (tools/mkspdisk.py --sectors).
build/m3-boot.atr: build/m3.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M3.COM $(DISK_DENSITY) $(DISK_FILES)

# The product disks: GEM.COM with the desktop beside it, one per DOS.  Its
# own disks because GEM.COM and M3.COM are 95 KB each and neither DOS's
# 1040 sectors hold both.  The DOS II+/D disk holds GEM.COM and the
# desktop alone, and the fixture's demonstration programs come off it to
# make the room: with the folder windows the three were 1,000 of its 1,010
# sectors, and running a program from the desktop took it past them.  The
# gate fixtures and M11.G4A are on the SpartaDOS disk only (tools/atr.py
# knows no double density, which is where DOS II+/D would go).
build/gem-boot.atr: build/gem.xex build/desktop.g4a build/desktop.rsc
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ GEM.COM $(DISK_DENSITY) \
	    --remove XMST.COM --remove XMST.TXT --remove MEMTEST.COM --remove MEMTEST.DOC \
	    --add build/desktop.g4a DESKTOP.G4A --add build/desktop.rsc DESKTOP.RSC

build/gem-sp.atr: build/gem.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(DESK_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) --name GEM.COM $(DISK_FILES) $(DESK_FILES)

# The SpartaDOS disk: a fresh SDFS volume booting the 3.2 fixture's DOS,
# the same files as the DOS 2 disk, the shell's applications and a
# directory tree for the selector (tools/mkspdisk.py).  2048 sectors of
# 128: SpartaDOS loads M3.COM's 100 KB from anywhere, where DOS 2.5 could
# not go past sector 720 -- and 1040 no longer hold it with the desktop
# beside it.  The gates size everything from the image, not from here.
SP_SECTORS = --sectors 2048
build/m14-boot.atr: build/m3.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(SHELL_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) $(DISK_FILES) $(SHELL_FILES)

# The desktop gate's disk (test-m17): the runner again, with the real
# desktop and its resource where test-m16's stand-in was.
build/m17-boot.atr: build/m3desk.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(DESK_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) $(DISK_FILES) $(DESK_FILES)

# The same program linked with bank $01 cut down to its top 16 KB, so that
# the far image is forced to spill into bank $02 today rather than on the day
# the code grows past 64 KB.  test-m6 boots this one as well as the real
# build and requires both to copy up, run from the bank the linker chose, and
# start the far heap above it.  The layout function is in src/gem4xe.scm.
build/m6split.elf: $(M3_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M3_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/m6split.map \
	      --memories-expression "(layout #x01c000 #x67ff)"

build/m6split.xex: build/m6split.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry --syms build/m6split.sym

build/m6split-boot.atr: build/m6split.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M3.COM $(DISK_DENSITY)

build/m2-boot.atr: build/m2.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M2.COM $(DISK_DENSITY)

build/hello-boot.atr: build/hello.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ HELLO.COM $(DISK_DENSITY)

test: test-host check-cc test-emu test-m1 test-m2 test-m3 test-m4 test-m5 test-m6 test-m7 test-m8 test-m9 test-m10 test-m11 test-m12 test-m13 test-m14 test-m14x test-m15 test-m15x test-m15d test-m16 test-m17 test-m18 test-m19

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

test-m6: build/m3-boot.atr build/m6split-boot.atr
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

# Native-mode interrupts: the OS ROM shadowed under itself and the vectors
# filled, then each source counted against the emulator -- frames, the
# POKEY timer's rate, keys into the ring, a trak-ball's counts taken in
# the handler with nothing polling -- and finally the return to DOS.
test-m10: build/m3-boot.atr
	python3 tests/emu/m10_irq.py

# The application ABI: the gate application is loaded from the blob in the
# image, relocated into the pool and a far bank, and run; every word it
# got back through the ABI's copy-out is compared with the reference's,
# and the screen with the reference's screen.
test-m11: build/m3-boot.atr build/m11_app.sym
	python3 tests/emu/m11_abi.py

# The file layer: CIO called through the OS from native mode, rsrc_load
# and rsrc_obfix against tools/rsc.py, the shell library's buffers, and
# the file selector driven over two disks -- its listings predicted from
# the images with tools/atr.py, its screens compared with the reference's.
test-m12: build/m3-boot.atr build/m12-d2.atr
	python3 tests/emu/m12_file.py

# Alerts and the pointer's shape: form_alert's parsing, layout and drawing
# against the reference, and each of graf_mouse's forms on the screen.
test-m13: build/m3-boot.atr
	python3 tests/emu/m13_alert.py

# SpartaGEM: the same program on a SpartaDOS disk, loaded by SpartaDOS
# 3.2g from it (test-m14) and by SpartaDOS X 4.50 from the vendor's
# emulator cartridge with the disk as D1: (test-m14x).  The DOS seam,
# CIO in the directory tree and the file selector walking it, against
# the image and the reference.
test-m14: build/m14-boot.atr
	python3 tests/emu/m14_sparta.py

test-m14x: build/m14-boot.atr
	@test -n "$(SRC_SDX)" || { echo "no SDX fixture: set [spartados].sdx_cart in fixtures.toml"; exit 1; }
	python3 tests/emu/m14_sparta.py --sdx="$(SRC_SDX)"

# ... and SpartaDOS X from an Ultimate 1MB flash image, U1MB switched on
# (test-m14u, test-m15u): the machine as it is actually built.  Not in
# `make test`: it needs the patched emulator and a saved BIOS profile.
test-m14u: build/m14-boot.atr
	@test -n "$(SRC_U1MB)" || { echo "no U1MB fixture: set [u1mb].flash in fixtures.toml"; exit 1; }
	python3 tests/emu/m14_sparta.py --u1mb="$(SRC_U1MB)"

# GEMDOS on CIO: the ST's trap #1 as a call block, answered from CIO and
# the DOS seam -- searches, paths, files, far memory, errors -- against the
# image, on SpartaDOS 3.2g (test-m15), SpartaDOS X (test-m15x) and DOS 2
# (test-m15d), whose flat directory answers the tree calls with EPTHNF.
test-m15: build/m14-boot.atr
	python3 tests/emu/m15_gdos.py

test-m15x: build/m14-boot.atr
	@test -n "$(SRC_SDX)" || { echo "no SDX fixture: set [spartados].sdx_cart in fixtures.toml"; exit 1; }
	python3 tests/emu/m15_gdos.py --sdx="$(SRC_SDX)"

test-m15u: build/m14-boot.atr
	@test -n "$(SRC_U1MB)" || { echo "no U1MB fixture: set [u1mb].flash in fixtures.toml"; exit 1; }
	python3 tests/emu/m15_gdos.py --u1mb="$(SRC_U1MB)"

test-m15d: build/m3-boot.atr build/m12-d2.atr
	python3 tests/emu/m15_gdos.py --dos2

# The shell loop: sh_main runs DESKTOP.G4A, what it asks for, the desktop
# again, until it asks to shut down -- with the harness at the keyboard
# and the screen checked against the model at each stop.  On the SpartaDOS
# disk, the only runner disk with room for the .G4A files.
test-m16: build/m14-boot.atr
	python3 tests/emu/m16_shell.py

# The desktop: DESKTOP.G4A under the shell, driven at the mouse and checked
# against tools/deskref.py -- the desktop itself transcribed against the AES
# model (phase 14, milestone 4).
test-m17: build/m17-boot.atr build/desktop.g4a build/desktop.sym
	python3 tests/emu/m17_desktop.py

# A program run from the desktop and the desktop's windows back after it
# (phase 14, milestone 6): two runs of the desktop against the model, with
# M11.G4A between them.
test-m18: build/m17-boot.atr build/desktop.g4a build/desktop.sym
	python3 tests/emu/m18_launch.py

# The desktop's first writes to a disk (phase 14, milestone 7): File ->
# New folder and File -> Delete, driven at the mouse and the keyboard
# against the model, and the disk image read back when the run is over.
# The gate boots a copy of milestone 5's disk, made afresh every run --
# it is the first whose target rewrites the directory it booted from.
test-m19: build/m17-boot.atr build/desktop.g4a build/desktop.sym
	python3 tests/emu/m19_files.py

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

.PHONY: all test test-host check-cc test-emu test-m1 test-m2 test-m3 test-m4 test-m5 test-m6 test-m7 test-m8 test-m9 test-m10 test-m11 test-m12 test-m13 test-m14 test-m14x test-m14u test-m15 test-m15x test-m15u test-m15d test-m16 test-m17 test-m18 test-m19 demo movie bench emu-stop clean
