#include "fiber.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define GUARD 0xF1BE5AFEu
#define PAINT 0x57ACC0DEu      /* unused stack of a job (high-water mark) */

void fiber_switch(uint32_t **save, uint32_t *load);
void fiber_start(void);

static fiber_t *volatile current;     /* read by the tick (fiber_tick) */
volatile uint8_t fiber_over;

static void fiber_main(fiber_t *f)
{
    f->fn(f->arg);
    f->done = 1;
    current = NULL;
    fiber_switch(&f->sp, f->back);      /* never comes back */
}

void fiber_prepare(fiber_t *f, void *stack, size_t size, void (*fn)(void *), void *arg)
{
    memset(f, 0, sizeof *f);
    f->stack = stack;
    f->size = size;
    f->fn = fn;
    f->arg = arg;
    for (size_t i = 0; i < 16 && i < size / 4; i++)
        f->stack[i] = GUARD;
#ifdef __aarch64__
    /* the RGB30 (src/rgb30/fiber.S): x19-x30, d8-d15, fpcr, pad */
    uint64_t *sp = (uint64_t *)((uintptr_t)((uint8_t *)stack + size) & ~(uintptr_t)15) - 22;
    memset(sp, 0, 22 * 8);
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    sp[20] = fpcr;
    sp[0] = (uint64_t)(uintptr_t)f;             /* x19: the argument */
    sp[1] = (uint64_t)(uintptr_t)fiber_main;    /* x20: the function */
    sp[11] = (uint64_t)(uintptr_t)fiber_start;  /* x30 */
    f->sp = (uint32_t *)sp;
#else
    /* the frame fiber_switch pops (fiber.S): fpscr, pad, d8-d15, r4-r12, lr */
    uint32_t *sp = (uint32_t *)((uintptr_t)((uint8_t *)stack + size) & ~7u) - 28;
    memset(sp, 0, 28 * 4);
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    sp[0] = fpscr;
    sp[18] = (uint32_t)(uintptr_t)f;            /* r4: the argument */
    sp[19] = (uint32_t)(uintptr_t)fiber_main;   /* r5: the function */
    sp[27] = (uint32_t)(uintptr_t)fiber_start;  /* lr */
    f->sp = sp;
#endif
}

int fiber_resume(fiber_t *f)
{
    if (f->done || current)
        return !f->done;
    current = f;
    fiber_over = 0;
    fiber_switch(&f->back, f->sp);
    current = NULL;
    for (int i = 0; i < 16; i++)
        if (f->stack[i] != GUARD) {
            /* the stack ran over: memory below it is already damaged */
            kprintf("\x1b[91mfiber: stack overflow (%lu bytes)\x1b[0m\n", (unsigned long)f->size);
            for (;;)
                ;
        }
    return !f->done;
}

void fiber_yield(void)
{
    fiber_t *f = current;
    if (!f)
        return;
    current = NULL;
    fiber_switch(&f->sp, f->back);
    current = f;
}

/* In the 1 kHz tick (IRQ): one timer read, and a store when the running fiber is late. */
void fiber_tick(void)
{
    fiber_t *f = current;
    if (f && (int32_t)(timer_ticks() - f->until) >= 0)
        fiber_over = 1;
}

fiber_t *fiber_current(void)
{
    return current;
}

void fiber_cancel(fiber_t *f)
{
    f->cancel = 1;
}

int fiber_cancelled(void)
{
    return current && current->cancel;
}

int fiber_run(fiber_t *f, uint32_t until)
{
    f->until = until;
    return fiber_resume(f);
}

void fiber_slice(void)
{
    watchdog_progress();        /* a long loop that calls this is alive, in a fiber or not (SYS-2) */
    fiber_t *f = current;
    if (f && (int32_t)(timer_ticks() - f->until) >= 0)
        fiber_yield();
}

uint32_t fiber_budget_us(void)
{
    const fiber_t *f = current;
    if (!f)
        return UINT32_MAX;
    const int32_t left = (int32_t)(f->until - timer_ticks());
    return left > 0 ? (uint32_t)left : 0;
}

int fiber_slice_tick(void)
{
    fiber_slice();
    return 0;
}

/* the jobs started so far, for fiber_jobs_report */
#define JOBS_MAX 12
static fiber_job_t *jobs[JOBS_MAX];
static int njobs;

int fiber_job_start(fiber_job_t *j, size_t stack, void (*fn)(void *), void *arg)
{
    if (!j->stack) {
        if (!(j->stack = malloc(stack)))
            return -1;
        j->size = stack;
        /* painted once: the report finds how far down the stack went (never repainted) */
        for (size_t i = 16; i < stack / 4; i++)
            ((uint32_t *)j->stack)[i] = PAINT;
        if (njobs < JOBS_MAX)
            jobs[njobs++] = j;
    }
    fiber_prepare(&j->f, j->stack, j->size, fn, arg);
    j->busy = 1;
    return 0;
}

/* the time a job ran from t0, and how far over `until` it went */
static void account(fiber_job_t *j, uint32_t t0, uint32_t until)
{
    const uint32_t now = timer_ticks();
    const int32_t over = (int32_t)(now - until);
    if (fiber_over && over > (int32_t)FIBER_STALL_US && over > (int32_t)j->longest)
        kprintf("fiber: %s did not yield: %lu.%lu ms past its slice\n", j->name ? j->name : "job",
                (unsigned long)(over / 1000), (unsigned long)(over / 100 % 10));
    j->took += now - t0;
    if (over > (int32_t)j->worst)
        j->worst = (uint32_t)over;
    if (over > (int32_t)j->longest)
        j->longest = (uint32_t)over;
}

int fiber_job_run(fiber_job_t *j, uint32_t until)
{
    /* a slice of its own ends at `until` (fiber_slice); a wait of the
     * network yields at once: again while the time is not over */
    do {
        if (!j->busy)
            return 0;
        const uint32_t t0 = timer_ticks();
        j->busy = fiber_run(&j->f, until);
        account(j, t0, until);
    } while (j->busy && (int32_t)(until - timer_ticks()) > 0);
    return j->busy;
}

void fiber_job_stop(fiber_job_t *j)
{
    if (!j->busy)
        return;
    const uint32_t t0 = timer_ticks();
    fiber_cancel(&j->f);
    while (fiber_run(&j->f, 0))
        ;
    j->busy = 0;
    /* the menu waited for all of it: a stop, counted apart from the slices
     * (it is not a slice that went over its time: leaving a tab, starting
     * a game) */
    const uint32_t waited = timer_ticks() - t0;
    if (waited > j->stopped)
        j->stopped = waited;
}

void fiber_share_run(const fiber_share_t *t, int n, int ctx, uint32_t until)
{
    const int32_t total = (int32_t)(until - timer_ticks());
    if (ctx < 0 || ctx >= FIBER_CONTEXTS || total <= 0)
        return;
    /* each its part, from when its turn comes */
    for (int i = 0; i < n; i++) {
        if (!t[i].share[ctx])
            continue;
        uint32_t end = timer_ticks() + (uint32_t)total * t[i].share[ctx] / 100;
        if ((int32_t)(end - until) > 0)
            end = until;
        if ((int32_t)(end - timer_ticks()) > 0)
            t[i].tick(end);
    }
    /* what is left, in the table's order */
    for (int i = 0; i < n && (int32_t)(until - timer_ticks()) > 0; i++)
        if (t[i].share[ctx])
            t[i].tick(until);
}

int fiber_jobs_report(char *buf, size_t n)
{
    size_t k = 0;
    int any = 0;
    if (n)
        buf[0] = 0;
    for (int i = 0; i < njobs; i++) {
        fiber_job_t *j = jobs[i];
        if (!j->took && !j->stopped)
            continue;
        if (n > k)
            k += (size_t)ksnprintf(buf + k, n - k, "%s%s %lu ms (+%lu.%lu", any ? ", " : "",
                                   j->name ? j->name : "job", (unsigned long)(j->took / 1000),
                                   (unsigned long)(j->worst / 1000), (unsigned long)(j->worst / 100 % 10));
        if (n > k) {
            /* the stack: from the first word below the top that is not paint */
            const uint32_t *w = (const uint32_t *)j->stack;
            size_t i = 16;
            while (i < j->size / 4 && w[i] == PAINT)
                i++;
            const size_t used = j->size - i * 4;
            if (used > j->hw)
                j->hw = (uint32_t)used;
            k += (size_t)ksnprintf(buf + k, n - k, ", stk %lu.%lu/%luK", (unsigned long)(j->hw / 1024),
                                   (unsigned long)(j->hw * 10 / 1024 % 10), (unsigned long)(j->size / 1024));
        }
        if (n > k && j->stopped)
            k += (size_t)ksnprintf(buf + k, n - k, ", stop %lu.%lu", (unsigned long)(j->stopped / 1000),
                                   (unsigned long)(j->stopped / 100 % 10));
        if (n > k)
            k += (size_t)ksnprintf(buf + k, n - k, ")");
        any = 1;
        j->took = j->worst = j->stopped = 0;
    }
    return any;
}

/* Boot diagnostics (measurement build): a job that spins 8 ms with no yield and uses 2 KiB of
 * stack. The tick must set fiber_over, the report must print the stack, the log must show
 * "did not yield". */
static uint32_t spin_budget_in, spin_budget_out;
static void selftest_spin(void *arg)
{
    (void)arg;
    spin_budget_in = fiber_budget_us();
    volatile uint8_t pad[2048];
    for (size_t i = 0; i < sizeof pad; i += 64)
        pad[i] = (uint8_t)i;
    for (uint32_t t0 = timer_ticks(); timer_ticks() - t0 < 8000;)
        ;
    spin_budget_out = fiber_budget_us();
}

int fiber_selftest(void)
{
    static fiber_job_t job = {.name = "selftest"};
    char rep[96];
    if (fiber_job_start(&job, 8192, selftest_spin, NULL) != 0)
        return 1;
    fiber_job_run(&job, timer_ticks() + 1000);
    const int ran = fiber_jobs_report(rep, sizeof rep);
    const int ok = ran && !job.busy && job.longest >= FIBER_STALL_US && job.hw >= 2048 && job.hw < 4096 &&
                    spin_budget_in > 0 && spin_budget_in <= 1000 && spin_budget_out == 0 &&
                    fiber_budget_us() == UINT32_MAX;
    kprintf("fiber selftest: %s\x1b[0m: %s\n", ok ? "\x1b[92mok" : "\x1b[91mFAILED", ran ? rep : "no report");
    return !ok;
}
