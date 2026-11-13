#include "kernel/jobs.h"
#include "kernel/offload.h"
#include "drivers/timer.h"
#include "lib/printf.h"
#include <string.h>

#define BAD ((size_t)-1)
#define CELLS_MAX 4096                  /* a grid is at most 64 x 64 */
#define SCRATCH_N 4                     /* one for each core: core 0 (inline) and the three workers */
#define WORKER_US 1000000u              /* a job on a worker that lasts more: the core is lost, core 0 runs the job */

/* ---- grid_path: A* on a grid ----
 * in:  w h sx sy gx gy flags, then w x h cells (u8). w, h 1..64. flags bit 0: eight directions (no cut of a corner).
 *      A cell 0 is free, 255 is blocked, 1..254 is free with that many tenths of a step added to each step into it.
 * out: found (1 or 0), a count n (u16 little endian), then n pairs x y from the start to the goal.
 * A step costs 10 (14 on a diagonal); the answer is the cheapest path, and the first one found among equals. */
typedef struct {
    uint32_t g[CELLS_MAX], f[CELLS_MAX];
    uint16_t par[CELLS_MAX], pos[CELLS_MAX], heap[CELLS_MAX];
    uint8_t state[CELLS_MAX];           /* 0 not seen, 1 open, 2 closed */
} astar_t;

typedef struct {
    uint16_t queue[CELLS_MAX];
} flood_t;

typedef union {
    astar_t a;
    flood_t f;
} scratch_t;

static scratch_t scratch[SCRATCH_N];

static void heap_up(astar_t *s, int i)
{
    const uint16_t c = s->heap[i];
    while (i > 0) {
        const int p = (i - 1) / 2;
        if (s->f[s->heap[p]] <= s->f[c])
            break;
        s->heap[i] = s->heap[p];
        s->pos[s->heap[i]] = (uint16_t)i;
        i = p;
    }
    s->heap[i] = c;
    s->pos[c] = (uint16_t)i;
}

static void heap_down(astar_t *s, int i, int n)
{
    const uint16_t c = s->heap[i];
    for (;;) {
        int k = 2 * i + 1;
        if (k >= n)
            break;
        if (k + 1 < n && s->f[s->heap[k + 1]] < s->f[s->heap[k]])
            k++;
        if (s->f[s->heap[k]] >= s->f[c])
            break;
        s->heap[i] = s->heap[k];
        s->pos[s->heap[i]] = (uint16_t)i;
        i = k;
    }
    s->heap[i] = c;
    s->pos[c] = (uint16_t)i;
}

static uint32_t estimate(int x, int y, int gx, int gy, int diag)
{
    const int dx = x > gx ? x - gx : gx - x, dy = y > gy ? y - gy : gy - y;
    return diag ? (uint32_t)(10 * (dx + dy) - 6 * (dx < dy ? dx : dy)) : (uint32_t)(10 * (dx + dy));
}

static size_t k_grid_path(const uint8_t *in, size_t n, uint8_t *out, size_t cap, scratch_t *sc)
{
    if (n < 7 || cap < 3)
        return BAD;
    const int w = in[0], h = in[1], sx = in[2], sy = in[3], gx = in[4], gy = in[5], diag = in[6] & 1;
    if (w < 1 || h < 1 || w > 64 || h > 64 || n != 7 + (size_t)w * h || sx >= w || sy >= h || gx >= w || gy >= h)
        return BAD;
    const uint8_t *cell = in + 7;
    astar_t *s = &sc->a;
    const int cells = w * h;
    memset(s->state, 0, (size_t)cells);
    const int start = sy * w + sx, goal = gy * w + gx;
    out[0] = 0;
    out[1] = out[2] = 0;
    if (cell[start] == 255 || cell[goal] == 255)
        return 3;
    int nheap = 0;
    s->g[start] = 0;
    s->f[start] = estimate(sx, sy, gx, gy, diag);
    s->par[start] = (uint16_t)start;
    s->state[start] = 1;
    s->heap[0] = (uint16_t)start;
    s->pos[start] = 0;
    nheap = 1;
    static const int8_t dir[8][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
    while (nheap > 0) {
        const int cur = s->heap[0];
        nheap--;
        if (nheap > 0) {
            s->heap[0] = s->heap[nheap];
            s->pos[s->heap[0]] = 0;
            heap_down(s, 0, nheap);
        }
        s->state[cur] = 2;
        if (cur == goal)
            break;
        const int cx = cur % w, cy = cur / w;
        for (int d = 0; d < (diag ? 8 : 4); d++) {
            const int nx = cx + dir[d][0], ny = cy + dir[d][1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            const int nb = ny * w + nx;
            if (cell[nb] == 255 || s->state[nb] == 2)
                continue;
            if (d >= 4 && (cell[cy * w + nx] == 255 || cell[ny * w + cx] == 255))
                continue;                       /* no cut of a corner */
            const uint32_t ng = s->g[cur] + (d >= 4 ? 14u : 10u) + cell[nb];
            if (s->state[nb] == 1 && ng >= s->g[nb])
                continue;
            s->g[nb] = ng;
            s->f[nb] = ng + estimate(nx, ny, gx, gy, diag);
            s->par[nb] = (uint16_t)cur;
            if (s->state[nb] == 1) {
                heap_up(s, s->pos[nb]);
            } else {
                s->state[nb] = 1;
                s->heap[nheap] = (uint16_t)nb;
                s->pos[nb] = (uint16_t)nheap;
                heap_up(s, nheap);
                nheap++;
            }
        }
    }
    if (s->state[goal] != 2)
        return 3;
    int len = 1;
    for (int c = goal; c != start; c = s->par[c])
        len++;
    if (cap < 3 + 2 * (size_t)len)
        return BAD;
    out[0] = 1;
    out[1] = (uint8_t)(len & 255);
    out[2] = (uint8_t)(len >> 8);
    int i = len - 1;
    for (int c = goal;; c = s->par[c]) {
        out[3 + 2 * i] = (uint8_t)(c % w);
        out[4 + 2 * i] = (uint8_t)(c / w);
        if (c == start)
            break;
        i--;
    }
    return 3 + 2 * (size_t)len;
}

/* ---- grid_flood: the distance of every cell to a goal ----
 * in:  w h gx gy flags, then w x h cells (u8): 255 is blocked, any other value is free. flags bit 0: eight directions.
 * out: w x h bytes: the steps from the goal (at most 254), 255 for a blocked cell or a cell that the goal does not reach.
 * A flow field: an agent steps to the neighbour with the smallest number. */
static size_t k_grid_flood(const uint8_t *in, size_t n, uint8_t *out, size_t cap, scratch_t *sc)
{
    if (n < 5)
        return BAD;
    const int w = in[0], h = in[1], gx = in[2], gy = in[3], diag = in[4] & 1;
    if (w < 1 || h < 1 || w > 64 || h > 64 || n != 5 + (size_t)w * h || gx >= w || gy >= h || cap < (size_t)w * h)
        return BAD;
    const uint8_t *cell = in + 5;
    const int cells = w * h;
    memset(out, 255, (size_t)cells);
    const int goal = gy * w + gx;
    if (cell[goal] == 255)
        return (size_t)cells;
    uint16_t *q = sc->f.queue;
    int head = 0, tail = 0;
    out[goal] = 0;
    q[tail++] = (uint16_t)goal;
    static const int8_t dir[8][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
    while (head < tail) {
        const int cur = q[head++];
        if (out[cur] >= 254)
            continue;
        const int cx = cur % w, cy = cur / w;
        for (int d = 0; d < (diag ? 8 : 4); d++) {
            const int nx = cx + dir[d][0], ny = cy + dir[d][1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            const int nb = ny * w + nx;
            if (cell[nb] == 255 || out[nb] != 255)
                continue;
            if (d >= 4 && (cell[cy * w + nx] == 255 || cell[ny * w + cx] == 255))
                continue;
            out[nb] = (uint8_t)(out[cur] + 1);
            q[tail++] = (uint16_t)nb;
        }
    }
    return (size_t)cells;
}

static const struct {
    const char *name;
    size_t (*fn)(const uint8_t *in, size_t n, uint8_t *out, size_t cap, scratch_t *sc);
} kernels[] = {
    { "grid_path", k_grid_path },
    { "grid_flood", k_grid_flood },
};
#define NKERNELS ((int)(sizeof kernels / sizeof kernels[0]))

/* ---- the slots ---- */
typedef struct {
    uint8_t state;
    uint8_t kernel;
    uint16_t gen;
    int core;                           /* the worker of a job that runs, 0 for core 0 */
    int sc;                             /* the scratch it uses */
    uint32_t t0;
    size_t inlen;
    size_t outlen;                      /* BAD: the input was not right */
    uint8_t in[JOB_IN_MAX] __attribute__((aligned(16)));
    uint8_t out[JOB_OUT_MAX] __attribute__((aligned(16)));
} job_t;

static job_t jobs[JOBS_SLOTS] __attribute__((aligned(64)));
static uint32_t on_workers;
static uint8_t core_busy[SCRATCH_N];  /* a worker keeps its job until a poll has read its end: a new post would clear its answer */

int jobs_kernel_id(const char *name)
{
    for (int i = 0; i < NKERNELS; i++)
        if (!strcmp(name, kernels[i].name))
            return i;
    return -1;
}

int jobs_workers(void)
{
    return offload_total();
}

uint32_t jobs_on_workers(void)
{
    return on_workers;
}

/* the function of a job: it runs on a worker or on core 0, and writes only the slot and the scratch */
static void job_main(void *p)
{
    job_t *j = p;
    j->outlen = kernels[j->kernel].fn(j->in, j->inlen, j->out, JOB_OUT_MAX, &scratch[j->sc]);
}

static void job_end(job_t *j)
{
    if (j->core > 0)
        core_busy[j->core] = 0;
    j->state = j->outlen == BAD ? JOB_FAIL : JOB_DONE;
}

static void job_inline(job_t *j)
{
    if (j->core > 0)
        core_busy[j->core] = 0;
    j->core = 0;
    j->sc = 0;
    job_main(j);
    job_end(j);
}

/* QUEUED to RUN on a free worker; with no worker at all, the job runs here and now */
static void job_launch(job_t *j)
{
    if (offload_total() == 0) {
        job_inline(j);
        return;
    }
    int c = 1;
    while (c < SCRATCH_N && (core_busy[c] || !offload_core_ok(c)))
        c++;
    if (c >= SCRATCH_N)
        return;                         /* every worker is busy: it stays queued, a later poll tries again */
    j->core = c;
    j->sc = c;
    j->t0 = timer_ticks();
    if (offload_post_core(c, job_main, j) != 0) {
        job_inline(j);
        return;
    }
    core_busy[c] = 1;
    j->state = JOB_RUN;
    on_workers++;
}

static job_t *slot_of(int id)
{
    if (id <= 0)
        return NULL;
    job_t *j = &jobs[(id - 1) % JOBS_SLOTS];
    return j->state != JOB_NONE && (uint16_t)((id - 1) / JOBS_SLOTS) == j->gen ? j : NULL;
}

int jobs_start(int kernel, const uint8_t *in, size_t len)
{
    if (kernel < 0 || kernel >= NKERNELS)
        return -1;
    if (len > JOB_IN_MAX)
        return -2;
    for (int i = 0; i < JOBS_SLOTS; i++) {
        job_t *j = &jobs[i];
        if (j->state != JOB_NONE)
            continue;
        j->kernel = (uint8_t)kernel;
        j->inlen = len;
        j->outlen = 0;
        memcpy(j->in, in, len);
        j->state = JOB_QUEUED;
        job_launch(j);
        return 1 + i + JOBS_SLOTS * j->gen;
    }
    return -3;
}

static void job_free(job_t *j)
{
    j->state = JOB_NONE;
    j->gen++;
}

int jobs_poll(int id, const uint8_t **out, size_t *len)
{
    job_t *j = slot_of(id);
    if (!j)
        return JOB_NONE;
    if (j->state == JOB_QUEUED)
        job_launch(j);
    if (j->state == JOB_RUN) {
        const int r = offload_poll_core(j->core);
        if (r == 1) {
            job_end(j);
        } else if (r < 0) {
            job_inline(j);              /* the worker faulted: the same job again, here */
        } else if (timer_ticks() - j->t0 > WORKER_US) {
            offload_abandon_core(j->core);
            job_inline(j);
        }
    }
    const int st = j->state;
    if (st == JOB_DONE) {
        if (out)
            *out = j->out;
        if (len)
            *len = j->outlen;
        job_free(j);                    /* (the buffer stays as it is until a job is posted into the slot) */
    } else if (st == JOB_FAIL) {
        job_free(j);
    }
    return st;
}

void jobs_cancel(int id)
{
    job_t *j = slot_of(id);
    if (!j)
        return;
    if (j->state == JOB_RUN) {          /* a worker cannot be stopped: the slot is kept until it ends */
        const uint32_t t0 = timer_ticks();
        int r;
        while (!(r = offload_poll_core(j->core)) && timer_ticks() - t0 < WORKER_US)
            ;
        if (r == 0)
            offload_abandon_core(j->core);
        core_busy[j->core] = 0;
    }
    job_free(j);
}

void jobs_reset(void)
{
    for (int i = 0; i < JOBS_SLOTS; i++)
        if (jobs[i].state != JOB_NONE)
            jobs_cancel(1 + i + JOBS_SLOTS * jobs[i].gen);
}

/* ---- the check: a worker gives the answer of core 0 ---- */
int jobs_selftest(void)
{
    enum { W = 24, H = 16 };
    static uint8_t in[7 + W * H], flood_in[5 + W * H];
    in[0] = W; in[1] = H; in[2] = 1; in[3] = 1; in[4] = W - 2; in[5] = H - 2; in[6] = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            in[7 + y * W + x] = (x == 12 && y != 3 && y != 12) || (y == 8 && x > 3 && x < 10) ? 255 : 0;
    flood_in[0] = W; flood_in[1] = H; flood_in[2] = W - 2; flood_in[3] = H - 2; flood_in[4] = 0;
    memcpy(flood_in + 5, in + 7, W * H);
    static uint8_t want_p[JOB_OUT_MAX], want_f[JOB_OUT_MAX];
    scratch_t *sc = &scratch[0];
    const size_t np = k_grid_path(in, sizeof in, want_p, sizeof want_p, sc);
    const size_t nf = k_grid_flood(flood_in, sizeof flood_in, want_f, sizeof want_f, sc);
    const uint32_t before = on_workers;
    int ids[6], bad = np == BAD || nf == BAD || want_p[0] != 1;
    for (int i = 0; i < 6; i++)
        ids[i] = jobs_start(i & 1, i & 1 ? flood_in : in, i & 1 ? sizeof flood_in : sizeof in);
    for (int i = 0; i < 6 && !bad; i++) {
        const uint8_t *o = NULL;
        size_t n = 0;
        int st = JOB_QUEUED;
        for (uint32_t t = 0; t < 2000000 && (st == JOB_QUEUED || st == JOB_RUN); t += 10) {
            st = jobs_poll(ids[i], &o, &n);
            if (st == JOB_QUEUED || st == JOB_RUN)
                timer_delay_us(10);
        }
        const uint8_t *want = i & 1 ? want_f : want_p;
        const size_t wn = i & 1 ? nf : np;
        bad = st != JOB_DONE || n != wn || memcmp(o, want, wn) != 0;
    }
    kprintf("smp: jobs %s (grid_path %u steps, %u jobs on workers)\n", bad ? "FAILED" : "ok", want_p[1] | want_p[2] << 8,
            (unsigned)(on_workers - before));
    if (bad || jobs_workers() == 0)
        return bad ? -1 : 0;
    /* the time: a snake of walls in a 64 x 64 grid (a path of about 2000 cells), one job on core 0 and three at once on the workers */
    static uint8_t snake[7 + 64 * 64];
    snake[0] = 64; snake[1] = 64; snake[2] = 0; snake[3] = 0; snake[4] = 0; snake[5] = 62; snake[6] = 1;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            snake[7 + y * 64 + x] = y % 2 == 1 && !(y % 4 == 1 ? x == 63 : x == 0) ? 255 : 0;
    static uint8_t ans[JOB_OUT_MAX];
    uint32_t t0 = timer_ticks();
    const size_t ns = k_grid_path(snake, sizeof snake, ans, sizeof ans, &scratch[0]);
    const uint32_t t_one = timer_ticks() - t0;
    int sid[3], got = 0;
    t0 = timer_ticks();
    for (int i = 0; i < 3; i++)
        sid[i] = jobs_start(0, snake, sizeof snake);
    for (int i = 0; i < 3; i++) {
        const uint8_t *o;
        size_t n;
        int st = JOB_QUEUED;
        while (st == JOB_QUEUED || st == JOB_RUN)
            st = jobs_poll(sid[i], &o, &n);
        got += st == JOB_DONE && n == ns && !memcmp(o, ans, n);
    }
    const uint32_t t_three = timer_ticks() - t0;
    kprintf("smp: jobs time: grid_path 64x64 snake %u cells: core 0 %u us, three jobs at once on the workers %u us (%d of 3 right)\n",
            ans[1] | ans[2] << 8, (unsigned)t_one, (unsigned)t_three, got);
    return 0;
}
