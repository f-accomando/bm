/* file_write for cartridges and its queue: see webfile.h. */
#include "webfile.h"

#include "webpolicy.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "kernel/fiber.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITER_STACK (64u * 1024u)

typedef struct {
    char name[13];
    wf_state_t state;
    uint8_t *data;
    size_t len;
    char err[96];
    uint32_t seq;               /* order of queueing: the oldest waits first, the oldest result goes first */
} job_t;

static job_t jobs[WF_JOBS];
static uint32_t seq;
static job_t *active;           /* the job the fiber writes, or NULL */
static fiber_t fib;
static void *fib_stack;
static int stopped;             /* the fiber was asked to stop and did */
static int run_rc;

static int say(char *err, size_t en, const char *msg)
{
    snprintf(err, en, "%s", msg);
    return -1;
}

/* the write itself, the rules of file_write: a folder of at most 128 files, a folder is no file */
static int do_write(const char *name, const uint8_t *data, size_t len, char *err, size_t en)
{
    char path[32];
    snprintf(path, sizeof path, "%s/%s", WEB_FILES_DIR, name);
    fat_entry_t e;
    const int there = fat_find(path, &e) == 0;
    if (there && e.is_dir)
        return say(err, en, "that name is a folder");
    if (!there) {
        int files = 0;
        fat_dir_t d;
        if (fat_opendir(&d, WEB_FILES_DIR) == 0)
            while (fat_readdir(&d, &e) > 0)
                files++;
        if (files >= FILE_MAX_FILES)
            return say(err, en, "the bm/web folder has 128 files already");
    }
    const int ok = there ? fat_replace(path, data, len) == 0
                         : fat_mkdirs(WEB_FILES_DIR) == 0 && fat_write_file(WEB_FILES_DIR, name, data, len) == 0;
    return ok ? 0 : say(err, en, fat_error());
}

static job_t *find(const char *name)
{
    job_t *best = NULL;
    for (int i = 0; i < WF_JOBS; i++)
        if (jobs[i].state != WF_NONE && strcmp(jobs[i].name, name) == 0 && (!best || jobs[i].seq > best->seq))
            best = &jobs[i];
    return best;
}

static void drop(job_t *j)
{
    free(j->data);
    memset(j, 0, sizeof *j);
}

/* after each piece: the cartridge's frame goes on; nonzero (asked to stop) leaves the old file */
static int writer_tick(void)
{
    fiber_slice();
    return fiber_cancelled();
}

static void writer_main(void *arg)
{
    job_t *j = arg;
    int (*was)(void) = fat_write_tick;
    fat_write_tick = writer_tick;
    run_rc = do_write(j->name, j->data, j->len, j->err, sizeof j->err);
    fat_write_tick = was;
    stopped = run_rc != 0 && fiber_cancelled();
}

static void end_fiber(void)
{
    free(fib_stack);
    fib_stack = NULL;
    active = NULL;
}

/* the fiber is over: the job is done, failed, or (stopped) waits to start again */
static void settle(void)
{
    job_t *j = active;
    end_fiber();
    if (stopped) {
        j->state = WF_QUEUED;
    } else {
        j->state = run_rc ? WF_ERROR : WF_DONE;
        free(j->data);
        j->data = NULL;
    }
}

static void finish(void)
{
    settle();
    sd_writer_end("file_write");
}

/* another writer wants the card (sdlock.c calls this; the lock is taken over, not ended here) */
static void pause_writer(void)
{
    if (!active)
        return;
    fiber_cancel(&fib);
    while (fiber_run(&fib, 0))
        ;
    settle();
}

int webfile_write(const char *name, const uint8_t *data, size_t len, size_t cap, char *err, size_t en)
{
    if (len > cap)
        return say(err, en, "the file is over the cap (file_write_max_kib in bm/config.txt)");
    if (sd_writer_begin("file_write", NULL) != 0)
        return say(err, en, "the SD card is busy writing: try again");
    job_t *same = find(name);
    if (same && same->state == WF_QUEUED)
        drop(same);                     /* this write replaces the queued one */
    const int rc = do_write(name, data, len, err, en);
    sd_writer_end("file_write");
    return rc;
}

void webfile_forget(const char *name)
{
    job_t *j = find(name);
    if (!j)
        return;
    if (j->state == WF_WRITING) {
        sd_writer_end("file_write");
        pause_writer();
    }
    drop(j);
}

int webfile_queue(const char *name, const uint8_t *data, size_t len, size_t cap, char *err, size_t en)
{
    uint8_t *copy = malloc(len ? len : 1);
    if (!copy)
        return say(err, en, "not enough memory");
    memcpy(copy, data, len);
    return webfile_queue_own(name, copy, len, cap, err, en);
}

int webfile_queue_own(const char *name, uint8_t *copy, size_t len, size_t cap, char *err, size_t en)
{
    if (len > cap) {
        free(copy);
        return say(err, en, "the file is over the cap (file_write_max_kib in bm/config.txt)");
    }
    job_t *j = find(name);
    if (j && j->state == WF_WRITING) {
        sd_writer_end("file_write");                /* ours: the writer stops, then the job is dropped */
        pause_writer();
    }
    if (j && j->state != WF_NONE) {
        drop(j);                        /* a waiting job, or a result of the same name: replaced */
    } else {
        job_t *old = NULL;
        j = NULL;
        for (int i = 0; i < WF_JOBS && !j; i++)
            if (jobs[i].state == WF_NONE)
                j = &jobs[i];
            else if ((jobs[i].state == WF_DONE || jobs[i].state == WF_ERROR) && (!old || jobs[i].seq < old->seq))
                old = &jobs[i];
        if (!j && old) {
            drop(old);
            j = old;
        }
        if (!j) {
            free(copy);
            return say(err, en, "the write queue is full: wait for a file_state to be done");
        }
    }
    memset(j, 0, sizeof *j);
    snprintf(j->name, sizeof j->name, "%s", name);
    j->data = copy;
    j->len = len;
    j->seq = ++seq;
    j->state = WF_QUEUED;
    return 0;
}

int webfile_pump(uint32_t budget_us)
{
    if (!active) {
        job_t *next = NULL;
        for (int i = 0; i < WF_JOBS; i++)
            if (jobs[i].state == WF_QUEUED && (!next || jobs[i].seq < next->seq))
                next = &jobs[i];
        if (!next || sd_writer_begin("file_write", pause_writer) != 0)
            return 0;
        if (!(fib_stack = malloc(WRITER_STACK))) {
            sd_writer_end("file_write");
            return 0;
        }
        next->state = WF_WRITING;
        stopped = 0;
        run_rc = 0;
        active = next;
        fat_write_done = 0;
        fiber_prepare(&fib, fib_stack, WRITER_STACK, writer_main, next);
    }
    const uint32_t until = timer_ticks() + budget_us;
    int more;
    do
        more = fiber_run(&fib, until);
    while (more && (int32_t)(until - timer_ticks()) > 0);
    if (!more)
        finish();
    return 1;
}

wf_state_t webfile_state(const char *name, char *err, size_t en, size_t *done)
{
    job_t *j = find(name);
    if (!j)
        return WF_NONE;
    if (done)
        *done = j->state == WF_DONE ? j->len : j == active ? fat_write_done : 0;
    if (j->state == WF_ERROR && err)
        snprintf(err, en, "%s", j->err);
    return j->state;
}

int webfile_busy(void)
{
    for (int i = 0; i < WF_JOBS; i++)
        if (jobs[i].state == WF_QUEUED || jobs[i].state == WF_WRITING)
            return 1;
    return 0;
}

void webfile_reset(void)
{
    webfile_flush();
    for (int i = 0; i < WF_JOBS; i++)
        drop(&jobs[i]);
}

void webfile_flush(void)
{
    /* each round writes pieces for a second; it ends when nothing waits, or when another writer
     * holds the card and the pump cannot start (the job stays queued) */
    while (webfile_busy() && webfile_pump(1000000u))
        ;
}
