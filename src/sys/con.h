/* con.h -- GEMDOS's console: the ST's con: device, a VT-52 on GEM's screen.
 *
 * A TOS program writes its text to GEMDOS handle 1 and reads its keys
 * from handle 0, and on the ST both end at the console: a VT-52 terminal
 * the BIOS draws in the system font over whatever is on the screen (The
 * Atari Compendium, 3.13, "The VT-52 Emulator").  A GEM program that
 * prints with Cconws writes over its own windows there, and so it does
 * here.  The screen is GEM's, so the console draws through the AES's own
 * workstation -- the text, fills and screen blits the AES draws with
 * (src/aes/graf.c) -- in cells of the system font: 80 x 30 on the VBXE's
 * 640 x 240, 40 x 24 on ANTIC.
 *
 * WHAT IS THE ST'S.  The escapes are the Compendium's table, every one:
 * the cursor moves, E J K L M d l o erasing and scrolling, Y for a
 * position, b and c for colours, e and f the cursor, j and k to save and
 * restore, p and q inverse, v and w wrapping.  CR, LF, BS and TAB do what
 * they do on the ST, and BEL and the other controls draw nothing.  Line
 * wrap starts off, as TOS's console starts (EmuTOS bios/vt52.c).
 *
 * WHAT IS NOT.  The colour numbers of ESC b and ESC c are the VDI's
 * indices, so 0 is white and 1 is black on either screen, where the ST
 * used its hardware pens.  The cursor is a solid block and it is drawn
 * only while a program waits for a key, where the ST blinks one whenever
 * it is enabled -- and it is enabled when a program starts, which is what
 * the ST's desktop does for a TOS program.
 *
 * The state is a record in far memory that the caller owns (GEMDOS's,
 * src/sys/gemdos.c), because bank $00 has no room for it.
 */
#ifndef GEM4XE_CON_H
#define GEM4XE_CON_H

#include <stdint.h>
#include "portab.h"
#include "../aes/aes.h"

#define CON_SIZE 12             /* the record's bytes */

/* Home, the default colours, wrap off and the cursor enabled, nothing
 * pending: the console a program starts with. */
void con_reset(uint32_t rec);

/* n bytes to the screen through the VT-52: controls and escapes obeyed,
 * an escape split across two writes carried over. */
void con_write(uint32_t rec, const uint8_t *s, uint16_t n);

/* A key, as the AES hands one out (scan code high, ASCII low).  With
 * `wait` it waits -- the machine goes on polling and the accessories go
 * on having turns, as they do in evnt_keybd -- and the cursor is shown
 * while it does; without, it answers 0 when no key is there. */
WORD con_key(uint32_t rec, WORD wait);

/* Whether a key is there, without taking it from the program: one that
 * is found is held for the next con_key. */
WORD con_ready(uint32_t rec);

/* The cursor's column, for a line editor that has to go back to where
 * its line began (GEMDOS's Cconrs). */
WORD con_col(uint32_t rec);

#endif /* GEM4XE_CON_H */
