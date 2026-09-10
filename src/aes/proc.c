/* proc.c -- who runs next.  See proc.h for the shape and src/sys/ctx.h
 * for the stack half.
 *
 * This is deliberately the smallest scheduler that is still GEM's: a
 * round robin over the processes that can go, entered from one place,
 * with no pre-emption and no timer.  The donor's is the same round robin
 * with the queues and event blocks broken out into lists; the lists earn
 * their keep at a dozen processes and cost more than they save at four.
 */
#include "aes/proc.h"

PROC *proc_tab;
PROC *proc_input;
WORD  proc_n;
WORD  proc_turns;   /* turns handed over: a measurement, not a gate */

/* event.c's, so that the application keeps the queue it always had --
 * sixteen messages in the banked window -- and only an accessory pays
 * the pool for one. */
extern WORD *gl_appqueue(WORD *max);

WORD proc_init(void *store)
{
    WORD i, max;

    proc_tab = (PROC *)store;
    if (!proc_tab)
        return FALSE;
    for (i = 0; i < NUM_PROCS; i++) {
        PROC *p = &proc_tab[i];
        p->p_stat = P_FREE;
        p->p_evwait = 0;
        p->p_tdead = 0;
        p->p_queue = 0;
        p->p_qmax = p->p_qcount = 0;
        p->p_bwactive = p->p_bwwant = p->p_bwdone = p->p_bwclicks = 0;
        p->p_bwparm = 0;
    }
    proc_tab[0].p_stat = P_LIVE;
    proc_tab[0].p_queue = gl_appqueue(&max);
    proc_tab[0].p_qmax = max;
    proc_n = 1;
    proc_input = proc_tab;
    return TRUE;
}

WORD proc_pid(const PROC *p)
{
    return (WORD)(p - proc_tab);
}

PROC *proc_of(WORD pid)
{
    if (pid < 0 || pid >= proc_n || proc_tab[pid].p_stat == P_FREE)
        return 0;
    return &proc_tab[pid];
}

PROC *proc_new(WORD *queue, WORD qmax)
{
    PROC *p;
    WORD *q = queue;

    if (proc_n >= NUM_PROCS || !q)
        return 0;
    p = &proc_tab[proc_n];
    p->p_queue = q;
    p->p_qmax = qmax;
    p->p_qcount = 0;
    p->p_stat = P_NEW;
    proc_n++;
    return p;
}

/* Give back the record proc_new() just handed out, when the load it was
 * for did not happen.  Only the last one: the table is filled in order
 * and nothing has a pointer to this record yet. */
void proc_drop(PROC *p)
{
    if (p == &proc_tab[proc_n - 1] && proc_n > 1) {
        p->p_stat = P_FREE;
        p->p_queue = 0;
        p->p_qmax = p->p_qcount = 0;
        proc_n--;
    }
}

WORD proc_ready(const PROC *p)
{
    if (p->p_stat == P_NEW)
        return TRUE;                /* it has never had its first turn */
    if (p->p_stat != P_LIVE)
        return FALSE;
    if (p == proc_input)
        return TRUE;                /* it can always look at the input */
    if (p->p_evwait == 0)
        return TRUE;                /* parked outside a wait: not waiting */
    if ((p->p_evwait & MU_MESAG) && p->p_qcount)
        return TRUE;
    if ((p->p_evwait & MU_TIMER) && gl_ticks >= p->p_tdead)
        return TRUE;
    return FALSE;
}

/* Give every other process a turn until none of them has a message left
 * unread, or until `rounds` turns have gone by -- the donor's
 * wait_for_accs, which blocks appl_exit until every accessory has taken
 * its AC_CLOSE out of the queue.
 *
 * The bound is what stops an accessory that has stopped reading from
 * hanging the machine: the donor gives up after 500 dispatcher rounds
 * and abandons it.  A round here is a context switch and a stack copy
 * rather than a register swap, so the number is smaller and the reason
 * is the same. */
void proc_drain(WORD rounds)
{
    WORD i;

    while (rounds-- > 0) {
        PROC *busy = 0;

        for (i = 1; i < proc_n; i++)
            if (proc_tab[i].p_stat == P_LIVE && proc_tab[i].p_qcount) {
                busy = &proc_tab[i];
                break;
            }
        if (!busy)
            return;
        proc_turns++;
        ctx_switch(&busy->p_ctx);
    }
}

void proc_yield(void)
{
    PROC *p;
    WORD i, here;

    if (proc_n < 2)
        return;
    if (!rlr->p_evwait)
        return;                     /* not parked on anything: keep going.
                                     * Without this a process between waits
                                     * would hand over and be handed back
                                     * on every poll, and pay two stack
                                     * copies each time for nothing. */
    if (!ct_idle())
        return;                     /* a gesture is in flight: see proc.h */

    /* Round robin from the one after the running process, so that a
     * process that is always ready cannot starve the one behind it. */
    here = proc_pid(rlr);
    for (i = 1; i < proc_n; i++) {
        p = &proc_tab[(here + i) % proc_n];
        if (p != rlr && proc_ready(p)) {
            proc_turns++;
            ctx_switch(&p->p_ctx);
            return;
        }
    }
}
