/* The SD map as a background job of the menu: sdmap_core.c does the walking, this file runs it in the menu's free time
 * (a fiber that gives the CPU back often: the Pi Zero's SD is slow). */
#include "kernel/sdmap.h"
#include "kernel/fiber.h"
#include "drivers/timer.h"

static fiber_job_t job = { .name = "sdmap" };
static int want, started, done;
static int result = 1;                  /* what the core said last: 1 more, 0 done, -1 error */

static void scan_main(void *arg)
{
    (void)arg;
    while (!fiber_cancelled()) {
        int r = sdmap_core_step(8);
        fiber_slice();
        if (r <= 0) {
            result = r;
            return;
        }
    }
}

void sdmap_request(void)
{
    if (!done)
        want = 1;
}

void sdmap_tick(uint32_t until)
{
    if (!want || done)
        return;
    if (!job.busy) {
        if (result <= 0) {              /* the last run ended: done, or an error (the numbers so far stay) */
            done = 1;
            want = 0;
            return;
        }
        if (!started) {
            sdmap_core_reset();
            started = 1;
        }
        if (fiber_job_start(&job, 16 * 1024, scan_main, NULL) != 0)
            return;
    }
    fiber_job_run(&job, until);
    if (!job.busy && result <= 0) {
        done = 1;
        want = 0;
    }
}

void sdmap_stop(void)
{
    fiber_job_stop(&job);
}

int sdmap_state(void)
{
    return done ? SDMAP_DONE : started ? SDMAP_SCANNING : SDMAP_NONE;
}

const sdmap_t *sdmap_result(void) { return sdmap_core_result(); }

int sdmap_permille(void) { return done ? 1000 : sdmap_core_permille(); }
