/* Run a pure function on another core when the kernel has one (RGB30: docs/progress/multicore.md, M5).
 * fn(blk) must be pure: it reads and writes only blk (a copy of the caller's block) and memory the caller
 * keeps alive and does not change meanwhile; no malloc, no libc state, no device, no Lua.
 * offload_run copies blk (at most OFFLOAD_BLK bytes) in, waits calling idle() (a fiber yield; may be NULL),
 * and copies it back. Returns 0 when it ran on a worker, -1 when there is none (the caller runs fn itself).
 * If the waiting fiber is stopped, the worker finishes alone and its answer is dropped. */
#ifndef OFFLOAD_H
#define OFFLOAD_H

#define OFFLOAD_BLK 64

int offload_run(void (*fn)(void *blk), void *blk, unsigned size, void (*idle)(void));

/* How many worker cores can take a job now (0: the Pi Zero W, the host tests, a console without cores=4). */
int offload_workers(void);

/* Run fn(blks[i]) for i = 0 .. n-1 at once: the blocks 1.. on workers, the rest and block 0 on the caller; returns when all are done.
 * The blocks stay where they are (no copy) and the caller keeps them alive; each is at least 64 bytes apart (a cache line). fn is pure as
 * for offload_run. A worker that faults or hangs: the caller runs its block (fn must give the same result run twice). Returns the number
 * of blocks that ran on a worker. With no worker, the caller runs them all, in order. */
int offload_par(void (*fn)(void *blk), void *const *blks, unsigned n);

/* The same cores for a job that is read later (kernel/jobs.h). One job at a time on a core. The caller keeps the argument alive until the job ends.
 * offload_total: the worker cores that work (0: the Pi, the host, a console without cores=4). offload_core_ok: the core is up and idle.
 * offload_post_core: 0, or -1. offload_poll_core: 1 done, 0 runs, -1 faulted. offload_abandon_core: the core did not end its job: it is not used again. */
int offload_total(void);
int offload_core_ok(int core);
int offload_post_core(int core, void (*fn)(void *arg), void *arg);
int offload_poll_core(int core);
void offload_abandon_core(int core);

#endif
