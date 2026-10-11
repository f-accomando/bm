/*
 * Fibers: code that runs on a stack of its own and gives the CPU back
 * when it waits (M25, the Market). The kernel has one core and no
 * threads: a fiber runs only inside fiber_resume, until it calls
 * fiber_yield (net_wait_step does, so HTTP and TLS wait without stopping
 * the menu) or ends. Interrupts run on the stack that is current, so a
 * fiber's stack has room for them too.
 */
#ifndef FIBER_H
#define FIBER_H

#include <stddef.h>
#include <stdint.h>

typedef struct fiber {
    uint32_t *sp;               /* the fiber's, while it is not running */
    uint32_t *back;             /* the resumer's, while the fiber runs */
    uint32_t *stack;            /* lowest word: a guard */
    size_t size;
    void (*fn)(void *);
    void *arg;
    int done;                   /* fn returned */
    int cancel;                 /* fiber_cancel: the waits give up */
    uint32_t until;             /* fiber_run: when fiber_slice gives the CPU back */
} fiber_t;

/* Gets f ready to run fn(arg) on the given stack (8-byte aligned, at least
 * a few KiB; the caller keeps it). */
void fiber_prepare(fiber_t *f, void *stack, size_t size, void (*fn)(void *), void *arg);
/* Runs f until it yields or ends. Returns 1 while it has more to do, 0
 * once fn has returned. Not from inside a fiber. */
int  fiber_resume(fiber_t *f);
/* Inside a fiber: back to fiber_resume; it goes on from here next time. */
void fiber_yield(void);
/* The fiber running now, NULL outside fibers. */
fiber_t *fiber_current(void);
/* Asks f to stop: from now on fiber_cancelled() is 1 inside it and the
 * network waits fail at once, so it ends soon. */
void fiber_cancel(fiber_t *f);
/* 1 if the running fiber was asked to stop (0 outside fibers). */
int  fiber_cancelled(void);

/* Time slices (the menu's work in the background, 2026-10-04): fiber_run
 * resumes f until `until` (timer_ticks); inside, fiber_slice() gives the
 * CPU back once that time is over and is cheap before (call it often: the
 * SD card's reads do, through fat_load_tick). Outside fibers it does
 * nothing, so the same code also runs straight through. */
int  fiber_run(fiber_t *f, uint32_t until);
void fiber_slice(void);
/* fiber_slice as a FAT tick (fat_write_tick, set once at boot): a long
 * write in a fiber gives the CPU back between its pieces; 0: it goes on */
int  fiber_slice_tick(void);

/* A job of the menu: a fiber with a stack of its own (allocated the first
 * time), started again for each piece of work. */
typedef struct {
    fiber_t f;
    void *stack;
    size_t size;
    int busy;                   /* started and not over */
    uint32_t longest;           /* the longest slice so far, us (over its time) */
    const char *name;           /* in fiber_jobs_report ("covers", "market"...) */
    uint32_t took, worst;       /* since that report: us run, and the most over its time */
    uint32_t stopped;           /* since that report: the longest fiber_job_stop, us */
} fiber_job_t;

/* Starts fn(arg) in j (it must not be busy). 0, or -1 without memory. */
int  fiber_job_start(fiber_job_t *j, size_t stack, void (*fn)(void *), void *arg);
/* Runs it until `until`, resumed again while it gives the CPU back before
 * (the network's waits yield at once); 1 while it has more to do. */
int  fiber_job_run(fiber_job_t *j, uint32_t until);
/* Asks it to stop and lets it end (its waits and reads fail at once). */
void fiber_job_stop(fiber_job_t *j);

/* What the jobs took since the last call, for the menu's log line: "covers
 * 31 ms (+2.0), market 4 ms (+0.3, stop 120.5)": the time each ran in its
 * slices, and in brackets the most it went over a deadline, ms (a stall of
 * the menu: a TLS handshake, a hash, a cover decoded do not give the CPU
 * back), then the longest stop if one waited (fiber_job_stop: the menu
 * waits for the job to end; apart since 2026-10-11). 0 if none ran; it
 * starts counting again. */
int  fiber_jobs_report(char *buf, size_t n);

/* The background's share of the CPU (2026-10-11). The jobs run only in
 * the free time of a frame, from a table: for each context (where that
 * time comes from) each job has a share of it, in %.
 *
 *   FIBER_MENU  the menu on the screen: the menu's jobs (carts.c, and
 *               src/rgb30/ui.c on the RGB30)
 *   FIBER_TOOL  a tool of the suite (bm Write, the editors): 0 for now,
 *               nothing calls it there (the jobs are stopped before an
 *               application starts: background_stop)
 *   FIBER_GAME  a game: 0, and it stays so unless a game asks for it: a
 *               game owns the machine, a lockstep one (Overbit, bmnet)
 *               must never lose time to the background
 *
 * fiber_share_run gives each job with a share in the context, in the
 * table's order, at least that part of the time up to `until` (from when
 * its turn comes; a job with nothing to do returns at once, and its part
 * goes to the next), then what is left to the same jobs in the same
 * order: the first rows are the ones the user waits for. The shares of a
 * context add up to 100 at most. A job that does not give the CPU back in
 * time takes more than its part (fiber_jobs_report says how much). A file
 * from the PC being written is not in the tables: it goes first, alone on
 * the SD card (netxfer_write_tick). */
enum { FIBER_GAME, FIBER_TOOL, FIBER_MENU, FIBER_CONTEXTS };

typedef struct {
    void (*tick)(uint32_t until);       /* runs its job until `until`; back at once with nothing to do */
    uint8_t share[FIBER_CONTEXTS];      /* its sure part of the free time, % (0: not in that context) */
} fiber_share_t;

void fiber_share_run(const fiber_share_t *t, int n, int ctx, uint32_t until);

#endif
