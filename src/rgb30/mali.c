/*
 * M41: the RGB30's Mali-G52, first step (see mali.h). The registers and
 * the order of the steps are those of the GPU's public documentation as
 * Linux's panfrost driver and Rockchip's power domains use them; the job
 * descriptors those Mesa's panfrost writes (a header of 32 bytes, then the
 * job's own words).
 */
#include "mali.h"

#include <string.h>

#include "lib/printf.h"

/* --- registers --- */

#define GPU_ID              0x000
#define GPU_L2_FEATURES     0x004
#define GPU_CORE_FEATURES   0x008
#define GPU_MMU_FEATURES    0x014
#define GPU_AS_PRESENT      0x018
#define GPU_JS_PRESENT      0x01c
#define GPU_INT_RAWSTAT     0x020
#define GPU_INT_CLEAR       0x024
#define GPU_INT_MASK        0x028
#define GPU_CMD             0x030
#define GPU_FAULT_STATUS    0x03c
#define GPU_THREAD_MAX      0x0a0
#define GPU_SHADER_PRESENT  0x100   /* _LO; _HI at + 4 */
#define GPU_TILER_PRESENT   0x110
#define GPU_L2_PRESENT      0x120
#define GPU_SHADER_READY    0x140
#define GPU_TILER_READY     0x150
#define GPU_L2_READY        0x160
#define GPU_SHADER_PWRON    0x180
#define GPU_TILER_PWRON     0x190
#define GPU_L2_PWRON        0x1a0
#define GPU_JM_CONFIG       0xf00

#define IRQ_RESET_COMPLETED (1u << 8)
#define CMD_SOFT_RESET      0x01
#define CMD_HARD_RESET      0x02

#define JOB_INT_RAWSTAT     0x1000
#define JOB_INT_CLEAR       0x1004
#define JOB_INT_MASK        0x1008
#define JS(n, r)            (0x1800 + (n) * 0x80 + (r))
#define JS_STATUS           0x24
#define JS_HEAD_NEXT        0x40    /* _LO; _HI at + 4 */
#define JS_AFFINITY_NEXT    0x50
#define JS_CONFIG_NEXT      0x58
#define JS_COMMAND_NEXT     0x60
#define JS_CFG_THREAD_PRI(n)        ((uint32_t)(n) << 16)
#define JS_CFG_START_FLUSH_CLEAN_INV (3u << 8)
#define JS_CFG_END_FLUSH_CLEAN_INV  (3u << 12)
#define JS_CMD_START        0x01

#define MMU_INT_RAWSTAT     0x2000
#define MMU_INT_CLEAR       0x2004
#define MMU_INT_MASK        0x2008
#define AS(n, r)            (0x2400 + (n) * 0x40 + (r))
#define AS_TRANSTAB         0x00    /* _LO; _HI at + 4 */
#define AS_MEMATTR          0x08
#define AS_LOCKADDR         0x10
#define AS_COMMAND          0x18
#define AS_FAULTSTATUS      0x1c
#define AS_FAULTADDRESS     0x20
#define AS_STATUS           0x28
#define AS_TRANSCFG         0x30
#define AS_CMD_UPDATE       0x01
#define AS_CMD_LOCK         0x02
#define AS_CMD_FLUSH_MEM    0x05
#define AS_ACTIVE           0x01

/* the Mali LPAE page tables: 4 levels of 512 entries (4 KiB granule) */
#define PT_TABLE            3ull            /* a table, levels 0-2 */
#define PT_BLOCK            1ull            /* a block or page (every level) */
#define PT_READ             (1ull << 6)     /* stage-2 style permissions */
#define PT_WRITE            (2ull << 6)
#define PT_SH_OUTER         (2ull << 8)     /* "outside the GPU" */
#define TRANSTAB_TABLE      3u              /* ADRMODE_TABLE */
#define TRANSTAB_READ_INNER (1u << 2)
#define MEMATTR_IMP_DEF     0x88u           /* index 0: the implementation's caching */
#define MEMATTR_WRITE_ALLOC 0x8du           /* index 1 */

/* the RK3566's power domain PD_GPU (PMU) and its clocks (CRU) */
#define PMU_REQ             0x50            /* bus idle request (hiword) */
#define PMU_ACK             0x60
#define PMU_IDLE            0x68
#define PMU_STATUS          0x98            /* 1: off */
#define PMU_PWR             0xa0            /* 1: off (hiword) */
#define PD_GPU_PWR          (1u << 0)
#define PD_GPU_REQ          (1u << 1)
#define CRU_CLKSEL_CON6     0x118           /* clk_gpu_src: parent 7:6, divider 3:0; pre mux 11 */
#define CRU_CLKGATE_CON2    0x308           /* 1: gated (hiword): 0 clk_gpu_src, 3 clk_gpu */
#define GATES_GPU           ((1u << 0) | (1u << 3))
#define HIWORD(mask, v)     (((uint32_t)(mask) << 16) | ((uint32_t)(v) & (mask)))

/* the jobs: a header of 32 bytes (64-bit pointers), then the payload */
enum { JOB_WRITE_VALUE = 2, JOB_CACHE_FLUSH = 3 };
#define WRITE_IMMEDIATE_32  6

/* the GPU's memory: three tables, then the jobs */
#define MEM_L0              0x0000
#define MEM_L1              0x1000
#define MEM_L2              0x2000
#define MEM_JOBS            0x3000
#define MEM_RESULT          0x3400

static const mali_hw_t *H;
static char line[160];

static uint32_t gr(uint32_t r)              { return H->rd(MALI_GPU_BASE + r); }
static void gw(uint32_t r, uint32_t v)      { H->wr(MALI_GPU_BASE + r, v); }

static void say(void)
{
    if (H->log)
        H->log(line);
}

/* waits until (reg & mask) == want; 0, or -1 after us microseconds */
static int poll(uintptr_t reg, uint32_t mask, uint32_t want, uint32_t us)
{
    const uint32_t t0 = H->us();
    for (;;) {
        if ((H->rd(reg) & mask) == want)
            return 0;
        if (H->us() - t0 > us)
            return -1;
    }
}

static int gpoll(uint32_t r, uint32_t mask, uint32_t want, uint32_t us)
{
    return poll(MALI_GPU_BASE + r, mask, want, us);
}

const char *mali_exception(uint32_t code)
{
    static const struct { uint8_t code; const char *name; } names[] = {
        { 0x00, "OK" }, { 0x01, "DONE" }, { 0x02, "INTERRUPTED" }, { 0x03, "STOPPED" }, { 0x04, "TERMINATED" },
        { 0x08, "ACTIVE" }, { 0x40, "JOB_CONFIG_FAULT" }, { 0x41, "JOB_POWER_FAULT" }, { 0x42, "JOB_READ_FAULT" },
        { 0x43, "JOB_WRITE_FAULT" }, { 0x44, "JOB_AFFINITY_FAULT" }, { 0x48, "JOB_BUS_FAULT" },
        { 0x50, "INSTR_INVALID_PC" }, { 0x51, "INSTR_INVALID_ENC" }, { 0x58, "DATA_INVALID_FAULT" },
        { 0x59, "TILE_RANGE_FAULT" }, { 0x5a, "ADDR_RANGE_FAULT" }, { 0x60, "OOM" }, { 0x7f, "UNKNOWN" },
        { 0x80, "DELAYED_BUS_FAULT" }, { 0x88, "GPU_SHAREABILITY_FAULT" }, { 0x89, "SYS_SHAREABILITY_FAULT" },
        { 0x8a, "GPU_CACHEABILITY_FAULT" },
    };
    code &= 0xff;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (names[i].code == code)
            return names[i].name;
    if (code >= 0xc0 && code <= 0xc7) return "TRANSLATION_FAULT";
    if (code >= 0xc8 && code <= 0xcb) return "PERMISSION_FAULT";
    if (code >= 0xd0 && code <= 0xd3) return "TRANSTAB_BUS_FAULT";
    if (code >= 0xd8 && code <= 0xdb) return "ACCESS_FLAG";
    if (code >= 0xe0 && code <= 0xe7) return "ADDRESS_SIZE_FAULT";
    if (code >= 0xe8 && code <= 0xef) return "MEMORY_ATTRIBUTE_FAULT";
    return "?";
}

const char *mali_name(uint32_t gpu_id, char *buf, int n)
{
    const uint32_t prod = gpu_id >> 16, arch = prod & 0xf00fu;
    static const struct { uint16_t id; const char *name; } models[] = {
        { 0x6000, "G71" }, { 0x6001, "G72" }, { 0x7000, "G51" }, { 0x7001, "G76" }, { 0x7002, "G52" },
        { 0x7003, "G31" }, { 0x9001, "G57" },
    };
    const char *name = NULL;
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++)
        if (models[i].id == arch)
            name = models[i].name;
    if (name)
        ksnprintf(buf, (size_t)n, "Mali-%s r%up%u", name, (unsigned)(gpu_id >> 12 & 15), (unsigned)(gpu_id >> 4 & 255));
    else
        ksnprintf(buf, (size_t)n, "unknown GPU %04x", (unsigned)prod);
    return buf;
}

static int bits(uint64_t v)
{
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

/* --- the steps --- */

static int power(void)
{
    const uintptr_t cru = MALI_CRU_BASE, pmu = MALI_PMU_BASE;
    const uint32_t gate0 = H->rd(cru + CRU_CLKGATE_CON2), sel = H->rd(cru + CRU_CLKSEL_CON6);
    H->wr(cru + CRU_CLKGATE_CON2, HIWORD(GATES_GPU, 0));
    const int was_off = (H->rd(pmu + PMU_STATUS) & PD_GPU_PWR) != 0;
    if (was_off) {
        H->wr(pmu + PMU_PWR, HIWORD(PD_GPU_PWR, 0));
        if (poll(pmu + PMU_STATUS, PD_GPU_PWR, 0, 10000)) {
            ksnprintf(line, sizeof line, "mali: power: the power domain PD_GPU did not turn on (PMU status %08x)",
                      (unsigned)H->rd(pmu + PMU_STATUS));
            say();
            return -1;
        }
    }
    if ((H->rd(pmu + PMU_IDLE) | H->rd(pmu + PMU_ACK)) & PD_GPU_REQ) {
        H->wr(pmu + PMU_REQ, HIWORD(PD_GPU_REQ, 0));    /* the GPU's bus out of idle */
        if (poll(pmu + PMU_ACK, PD_GPU_REQ, 0, 10000) || poll(pmu + PMU_IDLE, PD_GPU_REQ, 0, 10000)) {
            ksnprintf(line, sizeof line, "mali: power: the GPU's bus stays idle (ack %08x, idle %08x)",
                      (unsigned)H->rd(pmu + PMU_ACK), (unsigned)H->rd(pmu + PMU_IDLE));
            say();
            return -1;
        }
    }
    ksnprintf(line, sizeof line, "mali: power: PD_GPU %s, clocks on (gates %04x -> %04x, CLKSEL_CON6 %04x: "
              "parent %u, divider %u, %s)", was_off ? "turned on" : "was on", (unsigned)(gate0 & 0xffff),
              (unsigned)(H->rd(cru + CRU_CLKGATE_CON2) & 0xffff), (unsigned)(sel & 0xffff), (unsigned)(sel >> 6 & 3),
              (unsigned)(sel & 15) + 1, sel >> 11 & 1 ? "PVTPLL" : "clk_gpu_src");
    say();
    return 0;
}

static int identify(mali_info_t *in)
{
    in->gpu_id = gr(GPU_ID);
    in->l2_features = gr(GPU_L2_FEATURES);
    in->core_features = gr(GPU_CORE_FEATURES);
    in->mmu_features = gr(GPU_MMU_FEATURES);
    in->as_present = gr(GPU_AS_PRESENT);
    in->js_present = gr(GPU_JS_PRESENT);
    in->thread_max = gr(GPU_THREAD_MAX);
    in->shader_present = gr(GPU_SHADER_PRESENT) | (uint64_t)gr(GPU_SHADER_PRESENT + 4) << 32;
    in->tiler_present = gr(GPU_TILER_PRESENT) | (uint64_t)gr(GPU_TILER_PRESENT + 4) << 32;
    in->l2_present = gr(GPU_L2_PRESENT) | (uint64_t)gr(GPU_L2_PRESENT + 4) << 32;
    char name[40];
    ksnprintf(line, sizeof line, "mali: GPU_ID %08x: %s, %d shader core%s (%08x), L2 %08x, tiler %08x, "
              "address spaces %02x, job slots %02x", (unsigned)in->gpu_id, mali_name(in->gpu_id, name, sizeof name),
              bits(in->shader_present), bits(in->shader_present) == 1 ? "" : "s", (unsigned)in->shader_present, (unsigned)in->l2_present,
              (unsigned)in->tiler_present, (unsigned)in->as_present, (unsigned)in->js_present);
    say();
    ksnprintf(line, sizeof line, "mali: MMU %u-bit addresses, %u-bit physical; %u threads a core",
              (unsigned)(in->mmu_features & 255), (unsigned)(in->mmu_features >> 8 & 255), (unsigned)in->thread_max);
    say();
    if (in->gpu_id == 0 || in->gpu_id == 0xffffffffu || !in->shader_present || !in->l2_present ||
        !(in->as_present & 1) || !(in->js_present & 2)) {
        ksnprintf(line, sizeof line, "mali: not a Mali this driver can use (no cores, no AS 0 or no job slot 1)");
        say();
        return -1;
    }
    return 0;
}

static int reset(void)
{
    gw(GPU_INT_MASK, 0);
    gw(GPU_INT_CLEAR, IRQ_RESET_COMPLETED);
    gw(GPU_CMD, CMD_SOFT_RESET);
    const char *how = "soft";
    if (gpoll(GPU_INT_RAWSTAT, IRQ_RESET_COMPLETED, IRQ_RESET_COMPLETED, 10000)) {
        how = "hard (the soft one did not end)";
        gw(GPU_CMD, CMD_HARD_RESET);
        if (gpoll(GPU_INT_RAWSTAT, IRQ_RESET_COMPLETED, IRQ_RESET_COMPLETED, 10000)) {
            ksnprintf(line, sizeof line, "mali: reset: neither soft nor hard reset ended (raw %08x)",
                      (unsigned)gr(GPU_INT_RAWSTAT));
            say();
            return -1;
        }
    }
    gw(GPU_INT_CLEAR, 0xffffffffu);
    gw(JOB_INT_MASK, 0);                /* polled: no interrupts */
    gw(MMU_INT_MASK, 0);
    gw(GPU_JM_CONFIG, 0xfu << 16);      /* the IDVS group size (G52, as panfrost) */
    ksnprintf(line, sizeof line, "mali: reset: %s", how);
    say();
    return 0;
}

static int cores(const mali_info_t *in)
{
    /* one core group (the RK3566 has one L2) */
    const uint32_t l2 = (uint32_t)in->l2_present, sh = (uint32_t)in->shader_present, ti = (uint32_t)in->tiler_present;
    gw(GPU_L2_PWRON, l2);
    gw(GPU_L2_PWRON + 4, 0);
    int bad = gpoll(GPU_L2_READY, l2, l2, 20000) ? 1 : 0;
    if (!bad) {
        gw(GPU_SHADER_PWRON, sh);
        gw(GPU_SHADER_PWRON + 4, 0);
        bad = gpoll(GPU_SHADER_READY, sh, sh, 20000) ? 2 : 0;
    }
    if (!bad) {
        gw(GPU_TILER_PWRON, ti);
        gw(GPU_TILER_PWRON + 4, 0);
        bad = gpoll(GPU_TILER_READY, ti, ti, 20000) ? 3 : 0;
    }
    static const char *const what[4] = { "", "the L2", "the shader cores", "the tiler" };
    if (bad)
        ksnprintf(line, sizeof line, "mali: cores: %s did not power on (ready: L2 %08x, cores %08x, tiler %08x)",
                  what[bad], (unsigned)gr(GPU_L2_READY), (unsigned)gr(GPU_SHADER_READY), (unsigned)gr(GPU_TILER_READY));
    else
        ksnprintf(line, sizeof line, "mali: cores: L2, %d shader core%s and the tiler powered on", bits(sh),
                  bits(sh) == 1 ? "" : "s");
    say();
    return bad ? -1 : 0;
}

static void put64(uint8_t *p, uint64_t v)
{
    memcpy(p, &v, 8);
}

static int as_wait(void)
{
    return gpoll(AS(0, AS_STATUS), AS_ACTIVE, 0, 100000);
}

static int as_cmd(uint32_t cmd)
{
    if (as_wait())
        return -1;
    gw(AS(0, AS_COMMAND), cmd);
    return as_wait();
}

static int mmu(void)
{
    /* L0[0] -> L1, L1[map / 1 GiB] -> L2 (the map within one GiB), L2: 2 MiB blocks */
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    memset(m + MEM_L0, 0, 3 * 4096);
    const uint64_t first = H->map_pa >> 21, n = H->map_size >> 21;
    if ((H->map_pa >> 30) != ((H->map_pa + H->map_size - 1) >> 30) || (H->map_pa >> 39) || (H->mem_pa & 0x1fffff) ||
        H->mem_pa < H->map_pa || H->mem_pa + H->mem_size > H->map_pa + H->map_size) {
        ksnprintf(line, sizeof line, "mali: mmu: the GPU's memory must be inside one GiB, 2 MiB aligned");
        say();
        return -1;
    }
    put64(m + MEM_L0, (pa + MEM_L1) | PT_TABLE);
    put64(m + MEM_L1 + 8 * (H->map_pa >> 30 & 511), (pa + MEM_L2) | PT_TABLE);
    for (uint64_t i = 0; i < n; i++)
        put64(m + MEM_L2 + 8 * ((first + i) & 511), (first + i) << 21 | PT_READ | PT_WRITE | PT_SH_OUTER | PT_BLOCK);
    /* everything the MMU had flushed (a lock of the whole space, then
     * FLUSH_MEM), the tables, the attributes, legacy mode, UPDATE */
    gw(AS(0, AS_LOCKADDR), 63);
    gw(AS(0, AS_LOCKADDR) + 4, 0);
    int bad = as_cmd(AS_CMD_LOCK) || as_cmd(AS_CMD_FLUSH_MEM);
    const uint64_t transtab = (pa + MEM_L0) | TRANSTAB_READ_INNER | TRANSTAB_TABLE;
    gw(AS(0, AS_TRANSTAB), (uint32_t)transtab);
    gw(AS(0, AS_TRANSTAB) + 4, (uint32_t)(transtab >> 32));
    gw(AS(0, AS_MEMATTR), MEMATTR_IMP_DEF | MEMATTR_WRITE_ALLOC << 8 | MEMATTR_IMP_DEF << 16);
    gw(AS(0, AS_MEMATTR) + 4, 0);
    gw(AS(0, AS_TRANSCFG), 0);
    gw(AS(0, AS_TRANSCFG) + 4, 0);
    bad = bad || as_cmd(AS_CMD_UPDATE);
    gw(MMU_INT_CLEAR, 0xffffffffu);
    if (bad)
        ksnprintf(line, sizeof line, "mali: mmu: address space 0 stays busy (status %08x)", (unsigned)gr(AS(0, AS_STATUS)));
    else
        ksnprintf(line, sizeof line, "mali: mmu: address space 0 on, %u MiB at %08x 1:1 (Mali LPAE, tables at %08x)",
                  (unsigned)(H->map_size >> 20), (unsigned)H->map_pa, (unsigned)pa);
    say();
    return bad ? -1 : 0;
}

/* a job header at p: its type, index, the first dependency, the next job */
static void job_header(uint8_t *p, int type, int index, int dep, uint64_t next)
{
    memset(p, 0, 32);
    const uint32_t w4 = 1u | (uint32_t)type << 1 | (uint32_t)index << 16;     /* 64-bit descriptors */
    memcpy(p + 16, &w4, 4);
    const uint32_t w5 = (uint32_t)dep;
    memcpy(p + 20, &w5, 4);
    put64(p + 24, next);
}

/* a WRITE_VALUE job at p: a 32-bit value to the address */
static void job_write(uint8_t *p, int index, int dep, uint64_t next, uint64_t addr, uint32_t value)
{
    job_header(p, JOB_WRITE_VALUE, index, dep, next);
    memset(p + 32, 0, 32);
    put64(p + 32, addr);
    const uint32_t type = WRITE_IMMEDIATE_32;
    memcpy(p + 40, &type, 4);
    put64(p + 48, value);
}

/* the chain from head on job slot js (0 fragment, 1 the others); 0 done,
 * -1 failed (lines said) */
static int run_on(int js, uint64_t head, uint32_t *us, uint32_t *status, const char *what)
{
    gw(JOB_INT_CLEAR, 0xffffffffu);
    gw(MMU_INT_CLEAR, 0xffffffffu);
    gw(JS(js, JS_HEAD_NEXT), (uint32_t)head);
    gw(JS(js, JS_HEAD_NEXT) + 4, (uint32_t)(head >> 32));
    gw(JS(js, JS_AFFINITY_NEXT), gr(GPU_SHADER_PRESENT));
    gw(JS(js, JS_AFFINITY_NEXT) + 4, gr(GPU_SHADER_PRESENT + 4));
    gw(JS(js, JS_CONFIG_NEXT), 0 /* AS 0 */ | JS_CFG_THREAD_PRI(8) | JS_CFG_START_FLUSH_CLEAN_INV |
                               JS_CFG_END_FLUSH_CLEAN_INV);
    const uint32_t t0 = H->us();
    gw(JS(js, JS_COMMAND_NEXT), JS_CMD_START);
    const uint32_t done = 1u << js, failed = 1u << (16 + js);
    uint32_t raw = 0;
    while (!((raw = gr(JOB_INT_RAWSTAT)) & (done | failed)) && H->us() - t0 < 100000)
        ;
    *us = H->us() - t0;
    *status = gr(JS(js, JS_STATUS));
    const uint32_t mmu_raw = gr(MMU_INT_RAWSTAT);
    gw(JOB_INT_CLEAR, done | failed);
    if (raw & done && !(raw & failed))
        return 0;
    if (!(raw & (done | failed)))
        ksnprintf(line, sizeof line, "mali: %s: no end after 100 ms (JS_STATUS %02x %s, job raw %08x)", what,
                  (unsigned)*status, mali_exception(*status), (unsigned)raw);
    else
        ksnprintf(line, sizeof line, "mali: %s: failed, JS_STATUS %02x %s", what, (unsigned)*status,
                  mali_exception(*status));
    say();
    if (mmu_raw & 0x10001u) {
        const uint32_t fs = gr(AS(0, AS_FAULTSTATUS));
        ksnprintf(line, sizeof line, "mali: %s: MMU fault %08x (%s) at %08x%08x", what, (unsigned)fs,
                  mali_exception(fs), (unsigned)gr(AS(0, AS_FAULTADDRESS) + 4), (unsigned)gr(AS(0, AS_FAULTADDRESS)));
        say();
    }
    return -1;
}

static int run(uint64_t head, uint32_t *us, uint32_t *status, const char *what)
{
    return run_on(1, head, us, status, what);
}

static int jobs(mali_info_t *in)
{
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    volatile uint32_t *res = (volatile uint32_t *)(void *)(m + MEM_RESULT);
    res[0] = res[1] = res[2] = 0;
    /* one job */
    job_write(m + MEM_JOBS, 1, 0, 0, pa + MEM_RESULT, 0xb3d60001u);
    uint32_t st;
    if (run(pa + MEM_JOBS, &in->job_us, &st, "a WRITE_VALUE job") != 0)
        return MALI_E_JOB;
    in->js_status = st;
    memcpy(&in->job_header, m + MEM_JOBS, 4);
    if (res[0] != 0xb3d60001u) {
        ksnprintf(line, sizeof line, "mali: a WRITE_VALUE job: it ended (JS_STATUS %02x), but the word is %08x",
                  (unsigned)st, (unsigned)res[0]);
        say();
        return MALI_E_MEMORY;
    }
    ksnprintf(line, sizeof line, "mali: a WRITE_VALUE job: done in %u us, the word written (JS_STATUS %02x %s, "
              "header %02x %s)", (unsigned)in->job_us, (unsigned)st, mali_exception(st), (unsigned)in->job_header,
              mali_exception(in->job_header));
    say();
    /* a chain of two: the second waits for the first */
    job_write(m + MEM_JOBS + 0x80, 1, 0, pa + MEM_JOBS + 0x100, pa + MEM_RESULT + 4, 0xb3d60002u);
    job_write(m + MEM_JOBS + 0x100, 2, 1, 0, pa + MEM_RESULT + 8, 0xb3d60003u);
    if (run(pa + MEM_JOBS + 0x80, &in->chain_us, &st, "a chain of two jobs") != 0)
        return MALI_E_CHAIN;
    if (res[1] != 0xb3d60002u || res[2] != 0xb3d60003u) {
        ksnprintf(line, sizeof line, "mali: a chain of two jobs: it ended, but the words are %08x %08x",
                  (unsigned)res[1], (unsigned)res[2]);
        say();
        return MALI_E_CHAIN;
    }
    ksnprintf(line, sizeof line, "mali: a chain of two jobs: done in %u us", (unsigned)in->chain_us);
    say();
    return MALI_OK;
}

/* vdd_gpu (the RK817's DCDC2) on: the GPU's registers hang the bus without it */
static int supply(mali_info_t *in)
{
    in->supply_mv = H->supply_mv ? H->supply_mv() : -1;
    if (in->supply_mv == 0) {
        ksnprintf(line, sizeof line, "mali: supply: vdd_gpu (RK817 DCDC2) is off: the GPU is not touched");
        say();
        return -1;
    }
    if (in->supply_mv > 0)
        ksnprintf(line, sizeof line, "mali: supply: vdd_gpu %d mV", in->supply_mv);
    else
        ksnprintf(line, sizeof line, "mali: supply: vdd_gpu not read (no answer from the PMIC); going on");
    say();
    return 0;
}

/* --- a fragment job with no draw (bm3d 6.1) ---
 * The framebuffer descriptor of Bifrost (v7): its parameters (64 bytes,
 * then 64 of padding), then a render target of 64 bytes; no ZS/CRC
 * extension, no tiler (no primitive: every tile is only cleared), no frame
 * shader. The render target: the tile buffer R8G8B8A8, written back
 * linear as R8G8B8A8 with the channels turned to the screen's XRGB8888
 * (B G R in bytes 0-2, 0 in byte 3), clean tiles written (a cleared tile
 * is "clean"). Tiles of 16 x 16 pixels. */
enum { JOB_FRAGMENT = 9 };
#define FBD_SIZE        128
#define RT_SIZE         64
#define TILE_SHIFT      4
#define TIE_MINUS_180_IN_0_OUT 2
#define ZFMT_D24        1
#define CBUF_R8G8B8A8   1
#define COLOR_R8G8B8A8  19
#define BLOCK_LINEAR    2
#define SWZ(r, g, b, a) ((uint32_t)(r) | (uint32_t)(g) << 3 | (uint32_t)(b) << 6 | (uint32_t)(a) << 9)

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

/* the sample positions of one sample (1/256 of a pixel, the middle) */
static void samples(uint8_t *p)
{
    memset(p, 0, 256);
    put32(p, 128u | 128u << 16);
    put32(p + 32 * 4, 128u | 128u << 16);   /* the origin */
}

/* a framebuffer descriptor and its render target at f: w x h pixels at
 * base (row stride, the colour 0xRRGGBB) */
static void fbd(uint8_t *f, uint64_t samples_pa, uint32_t w, uint32_t h, uint64_t base, uint32_t stride, uint32_t rgb)
{
    memset(f, 0, FBD_SIZE + RT_SIZE);
    put64(f + 16, samples_pa);                              /* sample locations */
    put32(f + 32, (w - 1) | (h - 1) << 16);                 /* width, height */
    put32(f + 40, (w - 1) | (h - 1) << 16);                 /* bound max (min 0, 0) */
    put32(f + 44, 0u /* 1 sample */ | TIE_MINUS_180_IN_0_OUT << 6 | 8u << 9 /* 16 x 16 */ |
                      0u << 19 /* 1 target */ | 1u << 24 /* 1 KiB of colour a tile */);
    put32(f + 48, ZFMT_D24 << 16);
    uint8_t *rt = f + FBD_SIZE;
    put32(rt, CBUF_R8G8B8A8 << 26);
    put32(rt + 4, 1u /* write */ | COLOR_R8G8B8A8 << 3 | BLOCK_LINEAR << 8 | 1u << 15 /* dither */ |
                  SWZ(2, 1, 0, 4) << 16 | 1u << 31 /* clean pixels written */);
    put64(rt + 32, base);
    put32(rt + 40, stride);
    put32(rt + 44, stride * h);
    const uint32_t c = (rgb >> 16 & 255) | (rgb & 0xff00) | (rgb & 255) << 16 | 0xffu << 24;   /* R G B A */
    for (int i = 0; i < 4; i++)
        put32(rt + 48 + 4 * i, c);
}

/* a fragment job at j over w x h pixels with the descriptor at fbd_pa */
static void job_fragment(uint8_t *j, uint32_t w, uint32_t h, uint64_t fbd_pa)
{
    job_header(j, JOB_FRAGMENT, 1, 0, 0);
    memset(j + 32, 0, 32);
    put32(j + 32, 0);
    put32(j + 36, ((w - 1) >> TILE_SHIFT) | ((h - 1) >> TILE_SHIFT) << 16);
    put64(j + 40, fbd_pa | 1u);                             /* multi-target, 1 target, no extension */
}

#define MEM_FJOB        0x3800
#define MEM_FBD         0x3900
#define MEM_SAMPLES     0x3a00
#define MEM_SURF        0x10000
#define SURF            64                                  /* 64 x 64 pixels */

/* a 64 x 64 square of rgb cleared by the GPU at the top right of the
 * screen: 0, 1 if the job ended but the pixels are not there, -1 */
static int square(uint32_t rgb)
{
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    const uint32_t x0 = (H->screen_w - 80) & ~15u, y0 = 16;
    const uint64_t base = H->screen_pa + (uint64_t)y0 * H->screen_pitch + x0 * 4;
    fbd(m + MEM_FBD, pa + MEM_SAMPLES, SURF, SURF, base, H->screen_pitch, rgb);
    job_fragment(m + MEM_FJOB, SURF, SURF, pa + MEM_FBD);
    uint32_t us, st;
    if (run_on(0, pa + MEM_FJOB, &us, &st, "a square on the screen") != 0)
        return -1;
    uint32_t px;
    memcpy(&px, H->screen + (size_t)(y0 + 32) * H->screen_pitch + (x0 + 32) * 4, 4);
    return (px & 0xffffff) == (rgb & 0xffffff) ? 0 : 1;
}

static int fragment(mali_info_t *in)
{
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    volatile uint32_t *s = (volatile uint32_t *)(void *)(m + MEM_SURF);
    for (int i = 0; i < SURF * SURF + 64; i++)
        s[i] = 0xdeadbeefu;
    samples(m + MEM_SAMPLES);
    const uint32_t rgb = 0x20c060;
    fbd(m + MEM_FBD, pa + MEM_SAMPLES, SURF, SURF, pa + MEM_SURF, SURF * 4, rgb);
    job_fragment(m + MEM_FJOB, SURF, SURF, pa + MEM_FBD);
    uint32_t st;
    if (run_on(0, pa + MEM_FJOB, &in->frag_us, &st, "a fragment job (a clear)") != 0)
        return MALI_E_FRAGMENT;
    int wrong = 0, past = 0;
    for (int i = 0; i < SURF * SURF; i++)
        wrong += (s[i] & 0xffffff) != rgb;
    for (int i = SURF * SURF; i < SURF * SURF + 64; i++)
        past += s[i] != 0xdeadbeefu;
    in->frag_pixel = s[SURF * 17 + 5];
    if (wrong || past) {
        ksnprintf(line, sizeof line, "mali: a fragment job (a clear): it ended, but %d of %d pixels are not %06x "
                  "(one is %08x) and %d words past the surface changed", wrong, SURF * SURF, (unsigned)rgb,
                  (unsigned)in->frag_pixel, past);
        say();
        return MALI_E_FRAGMENT;
    }
    ksnprintf(line, sizeof line, "mali: a fragment job (a clear): done in %u us, %dx%d pixels of %06x (%08x)",
              (unsigned)in->frag_us, SURF, SURF, (unsigned)rgb, (unsigned)in->frag_pixel);
    say();
    /* the same on the screen: a square at the top right */
    if (H->screen_pa && H->screen && H->screen_w >= 128 && H->screen_h >= 96) {
        const int r = square(0x40d060);
        ksnprintf(line, sizeof line, "mali: a square on the screen (top right, green): %s",
                  r == 0 ? "drawn by the GPU" : r > 0 ? "the job ended, the pixels are not there" : "failed");
        say();
        if (r != 0)
            return MALI_E_FRAGMENT;
    }
    return MALI_OK;
}

static int probed_ok;

int mali_square(const mali_hw_t *hw, uint32_t rgb)
{
    if (!probed_ok || hw != H)
        return -1;
    return square(rgb);
}

int mali_probe(const mali_hw_t *hw, mali_info_t *info)
{
    static mali_info_t dummy;
    mali_info_t *in = info ? info : &dummy;
    memset(in, 0, sizeof *in);
    H = hw;
    if (supply(in) != 0) {
        in->step = MALI_E_SUPPLY;
        return MALI_E_SUPPLY;
    }
    int r = MALI_E_POWER;
    if (power() == 0 && (r = MALI_E_ID, identify(in) == 0) && (r = MALI_E_RESET, reset() == 0) &&
        (r = MALI_E_CORES, cores(in) == 0) && (r = MALI_E_MMU, mmu() == 0) && (r = jobs(in)) == MALI_OK)
        r = fragment(in);
    probed_ok = r == MALI_OK;
    in->step = r;
    return r;
}
