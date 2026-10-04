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

#endif
