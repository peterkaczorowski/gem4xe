# The gem4xe application kit.
#
#     make                          hello.g4a, from example/hello.c
#     make APP=mine.c               mine.g4a
#     make APP=mine.c NAME=prog     prog.g4a
#     make APP=mine.c ASM="a.s b.s" mine.g4a, with your assembly linked in
#     make clean
#
# A program is linked THREE times -- once at the placeholder addresses
# lib/gemapp.scm gives it, once with its near region a page higher and
# once with its far region a bank higher -- and tools/mkg4a.py takes the
# bytes that moved as the loader's fixup lists.  Neither the compiler
# nor the linker emits relocations for a program that is placed at load
# time, so they are derived rather than declared; see the comment at the
# top of tools/mkg4a.py.
#
# The three budget numbers are yours to raise, and the link enforces
# them:
#
#   BSS    bytes of near memory that carry no bits -- your stack, your
#          uninitialised data.  It includes STACK.
#   BITS   bytes that do -- constants, the initial values of data.
#   STACK  of BSS, how much is stack.  The AES calls back into your
#          program (a redraw inside form_do, say), so leave room.
#
# All of it comes out of gem4xe's 2 KB application pool in bank $00,
# shared with whatever else is resident, so ask for what you need.  Your
# CODE is not in that budget: it goes in a far bank of its own, and you
# have 64 KB of it.

CALYPSI ?= $(HOME)/dev/toolchains/calypsi-65816
APP     ?= example/hello.c
ASM     ?=
NAME    ?= $(basename $(notdir $(APP)))
BSS     ?= 2048
BITS    ?= 256
STACK   ?= 256

CC  = $(CALYPSI)/bin/cc65816
AS  = $(CALYPSI)/bin/as65816
LD  = $(CALYPSI)/bin/ln65816
# The code model must be large -- your code is in a far bank and every
# call to it is a jsl -- and the data model small, because the near
# region is addressed through DB.  gem4xe itself is built this way.
CFLAGS = --code-model=large --data-model=small -O2 -I include
LDLIB  = clib-lc-sd.a

# build/clib.o is lib/clib.c: memcpy, memset and the str* functions.  They
# are here rather than taken from clib-lc-sd.a because Calypsi's come from
# Apache NuttX and Apache 2.0 cannot be linked into a GPLv2 binary -- and
# gem4xe is GPLv2.  If your program is under something else you may use
# the library's instead; if it is GPL, use these.
#
# ASM names assembly sources of your own, linked after the program in the
# order given.  Code goes in `farcode`, and a function returns with rtl:
# every call into it is a jsl.
ASM_OBJS = $(patsubst %.s,build/%.o,$(notdir $(ASM)))
OBJS = build/crt_gemapp.o build/gemabi.o build/gemlib.o build/clib.o \
       build/$(NAME).o $(ASM_OBJS)
LDOPT = lib/gemapp.scm $(OBJS) $(LDLIB) --rtattr exit=simplified \
        --cstartup gemapp --stack-size $(STACK)

all: $(NAME).g4a

build:
	@mkdir -p build

build/%.o: lib/%.s | build
	$(AS) -o $@ $<

build/%.o: lib/%.c include/gem.h | build
	$(CC) $(CFLAGS) -o $@ $<

build/$(NAME).o: $(APP) include/gem.h | build
	$(CC) $(CFLAGS) -o $@ $<

$(foreach s,$(ASM),$(eval build/$(basename $(notdir $(s))).o: $(s) | build ; $$(AS) -o $$@ $$<))

# At the placeholders, then a page up, then a bank up.
build/$(NAME).elf: $(OBJS) lib/gemapp.scm
	$(LD) $(LDOPT) -o $@ --list-file build/$(NAME).map \
	    --memories-expression "(app-layout #x1000 #x020000 $(BSS) $(BITS))"
build/$(NAME)-near.elf: $(OBJS) lib/gemapp.scm
	$(LD) $(LDOPT) -o $@ \
	    --memories-expression "(app-layout #x1100 #x020000 $(BSS) $(BITS))"
build/$(NAME)-far.elf: $(OBJS) lib/gemapp.scm
	$(LD) $(LDOPT) -o $@ \
	    --memories-expression "(app-layout #x1000 #x030000 $(BSS) $(BITS))"

$(NAME).g4a: build/$(NAME).elf build/$(NAME)-near.elf build/$(NAME)-far.elf
	python3 tools/mkg4a.py build/$(NAME).elf build/$(NAME)-near.elf \
	    build/$(NAME)-far.elf $@ --syms build/$(NAME).sym

clean:
	rm -rf build $(NAME).g4a

.PHONY: all clean
