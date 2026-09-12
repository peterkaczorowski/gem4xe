/* zwin.h -- bank-$00 bss in the banked window.
 *
 * ZWIN on a definition puts it in section `zwin`, which src/gem4xe.scm
 * places at $4000-$47FF: the first 2 KB of the window a DOS may bank over
 * while it services a call, ahead of the application pool.  It is for
 * data that only gem4xe's own code touches from its own calls -- never an
 * interrupt handler, which can run with a bank in, and never anything a
 * test polls from the host while a DOS call is in flight (m14).  What
 * lives there is the large and the read-mostly: on a Rapidus this window
 * reads at full speed but writes through to the bus (src/sys/rapidus.c),
 * and taking it out of $2000-$3FFF is what gives the stack its 2 KB.
 */
#ifndef GEM4XE_ZWIN_H
#define GEM4XE_ZWIN_H

#include "portab.h"

#define ZWIN SECTION("zwin")

#endif
