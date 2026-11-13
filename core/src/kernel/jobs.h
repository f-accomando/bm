/* Pure jobs for the games (docs/api/jobs.md, V2 of docs/progress/multicore.md).
 * A game posts a job by name with a block of bytes and reads the answer later. The kernel runs the job on a worker core
 * (RGB30 with cores=4) or at once on core 0 (the Pi Zero W, the host, a console without workers): the answer is the same.
 * A job is a function of the kernel that reads its input block and writes its output block. A game cannot add one:
 * a worker never runs Lua and never touches the heap. */
#ifndef KERNEL_JOBS_H
#define KERNEL_JOBS_H

#include <stddef.h>
#include <stdint.h>

#define JOBS_SLOTS   8
#define JOB_IN_MAX   4608               /* grid_path: 7 + 64 x 64 */
#define JOB_OUT_MAX  8448               /* grid_path: 3 + 2 x 4096 */

enum { JOB_NONE = 0, JOB_QUEUED, JOB_RUN, JOB_DONE, JOB_FAIL };

int jobs_kernel_id(const char *name);                   /* the number of a job by name, -1 if there is none */
/* Posts a job. Returns its id (above 0), or a negative number: -1 no such kernel, -2 the input is too long, -3 every slot is busy. */
int jobs_start(int kernel, const uint8_t *in, size_t len);
/* The state of a job: JOB_QUEUED, JOB_RUN, JOB_DONE, JOB_FAIL, or JOB_NONE for an id that is not valid (or already read).
 * With JOB_DONE, *out and *len are the answer; it stays valid until the next jobs_start. The call that gives JOB_DONE
 * or JOB_FAIL frees the slot. */
int jobs_poll(int id, const uint8_t **out, size_t *len);
void jobs_cancel(int id);                               /* the answer is not wanted */
void jobs_reset(void);                                  /* at the start and the end of a run: waits for the jobs on workers, frees every slot */
int jobs_workers(void);                                 /* worker cores that can take a job (0: the job runs at once on core 0) */
uint32_t jobs_on_workers(void);                         /* jobs that ran on a worker since the start */

int jobs_selftest(void);                                /* the QEMU check: the answer of a worker is the answer of core 0 */

#endif
