/* proc.h -- the AES's processes.
 *
 * GEM gives every process a queue of messages, a set of event blocks
 * saying what it is waiting for, and a stack; the dispatcher runs
 * whichever of them can go (EmuTOS aes/gemdisp.c, aes/gemasync.c).  Until
 * the desk accessories there was one process here and every one of those
 * was a file static in src/aes/event.c, which is what the comment above
 * gl_queue said in as many words: "One process here, so one queue."
 *
 * This is that state, made plural.  The stack half is src/sys/ctx.h --
 * the contexts share the one engine stack and take turns on it.  What is
 * here is the AES half: who is running, what each of the others is
 * waiting for, and whether it has arrived.
 *
 * HOW A TURN CHANGES HANDS.  At one place only: ev_poll(), which every
 * wait loop in event.c spins on.  A process about to spin records what it
 * is waiting for in p_evwait, and ev_poll hands the processor to any
 * OTHER process whose wait is already satisfied.  Nothing pre-empts and
 * nothing is timed: a process that never waits never yields, exactly as
 * in the donor, where a program that computes for a minute freezes the
 * accessories for a minute too.
 *
 * WHEN A TURN MAY NOT CHANGE HANDS.  While the control manager is inside
 * a gesture -- a menu pulled down, a gadget held, a button still down --
 * the mouse belongs to it until the button comes up, which is GEM's own
 * rule (set_mown refuses to transfer with a button down) and here also a
 * practical one: ct_run is a call nested in the application's wait and
 * its state is the AES's, not the process's.
 *
 * HOW BIG.  The Desk box has six accessory slots because the resource
 * has eight children and the AES rebuilds the chain by index
 * (tools/deskrsc.py, and the invariant is the donor's).  Six SLOTS is not
 * six PROGRAMS: one accessory may register more than one name, and on
 * this machine the binding limit is bank $00, not the menu -- the pool is
 * 14 KB and the desktop with its resource is most of it.  So the slots
 * stay at six and the processes are counted separately, and the loader
 * stops when the pool says no rather than when this number does.
 */
#ifndef GEM4XE_PROC_H
#define GEM4XE_PROC_H

#include "aes/aes.h"
#include "sys/ctx.h"

#define NUM_ACCS    6           /* menu slots: the Desk box's children */
#define NUM_PROCS   4           /* the application, and three accessories */
#define ACC_MSGS    8           /* an accessory's queue, in messages */

#define P_FREE  0               /* the record is not in use */
#define P_NEW   1               /* loaded, never given a turn */
#define P_LIVE  2               /* running, or parked with a wait recorded */
#define P_DONE  3               /* its program returned */

typedef struct PROC {
    CTX      p_ctx;             /* its turn on the engine stack */
    WORD     p_stat;
    WORD     p_evwait;          /* the MU_* it is parked on; 0 while running */
    uint32_t p_tdead;           /* MU_TIMER: the tick it is due at */
    WORD    *p_queue;           /* p_qmax messages of eight words */
    WORD     p_qmax, p_qcount;
    /* The one button wait a process can have outstanding.  It was a file
     * static in event.c and is per process for the same reason the queue
     * is: two of them can be inside ev_multi at once now. */
    WORD     p_bwactive, p_bwwant, p_bwdone, p_bwclicks;
    uint32_t p_bwparm;
} PROC;

extern PROC *proc_tab;          /* NUM_PROCS records, the caller's memory */
extern WORD  proc_n;            /* records in use: 1 until an accessory loads */

/* Who the mouse and the keyboard belong to.  GEM keeps one gl_mowner and
 * hands it about as the pointer crosses windows (geminput.c); here it
 * starts as the application and moves to an accessory for as long as one
 * is open.  It is also what keeps the round robin honest: THE INPUT
 * OWNER IS ALWAYS READY TO RUN, because it is the one that can see the
 * mouse, and without that rule an accessory that finishes a dialog and
 * parks on a message starves the application for ever -- neither of them
 * has a message or a deadline, so neither would ever be chosen. */
extern PROC *proc_input;

/* The running process, the donor's name for it.  p_ctx is the first
 * member, so the context the switch is holding IS the process -- which
 * means there is no second copy of "who is running" to keep in step with
 * ctx_cur, and no way for the two to disagree. */
#define rlr     ((PROC *)ctx_cur)

/* The application's process and its context.  Call ctx_init() on
 * proc_app->p_ctx FROM THE SHALLOWEST PLACE THAT WILL EVER SWITCH --
 * src/sys/ctx.h says why, and main() is that place. */
#define proc_app    (&proc_tab[0])

/* The records, in memory the CALLER owns: PROC_STORE bytes of bank $00.
 * The engine takes them from the pool; a milestone runner that has no
 * loader and no pool passes a static array, which is why proc.c allocates
 * nothing itself.  FALSE for a null store. */
#define PROC_STORE  (NUM_PROCS * sizeof(PROC))
WORD  proc_init(void *store);
WORD  proc_pid(const PROC *p);
PROC *proc_of(WORD pid);

/* A record for a program about to be loaded, taking the queue the caller
 * has found room for.  0 when there is no record left. */
PROC *proc_new(WORD *queue, WORD qmax);

/* Somebody else's turn, if anybody else can go.  ev_poll() calls it. */
void  proc_yield(void);

/* TRUE when p could run now: it has never had a turn, or what it is
 * waiting for has arrived. */
WORD  proc_ready(const PROC *p);

#endif /* GEM4XE_PROC_H */
