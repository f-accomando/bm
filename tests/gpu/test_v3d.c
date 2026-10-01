/*
 * The V3D driver's job runner (src/gpu/v3d.c) against a model of the
 * V3D's registers (M30). v3d_run must wait for the frame counts; the
 * binner's out-of-memory flag, on from reset as the Pi showed, is no
 * error; a list that never ends, or ends with an error, is one.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "drivers/mmio.h"           /* the model: tests/gpu/mock */
#include "gpu/v3d.h"

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

#define V3D_BASE (PERIPHERAL_BASE + 0xC00000u)
enum { IDENT0 = 0x000, CT0CS = 0x100, CT1CS = 0x104, CT0EA = 0x108, CT1EA = 0x10C, PCS = 0x130,
       BFC = 0x134, RFC = 0x138, BPOA = 0x308, BPOS = 0x30C };

static uint32_t regs[0x1000 / 4];
static int bin_delay, rnd_delay;    /* reads of BFC / RFC until the job ends; 0: never */
static int bin_left, rnd_left;
static uint32_t rnd_error;          /* CT1CS when the rendering list fails */
static uint32_t now;

uint32_t mock_read(uint32_t addr)
{
    uint32_t off = addr - V3D_BASE;
    if (off >= sizeof regs)
        return 0;
    if (off == BFC && bin_left > 0 && --bin_left == 0)
        regs[BFC / 4] = 1;
    if (off == RFC && rnd_left > 0 && --rnd_left == 0) {
        if (rnd_error)
            regs[CT1CS / 4] = rnd_error;
        else
            regs[RFC / 4] = 1;
    }
    return regs[off / 4];
}

void mock_write(uint32_t addr, uint32_t v)
{
    uint32_t off = addr - V3D_BASE;
    if (off >= sizeof regs)
        return;
    if ((off == BFC || off == RFC) && (v & 1)) {
        regs[off / 4] = 0;              /* write 1: the count back to 0 */
        return;
    }
    if (off == CT0CS || off == CT1CS) {
        if (v & 1u << 15)
            regs[off / 4] = 0;          /* reset */
        return;
    }
    regs[off / 4] = v;
    if (off == CT0EA)                   /* the end address starts a thread */
        bin_left = bin_delay;
    if (off == CT1EA)
        rnd_left = rnd_delay;
}

/* the kernel's services v3d.c uses */
int prop_query(uint32_t tag, uint32_t *vals, unsigned n) { (void)tag; (void)vals; (void)n; return 0; }
uint32_t timer_ticks(void) { return now += 50; }
void timer_delay_ms(uint32_t ms) { now += ms * 1000; }
void dcache_clean_invalidate_all(void) {}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static int job(int bin, int bdelay, int rdelay, uint32_t *took)
{
    bin_delay = bdelay;
    rnd_delay = rdelay;
    uint32_t t0 = now;
    int r = v3d_run(bin ? 0x40100000u : 0, bin ? 0x40100100u : 0, 0x40200000u, 0x40200100u, 200000, NULL, NULL);
    if (took)
        *took = now - t0;
    return r;
}

int main(void)
{
    regs[IDENT0 / 4] = 0x02443356u;
    CHECK(v3d_init() == 0, "init: %s", v3d_status());

    /* as on the Pi: binner out of memory and rendering mode active */
    regs[PCS / 4] = 0x104;
    CHECK(job(0, 0, 40, NULL) == 0, "a clear (no binning) with PCS 0x104 failed");
    CHECK(regs[RFC / 4] == 0, "RFC not given back");

    v3d_set_overflow(0x40300000u, 1u << 20);
    CHECK(job(1, 30, 30, NULL) == 0, "a job with binning failed");
    CHECK(regs[BPOA / 4] == 0x40300000u && regs[BPOS / 4] == 1u << 20, "overflow memory not given: %08x %08x",
          regs[BPOA / 4], regs[BPOS / 4]);

    uint32_t took;
    CHECK(job(0, 0, 0, &took) == -1, "a list that never ends was a success");
    CHECK(took >= 200000, "gave up after %u us", took);
    CHECK(job(1, 0, 10, NULL) == -1, "a binning list that never ends was a success");

    rnd_error = 1u << 3;
    CHECK(job(0, 0, 5, &took) == -1, "a list with an error was a success");
    CHECK(took < 200000, "an error waited for the timeout");
    rnd_error = 0;
    regs[CT1CS / 4] = 0;
    CHECK(job(0, 0, 3, NULL) == 0, "the next job after an error failed");

    printf("v3d: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
