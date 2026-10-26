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

#endif
