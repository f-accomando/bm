/* The SD map as a background job of the menu: sdmap_core.c does the walking, this file runs it in the menu's free time
 * (a fiber that gives the CPU back often: the Pi Zero's SD is slow). */
#include "kernel/sdmap.h"
#include "kernel/fiber.h"
#include "kernel/config.h"
#include "fs/fat.h"
#include "drivers/timer.h"

#include <stdlib.h>
#include <string.h>

size_t sdmap_core_save(char *out, size_t n);        /* sdmap_core.c: the cache text */
int sdmap_core_load(const char *text, size_t len);

static fiber_job_t job = { .name = "sdmap" };
static int want, started, done;
static int cache_tried, have_cache;     /* bm/sdmap.txt read once per boot; its numbers kept in `cache` */
static sdmap_t cache;
static int result = 1;                  /* what the core said last: 1 more, 0 done, -1 error */

/* the numbers of the last complete scan, for the next boot (inside the job: the write gives the CPU back) */
static void save_cache(void)
{
    static char buf[6144];
    size_t n = sdmap_core_save(buf, sizeof buf);
    fat_entry_t e;
    if (!n)
        return;
    if (config_find_file("sdmap.txt", &e) == 0)
        fat_replace("/bm/sdmap.txt", buf, n);
    else
        fat_write_file("/bm", "sdmap.txt", buf, n);
}

static void load_cache(void)
{
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    cache_tried = 1;
    if (config_find_file("sdmap.txt", &e) != 0 || fat_load(&e, &data, &len) != 0)
        return;
    if (len <= 6144 && sdmap_core_load((const char *)data, len)) {
        cache = *sdmap_core_result();
        have_cache = 1;
    }
    free(data);
}

static void scan_main(void *arg)
{
    (void)arg;
    while (!fiber_cancelled()) {
        int r = sdmap_core_step(8);
        fiber_slice();
        if (r <= 0) {
            result = r;
            if (r == 0)
                save_cache();
            return;
        }
    }
}

void sdmap_request(void)
{
    if (!cache_tried)
        load_cache();
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
    return done ? SDMAP_DONE : have_cache ? SDMAP_CACHED : started ? SDMAP_SCANNING : SDMAP_NONE;
}

const sdmap_t *sdmap_result(void) { return !done && have_cache ? &cache : sdmap_core_result(); }

int sdmap_permille(void) { return done ? 1000 : sdmap_core_permille(); }
