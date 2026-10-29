/* Host test of kernel/jobs.c (V2): the jobs run at once on core 0 here (no worker), and the answers are the ones we know. */
#include "kernel/jobs.h"
#include "kernel/offload.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint32_t timer_ticks(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000000u + t.tv_nsec / 1000u);
}
void timer_delay_us(uint32_t us) { (void)us; }
int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* a grid from rows of text: '.' free, '#' blocked, digits a cost */
static size_t make_path(uint8_t *in, const char *rows[], int w, int h, int sx, int sy, int gx, int gy, int diag)
{
    in[0] = (uint8_t)w; in[1] = (uint8_t)h; in[2] = (uint8_t)sx; in[3] = (uint8_t)sy; in[4] = (uint8_t)gx; in[5] = (uint8_t)gy; in[6] = (uint8_t)diag;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const char c = rows[y][x];
            in[7 + y * w + x] = c == '#' ? 255 : c == '.' ? 0 : (uint8_t)((c - '0') * 10);
        }
    return 7 + (size_t)w * h;
}

static int run(const char *name, const uint8_t *in, size_t n, const uint8_t **out, size_t *len)
{
    const int k = jobs_kernel_id(name);
    const int id = jobs_start(k, in, n);
    if (id <= 0)
        return -id;
    int st = jobs_poll(id, out, len);
    CHECK(st == JOB_DONE || st == JOB_FAIL, "no worker: the job is over at once (state %d)", st);
    return st == JOB_DONE ? 0 : 100;
}

int main(void)
{
    uint8_t in[JOB_IN_MAX];
    const uint8_t *out = NULL;
    size_t n = 0;

    CHECK(jobs_kernel_id("grid_path") >= 0 && jobs_kernel_id("grid_flood") >= 0 && jobs_kernel_id("nope") == -1, "names");
    CHECK(jobs_workers() == 0, "no worker on the host");

    /* a straight corridor, four directions: 5 cells from (0,0) to (4,0) */
    const char *a[] = { ".....", "#####" };
    size_t m = make_path(in, a, 5, 2, 0, 0, 4, 0, 0);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && n == 3 + 2 * 5 && out[0] == 1 && out[1] == 5 && out[2] == 0, "corridor");
    CHECK(out[3] == 0 && out[4] == 0 && out[11] == 4 && out[12] == 0, "corridor ends");

    /* around a wall: start and goal on both sides */
    const char *b[] = { "..#..", "..#..", "..#..", "....." };
    m = make_path(in, b, 5, 4, 0, 0, 4, 0, 0);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && out[0] == 1, "wall");
    CHECK(out[1] == 11, "around the wall: 11 cells, got %d", out[1]);   /* down 3, right 4, up 3: 10 steps, 11 cells */

    /* no way */
    const char *c[] = { ".#.", ".#.", ".#." };
    m = make_path(in, c, 3, 3, 0, 0, 2, 2, 1);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && n == 3 && out[0] == 0 && out[1] == 0, "no path");

    /* eight directions: a diagonal in 4 cells, no cut of the corner of a blocked cell */
    const char *d[] = { "....", "....", "....", "...." };
    m = make_path(in, d, 4, 4, 0, 0, 3, 3, 1);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && out[1] == 4, "diagonal in 4 cells, got %d", out[1]);
    const char *e[] = { ".#", "#." };
    m = make_path(in, e, 2, 2, 0, 0, 1, 1, 1);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && out[0] == 0, "no cut of a corner");

    /* a cost: the path goes around a dear cell when the way is short */
    const char *f[] = { ".9.", "...", "..." };
    m = make_path(in, f, 3, 3, 0, 0, 2, 0, 0);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && out[0] == 1 && out[1] == 5, "around a dear cell: 5 cells, got %d", out[1]);

    /* start is the goal; start blocked */
    m = make_path(in, a, 5, 2, 2, 0, 2, 0, 0);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && out[0] == 1 && out[1] == 1 && out[3] == 2, "start is the goal");
    m = make_path(in, a, 5, 2, 0, 1, 4, 0, 0);
    CHECK(run("grid_path", in, m, &out, &n) == 0 && out[0] == 0, "start blocked");

    /* bad input: the job fails, the game goes on */
    CHECK(run("grid_path", in, 3, &out, &n) == 100, "short input fails");
    in[0] = 65;
    CHECK(run("grid_path", in, 7 + 65 * 2, &out, &n) == 100, "65 wide fails");
    CHECK(jobs_start(0, in, JOB_IN_MAX + 1) == -2, "too long");

    /* a flow field */
    uint8_t fin[5 + 25];
    fin[0] = 5; fin[1] = 5; fin[2] = 0; fin[3] = 0; fin[4] = 0;
    for (int i = 0; i < 25; i++)
        fin[5 + i] = i == 6 || i == 7 || i == 8 ? 255 : 0;
    CHECK(run("grid_flood", fin, sizeof fin, &out, &n) == 0 && n == 25, "flood size");
    CHECK(out[0] == 0 && out[1] == 1 && out[4] == 4 && out[5] == 1 && out[6] == 255 && out[9] == 5 && out[10] == 2, "flood");
    CHECK(out[24] == 8, "the far corner is 8 steps (%d)", out[24]);

    /* slots: 8 at a time, ids are not reused at once, a read frees the slot */
    int ids[JOBS_SLOTS];
    m = make_path(in, a, 5, 2, 0, 0, 4, 0, 0);
    int first = jobs_start(0, in, m);
    CHECK(first > 0, "id");
    CHECK(jobs_poll(first, &out, &n) == JOB_DONE, "read");
    CHECK(jobs_poll(first, &out, &n) == JOB_NONE, "an id is read once");
    for (int i = 0; i < JOBS_SLOTS; i++) {
        ids[i] = jobs_start(0, in, m);
        CHECK(ids[i] > 0 && ids[i] != first, "id %d", i);
    }
    CHECK(jobs_start(0, in, m) == -3, "all slots in use");
    jobs_cancel(ids[0]);
    CHECK(jobs_poll(ids[0], &out, &n) == JOB_NONE, "cancelled");
    jobs_reset();
    CHECK(jobs_start(0, in, m) > 0, "reset frees the slots");
    jobs_reset();

    /* a 64 x 64 open grid, eight directions: the corner to the corner in 64 cells */
    static uint8_t big[7 + 64 * 64];
    big[0] = 64; big[1] = 64; big[2] = 0; big[3] = 0; big[4] = 63; big[5] = 63; big[6] = 1;
    memset(big + 7, 0, 64 * 64);
    const uint32_t t0 = timer_ticks();
    CHECK(run("grid_path", big, sizeof big, &out, &n) == 0 && out[0] == 1 && out[1] == 64, "64 x 64 diagonal");
    printf("64 x 64 open grid, A*: %u us on this PC\n", (unsigned)(timer_ticks() - t0));

    printf(failures ? "jobs: %d FAILED\n" : "jobs: all ok\n", failures);
    return failures != 0;
}
