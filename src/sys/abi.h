/* abi.h -- the application binary interface.
 *
 * An application reaches gem4xe with a COP instruction: COP #$73 for the
 * VDI, COP #$C8 for the AES, COP #$01 for GEMDOS (the ST's trap numbers),
 * X:C holding the address of its parameter block (src/app/gem.h documents
 * the application's side).  The handler in abi.s
 * catches the trap, records where the block is and which signature was used,
 * and calls gem_entry() on gem4xe's own direct page, data bank and stack.
 * gem_entry() copies the block's inputs into gem4xe's arrays, runs the call,
 * and copies the outputs back: the DRI entry-shim discipline, which is what
 * lets the VDI's argument-free handlers and the AES's fixed arrays serve a
 * caller whose arrays are anywhere in the 16 MB.
 *
 * An application is a far subroutine of gem4xe's: app_run() calls its entry
 * with gem_api_sp set to gem4xe's stack, so that a COP from the application
 * -- which runs on a stack of its own, in its own bank-$00 pool -- is served
 * on gem4xe's, where the depth the VDI and AES need is known to exist.
 */
#ifndef GEM4XE_ABI_H
#define GEM4XE_ABI_H

#include "portab.h"
#include <stdint.h>

#define ABI_GEMDOS 0x01     /* COP signature bytes; the app's gemabi.s */
#define ABI_VDI    0x73
#define ABI_AES    0xC8

/* Written by the handler in abi.s before gem_entry() runs. */
extern uint32_t gem_pb;        /* the parameter block: X:C at the COP */
extern uint8_t  gem_which;     /* the signature byte after the COP opcode */

/* The stack switch.  gem_api_sp is gem4xe's S while an application runs
 * (0 otherwise: a COP from gem4xe's own code stays on the current stack);
 * gem_depth counts nested COPs so only the outermost switches. */
extern uint16_t gem_api_sp;
extern uint8_t  gem_depth;

extern uint16_t gem_calls;     /* COPs served, every process's */
/* Of those, the ones the APPLICATION made.  A gate that wants to know
 * where a program has got to has to count only that program's calls, and
 * gem_calls stopped being that the moment a desk accessory could be
 * resident beside it (src/aes/proc.h). */
extern uint16_t app_calls;
extern uint16_t gem_bad;       /* COPs refused: signature, opcode, bank */

void gem_entry(void);          /* the C side of the handler */

/* Call an application's entry point (a far address) as a subroutine and
 * return what its main() returned.  abi.s. */
SIMPLE_CALL int16_t app_run(uint32_t entry);

#endif /* GEM4XE_ABI_H */
