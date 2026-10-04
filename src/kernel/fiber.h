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

/* A job of the menu: a fiber with a stack of its own (allocated the first
 * time), started again for each piece of work. */
typedef struct {
    fiber_t f;
    void *stack;
    size_t size;
    int busy;                   /* started and not over */
    uint32_t longest;           /* the longest slice so far, us (over its time) */
} fiber_job_t;

/* Starts fn(arg) in j (it must not be busy). 0, or -1 without memory. */
int  fiber_job_start(fiber_job_t *j, size_t stack, void (*fn)(void *), void *arg);
/* Runs it until `until`; 1 while it has more to do. */
int  fiber_job_run(fiber_job_t *j, uint32_t until);
/* Asks it to stop and lets it end (its waits and reads fail at once). */
void fiber_job_stop(fiber_job_t *j);

#endif
