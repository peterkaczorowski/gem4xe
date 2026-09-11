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
#   make test-m26   one binary, two screens: GEM.COM with a VBXE, without
#                   one, and in safe mode from GEM4XE.CFG
#   make check-cc   the compiler bugs we work around, in the vendor's simulator
#   make bench      GEMBench's tests on this machine, in milliseconds (docs/bench.md)
#   make test       all of them
#   make emu-stop   kill leftover emulators (never use pkill -f: it kills the shell)

CALYPSI  ?= $(HOME)/dev/toolchains/calypsi-65816
EMUTOS   ?= $(HOME)/dev/emutos
# The compiler, through tools/ccdep.sh, which records what each object
# really depends on into build/*.d and is included below.  The hand-written
# header lists on the rules stay: they are what makes the first build after
# a clean correct.  See the top of that script for what a missing one cost.
CC65816   = $(CALYPSI)/bin/cc65816
export CC65816
CC        = tools/ccdep.sh
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

# What the compiler said each object opened, last time it was compiled.
-include $(wildcard build/*.d)

SRC_DOS  ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['dos']['sd_dos2'])" 2>/dev/null)
# A double-density DOS 2 disk, [dos].dd_dos2: 720 x 256 is where the DOS 2
# product disk lives, because 707 sectors of 253 bytes hold GEM, the desktop,
# the DOS's own shell and applications besides -- and because that DOS runs
# AUTORUN.SYS, which is how the disk comes up in the desktop.
SRC_DD   ?= $(shell python3 -c "import tomllib;print(tomllib.load(open('fixtures.toml','rb'))['dos']['dd_dos2'])" 2>/dev/null)
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
# The context switch on its own: farmem for the parked extents, app_run
# out of abi.s for the way a context enters its program, and the runner's
# own stubs for the engine that is deliberately not linked.
M27_OBJS   = build/crt_atari.o build/farload.o build/div16.o build/m27_ctx.o \
             build/ctx.o build/ctxs.o build/farmem.o build/abis.o
# The ANTIC surface milestone: no VBXE object at all, which is the point
M24_OBJS   = build/crt_atari.o build/farload.o build/div16.o build/m24_antic.o build/antic.o build/font8x8.o
# The VDI on the ANTIC device: the same vdi.c, compiled for the other
# side of the seam and linked against dev_antic.o.
M25_OBJS   = build/crt_atari.o build/farload.o build/div16.o build/m25_antic_vdi.o \
             build/vdi.o build/dev_antic.o build/antic.o build/pointer.o \
             build/font8x8.o build/font6x6.o build/fillpat.o build/sintbl.o build/font.o \
             build/farmem.o build/irq.o build/irqs.o build/rapidus.o \
             build/cio.o build/cios.o build/dos.o build/m25_stub.o \
             build/graf.o build/objc.o build/grlib.o build/event.o \
             build/proc.o build/ctx.o build/ctxs.o \
             build/wind.o build/ctrl.o build/menu.o build/form.o \
             build/alert.o build/gemdata.o build/lang.o build/lang_rsc.o \
             build/rsrc.o build/apppool.o
M3_OBJS    = build/crt_atari.o build/farload.o build/div16.o build/m3_vdi.o build/vdi.o build/dev_vbxe.o build/pointer.o build/objc.o build/graf.o build/event.o build/proc.o build/ctx.o build/ctxs.o build/grlib.o build/form.o build/alert.o build/wind.o build/ctrl.o build/menu.o build/farmem.o build/rapidus.o build/irq.o build/irqs.o build/abi.o build/abis.o build/app.o build/apppool.o build/cio.o build/cios.o build/dos.o build/gemdos.o build/rsrc.o build/shel.o build/app_blob.o build/font8x8.o build/fillpat.o build/sintbl.o build/vbxe.o build/antic.o build/fsel.o build/fsel_rsc.o build/gemdata.o build/lang.o build/lang_rsc.o build/font.o build/clock.o

# GEM.COM, the product (src/gem.c): the runner's objects with the runner
# itself and its compiled-in test application taken out, linked on the
# same rules with the application pool given the whole of $4000-$7FFF
# (src/gem4xe.scm, `layout`).
# GEM.COM links BOTH devices and chooses at start-up, so the ANTIC
# surface, its driver and its face come in on top of the runner's set.
GEM_OBJS   = $(filter-out build/m3_vdi.o build/app_blob.o,$(M3_OBJS)) \
             build/dev_antic.o build/font6x6.o \
             build/config.o build/gem.o

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
     build/m12-d2.atr build/m14-boot.atr build/m17-boot.atr build/gem-boot.atr build/gem-sp.atr \
     build/gem-cf.img

build/%.o: src/%.s
	@mkdir -p build
	$(AS) -o $@ $<

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# The two device files.  GEM4XE_DEV_IMPL says "this is one device's own
# translation unit", so vdidev.h gives it compile-time geometry instead
# of the pointer, and GEM4XE_DEV_PREFIX stamps its functions -- which is
# what lets BOTH be linked into one binary without a rename by hand.
build/dev_vbxe.o: src/vdi/dev_vbxe.c src/vdi/vdidev.h src/vdi/vdi.h src/vbxe/vbxe.h
	@mkdir -p build
	$(CC) $(CFLAGS) -DGEM4XE_DEV_IMPL -DGEM4XE_DEV_PREFIX=vbd_ \
	      -I src -I src/vdi -o $@ $<

build/font6x6.o: src/vdi/font6x6.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I src/vdi -o $@ $<

src/vdi/font6x6.c: tools/fontconv6.py
	python3 tools/fontconv6.py "$(EMUTOS)/bios/fnt_st_6x6.c" $@

build/dev_antic.o: src/vdi/dev_antic.c src/vdi/vdidev.h src/vdi/vdi.h src/antic/antic.h
	@mkdir -p build
	$(CC) $(CFLAGS) -DGEM4XE_DEV_IMPL -DGEM4XE_DEV_PREFIX=and_ \
	      -DGEM4XE_DEV_ANTIC -I src -I src/vdi -o $@ $<

# There is no second copy of the device-INdependent halves any more.
# build/vdi.o, build/pointer.o and build/font.o serve both screens: that
# is what the vtable bought, and it is checkable rather than claimed --
# test-m3 and test-m25 link the same vdi.o.
build/m25_antic_vdi.o: src/m25_antic_vdi.c src/vdi/vdi.h src/vdi/vdidev.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I src/vdi -o $@ $<

build/vbxe.o: src/vbxe/vbxe.c src/vbxe/vbxe.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src/vbxe -o $@ $<

build/antic.o: src/antic/antic.c src/antic/antic.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src/antic -I src -o $@ $<

build/m24_antic.o: src/m24_antic.c src/antic/antic.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/m2_vbxe.o: src/m2_vbxe.c src/vbxe/vbxe.h
build/m3_vdi.o:  src/m3_vdi.c  src/vbxe/vbxe.h src/antic/antic.h src/vdi/vdi.h src/vdi/vdidev.h src/sys/irq.h src/sys/abi.h src/sys/app.h src/sys/cio.h src/sys/dos.h

build/vdi.o: src/vdi/vdi.c src/vdi/vdi.h src/vdi/vdidev.h src/vdi/pointer.h src/sys/irq.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/pointer.o: src/vdi/pointer.c src/vdi/pointer.h src/vdi/vdi.h src/vdi/vdidev.h src/sys/irq.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/objc.o: src/aes/objc.c src/aes/aes.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/graf.o: src/aes/graf.c src/aes/aes.h src/vdi/vdi.h build/gemdata.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

build/proc.o: src/aes/proc.c src/aes/proc.h src/aes/aes.h src/sys/ctx.h src/sys/app.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/event.o: src/aes/event.c src/aes/aes.h src/aes/proc.h src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/grlib.o: src/aes/grlib.c src/aes/aes.h src/vdi/vdi.h build/gemdata.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

build/alert.o: src/aes/alert.c src/aes/aes.h src/sys/app.h build/gemdata.h build/lang_rsc.h
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

# The context switch: the bookkeeping in C, the three instructions C
# cannot write in assembly (src/sys/ctx.h).
build/ctx.o: src/sys/ctx.c src/sys/ctx.h src/sys/abi.h src/sys/farmem.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/ctxs.o: src/sys/ctx.s
	@mkdir -p build
	$(AS) -o $@ $<

build/m27_ctx.o: src/m27_ctx.c src/sys/ctx.h src/sys/farmem.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

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

# GEM.COM itself: it names both devices and the config, so it has to be
# rebuilt when the seam moves.
build/gem.o: src/gem.c src/vdi/vdi.h src/vdi/vdidev.h src/vdi/pointer.h \
             src/vdi/font.h src/aes/aes.h src/sys/config.h \
             src/vbxe/vbxe.h src/antic/antic.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# GEM4XE.CFG: what the machine should be told before it has a screen.
build/config.o: src/sys/config.c src/sys/config.h src/sys/cio.h src/vdi/pointer.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# GEMDOS for the applications: the ST's trap #1 on CIO, through the seam.
build/gemdos.o: src/sys/gemdos.c src/sys/clock.h src/sys/gemdos.h src/sys/dos.h src/sys/cio.h src/sys/farmem.h src/sys/app.h
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

# LANG.RSC: what the system says.  One description makes three things --
# the file a translator replaces, the same bytes as the far fallback, and
# the indices the C uses (tools/langrsc.py).
build/lang.rsc build/lang_rsc.c build/lang_rsc.h: tools/langrsc.py tools/rsc.py
	@mkdir -p build
	python3 tools/langrsc.py build/lang.rsc build/lang_rsc.c build/lang_rsc.h
build/lang_rsc.o: build/lang_rsc.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<
build/clock.o: src/sys/clock.c src/sys/clock.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

build/font.o: src/vdi/font.c src/vdi/font.h src/vdi/vdi.h src/vdi/vdidev.h src/sys/cio.h src/sys/farmem.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

# The character sets a translation can ship as SYSTEM.FNT, written out of
# an EmuTOS checkout in the format the VDI reads (tools/mkfnt.py).  Not
# built by `all`: the product's font is the one linked in, and these are
# for whoever is translating.  `make fonts` writes them.
FONTS = build/l2.fnt build/ru.fnt build/gr.fnt build/tr.fnt
fonts: $(FONTS)
build/l2.fnt: tools/mkfnt.py ; @mkdir -p build && python3 tools/mkfnt.py $(EMUTOS)/bios/fnt_l2_8x8.c $@
build/ru.fnt: tools/mkfnt.py ; @mkdir -p build && python3 tools/mkfnt.py $(EMUTOS)/bios/fnt_ru_8x8.c $@
build/gr.fnt: tools/mkfnt.py ; @mkdir -p build && python3 tools/mkfnt.py $(EMUTOS)/bios/fnt_gr_8x8.c $@
build/tr.fnt: tools/mkfnt.py ; @mkdir -p build && python3 tools/mkfnt.py $(EMUTOS)/bios/fnt_tr_8x8.c $@

# The system font as a file, and the same font inverted: what test-m21
# loads, built from the strip that is checked in so the gate needs no
# EmuTOS checkout.
build/st.fnt: tools/mkfnt.py src/vdi/font8x8.c
	@mkdir -p build
	python3 tools/mkfnt.py --from-strip src/vdi/font8x8.c $@
build/inv.fnt: tools/mkfnt.py build/st.fnt
	python3 tools/mkfnt.py --invert build/st.fnt $@

build/lang.o: src/aes/lang.c src/aes/aes.h src/sys/cio.h src/sys/farmem.h src/vdi/font.h build/lang_rsc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

build/fsel_rsc.c build/fsel_rsc.h: tools/fselrsc.py tools/rsc.py tools/aesref.py
	@mkdir -p build
	python3 tools/fselrsc.py build/fsel_rsc.c build/fsel_rsc.h
build/fsel_rsc.o: build/fsel_rsc.c
	$(CC) $(CFLAGS) -o $@ $<
build/fsel.o: src/aes/fsel.c src/aes/aes.h src/sys/app.h src/sys/cio.h src/sys/dos.h src/sys/farmem.h build/fsel_rsc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<
build/shel.o: src/aes/shel.c src/aes/aes.h src/sys/app.h src/sys/cio.h src/sys/dos.h src/sys/farmem.h build/lang_rsc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I build -o $@ $<

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

# The gate accessory (src/m28_acc.c): a .G4A like any other program, on
# the disk with the extension the AES looks for.  Its near region is the
# smallest of anything here -- it draws nothing and owns no window -- so
# what test-m28 measures is close to the floor an accessory costs.
ACC_OBJS   = $(G4A_LIB) build/app/m28_acc.o
$(eval $(call g4a,m28_acc,$(ACC_OBJS),1152,128,384,,))

# The two accessories (src/apps): the first programs written to the
# application ABI that are not tests.  Each is one C file, one resource
# built on the host, and the same three-way link every .g4a takes.
build/apps/%.o: src/apps/%.c src/app/gem.h build/calcrsc.h build/clockrsc.h
	@mkdir -p build/apps
	$(CC) $(CFLAGS) -I src/app -I build -o $@ $<

build/calc.rsc build/calcrsc.h: tools/calcrsc.py tools/rsc.py tools/aesref.py
	@mkdir -p build
	python3 tools/calcrsc.py build/calc.rsc build/calcrsc.h

build/clock.rsc build/clockrsc.h: tools/clockrsc.py tools/rsc.py tools/aesref.py
	@mkdir -p build
	python3 tools/clockrsc.py build/clock.rsc build/clockrsc.h

CALC_OBJS  = $(G4A_LIB) build/apps/calc.o
CLOCK_OBJS = $(G4A_LIB) build/apps/clock.o build/apps/clockapp.o
$(eval $(call g4a,calc,$(CALC_OBJS),1536,256,512,,))
$(eval $(call g4a,clock,$(CLOCK_OBJS),1536,256,512,,))

# The clock AS AN ACCESSORY (src/apps/clockacc.c): the same clock.o, a
# different main, and the extension the AES looks for in the system's own
# directory.  The reservations are what the map says it uses rather than
# round numbers, because an accessory is charged to the application pool
# for as long as the machine is on -- 14 KB for the desktop, its resource
# and everything resident beside it (docs/phase36.md).
CLOCKACC_OBJS = $(G4A_LIB) build/apps/clock.o build/apps/clockacc.o
$(eval $(call g4a,clockacc,$(CLOCKACC_OBJS),1152,128,384,,))

# The desktop (src/desk): a bigger near region than the gate application's,
# for the object trees a desktop keeps in bank $00, and DESKTOP.RSC beside
# it on the disk (tools/deskrsc.py; the icons from EmuTOS desk/icons.c
# through tools/iconconv.py, checked in like the font).  The milestone-3
# desktop (src/m16_desk.c: a line of help, R/X/Q) stays as the stand-in
# test-m16 drives the shell loop with.
# The near region is 15 pages: the direct page, the bss, the constants.
# The bss holds the desktop's globals (GLOBES, src/desk/desk.h: the screen
# tree, the window nodes, the icon records, ~2 KB) and its stack, which
# is the gate application's 256 bytes and more: the folder window's open
# -- the button, do_open, do_dopen, do_wopen, a call to the AES on top --
# ran 256 bytes out (phase 14, milestone 5).
#
# 640 is the desktop's stack and the low-water mark says ~300 bytes of it
# are ever used; 2944 of bss is what the rest of it measures.  For a
# while the stack was 896 and the bss 3200, because the honest numbers
# crashed -- which was never the desktop's doing: it is the emulator's
# SEI-shadow IRQ storm (tools/altirra/, patch 1), which paints the whole
# of bank $00, hardware registers and all, and which a couple of hundred
# bytes of layout anywhere is enough to trip or untrip.  THAT IS WHY THE
# NUMBERS HERE ARE THE MEASURED ONES AND NOT A DODGE: a dodge lasts
# until the next thing that moves the code, which phase 27 demonstrated
# by moving the storm from m19 into m18.  The desktop gates run against
# a patched emulator -- ALTIRRASDL=<build> -- and say so when they find
# the storm (tests/emu/m7_form.py storm_check).  docs/phase26.md.
#
# The pool is shared with DESKTOP.RSC (6226 bytes) and with GEMDOS's
# work area (src/sys/gemdos.c), which is why the desktop gates run a
# runner whose staging leaves the pool the room GEM.COM leaves it
# (build/m3desk.xex, above).
DESK_OBJS  = $(G4A_LIB) build/desk/desktop.o build/desk/deskobj.o build/desk/deskwin.o \
             build/desk/deskfun.o
DESK_BSS   = 2944
DESK_BITS  = 512
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

# The VDI's sine table, from EmuTOS's vdi_gdp.c by sinconv.py: the GDPs
# draw every curve out of it, and tools/vdiref.py reads the generated file
# so the reference and the driver cannot disagree about a circle.
build/sintbl.o: src/vdi/sintbl.c src/vdi/vdi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -o $@ $<

src/vdi/sintbl.c:
	python3 tools/sinconv.py $(EMUTOS)/vdi/vdi_gdp.c $@

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

build/m25.elf: $(M25_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M25_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/m25.map

build/m25.xex: build/m25.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry

build/m25-boot.atr: build/m25.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M25.COM $(DISK_DENSITY)

build/m27.elf: $(M27_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M27_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/m27.map

build/m27.xex: build/m27.elf
	python3 tools/mkxex.py $< $@ --entry _atari_entry --syms build/m27.sym

build/m24.elf: $(M24_OBJS) src/gem4xe.scm
	$(LD) src/gem4xe.scm $(M24_OBJS) -o $@ $(LIB) $(LDFLAGS) --list-file build/m24.map

build/m24.xex: build/m24.elf
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
# NOT LANG.RSC.  The runner's disks are what test-m12's file selector
# lists and what its far-heap accounting counts, and a tenth file
# overflows the selector's nine lines while the language resource in far
# memory moves the heap cursor by its own size -- both of which that gate
# measures.  test-m20 makes its own copies of this disk with the file
# added, removed and translated, which is the point there; every other
# gate runs on the English linked into the image (src/aes/lang.c).
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
# The two accessories (src/apps), on the media with room for them: the
# SpartaDOS floppy, the CF card, and test-m22's own disk.  A prerequisite
# list is expanded where it is written, so these live above every rule
# that names them.
# In a FOLDER, as the product has them: an application and its resource
# live together in \APPS\, and finding the resource from there is the
# thing test-m22 now covers (docs/phase29.md).
APP_FILES = --mkdir APPS \
            --add build/calc.g4a "APPS>CALC.G4A" --add build/calc.rsc "APPS>CALC.RSC" \
            --add build/clock.g4a "APPS>CLOCK.G4A" --add build/clock.rsc "APPS>CLOCK.RSC"
APP_DEPS  = build/calc.g4a build/calc.rsc build/clock.g4a build/clock.rsc
# The gate accessory (src/m28_acc.c), in the system's own directory with
# the extension the AES looks for there: an accessory is not in \APPS\
# with the programs, because it is not one -- it is loaded once at
# start-up and outlives every program (src/aes/shel.c).
ACC_FILES = --add build/m28_acc.g4a M28.ACC
ACC_DEPS  = build/m28_acc.g4a
# The accessory the PRODUCT ships: the clock, in the system's own
# directory rather than \APPS\, because an accessory is not a program the
# desktop launches -- the AES loads it once at start-up and it outlives
# every program (src/aes/shel.c).
ACCP_DEPS = build/clockacc.g4a build/clock.rsc

build/test.rsc: tools/mkrsc.py tools/rsc.py tools/aesref.py
	@mkdir -p build
	python3 tools/mkrsc.py $@

# The selector's second drive: the fixture DOS disk with fifteen small
# files on it, D2: when test-m12 boots (tools/mkfsdisk.py).
build/m12-d2.atr: tools/mkfsdisk.py tools/atr.py
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkfsdisk.py "$(SRC_DOS)" $@

# The runner's disk is DOUBLE density, and that is why the gates type an
# L before the name.  The single-density fixture's DOS boots to a command
# prompt (nicer to drive) but its 1040 enhanced sectors held the runner
# with nothing to spare -- 117 KB of program on a 128 KB disk -- and the
# boot code it carries cannot read a double-density disk at all: written
# onto one, it stops at BOOT ERROR.  The double-density fixture's DOS
# boots its own disk, gives the familiar DOS 2 menu, and leaves 190
# sectors free, so the runner has 48 KB to grow into.  L is that menu's
# BINARY LOAD, and the file is called M3 with no extension so that the
# three keys after it are the ones every gate already typed.
# The shell gate (test-m16) runs on the SpartaDOS disk, whose size is
# ours to choose (tools/mkspdisk.py --sectors).
build/m3-boot.atr: build/m3.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc
	@test -n "$(SRC_DD)" || { echo "no double-density DOS fixture: set [dos].dd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DD)" $< $@ M3 --sweep $(DISK_FILES)

# The product disks: the system with the desktop beside it, one per DOS,
# and both of them boot into it with nothing typed.
#
# The DOS 2 one is DOUBLE density.  Single density does not hold GEM.COM
# at all and enhanced holds it with three sectors to spare, which left no
# room for the DOS's own shell -- and a DOS with no shell to return to
# dies when GEM hands the machine back.  720 sectors of 253 bytes hold
# the system, DOS.SYS, DUP.SYS and the rest for applications, which is
# what makes the disk a place to keep them rather than one program.
# M11.G4A -- the demonstration program -- is NOT on this one: 180 KB is
# the tightest medium gem4xe ships on, and by the time the desktop could
# save a layout (phase 25) the demo was the difference between having
# room for somebody's own program and not.  The SpartaDOS floppy and the
# CF card both carry it.  The program
# is named AUTORUN.SYS because that is what the DOS runs at boot, and
# --sweep takes everything but the DOS off the fixture, which was
# somebody's magazine disk (docs/shipping.md, section 2).
build/816.com: tools/mk816.py
	@mkdir -p build
	python3 tools/mk816.py $@

# The DOS 2 floppy carries the SYSTEM and no applications, deliberately.
# It has 87 sectors free, the calculator and the clock want 49 of them,
# and tests/emu/product_boot.py holds a floor of 80 -- which exists so
# that the smallest disk is still somewhere a person can put a program
# of their own.  Filling that space with ours would be taking exactly
# what the floor is there to keep.  The other two media have room.
# GEM4XE.CFG as it ships: every setting commented out, so that finding
# the file is finding its documentation.  Translated to the Atari's EOL
# ($9B) on the way to the disk -- src/sys/config.c reads CR, LF and EOL
# alike, but a file that a DOS editor opens should already be in the
# form that editor writes.
build/gem4xe.cfg: dist/gem4xe.cfg
	@mkdir -p build
	python3 -c "import sys; \
	    open(sys.argv[2],'wb').write(open(sys.argv[1],'rb').read() \
	        .replace(b'\r\n', b'\n').replace(b'\n', b'\x9b'))" $< $@

# ...and the SHORT one the double-density DOS 2 floppy carries, because
# that disk has about 2 KB free once GEM's 122 KB and the desktop are on
# it and eight sectors of commentary is the wrong thing to spend it on.
# It is the same file with the prose taken out and a pointer to where the
# prose is, generated rather than written, so there is one source for
# what the keys are.  The file's own opening line says this costs
# nothing: "No file at all is the same as this one with everything
# commented out, which is what it is."
build/gem4xe-min.cfg: dist/gem4xe.cfg tools/mincfg.py
	@mkdir -p build
	python3 tools/mincfg.py $< $@

# ...and the safe-mode one the ANTIC gate boots with, which is the same
# file with the one line uncommented.
build/safe.cfg: dist/gem4xe.cfg
	@mkdir -p build
	python3 -c "import sys; \
	    open(sys.argv[2],'wb').write(open(sys.argv[1],'rb').read() \
	        .replace(b'# VIDEO=AUTO', b'VIDEO=ANTIC') \
	        .replace(b'\r\n', b'\n').replace(b'\n', b'\x9b'))" $< $@

build/gem-boot.atr: build/gem.xex build/desktop.g4a build/desktop.rsc build/m11_app.g4a build/lang.rsc build/816.com build/gem4xe-min.cfg
	@test -n "$(SRC_DD)" || { echo "no double-density DOS fixture: set [dos].dd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DD)" $< $@ AUTORUN.SYS --sweep \
	    --add build/desktop.g4a DESKTOP.G4A --add build/desktop.rsc DESKTOP.RSC \
	    --add build/lang.rsc LANG.RSC --add build/816.com 816.COM \
	    --add build/gem4xe-min.cfg GEM4XE.CFG

# NO ACCESSORY ON THIS DISK, and it is not a choice: a double-density DOS
# 2 floppy is 184 KB and GEM.COM is 122 KB of it, which leaves eleven
# sectors after the desktop and its resource.  CLOCK.ACC wants twenty.
# The accessory ships on the install disk and the CF card, which is where
# docs/shipping.md sends anybody who wants to use the thing rather than
# just see it boot.
#
# The product's SpartaDOS floppy is an INSTALL disk: the same \GEM\ and
# \APPS\ layout the card has, so copying it onto an APT hard drive is a
# directory copy and not a decision, and none of the file layer's
# fixtures -- those belong on a gate's disk (--tree) and not on this one.
# The same shipped disk with the safe-mode line uncommented: what a user
# writes from the DOS prompt when the VBXE's output is not something
# their monitor will show (tests/emu/m26_fallback.py).
build/gem-antic.atr: build/gem.xex build/desktop.g4a build/desktop.rsc build/m11_app.g4a build/lang.rsc build/816.com build/safe.cfg
	@test -n "$(SRC_DD)" || { echo "no double-density DOS fixture: set [dos].dd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DD)" $< $@ AUTORUN.SYS --sweep \
	    --add build/desktop.g4a DESKTOP.G4A --add build/desktop.rsc DESKTOP.RSC \
	    --add build/lang.rsc LANG.RSC --add build/816.com 816.COM \
	    --add build/safe.cfg GEM4XE.CFG

build/gem-sp.atr: build/gem.xex build/lang.rsc build/816.com build/gem4xe.cfg $(DESK_DEPS) $(APP_DEPS) $(ACCP_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) \
	    --name "GEM>GEM.COM" --boot "CD >GEM|GEM" --mkdir GEM --mkdir APPS \
	    --add build/desktop.g4a "GEM>DESKTOP.G4A" \
	    --add build/desktop.rsc "GEM>DESKTOP.RSC" \
	    --add build/lang.rsc "GEM>LANG.RSC" \
	    --add build/816.com "GEM>816.COM" \
	    --add build/gem4xe.cfg "GEM>GEM4XE.CFG" \
	    --add build/clockacc.g4a "GEM>CLOCK.ACC" \
	    --add build/clock.rsc "GEM>CLOCK.RSC" \
	    --add build/m11_app.g4a "APPS>M11.G4A" \
	    --add build/calc.g4a "APPS>CALC.G4A" --add build/calc.rsc "APPS>CALC.RSC" \
	    --add build/clock.g4a "APPS>CLOCK.G4A" --add build/clock.rsc "APPS>CLOCK.RSC"

# The CF card: an APT table and two SDFS partitions, with the system in
# \GEM\ and the demonstration application in \APPS\ -- the install
# layout of docs/shipping.md section 4, on the volume it was written for.
# It needs no fixture: unlike a floppy, a card carries no DOS of its own,
# because SpartaDOS X boots from U1MB flash and the U1MB's PBI BIOS --
# from the same flash -- reads the APT table and mounts the partitions as
# D1: and D2: before any DOS runs.  No SIDE.SYS, no driver on the card.
# tools/apt.py writes the table, tests/host/test_apt.py checks it against
# the rules Altirra's own parser applies, and test-cf boots it.
build/gem-cf.img: build/gem.xex build/desktop.g4a build/desktop.rsc build/m11_app.g4a \
                  build/lang.rsc build/gem4xe.cfg $(APP_DEPS) $(ACCP_DEPS) tools/mkcf.py tools/apt.py tools/atr.py
	@rm -f $@
	python3 tools/mkcf.py $@

# The SpartaDOS disk: a fresh SDFS volume booting the 3.2 fixture's DOS,
# the same files as the DOS 2 disk, the shell's applications and a
# directory tree for the selector (tools/mkspdisk.py).  2048 sectors of
# 128: SpartaDOS loads M3.COM's 100 KB from anywhere, where DOS 2.5 could
# not go past sector 720 -- and 1040 no longer hold it with the desktop
# beside it.  The gates size everything from the image, not from here.
# 2560 sectors, which is bigger than a floppy on purpose: it leaves more
# than 999 free, and 999 is what a directory listing's three characters
# could say.  Dfree reads the file system's own count now (src/sys/gemdos.c),
# so test-m15 proves the old ceiling is gone rather than describing it.
SP_SECTORS = --sectors 2560
build/m14-boot.atr: build/m3.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(SHELL_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) --tree $(DISK_FILES) $(SHELL_FILES)

# The accessories' disk (test-m22): test-m16's, with the two programs
# and their resources added.  A disk of its own rather than SHELL_FILES,
# because the file layer's gates count what is in a directory -- the
# selector shows nine names and test-m12 predicts the listing -- and four
# more files there would be four more names.
build/m22-boot.atr: build/m3.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(SHELL_DEPS) $(APP_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) --tree $(DISK_FILES) $(SHELL_FILES) $(APP_FILES)

# The desktop gate's disk (test-m17): the runner again, with the real
# desktop and its resource where test-m16's stand-in was.
build/m17-boot.atr: build/m3desk.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(DESK_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) --tree $(DISK_FILES) $(DESK_FILES)

# The desktop-and-accessory gate's disk (test-m23): test-m17's, with the
# two accessories in \APPS\ as the product media carries them.  This is
# the disk that reproduces the user's own layout -- the real desktop, a
# folder, and an application inside it that waits for the mouse.
# The accessories gate's disk (test-m28): test-m17's, with one accessory
# in the system's directory, which is where the AES looks for *.ACC.
build/m28-boot.atr: build/m3desk.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(DESK_DEPS) $(ACC_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) --tree $(DISK_FILES) $(DESK_FILES) $(ACC_FILES)

build/m23-boot.atr: build/m3desk.xex tests/fixtures/test.txt tests/fixtures/out.txt build/test.rsc $(DESK_DEPS) $(APP_DEPS) tools/mkspdisk.py tools/atr.py
	@test -n "$(SRC_SP32)" || { echo "no SpartaDOS fixture: set [spartados].disk_32 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkspdisk.py "$(SRC_SP32)" $< $@ $(SP_SECTORS) --tree $(DISK_FILES) $(DESK_FILES) $(APP_FILES)

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

# Same program, same disk shape as the runner's: double density, called
# M3, started from the DOS menu's BINARY LOAD.
build/m6split-boot.atr: build/m6split.xex
	@test -n "$(SRC_DD)" || { echo "no double-density DOS fixture: set [dos].dd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DD)" $< $@ M3 --sweep

build/m2-boot.atr: build/m2.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M2.COM $(DISK_DENSITY)

build/m27-boot.atr: build/m27.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M27.COM $(DISK_DENSITY)

build/m24-boot.atr: build/m24.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ M24.COM $(DISK_DENSITY)

build/hello-boot.atr: build/hello.xex
	@test -n "$(SRC_DOS)" || { echo "no DOS fixture: set [dos].sd_dos2 in fixtures.toml"; exit 1; }
	@rm -f $@
	python3 tools/mkdisk.py "$(SRC_DOS)" $< $@ HELLO.COM $(DISK_DENSITY)

test: test-host check-cc test-emu test-m1 test-m2 test-m3 test-m4 test-m5 test-m6 test-m7 test-m8 test-m9 test-m10 test-m11 test-m12 test-m13 test-m14 test-m14x test-m15 test-m15x test-m15d test-m16 test-m17 test-m18 test-m19 test-m20 test-m21 test-m22 test-m23 test-m24 test-m25 test-m26 test-m27 test-m28 test-boot

# GACS's engine on the 65816 -- the application gem4xe exists for, asked
# whether it still compiles, links and computes there (docs/gacs.md).
# Needs the GACS checkout (GACS=, default ~/dev/gacs) and Calypsi's own
# simulator, so it is not part of `make test`: it depends on another
# project, the way test-m14u depends on a flash image.
gacs-check:
	python3 tools/gacscheck.py

# The cc65816 code generation bugs gem4xe works around, run in the vendor's
# own simulator: fails only if a workaround shape has stopped compiling
# right; a bug that has gone away is reported so its workaround can go.
check-cc:
	python3 tools/ccbug/check.py --calypsi $(CALYPSI)

# The host tests want the gate application built, because the kit's own
# test rebuilds it out of the kit and compares the bytes: what test-m11
# proves about that binary is what the kit inherits.
test-host: build/m11_app.g4a
	python3 -m unittest discover -s tests/host -t .

# The application kit (tools/mksdk.py, tools/sdk/): what somebody who is
# not this repository needs in order to build a program that runs on
# gem4xe -- the header, the bindings and the start-up as source, the
# linker's rules, the packer, and one whole example.  Its gate is in the
# host tests (tests/host/test_sdk.py), which builds it out of a copy of
# itself in a directory of its own.
SDK_FILES = tools/mksdk.py tools/sdk/README.md tools/sdk/Makefile \
            tools/sdk/hello.c src/app/gem.h src/app/gemlib.c \
            src/app/gemabi.s src/app/crt_gemapp.s src/app/gemapp.scm \
            tools/mkg4a.py tools/mkxex.py COPYING

sdk: build/gem4xe-sdk.tar.gz
build/gem4xe-sdk.tar.gz: $(SDK_FILES)
	python3 tools/mksdk.py build/gem4xe-sdk --tar $@

# The distribution: what a tester is handed -- the bootable disks, the
# system's files loose for a disk of their own, the kit, and a page that
# says how to try it (tools/dist/README.md, filled in by tools/mkdist.py
# from the images and from the desktop's own menu, so the half of it
# that could go stale cannot).  DIST is the name it takes: the date and
# the commit unless you say otherwise.
DIST ?= build/gem4xe-$(shell date +%F)-$(shell git rev-parse --short HEAD 2>/dev/null || echo local)
DIST_DISKS = build/gem-sp.atr build/gem-boot.atr build/gem-cf.img
DIST_SYS   = build/gem.xex build/desktop.g4a build/desktop.rsc \
             build/lang.rsc build/816.com build/m11_app.g4a build/gem4xe.cfg

dist: $(DIST_SYS) $(DIST_DISKS) build/gem4xe-sdk.tar.gz \
      tools/mkdist.py tools/dist/README.md tools/mksdk.py
	python3 tools/mkdist.py $(DIST) --tar $(DIST).tar.gz

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

# The two accessories (src/apps): the calculator driven at its keypad and
# the clock left to tick, both run through the shell loop and both checked
# against what the host AES makes of their own resources.
test-m22: build/m22-boot.atr build/calc.sym build/clock.sym
	python3 tests/emu/m22_apps.py

# A program run from the desktop and the desktop's windows back after it
# (phase 14, milestone 6): two runs of the desktop against the model, with
# M11.G4A between them.
test-m18: build/m17-boot.atr build/desktop.g4a build/desktop.sym
	python3 tests/emu/m18_launch.py

# An accessory opened from a folder and used: the real desktop at the
# mouse, a folder, and a program that waits for the mouse itself.
test-m23: build/m23-boot.atr build/desktop.g4a build/desktop.sym build/calc.sym
	python3 tests/emu/m23_deskapp.py

# The ANTIC surface, on a machine with no VBXE in it at all.
test-m24: build/m24-boot.atr
	python3 tests/emu/m24_antic.py

test-m27: build/m27-boot.atr
	python3 tests/emu/m27_ctx.py

test-m28: build/m28-boot.atr
	python3 tests/emu/m28_acc.py

# Where bank $00 has gone, and whether there is enough of it left.  Run it
# after a change that adds a table or a program; tests/host/test_memory.py
# runs it too, so make test says so without being asked.
memcheck: build/gem.xex build/desktop.g4a build/desktop.rsc build/clockacc.g4a build/clock.rsc
	python3 tools/memreport.py

# ...and the VDI itself on it: the same vdi.c, the other side of the seam.
test-m25: build/m25-boot.atr
	python3 tests/emu/m25_antic_vdi.py

# ONE BINARY, TWO SCREENS: the shipped GEM.COM on a machine with a VBXE
# and on one without, and then the safe mode -- VIDEO=ANTIC in
# GEM4XE.CFG beating a VBXE that works.  Nothing is typed in any of the
# three; the disks start GEM themselves.
test-m26: build/gem-boot.atr build/gem-antic.atr build/gem.sym
	python3 tests/emu/m26_fallback.py

# The desktop's writes to a disk (phase 14, milestone 7; phase 19): File
# -> New folder, File -> Delete, and File -> Show info -- which is also
# the rename -- driven at the mouse and the keyboard against the model,
# and the disk image read back when the run is over.
# The gate boots a copy of milestone 5's disk, made afresh every run --
# it is the first whose target rewrites the directory it booted from.
test-m19: build/m17-boot.atr build/desktop.g4a build/desktop.sym
	python3 tests/emu/m19_files.py

# What the system says, and where it says it from (docs/shipping.md,
# section 5): form_error on three disks -- the product's LANG.RSC, a
# translation of it, and no file at all -- each compared with the model
# given the strings that disk carries.
test-m20: build/m3-boot.atr build/lang.rsc
	python3 tests/emu/m20_lang.py

# A loadable font (docs/shipping.md, section 5): the system font as a
# file, inverted so every glyph differs, loaded at start-up off one disk
# and absent from another -- with vqt_name, vst_font and the GDOS pair
# either way.
test-m21: build/m3-boot.atr build/inv.fnt
	python3 tests/emu/m21_font.py

# The product disk booting into the desktop with nothing typed: the
# batch file the SpartaDOSes run, the loader's refusal on the 6502, the
# switch, and the desk against the model (docs/shipping.md, section 2).
# BOTH product disks: the gate boots both, and naming only one here left
# the other stale whenever GEM.COM was rebuilt -- which looked exactly
# like the far image being mangled by the DOS, and cost an afternoon
# twice (docs/phase16.md).
test-boot: build/gem-sp.atr build/gem-boot.atr build/desktop.g4a build/desktop.sym
	python3 tests/emu/product_boot.py

# The same boot off the product CF card, on the machine this project is
# for: the U1MB flash's SpartaDOS X and PBI BIOS, a SIDE 2 with the card
# on its IDE bus.  Outside `make test` for the same reason as test-m14u:
# it needs both the U1MB fixture and the patched emulator (ALTIRRASDL=).
test-cf: build/gem-cf.img build/desktop.g4a build/desktop.sym
	@test -n "$(SRC_U1MB)" || { echo "no U1MB fixture: set [u1mb].flash in fixtures.toml"; exit 1; }
	python3 tests/emu/cf_boot.py

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

.PHONY: all fonts sdk dist memcheck gacs-check test test-host check-cc test-emu test-m1 test-m2 test-m3 test-m4 test-m5 test-m6 test-m7 test-m8 test-m9 test-m10 test-m11 test-m12 test-m13 test-m14 test-m14x test-m14u test-m15 test-m15x test-m15u test-m15d test-m16 test-m17 test-m18 test-m19 test-m20 test-m21 test-m22 test-m23 test-m24 test-m25 test-m26 test-m27 test-m28 test-boot test-cf demo movie bench emu-stop clean
