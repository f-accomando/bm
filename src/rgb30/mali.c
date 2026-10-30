/*
 * The Mali-G52 driver, first step (see mali.h). The registers and
 * the order of the steps are those of the GPU's public documentation as
 * Linux's panfrost driver and Rockchip's power domains use them; the job
 * descriptors those Mesa's panfrost writes (a header of 32 bytes, then the
 * job's own words).
 */
#include "mali.h"
#include "mali_desc.h"
#include "mali_shaders.h"
#include "mali_variants.h"

#include <string.h>
#ifdef PLAT_RK3566
#include <arm_neon.h>
#endif

#include "lib/printf.h"

/* --- registers --- */

#define GPU_ID              0x000
#define GPU_L2_FEATURES     0x004
#define GPU_CORE_FEATURES   0x008
#define GPU_TILER_FEATURES  0x00c
#define GPU_MMU_FEATURES    0x014
#define GPU_AS_PRESENT      0x018
#define GPU_JS_PRESENT      0x01c
#define GPU_INT_RAWSTAT     0x020
#define GPU_INT_CLEAR       0x024
#define GPU_INT_MASK        0x028
#define GPU_CMD             0x030
#define GPU_FAULT_STATUS    0x03c
#define GPU_FAULT_ADDRESS   0x040
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
#define JS_HEAD             0x00    /* _LO; _HI at + 4 */
#define JS_TAIL             0x08    /* _LO; _HI at + 4 */
#define JS_CONFIG           0x18
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

/* the Mali LPAE page tables: 4 levels of 512 entries (4 KB granule) */
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
static char line[256];

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
    const uint32_t tf = gr(GPU_TILER_FEATURES);
    ksnprintf(line, sizeof line, "mali: tiler features %08x (max_levels %u, bin_size %u)",
              (unsigned)tf, (unsigned)(tf >> 8 & 15), (unsigned)(tf & 255));
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
    /* L0[0] -> L1, L1[map / 1 GB] -> L2 (the map within one GB), L2: 2 MB blocks */
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    memset(m + MEM_L0, 0, 3 * 4096);
    const uint64_t first = H->map_pa >> 21, n = H->map_size >> 21;
    if ((H->map_pa >> 30) != ((H->map_pa + H->map_size - 1) >> 30) || (H->map_pa >> 39) || (H->mem_pa & 0x1fffff) ||
        H->mem_pa < H->map_pa || H->mem_pa + H->mem_size > H->map_pa + H->map_size) {
        ksnprintf(line, sizeof line, "mali: mmu: the GPU's memory must be inside one GB, 2 MB aligned");
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
        ksnprintf(line, sizeof line, "mali: mmu: address space 0 on, %u MB at %08x 1:1 (Mali LPAE, tables at %08x)",
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

/* start the chain from head on job slot js (0 fragment, 1 the others); only this slot's interrupt bits are cleared (the
 * other slot may be running a frame) */
static void job_kick(int js, uint64_t head)
{
    gw(JOB_INT_CLEAR, 1u << js | 1u << (16 + js));
    gw(JS(js, JS_HEAD_NEXT), (uint32_t)head);
    gw(JS(js, JS_HEAD_NEXT) + 4, (uint32_t)(head >> 32));
    gw(JS(js, JS_AFFINITY_NEXT), gr(GPU_SHADER_PRESENT));
    gw(JS(js, JS_AFFINITY_NEXT) + 4, gr(GPU_SHADER_PRESENT + 4));
    gw(JS(js, JS_CONFIG_NEXT), 0 /* AS 0 */ | JS_CFG_THREAD_PRI(8) | JS_CFG_START_FLUSH_CLEAN_INV |
                               JS_CFG_END_FLUSH_CLEAN_INV);
    gw(JS(js, JS_COMMAND_NEXT), JS_CMD_START);
}

/* has the slot's chain ended? 0 running, 1 done, -1 failed; *raw the job interrupts' state, *status the slot's */
static int job_poll(int js, uint32_t *status, uint32_t *raw)
{
    const uint32_t done = 1u << js, failed = 1u << (16 + js);
    *raw = gr(JOB_INT_RAWSTAT);
    if (!(*raw & (done | failed)))
        return 0;
    *status = gr(JS(js, JS_STATUS));
    gw(JOB_INT_CLEAR, done | failed);
    return *raw & done && !(*raw & failed) ? 1 : -1;
}

static void addr_map(uint64_t a, char *buf, size_t sz)
{
    if (!a) {
        ksnprintf(buf, sz, "none");
        return;
    }
    if (H && a >= H->mem_pa && a < H->mem_pa + H->mem_size) {
        uint32_t off = (uint32_t)(a - H->mem_pa);
        if (off < MALI_SET_BASE(0)) {
            if (off == MEM_L0) ksnprintf(buf, sz, "PT_L0");
            else if (off == MEM_L1) ksnprintf(buf, sz, "PT_L1");
            else if (off == MEM_L2) ksnprintf(buf, sz, "PT_L2");
            else if (off == MEM_JOBS) ksnprintf(buf, sz, "MEM_JOBS");
            else if (off == MEM_RESULT) ksnprintf(buf, sz, "MEM_RESULT");
            else if (off == 0x3800) ksnprintf(buf, sz, "MEM_FJOB");
            else if (off == 0x3900) ksnprintf(buf, sz, "MEM_FBD");
            else if (off == 0x3a00) ksnprintf(buf, sz, "MEM_SAMPLES");
            else if (off >= 0x10000 && off < 0x20000) ksnprintf(buf, sz, "MEM_SURF+0x%x", off - 0x10000);
            else ksnprintf(buf, sz, "mem+0x%x", off);
            return;
        }
        int s = (off >= MALI_SET_BASE(1)) ? 1 : 0;
        uint32_t base = MALI_SET_BASE(s);
        uint32_t rel = off - base;
        if (rel == SO_FJOB) ksnprintf(buf, sz, "s%d.FJOB", s);
        else if (rel == SO_FBD) ksnprintf(buf, sz, "s%d.FBD", s);
        else if (rel == SO_SAMPLES) ksnprintf(buf, sz, "s%d.SAMPLES", s);
        else if (rel == SO_HEAPD) ksnprintf(buf, sz, "s%d.HEAPD", s);
        else if (rel == SO_TCTX) ksnprintf(buf, sz, "s%d.TCTX", s);
        else if (rel == SO_VRSD) ksnprintf(buf, sz, "s%d.VRSD", s);
        else if (rel == SO_LS) ksnprintf(buf, sz, "s%d.LS", s);
        else if (rel == SO_VIEWPORT) ksnprintf(buf, sz, "s%d.VIEWPORT", s);
        else if (rel == SO_ATTR) ksnprintf(buf, sz, "s%d.ATTR", s);
        else if (rel == SO_VATTR) ksnprintf(buf, sz, "s%d.VATTR", s);
        else if (rel == SO_VS) ksnprintf(buf, sz, "s%d.VS", s);
        else if (rel == SO_FS) ksnprintf(buf, sz, "s%d.FS", s);
        else if (rel == SO_VS3) ksnprintf(buf, sz, "s%d.VS3", s);
        else if (rel >= 0x10000 && rel < 0x20000) ksnprintf(buf, sz, "s%d.VERTS+0x%x", s, rel - 0x10000);
        else if (rel >= 0x20000 && rel < 0x30000) ksnprintf(buf, sz, "s%d.POSITIONS+0x%x", s, rel - 0x20000);
        else if (rel >= 0x30000 && rel < 0x38000) ksnprintf(buf, sz, "s%d.PUSH+0x%x", s, rel - 0x30000);
        else if (rel >= 0x38000 && rel < 0x40000) ksnprintf(buf, sz, "s%d.VPUSH+0x%x", s, rel - 0x38000);
        else if (rel >= SO_DRAWS && rel < SO_HEAP) {
            uint32_t doff = rel - SO_DRAWS;
            uint32_t d = doff / MEM_DRAW_SIZE;
            uint32_t r = doff % MEM_DRAW_SIZE;
            if (r == D_VJOB) ksnprintf(buf, sz, "s%d.draw[%u].VJOB", s, d);
            else if (r == D_TJOB) ksnprintf(buf, sz, "s%d.draw[%u].TJOB", s, d);
            else if (r == D_FRSD) ksnprintf(buf, sz, "s%d.draw[%u].FRSD", s, d);
            else if (r == D_ABUFS) ksnprintf(buf, sz, "s%d.draw[%u].ABUFS", s, d);
            else if (r == D_VBUFS) ksnprintf(buf, sz, "s%d.draw[%u].VBUFS", s, d);
            else if (r == D_UBO) ksnprintf(buf, sz, "s%d.draw[%u].UBO", s, d);
            else if (r == D_COLOUR) ksnprintf(buf, sz, "s%d.draw[%u].COLOUR", s, d);
            else if (r == D_VUBO) ksnprintf(buf, sz, "s%d.draw[%u].VUBO", s, d);
            else if (r == D_VRSD) ksnprintf(buf, sz, "s%d.draw[%u].VRSD", s, d);
            else ksnprintf(buf, sz, "s%d.draw[%u]+0x%x", s, d, r);
        }
        else if (rel >= SO_HEAP && rel < SO_SURF) ksnprintf(buf, sz, "s%d.HEAP+0x%x", s, rel - SO_HEAP);
        else if (rel >= SO_SURF) ksnprintf(buf, sz, "s%d.SURF+0x%x", s, rel - SO_SURF);
        else ksnprintf(buf, sz, "s%d+0x%x", s, rel);
        return;
    }
    if (H && H->screen_pa && a >= H->screen_pa && a < H->screen_pa + (uint64_t)H->screen_pitch * H->screen_h) {
        uint32_t off = (uint32_t)(a - H->screen_pa);
        ksnprintf(buf, sz, "screen+0x%x", off);
        return;
    }
    ksnprintf(buf, sz, "outside");
}

/* the lines for a chain that failed (raw without the slot's bits: it never ended) */
static void job_fail(int js, const char *what, uint32_t status, uint32_t raw)
{
    const uint32_t mmu_raw = gr(MMU_INT_RAWSTAT);
    const uint64_t head = (uint64_t)gr(JS(js, JS_HEAD)) | ((uint64_t)gr(JS(js, JS_HEAD + 4)) << 32);
    const uint64_t tail = (uint64_t)gr(JS(js, JS_TAIL)) | ((uint64_t)gr(JS(js, JS_TAIL + 4)) << 32);
    char hbuf[48], tbuf[48];
    addr_map(head, hbuf, sizeof hbuf);
    addr_map(tail, tbuf, sizeof tbuf);

    if (!(raw & (1u << js | 1u << (16 + js))))
        ksnprintf(line, sizeof line, "mali: %s: no end after 100 ms (JS_STATUS %02x %s, job raw %08x)", what,
                  (unsigned)status, mali_exception(status), (unsigned)raw);
    else
        ksnprintf(line, sizeof line, "mali: %s: failed, JS_STATUS %02x %s", what, (unsigned)status,
                  mali_exception(status));
    say();

    ksnprintf(line, sizeof line, "mali: %s: JS%d head %08x%08x [%s], tail %08x%08x [%s]",
              what, js, (unsigned)(head >> 32), (unsigned)head, hbuf,
              (unsigned)(tail >> 32), (unsigned)tail, tbuf);
    say();

    const uint32_t gfs = gr(GPU_FAULT_STATUS);
    const uint64_t gfa = (uint64_t)gr(GPU_FAULT_ADDRESS) | ((uint64_t)gr(GPU_FAULT_ADDRESS + 4) << 32);
    if (gfs || gfa) {
        char gbuf[48];
        addr_map(gfa, gbuf, sizeof gbuf);
        ksnprintf(line, sizeof line, "mali: %s: GPU fault %08x at %08x%08x [%s]",
                  what, (unsigned)gfs, (unsigned)(gfa >> 32), (unsigned)gfa, gbuf);
        say();
    }

    if (mmu_raw & 0x10001u) {
        const uint32_t fs = gr(AS(0, AS_FAULTSTATUS));
        const uint64_t fa = (uint64_t)gr(AS(0, AS_FAULTADDRESS)) | ((uint64_t)gr(AS(0, AS_FAULTADDRESS) + 4) << 32);
        char abuf[48];
        addr_map(fa, abuf, sizeof abuf);
        ksnprintf(line, sizeof line, "mali: %s: MMU fault %08x (%s) at %08x%08x [%s]", what, (unsigned)fs,
                  mali_exception(fs), (unsigned)(fa >> 32), (unsigned)fa, abuf);
        say();
    }
    gw(MMU_INT_CLEAR, 0xffffffffu);
}

/* the chain from head on job slot js and the wait for its end; 0 done, -1 failed (lines said) */
static int run_on(int js, uint64_t head, uint32_t *us, uint32_t *status, const char *what)
{
    gw(MMU_INT_CLEAR, 0xffffffffu);
    const uint32_t t0 = H->us();
    job_kick(js, head);
    uint32_t raw = 0;
    int r;
    while (!(r = job_poll(js, status, &raw)) && H->us() - t0 < 100000)
        ;
    *us = H->us() - t0;
    if (!r)
        *status = gr(JS(js, JS_STATUS));
    if (r > 0)
        return 0;
    job_fail(js, what, *status, raw);
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

static int be_aa_frame;                         /* the frame being built is 4x MSAA (the resolve reads it too) */

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

/* the sample positions of one sample (1/256 of a pixel, the middle) */
static void samples(uint8_t *p, int aa)
{
    memset(p, 0, 256);
    if (aa) {                                   /* the rotated 4x grid (Mesa pan_samples.c), 1/256 of a pixel */
        put32(p, 96u | 32u << 16);
        put32(p + 4, 224u | 96u << 16);
        put32(p + 8, 32u | 160u << 16);
        put32(p + 12, 160u | 224u << 16);
    } else {
        put32(p, 128u | 128u << 16);
    }
    put32(p + 32 * 4, 128u | 128u << 16);   /* the origin */
}

/* a framebuffer descriptor and its render target at f: w x h pixels at
 * base (row stride, the colour 0xRRGGBB) */
/* aa: 4x MSAA. The tile holds 4 samples a pixel and the write-back averages them. The surface is cleared to 0 (alpha 0) and
 * keeps the alpha: it is the share of the samples a triangle covered, and the colour is premultiplied by it. */
static void fbd(uint8_t *f, uint64_t samples_pa, uint32_t w, uint32_t h, uint64_t base, uint32_t stride, uint32_t rgb, int aa)
{
    memset(f, 0, FBD_SIZE + RT_SIZE);
    put64(f + 16, samples_pa);                              /* sample locations */
    put32(f + 32, (w - 1) | (h - 1) << 16);                 /* width, height */
    put32(f + 40, (w - 1) | (h - 1) << 16);                 /* bound max (min 0, 0) */
    put32(f + 44, (aa ? 2u | 2u << 3 : 0u) /* 4 samples, rotated grid, or 1 sample */ | TIE_MINUS_180_IN_0_OUT << 6 | 8u << 9 /* 16 x 16 */ |
                      0u << 19 /* 1 target */ | (aa ? 4u : 1u) << 24 /* 4 KB or 1 KB of colour a tile */);
    put32(f + 48, ZFMT_D24 << 16);
    const float zclear = 1.0f;                              /* the far plane: the depth test is "less or equal" */
    memcpy(f + 52, &zclear, 4);
    uint8_t *rt = f + FBD_SIZE;
    put32(rt, CBUF_R8G8B8A8 << 26);
    put32(rt + 4, 1u /* write */ | COLOR_R8G8B8A8 << 3 | BLOCK_LINEAR << 8 | 1u << 15 /* dither */ |
                  SWZ(2, 1, 0, aa ? 3 : 4) << 16 | (aa ? 1u << 12 : 0u) /* average the samples */ | 1u << 31 /* clean pixels written */);
    put64(rt + 32, base);
    put32(rt + 40, stride);
    put32(rt + 44, stride * h);
    const uint32_t c = aa ? 0u : (rgb >> 16 & 255) | (rgb & 0xff00) | (rgb & 255) << 16 | 0xffu << 24;   /* R G B A */
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
    fbd(m + MEM_FBD, pa + MEM_SAMPLES, SURF, SURF, base, H->screen_pitch, rgb, 0);
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
    samples(m + MEM_SAMPLES, 0);
    const uint32_t rgb = 0x20c060;
    fbd(m + MEM_FBD, pa + MEM_SAMPLES, SURF, SURF, pa + MEM_SURF, SURF * 4, rgb, 0);
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

/* --- a triangle (the Mali-G52 driver, not yet a bm3d version: it has not run on the device) ---
 * A Vertex job (the shader passes three positions in pixels through), a Tiler job (the triangle into the
 * tiler's lists) and a Fragment job (the tiles shaded with the draw's colour): the descriptors of
 * mali_desc.c, the shaders of mali_shaders.h (tools/mali/mkshader.c). A 96 x 96 square of the screen, under
 * the clear's square: dark blue, an orange triangle on it. The jobs run as Mesa's panvk submits them
 * (vertex then tiler on one slot, the fragment job on its own once they have ended). */

/* --- a frame: up to MALI_MAX_DRAWS draws (M41 phase 2) ---
 * Draw d has its Vertex job, its Tiler job and its own tables at SO_DRAWS + MEM_DRAW_SIZE * d of the set; the pieces every
 * draw shares (heap, tiler context, vertex Renderer State, local storage, viewport, the attribute descriptors, the shader
 * code) are in the set's first 0x1000 bytes and at SO_VS. The vertices, the positions the vertex jobs write and the pushed
 * words are at the places mali.h names (MALI_FRAME_*, set 0's), draw after draw. */

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* Everything for the frame in the GPU memory: the draws (valid, checked by the caller) into a w x h surface at target_pa,
 * cleared to `clear` first. The jobs are chained from draws_pa (the first Vertex job); the Fragment job is at the set's SO_FJOB. */
/* where the ARM's time goes in the backend (G3 of docs/progress/multicore.md): counter ticks, in us at the stats read.
 * flush includes the job and its wait; resolve is inside flush (a frame's own resolve) */
static uint64_t t_mesh, t_flush, t_resolve, t_sort, t_build, t_jobs, t_b1, t_b2, t_b3;
#define BE_HW_MAX_DRAWS_SH 1024                 /* the fast path's draws (BE_HW_MAX_DRAWS) */
#if defined(__aarch64__)
static inline uint64_t cnt_now(void) { uint64_t v; __asm__ volatile("isb\n mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline uint64_t cnt_freq(void) { uint64_t f; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f)); return f; }
#else                                           /* the host test builds this file too */
static inline uint64_t cnt_now(void) { return 0; }
static inline uint64_t cnt_freq(void) { return 0; }
#endif

/* A copy into the GPU's memory (Normal non-cacheable): 16 bytes a store. The library's memcpy went byte by byte there when
 * a pointer was not aligned (G3 report of 26 Oct: 25 us a draw in frame_build_ex, 10 ms of a 15 ms frame at 400 draws). */
static inline void copy_nc16(void *dst, const void *src, size_t n)
{
    typedef unsigned __int128 __attribute__((may_alias)) u128;   /* the callers pass floats too */
    u128 *d = dst;
    const u128 *s = src;
    for (size_t i = 0; i < n / 16; i++)
        d[i] = s[i];
}

/* Zero n bytes (16-aligned, n a multiple of 16): 16 bytes a store. The library's memset is a byte loop (a draw's 1344 bytes
 * cost 3.4 us: 5 of the 10 ms of frame_build_ex at 1480 draws, G3 report of 26 Oct). */
static inline void zero16(void *dst, size_t n)
{
    typedef unsigned __int128 __attribute__((may_alias)) u128;   /* the callers pass floats too */
    u128 *d = dst;
    for (size_t i = 0; i < n / 16; i++) {
        d[i] = 0;
        __asm__ volatile("" ::: "memory");   /* the compiler keeps the loop (it would turn it into memset) */
    }
}

/* What each draw slot of the fast path's arena holds (the arena is Normal non-cacheable: a store costs far more than a compare in
 * the cache; 6.8 us of 15 per draw in the G3 report of 26 Oct). Only frame_build_ex writes those slots. A line goes to the
 * arena when it differs from what the slot held, or when the slot was never written. */
static uint8_t slot_shadow[BE_HW_MAX_DRAWS_SH][0x540] __attribute__((aligned(64)));
static uint8_t slot_ok[BE_HW_MAX_DRAWS_SH];

static void copy_slot(uint8_t *dst, const uint8_t *loc, uint32_t d)
{
    if (d >= BE_HW_MAX_DRAWS_SH) {
        copy_nc16(dst, loc, 0x540);
        return;
    }
    uint8_t *sh = slot_shadow[d];
    for (unsigned line = 0; line < 0x540 / 64; line++) {
        const int job_line = 64 * line == D_VJOB || 64 * line == D_TJOB;   /* the GPU writes the job headers' first 16 bytes */
        if (slot_ok[d] && !job_line && memcmp(sh + 64 * line, loc + 64 * line, 64) == 0)
            continue;
        copy_nc16(dst + 64 * line, loc + 64 * line, 64);
        memcpy(sh + 64 * line, loc + 64 * line, 64);
    }
    slot_ok[d] = 1;
}

#define MALI_BUILD_CORES 4
#define MALI_PAR_MIN_DRAWS 32                   /* a step on the cores has a cost of its own (a few us) */
static int (*mp_workers)(void);
static int (*mp_par)(void (*fn)(void *), void *const *, unsigned);
void mali_par_set(int (*workers)(void), int (*par)(void (*fn)(void *), void *const *, unsigned))
{
    mp_workers = workers;
    mp_par = par;
}

/* The area R2, 3 MB at [mem - 13 MB, mem - 10 MB): the varyings (0x80000 bytes, 32768 vertices of 16) and the vertices (2 MB, 32768 of
 * up to 64 bytes) of the cached meshes, so a frame holds 10922 faces of them; the other draws keep the 16384 vertices of the arena. */
#define BE_R2_BELOW     (13u << 20)

typedef struct {                                /* what every core reads while it builds draws */
    const mali_draw_t *draws;
    uint32_t ndraws;
    uint64_t pa, dpa0;
    uint32_t sb;
    uint8_t *verts_m, *push_m, *vpush_m, *draws_m;
    uint64_t verts_pa, pos_pa, push_pa, vpush_pa;
    const uint8_t *frs_buf, *vrs3_buf, *frst_buf, *frsta_buf;   /* frsta: the textured state that kills the texels with alpha 0 */
    uint8_t *tv_m;                              /* textured draws: vertices of 32 bytes (position, u v k), varyings of 16 */
    uint64_t tv_pa, tvy_pa;
    int fast_pack;
    uint8_t *lv_m, *litp_m;                     /* Q2: lit vertices of 48 bytes, the uniform words of a lit draw (448 each) */
    uint64_t lv_pa, litp_pa;
    uint8_t *lv2_m;                             /* the vertices of the cached meshes (the area R2) */
    uint64_t lv2_pa, tvy2_pa;
    uint8_t *texa_m;                            /* sprites: the texture descriptors of a draw (576 bytes each) */
    uint64_t texa_pa;
    const uint8_t *mem;                         /* the GPU memory as the ARM sees it, and its address for the GPU */
    uint64_t mem_pa;
    const uint8_t *frsl_buf, *frsd_buf, *frsp_buf;   /* the 2D rectangles' and sprites' fragment states (no depth test) */
    int zbuf;                                   /* G2: the frame has a depth test: z = 1 - near / depth (see be_mesh) */
} bctx_t;

typedef struct {                                /* the block of one core: its draws and its own counters */
    const bctx_t *c;
    uint32_t d0, d1, first;                     /* draws [d0, d1); first: the first vertex of draw d0 */
    uint64_t t1, t2, t3;
} __attribute__((aligned(64))) bblk_t;

static void build_draws(void *p)
{
    bblk_t *k = p;
    const bctx_t *c = k->c;
    const mali_draw_t *draws = c->draws;
    const uint32_t ndraws = c->ndraws, sb = c->sb;
    const uint64_t pa = c->pa, verts_pa = c->verts_pa, pos_pa = c->pos_pa, push_pa = c->push_pa, vpush_pa = c->vpush_pa;
    uint8_t *verts_m = c->verts_m, *push_m = c->push_m, *vpush_m = c->vpush_m, *draws_m = c->draws_m;
    const uint8_t *frs_buf = c->frs_buf, *vrs3_buf = c->vrs3_buf, *frst_buf = c->frst_buf;
    uint8_t *tv_m = c->tv_m;
    const uint64_t tv_pa = c->tv_pa, tvy_pa = c->tvy_pa;
    const uint64_t draws_pa = c->dpa0;
    const int fast_pack = c->fast_pack;
    uint32_t first = k->first;                  /* the draw's first vertex, in the frame's arrays */
    k->t1 = k->t2 = k->t3 = 0;
    for (uint32_t d = k->d0; d < k->d1; d++) {
        uint8_t loc[0x540] __attribute__((aligned(16)));
        zero16(loc, sizeof loc);
        const uint64_t dpa = draws_pa + (uint64_t)MEM_DRAW_SIZE * d;
        const uint32_t nv = 3 * draws[d].n;
        const uint32_t rgb = draws[d].rgb;

        if (draws[d].litv && c->lv_m) {         /* Q2: a lit mesh: the vertex shader lights the faces */
            const uint64_t lx0 = cnt_now();
            const float (*litv)[12] = draws[d].litv;
            const int litx = draws[d].litx, vstride = litx ? 64 : 48;
            const int tm = draws[d].tex_pa != 0;    /* a textured mesh: BM_VS_TEXM and the texture shader (the cache entry has u v) */
            if (draws[d].res_blk) {             /* the face cache: the blocks of the visible faces, copied as they are */
                const size_t fs = (size_t)vstride * 3;
                for (uint32_t kx = 0; kx < draws[d].n; kx++)
                    copy_nc16(c->lv2_m + (size_t)vstride * first + fs * kx,
                              (const uint8_t *)draws[d].res_blk + fs * draws[d].res_idx[kx], fs);
            }
            for (uint32_t i = 0; i < nv && !draws[d].res_blk; i++) {
                const float v16[16] __attribute__((aligned(16))) = { draws[d].xyz[i][0], draws[d].xyz[i][1], draws[d].xyz[i][2], 1.0f,
                                                                     litv[i][0], litv[i][1], litv[i][2], litv[i][3],
                                                                     litv[i][4], litv[i][5], litv[i][6], litv[i][7],
                                                                     litv[i][8], litv[i][9], litv[i][10], litv[i][11] };
                volatile float *lvp = (volatile float *)(void *)(c->lv_m + (size_t)vstride * (first + i));
                for (int q = 0; q < vstride / 4; q++)
                    lvp[q] = v16[q];
            }
            volatile float *lpp = (volatile float *)(void *)(c->litp_m + draws[d].uoff);
            uint32_t ubo_entries = litx == 2 ? 15u + 12u * (uint32_t)draws[d].nbones : litx ? 26u : 9u;
            if (litx == 2) {                    /* BM_VS_LITB: the mesh block, then a block a bone */
                const float *L0 = draws[d].light;
                static const unsigned mesh_from[15] = { 16, 20, 24, 28, 32, 36, 40, 56, 60, 64, 68, 72, 76, 80, 84 };
                for (unsigned q = 0; q < 15; q++)
                    for (unsigned e = 0; e < 4; e++)
                        lpp[4 * q + e] = L0[mesh_from[q] + e];
                for (uint32_t bn = 0; bn < (uint32_t)draws[d].nbones; bn++) {
                    const float *bl = draws[d].light + 88 * bn, *mv = draws[d].mvp + 16 * bn;
                    volatile float *bp = lpp + 60 + 48 * bn;
                    for (unsigned q = 0; q < 16; q++)
                        bp[q] = mv[q];
                    for (unsigned q = 0; q < 16; q++)       /* sun, up, view, half */
                        bp[16 + q] = bl[q];
                    for (unsigned q = 0; q < 12; q++)       /* the camera matrix */
                        bp[32 + q] = bl[44 + q];
                    bp[44] = bl[39];                        /* fog amount */
                    bp[45] = bp[46] = bp[47] = 0.0f;
                }
            } else if (litx) {                         /* BM_VS_LITX reads uniform buffer 0 as it is: the matrix, then the light block */
                for (unsigned q = 0; q < 104; q++)
                    lpp[q] = q < 16 ? draws[d].mvp[q] : draws[d].light[q - 16];
            } else {                            /* BM_VS_LIT: the pushed words in the order the compiler chose: word q is the word
                                                 * BM_VS_LIT_push[q] of uniform buffer 0 */
                const unsigned npush = tm ? BM_VS_TEXM_PUSH_COUNT : BM_VS_LIT_PUSH_COUNT;
                for (unsigned q = 0; q < 36; q++) {
                    const unsigned wi = q < npush ? (tm ? BM_VS_TEXM_push[q][1] : BM_VS_LIT_push[q][1]) >> 2 : 0;
                    lpp[q] = q >= npush ? 0.0f : wi < 16 ? draws[d].mvp[wi] : draws[d].light[wi - 16];
                }
            }
            zero16(push_m + 64 * (size_t)d, 32);
            memcpy(loc + D_FRSD, tm ? ((draws[d].tex_pa & 1) ? c->frsta_buf : frst_buf) : c->frsl_buf, 80);
            md_attribute_buffer(loc + D_ABUFS, (draws[d].res_blk ? c->lv2_pa : c->lv_pa) + (uint64_t)vstride * first, vstride, vstride * nv);
            md_attribute_buffer(loc + D_ABUFS + 16, 0, 0, 0);
            md_attribute_buffer(loc + D_VBUFS, (draws[d].res_blk ? c->tvy2_pa : tvy_pa) + 16 * (uint64_t)first, 16, 16 * nv);
            md_attribute_buffer(loc + D_VBUFS + 16, pos_pa + 16 * (uint64_t)first, 16, 16 * nv);
            md_attribute_buffer(loc + D_VBUFS + 32, 0, 0, 0);
            md_uniform_buffer(loc + D_VUBO, c->litp_pa + draws[d].uoff, ubo_entries);
            md_draw_t lvd = { .state = pa + sb + (litx == 2 ? SO_VRSDB : litx ? SO_VRSDX : tm ? SO_VRSDM : SO_VRSDL), .attribute_buffers = dpa + D_ABUFS, .attributes = pa + sb + SO_ATTR,
                              .varying_buffers = dpa + D_VBUFS, .varyings = pa + sb + SO_VATTR,
                              .uniform_buffers = dpa + D_VUBO, .push_uniforms = c->litp_pa + draws[d].uoff,
                              .thread_storage = pa + sb + SO_LS };
            job_header(loc + D_VJOB, MD_JOB_VERTEX, 2 * (int)d + 1, d ? 2 * (int)d : 0, dpa + D_TJOB);
            md_vertex_job(loc + D_VJOB, nv, &lvd);
            md_draw_t ltd = { .position = pos_pa + 16 * (uint64_t)first, .state = dpa + D_FRSD,
                              .viewport = pa + sb + SO_VIEWPORT, .push_uniforms = push_pa + 64 * (uint64_t)d,
                              .thread_storage = pa + sb + SO_LS, .varying_buffers = dpa + D_VBUFS,
                              .varyings = pa + sb + SO_VATTR + MD_ATTRIBUTE_SIZE,
                              .textures = tm ? (draws[d].tex_pa & ~1ull) : 0, .samplers = tm ? (draws[d].tex_pa & ~1ull) + 0x80 : 0 };
            md_primitive_t lprim = { .mode = MD_TRIANGLES, .index_count = nv };
            job_header(loc + D_TJOB, MD_JOB_TILER, 2 * (int)d + 2, 2 * (int)d + 1,
                       d + 1 < ndraws ? dpa + MEM_DRAW_SIZE + D_VJOB : 0);
            md_tiler_job(loc + D_TJOB, &lprim, pa + sb + SO_TCTX, &ltd);
            const uint64_t lx1 = cnt_now();
            k->t2 += lx1 - lx0;
            if (fast_pack)
                copy_slot(draws_m + (size_t)MEM_DRAW_SIZE * d, loc, d);
            else
                copy_nc16(draws_m + (size_t)MEM_DRAW_SIZE * d, loc, sizeof loc);
            k->t3 += cnt_now() - lx1;
            first += (nv + 3) & ~3u;
            continue;
        }

        if (draws[d].uvk && tv_m) {              /* G1: a textured draw */
            const uint64_t tx0 = cnt_now();
            const float (*uvk)[3] = draws[d].uvk;
            for (uint32_t i = 0; i < nv; i++) {
                /* clip position (x w, y w, z w, w) with w = depth: the shader divides by w, the varyings are interpolated
                 * with perspective */
                const float z = draws[d].xyz[i][2], w = z > 1e-9f ? 1.0f / z : 1e9f;
                const float v8[8] __attribute__((aligned(16))) = { draws[d].xyz[i][0] * w, draws[d].xyz[i][1] * w, c->zbuf ? w - R3D_NEAR : 0.5f * w, w,
                                                                   uvk[i][0], uvk[i][1], uvk[i][2], 0.0f };
                volatile float *tvp = (volatile float *)(void *)(tv_m + 32 * (size_t)(first + i));
                for (int q = 0; q < 8; q++)
                    tvp[q] = v8[q];
            }
            zero16(push_m + 64 * (size_t)d, 32);
            memcpy(loc + D_FRSD, draws[d].d2 == 1 ? c->frsd_buf : draws[d].d2 == 2 ? c->frsp_buf : (draws[d].tex_pa & 1) ? c->frsta_buf : frst_buf, 80);
            uint64_t draw_tex = draws[d].tex_pa & ~1ull, draw_smp = draws[d].tex_pa ? (draws[d].tex_pa & ~1ull) + 0x80 : 0;
            if (draws[d].d2 == 2 && draws[d].ntex) {    /* sprites: the Texture descriptors of the sheets in a row, the sampler of the first */
                uint8_t *ta = c->texa_m + 576 * (size_t)d;
                for (uint32_t kx = 0; kx < draws[d].ntex; kx++)
                    copy_nc16(ta + 32 * kx, c->mem + (draws[d].texs[kx] - c->mem_pa), 32);
                copy_nc16(ta + 512, c->mem + (draws[d].texs[0] - c->mem_pa) + 0x80, 32);
                draw_tex = c->texa_pa + 576 * (uint64_t)d;
                draw_smp = draw_tex + 512;
                md_rs_texture_count((uint8_t *)(void *)(loc + D_FRSD), draws[d].ntex);
            }
            md_attribute_buffer(loc + D_ABUFS, tv_pa + 32 * (uint64_t)first, 32, 32 * nv);
            md_attribute_buffer(loc + D_ABUFS + 16, 0, 0, 0);
            md_attribute_buffer(loc + D_VBUFS, tvy_pa + 16 * (uint64_t)first, 16, 16 * nv);
            md_attribute_buffer(loc + D_VBUFS + 16, pos_pa + 16 * (uint64_t)first, 16, 16 * nv);
            md_attribute_buffer(loc + D_VBUFS + 32, 0, 0, 0);
            md_draw_t tvd = { .state = pa + sb + SO_VRSDT, .attribute_buffers = dpa + D_ABUFS, .attributes = pa + sb + SO_ATTR,
                              .varying_buffers = dpa + D_VBUFS, .varyings = pa + sb + SO_VATTR,
                              .thread_storage = pa + sb + SO_LS };
            job_header(loc + D_VJOB, MD_JOB_VERTEX, 2 * (int)d + 1, d ? 2 * (int)d : 0, dpa + D_TJOB);
            md_vertex_job(loc + D_VJOB, nv, &tvd);
            md_draw_t ttd = { .position = pos_pa + 16 * (uint64_t)first, .state = dpa + D_FRSD,
                              .viewport = pa + sb + SO_VIEWPORT, .push_uniforms = push_pa + 64 * (uint64_t)d,
                              .thread_storage = pa + sb + SO_LS, .varying_buffers = dpa + D_VBUFS,
                              .varyings = pa + sb + SO_VATTR + MD_ATTRIBUTE_SIZE, .textures = draw_tex,
                              .samplers = draw_smp };
            md_primitive_t tprim = { .mode = MD_TRIANGLES, .index_count = nv };
            job_header(loc + D_TJOB, MD_JOB_TILER, 2 * (int)d + 2, 2 * (int)d + 1,
                       d + 1 < ndraws ? dpa + MEM_DRAW_SIZE + D_VJOB : 0);
            md_tiler_job(loc + D_TJOB, &tprim, pa + sb + SO_TCTX, &ttd);
            const uint64_t tx1 = cnt_now();
            k->t2 += tx1 - tx0;
            if (fast_pack)
                copy_slot(draws_m + (size_t)MEM_DRAW_SIZE * d, loc, d);
            else
                copy_nc16(draws_m + (size_t)MEM_DRAW_SIZE * d, loc, sizeof loc);
            k->t3 += cnt_now() - tx1;
            first += (nv + 3) & ~3u;
            continue;
        }

        const uint64_t tb0 = cnt_now();
        /* the data: the positions as (x y z 1), the colour twice (the buffer and the pushed words 4-7) */
        for (uint32_t i = 0; i < nv; i++) {
            float zv = draws[d].xyz[i][2];
            if (!draws[d].mvp)                  /* the screen-space z is 1/depth (nearer is larger): the depth buffer wants 0 near, 1 far */
                zv = c->zbuf ? 1.0f - R3D_NEAR * zv : zv;
            float v4[4] = { draws[d].xyz[i][0], draws[d].xyz[i][1], zv, 1.0f };
            memcpy(verts_m + 16 * (size_t)(first + i), v4, 16);
        }
        const float col[4] = { (float)(rgb >> 16 & 0xff) / 255.0f, (float)(rgb >> 8 & 0xff) / 255.0f,
                               (float)(rgb & 0xff) / 255.0f, 1.0f };
        memcpy(loc + D_COLOUR, col, 16);
        memcpy(push_m + 64 * (size_t)d + 16, col, 16);   /* BM_FS_push says ubo 0 offsets 0, 4, 8, 12 */

        const uint64_t tb1 = cnt_now();
        k->t1 += tb1 - tb0;
        /* the draw's tables */
        memcpy(loc + D_FRSD, frs_buf, 80);
        md_attribute_buffer(loc + D_ABUFS, verts_pa + 16 * (uint64_t)first, 16, 16 * nv);
        md_attribute_buffer(loc + D_VBUFS, 0, 0, 0);
        md_attribute_buffer(loc + D_VBUFS + 16, pos_pa + 16 * (uint64_t)first, 16, 16 * nv);
        md_attribute_buffer(loc + D_VBUFS + 32, 0, 0, 0);
        md_uniform_buffer(loc + D_UBO, dpa + D_COLOUR, 1);
        if (draws[d].mvp) {   /* phase 3: BM_VS3 with the matrix pushed (BM_VS3_push: words 0..15 are bytes 0..60 of ubo 0) */
            memcpy(loc + D_VRSD, vrs3_buf, 64);
            float vp[16];
            for (int k = 0; k < 16; k++)
                vp[BM_VS3_push[k][1] >> 2] = draws[d].mvp[k];
            memcpy(vpush_m + 64 * (size_t)d, vp, 64);
            md_uniform_buffer(loc + D_VUBO, vpush_pa + 64 * (uint64_t)d, 4);
        }

        /* the jobs: Vertex 2d + 1 (after the previous Tiler job), Tiler 2d + 2 (after its Vertex job) */
        md_draw_t vd = { .state = pa + sb + SO_VRSD, .attribute_buffers = dpa + D_ABUFS, .attributes = pa + sb + SO_ATTR,
                         .varying_buffers = dpa + D_VBUFS, .varyings = pa + sb + SO_VATTR, .thread_storage = pa + sb + SO_LS };
        if (draws[d].mvp) {
            vd.state = dpa + D_VRSD;
            vd.uniform_buffers = dpa + D_VUBO;
            vd.push_uniforms = vpush_pa + 64 * (uint64_t)d;
        }
        job_header(loc + D_VJOB, MD_JOB_VERTEX, 2 * (int)d + 1, d ? 2 * (int)d : 0, dpa + D_TJOB);
        md_vertex_job(loc + D_VJOB, nv, &vd);
        md_draw_t td = { .position = pos_pa + 16 * (uint64_t)first, .state = dpa + D_FRSD,
                         .viewport = pa + sb + SO_VIEWPORT, .uniform_buffers = dpa + D_UBO,
                         .push_uniforms = push_pa + 64 * (uint64_t)d, .thread_storage = pa + sb + SO_LS };
        md_primitive_t prim = { .mode = MD_TRIANGLES, .index_count = nv };
        job_header(loc + D_TJOB, MD_JOB_TILER, 2 * (int)d + 2, 2 * (int)d + 1,
                   d + 1 < ndraws ? dpa + MEM_DRAW_SIZE + D_VJOB : 0);
        md_tiler_job(loc + D_TJOB, &prim, pa + sb + SO_TCTX, &td);
        const uint64_t tb2 = cnt_now();
        k->t2 += tb2 - tb1;
        if (fast_pack)
            copy_slot(draws_m + (size_t)MEM_DRAW_SIZE * d, loc, d);
        else
            copy_nc16(draws_m + (size_t)MEM_DRAW_SIZE * d, loc, sizeof loc);
        k->t3 += cnt_now() - tb2;
        first += (nv + 3) & ~3u;                        /* 64-byte alignment: 4 vertices per step for D_ABUFS/td.position */
    }
}

static void frame_build_ex(uint32_t set, const mali_draw_t *draws, uint32_t ndraws, uint32_t w, uint32_t h,
                           uint64_t target_pa, uint32_t pitch, uint32_t clear,
                           uint8_t *verts_m, uint64_t verts_pa, uint64_t pos_pa,
                           uint8_t *push_m, uint64_t push_pa, uint8_t *vpush_m, uint64_t vpush_pa,
                           uint8_t *draws_m, uint64_t draws_pa, int fast_pack,
                           uint8_t *tv_m, uint64_t tv_pa, uint64_t tvy_pa, int zbuf)
{
    const uint32_t sb = MALI_SET_BASE(set);
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;

    static uint8_t hdr[0x1000];
    memset(hdr, 0, sizeof hdr);
    if (!fast_pack)
        memset(draws_m, 0, (size_t)MEM_DRAW_SIZE * ndraws);
    memcpy(m + sb + SO_VS, BM_VS_bin, sizeof BM_VS_bin);
    memcpy(m + sb + SO_FS, BM_FS_bin, sizeof BM_FS_bin);
    for (uint32_t d = 0; d < ndraws; d++)
        if (draws[d].mvp) {
            memcpy(m + sb + SO_VS3, BM_VS3_bin, sizeof BM_VS3_bin);
            break;
        }
    int any_tex = 0;
    for (uint32_t d = 0; d < ndraws && !any_tex; d++)
        any_tex = (draws[d].uvk != NULL || (draws[d].litv != NULL && draws[d].tex_pa)) && tv_m != NULL;
    if (any_tex) {
        memcpy(m + sb + SO_VS_TEX, BM_VS_TEX_bin, sizeof BM_VS_TEX_bin);
        memcpy(m + sb + SO_FS_TEX, BM_FS_TEX_bin, sizeof BM_FS_TEX_bin);
        memcpy(m + sb + SO_FS_TEXA, BM_FS_TEXA_bin, sizeof BM_FS_TEXA_bin);     /* (the textures with holes) */
    }
    int any_lit = 0;
    for (uint32_t d = 0; d < ndraws && !any_lit; d++)
        any_lit = draws[d].litv != NULL && tv_m != NULL;
    int any_litx = 0;
    for (uint32_t d = 0; d < ndraws && !any_litx; d++)
        any_litx = draws[d].litv != NULL && draws[d].litx && tv_m != NULL;
    int any_d2r = 0, any_d2s = 0;
    for (uint32_t d = 0; d < ndraws; d++) {
        any_d2r |= draws[d].d2 == 1 && tv_m != NULL;
        any_d2s |= draws[d].d2 == 2 && tv_m != NULL;
    }
    if (any_lit)
        memcpy(m + sb + SO_VS_LIT, BM_VS_LIT_bin, sizeof BM_VS_LIT_bin);
    if (any_lit || any_d2r)
        memcpy(m + sb + SO_FS_VCOL, BM_FS_VCOL_bin, sizeof BM_FS_VCOL_bin);
    if (any_d2s)
        memcpy(m + sb + SO_FS_SPR, BM_FS_SPR_bin, sizeof BM_FS_SPR_bin);
    int any_litb = 0;
    for (uint32_t d = 0; d < ndraws && !any_litb; d++)
        any_litb = draws[d].litv != NULL && draws[d].litx == 2 && tv_m != NULL;
    int any_tm = 0;
    for (uint32_t d = 0; d < ndraws && !any_tm; d++)
        any_tm = draws[d].litv != NULL && draws[d].tex_pa && tv_m != NULL;
    if (any_tm)
        memcpy(m + sb + SO_VS_TEXM, BM_VS_TEXM_bin, sizeof BM_VS_TEXM_bin);
    if (any_litx)
        memcpy(m + sb + SO_VS_LITX, BM_VS_LITX_bin, sizeof BM_VS_LITX_bin);
    if (any_litb)
        memcpy(m + sb + SO_VS_LITB, BM_VS_LITB_bin, sizeof BM_VS_LITB_bin);

    /* tiler: the heap and the context */
    md_tiler_heap(hdr + SO_HEAPD, pa + sb + SO_HEAP, HEAP_SIZE);
    md_tiler_context(hdr + SO_TCTX, w, h, HIERARCHY_MASK, pa + sb + SO_HEAPD);

    /* the shaders' states */
    md_renderer_state_t vrs = { .shader = pa + sb + SO_VS, .attribute_count = BM_VS_ATTRIBUTE_COUNT,
                                .varying_count = BM_VS_VARYING_COUNT, .ubo_count = BM_VS_UBO_COUNT,
                                .work_regs = BM_VS_WORK_REGS, .preload_regs = BM_VS_PRELOAD,
                                .push_count = BM_VS_PUSH_COUNT };
    md_renderer_state(hdr + SO_VRSD, &vrs);
    md_renderer_state_t frs = { .fragment = 1, .shader = pa + sb + SO_FS, .attribute_count = BM_FS_ATTRIBUTE_COUNT,
                                .varying_count = BM_FS_VARYING_COUNT, .ubo_count = BM_FS_UBO_COUNT,
                                .work_regs = BM_FS_WORK_REGS, .preload_regs = BM_FS_PRELOAD,
                                .push_count = BM_FS_PUSH_COUNT, .pixel_kill = MD_KILL_WEAK_EARLY,
                                .zs_update = MD_KILL_WEAK_EARLY, .wait6 = BM_FS_WAIT6, .wait7 = BM_FS_WAIT7,
                                .depth_func = zbuf ? MD_FUNC_LEQUAL : 0, .depth_write = zbuf };
    md_local_storage(hdr + SO_LS);
    md_viewport(hdr + SO_VIEWPORT, 0, 0, w, h, 0, 0, w - 1, h - 1);
    md_attribute(hdr + SO_ATTR, 0, MD_FMT_RGBA32F | MD_ORDER_RGBA, 0, 1);
    md_attribute(hdr + SO_VATTR, 1, MD_FMT_SNAP4 | MD_ORDER_RGBA, 0, 0);
    md_attribute(hdr + SO_ATTR + MD_ATTRIBUTE_SIZE, 0, MD_FMT_RGBA32F | MD_ORDER_RGBA, 16, 1);  /* BM_VS_TEX: (u v k) */
    md_attribute(hdr + SO_VATTR + MD_ATTRIBUTE_SIZE, 0, MD_FMT_RGBA32F | MD_ORDER_RGBA, 0, 0); /* its varying 0 */
    md_attribute(hdr + SO_ATTR + 2 * MD_ATTRIBUTE_SIZE, 0, MD_FMT_RGBA32F | MD_ORDER_RGBA, 32, 1);  /* BM_VS_LIT: the base colour */
    md_attribute(hdr + SO_ATTR + 3 * MD_ATTRIBUTE_SIZE, 0, MD_FMT_RGBA32F | MD_ORDER_RGBA, 48, 1);  /* BM_VS_LITX: the face centre */

    uint8_t frs_buf[80], vrs3_buf[64];
    memset(frs_buf, 0, sizeof frs_buf);
    md_renderer_state(frs_buf, &frs);
    md_blend_opaque(frs_buf + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);
    md_renderer_state_t vrs3 = { .shader = pa + sb + SO_VS3, .attribute_count = BM_VS3_ATTRIBUTE_COUNT,
                                 .varying_count = BM_VS3_VARYING_COUNT, .ubo_count = BM_VS3_UBO_COUNT,
                                 .work_regs = BM_VS3_WORK_REGS, .preload_regs = BM_VS3_PRELOAD,
                                 .push_count = BM_VS3_PUSH_COUNT };
    memset(vrs3_buf, 0, sizeof vrs3_buf);
    md_renderer_state(vrs3_buf, &vrs3);
    md_renderer_state_t vrst = { .shader = pa + sb + SO_VS_TEX, .attribute_count = BM_VS_TEX_ATTRIBUTE_COUNT,
                                 .varying_count = BM_VS_TEX_VARYING_COUNT, .ubo_count = BM_VS_TEX_UBO_COUNT,
                                 .work_regs = BM_VS_TEX_WORK_REGS, .preload_regs = BM_VS_TEX_PRELOAD,
                                 .push_count = BM_VS_TEX_PUSH_COUNT };
    md_renderer_state(hdr + SO_VRSDT, &vrst);
    md_renderer_state_t vrsl = { .shader = pa + sb + SO_VS_LIT, .attribute_count = BM_VS_LIT_ATTRIBUTE_COUNT,
                                 .varying_count = BM_VS_LIT_VARYING_COUNT, .ubo_count = BM_VS_LIT_UBO_COUNT,
                                 .work_regs = BM_VS_LIT_WORK_REGS, .preload_regs = BM_VS_LIT_PRELOAD,
                                 .push_count = BM_VS_LIT_PUSH_COUNT };
    md_renderer_state(hdr + SO_VRSDL, &vrsl);
    md_renderer_state_t vrslx = { .shader = pa + sb + SO_VS_LITX, .attribute_count = BM_VS_LITX_ATTRIBUTE_COUNT,
                                  .varying_count = BM_VS_LITX_VARYING_COUNT, .ubo_count = BM_VS_LITX_UBO_COUNT,
                                  .work_regs = BM_VS_LITX_WORK_REGS, .preload_regs = BM_VS_LITX_PRELOAD,
                                  .push_count = BM_VS_LITX_PUSH_COUNT };
    md_renderer_state(hdr + SO_VRSDX, &vrslx);
    md_renderer_state_t vrsm = { .shader = pa + sb + SO_VS_TEXM, .attribute_count = BM_VS_TEXM_ATTRIBUTE_COUNT,
                                 .varying_count = BM_VS_TEXM_VARYING_COUNT, .ubo_count = BM_VS_TEXM_UBO_COUNT,
                                 .work_regs = BM_VS_TEXM_WORK_REGS, .preload_regs = BM_VS_TEXM_PRELOAD,
                                 .push_count = BM_VS_TEXM_PUSH_COUNT };
    md_renderer_state(hdr + SO_VRSDM, &vrsm);
    md_renderer_state_t vrslb = { .shader = pa + sb + SO_VS_LITB, .attribute_count = BM_VS_LITB_ATTRIBUTE_COUNT,
                                  .varying_count = BM_VS_LITB_VARYING_COUNT, .ubo_count = BM_VS_LITB_UBO_COUNT,
                                  .work_regs = BM_VS_LITB_WORK_REGS, .preload_regs = BM_VS_LITB_PRELOAD,
                                  .push_count = BM_VS_LITB_PUSH_COUNT };
    md_renderer_state(hdr + SO_VRSDB, &vrslb);
    uint8_t frsl_buf[80];
    memset(frsl_buf, 0, sizeof frsl_buf);
    md_renderer_state_t frsl = { .fragment = 1, .shader = pa + sb + SO_FS_VCOL, .attribute_count = BM_FS_VCOL_ATTRIBUTE_COUNT,
                                 .varying_count = BM_FS_VCOL_VARYING_COUNT, .ubo_count = BM_FS_VCOL_UBO_COUNT,
                                 .work_regs = BM_FS_VCOL_WORK_REGS, .preload_regs = BM_FS_VCOL_PRELOAD,
                                 .push_count = BM_FS_VCOL_PUSH_COUNT, .pixel_kill = MD_KILL_WEAK_EARLY,
                                 .zs_update = MD_KILL_WEAK_EARLY, .wait6 = BM_FS_VCOL_WAIT6, .wait7 = BM_FS_VCOL_WAIT7,
                                 .depth_func = zbuf ? MD_FUNC_LEQUAL : 0, .depth_write = zbuf };
    md_renderer_state(frsl_buf, &frsl);
    md_blend_opaque(frsl_buf + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);
    uint8_t frsd_buf[80], frsp_buf[80];         /* the 2D: the colour shader and the sprite shader, neither tests or writes depth */
    memset(frsd_buf, 0, sizeof frsd_buf);
    memset(frsp_buf, 0, sizeof frsp_buf);
    frsl.depth_func = 0;
    frsl.depth_write = 0;
    md_renderer_state(frsd_buf, &frsl);
    md_blend_opaque(frsd_buf + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);
    md_renderer_state_t frsp = { .fragment = 1, .shader = pa + sb + SO_FS_SPR, .attribute_count = BM_FS_SPR_ATTRIBUTE_COUNT,
                                 .varying_count = BM_FS_SPR_VARYING_COUNT, .ubo_count = BM_FS_SPR_UBO_COUNT,
                                 .sampler_count = 1, .texture_count = 1,
                                 .work_regs = BM_FS_SPR_WORK_REGS, .preload_regs = BM_FS_SPR_PRELOAD,
                                 .push_count = BM_FS_SPR_PUSH_COUNT, .pixel_kill = MD_KILL_WEAK_EARLY,
                                 .zs_update = MD_KILL_WEAK_EARLY, .wait6 = BM_FS_SPR_WAIT6, .wait7 = BM_FS_SPR_WAIT7 };
    md_renderer_state(frsp_buf, &frsp);
    md_blend_opaque(frsp_buf + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);
    uint8_t frst_buf[80];
    memset(frst_buf, 0, sizeof frst_buf);
    md_renderer_state_t frst = { .fragment = 1, .shader = pa + sb + SO_FS_TEX, .attribute_count = BM_FS_TEX_ATTRIBUTE_COUNT,
                                 .varying_count = BM_FS_TEX_VARYING_COUNT, .ubo_count = BM_FS_TEX_UBO_COUNT,
                                 .sampler_count = 1, .texture_count = 1,
                                 .work_regs = BM_FS_TEX_WORK_REGS, .preload_regs = BM_FS_TEX_PRELOAD,
                                 .push_count = BM_FS_TEX_PUSH_COUNT, .pixel_kill = MD_KILL_WEAK_EARLY,
                                 .zs_update = MD_KILL_WEAK_EARLY, .wait6 = BM_FS_TEX_WAIT6, .wait7 = BM_FS_TEX_WAIT7,
                                 .depth_func = zbuf ? MD_FUNC_LEQUAL : 0, .depth_write = zbuf };
    md_renderer_state(frst_buf, &frst);
    md_blend_opaque(frst_buf + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);
    /* the same with holes: the shader kills a texel with alpha 0, so the depth is written after the shader (force late) */
    uint8_t frsta_buf[80];
    memset(frsta_buf, 0, sizeof frsta_buf);
    md_renderer_state_t frsta = { .fragment = 1, .shader = pa + sb + SO_FS_TEXA, .attribute_count = BM_FS_TEXA_ATTRIBUTE_COUNT,
                                  .varying_count = BM_FS_TEXA_VARYING_COUNT, .ubo_count = BM_FS_TEXA_UBO_COUNT,
                                  .sampler_count = 1, .texture_count = 1,
                                  .work_regs = BM_FS_TEXA_WORK_REGS, .preload_regs = BM_FS_TEXA_PRELOAD,
                                  .push_count = BM_FS_TEXA_PUSH_COUNT, .pixel_kill = MD_KILL_FORCE_LATE,
                                  .zs_update = MD_KILL_FORCE_LATE, .wait6 = BM_FS_TEXA_WAIT6, .wait7 = BM_FS_TEXA_WAIT7,
                                  .depth_func = zbuf ? MD_FUNC_LEQUAL : 0, .depth_write = zbuf };
    md_renderer_state(frsta_buf, &frsta);
    md_blend_opaque(frsta_buf + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);

    bctx_t bc = { .draws = draws, .ndraws = ndraws, .pa = pa, .dpa0 = draws_pa, .sb = sb, .verts_m = verts_m,
                  .push_m = push_m, .vpush_m = vpush_m, .draws_m = draws_m, .verts_pa = verts_pa, .pos_pa = pos_pa,
                  .push_pa = push_pa, .vpush_pa = vpush_pa, .frs_buf = frs_buf, .vrs3_buf = vrs3_buf,
                  .frst_buf = frst_buf, .frsta_buf = frsta_buf, .tv_m = tv_m, .tv_pa = tv_pa, .tvy_pa = tvy_pa, .fast_pack = fast_pack,
                  .lv_m = tv_m ? tv_m - 0x720000 + 0x80000 : NULL, .lv_pa = tv_pa - 0x720000 + 0x80000,   /* BE_LIT_FROM_TV, BE_LIT_V_OFF */
                  .litp_m = tv_m ? tv_m - 0x720000 : NULL, .litp_pa = tv_pa - 0x720000,
                  .texa_m = tv_m ? tv_m - 0x720000 + 0x180000 : NULL, .texa_pa = tv_pa - 0x720000 + 0x180000,   /* after the lit area's 0x180000 bytes */
                  .lv2_m = tv_m ? m - BE_R2_BELOW + 0x80000 : NULL, .lv2_pa = pa - BE_R2_BELOW + 0x80000, .tvy2_pa = pa - BE_R2_BELOW,
                  .mem = m, .mem_pa = pa,
                  .frsl_buf = frsl_buf, .frsd_buf = frsd_buf, .frsp_buf = frsp_buf, .zbuf = zbuf };
    static bblk_t blk[MALI_BUILD_CORES];
    void *blks[MALI_BUILD_CORES];
    unsigned nb = 1;
    if (mp_par && mp_workers && fast_pack && ndraws >= MALI_PAR_MIN_DRAWS) {
        nb = (unsigned)mp_workers() + 1;
        if (nb > MALI_BUILD_CORES)
            nb = MALI_BUILD_CORES;
    }
    uint32_t first = 0;
    for (unsigned i = 0, d = 0; i < nb; i++) {
        blk[i].c = &bc;
        blk[i].d0 = d;
        blk[i].first = first;
        const uint32_t d1 = ndraws * (i + 1) / nb;
        for (; d < d1; d++)
            first += (3 * draws[d].n + 3) & ~3u;
        blk[i].d1 = d1;
        blks[i] = &blk[i];
    }
    if (nb > 1)
        mp_par(build_draws, blks, nb);
    else
        build_draws(blks[0]);
    for (unsigned i = 0; i < nb; i++) {
        t_b1 += blk[i].t1;
        t_b2 += blk[i].t2;
        t_b3 += blk[i].t3;
    }

    /* the fragment job: the clear's descriptor with the tiler's context */
    samples(hdr + SO_SAMPLES, be_aa_frame);
    fbd(hdr + SO_FBD, pa + sb + SO_SAMPLES, w, h, target_pa, pitch, clear, be_aa_frame);
    put64(hdr + SO_FBD + 56, pa + sb + SO_TCTX);
    job_fragment(hdr + SO_FJOB, w, h, pa + sb + SO_FBD);
    memcpy(m + sb, hdr, sizeof hdr);
}

static void frame_build(uint32_t set, const mali_draw_t *draws, uint32_t ndraws, uint32_t w, uint32_t h,
                        uint64_t target_pa, uint32_t pitch, uint32_t clear)
{
    const uint32_t sb = MALI_SET_BASE(set);
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    const uint32_t verts = sb + (MALI_FRAME_VERTS - MALI_SET_BASE(0)),
                   positions = sb + (MALI_FRAME_POSITIONS - MALI_SET_BASE(0)),
                   push = sb + (MALI_FRAME_PUSH - MALI_SET_BASE(0)),
                   vpush = sb + (MALI_FRAME_VPUSH - MALI_SET_BASE(0));
    frame_build_ex(set, draws, ndraws, w, h, target_pa, pitch, clear,
                   m + verts, pa + verts, pa + positions,
                   m + push, pa + push, m + vpush, pa + vpush,
                   m + sb + SO_DRAWS, pa + sb + SO_DRAWS, 0, NULL, 0, 0, 0);
}

#define TRI_X0      ((H->screen_w - 112) & ~15u)
#define TRI_Y0      96u
#define TRI_CLEAR   0x102050u

/* the triangle's three jobs, the tables and the fragment job's descriptor into hw->mem: set 0, one draw, the screen as target */
static void tri_build(void)
{
    static const float v[3][3] = { { 8, 88, 0.5f }, { 88, 88, 0.5f }, { 48, 8, 0.5f } };
    const mali_draw_t d = { .xyz = v, .n = 1, .rgb = 0xff8019 };
    frame_build(0, &d, 1, TRI, TRI, H->screen_pa + (uint64_t)TRI_Y0 * H->screen_pitch + TRI_X0 * 4, H->screen_pitch, TRI_CLEAR);
}

void mali_tri_build(const mali_hw_t *hw)
{
    H = hw;
    tri_build();
}

static void vertex_words(void)
{
    uint32_t out[4];
    for (int k = 0; k < 4; k++)
        out[k] = rd32(H->mem + MALI_FRAME_POSITIONS + 4 * k);
    ksnprintf(line, sizeof line, "mali: the vertex job wrote %08x %08x %08x %08x (8.0 88.0 0.5 1.0 are 41000000 "
              "42b00000 3f000000 3f800000)", (unsigned)out[0], (unsigned)out[1], (unsigned)out[2], (unsigned)out[3]);
    say();
}

/* build the jobs and run the vertex and tiler jobs. n < 0: the base path; else variant n of mv_table.
 * 0 done, -1 failed (lines said); *st is JS_STATUS */
static int tri_try(mali_info_t *in, int n, uint32_t *st)
{
    const uint64_t pa = H->mem_pa;
    uint64_t head = pa + MV_VJOB;
    char what[48];
    uint32_t us;

    tri_build();
    if (n >= 0) {
        mv_apply(H->mem, pa, n);
        if (mv_table[n].flags & MV_TILER_ONLY) {
            memcpy(H->mem + MALI_FRAME_POSITIONS, H->mem + MALI_FRAME_VERTS, 3 * 16);   /* what the vertex job would write */
            head = pa + MV_TJOB;
        }
        ksnprintf(what, sizeof what, "variant %d %s", n, mv_table[n].name);
    } else {
        ksnprintf(what, sizeof what, "a vertex and a tiler job");
    }
    if (run_on(1, head, &us, st, what) != 0)
        return -1;
    in->tri_us += us;
    if (n == -1 || n == 0)
        vertex_words();
    return 0;
}

/* after a failed job: reset, cores and MMU again, so the next try starts from a known GPU; 0 or -1 */
static int tri_recover(const mali_info_t *in)
{
    return reset() == 0 && cores(in) == 0 && mmu() == 0 ? 0 : -1;
}

/* 0 drawn, 1 the jobs ended but the triangle is not there, -1 failed.
 * The base path first; if its jobs fail, every variant of mv_table runs once and says its result (the first
 * one of them that ends, other than the two that run one job alone, goes on to the fragment job). */
static int triangle(mali_info_t *in)
{
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    uint32_t st;
    int pick = -1;

    in->tri_us = 0;
    if (tri_try(in, -1, &st) != 0) {
        ksnprintf(line, sizeof line, "mali: variant base: JS_STATUS 0x%02x", (unsigned)st);
        say();
        if (tri_recover(in) != 0) {
            ksnprintf(line, sizeof line, "mali: tri_recover failed after base failure");
            say();
            return -1;
        }
        for (int n = 0; n < MV_COUNT; n++) {
            const int bad = tri_try(in, n, &st) != 0;
            if (bad)
                ksnprintf(line, sizeof line, "mali: variant %d %s: JS_STATUS 0x%02x", n, mv_table[n].name,
                          (unsigned)st);
            else
                ksnprintf(line, sizeof line, "mali: variant %d %s: ok", n, mv_table[n].name);
            say();
            if (bad && tri_recover(in) != 0) {
                ksnprintf(line, sizeof line, "mali: tri_recover failed after variant %d (%s)", n, mv_table[n].name);
                say();
                return -1;
            }
            if (!bad && pick < 0 && !(mv_table[n].flags & MV_ISOLATE))
                pick = n;
        }
        if (pick < 0)
            return -1;
        ksnprintf(line, sizeof line, "mali: variant %d %s goes on to the fragment job", pick, mv_table[pick].name);
        say();
        if (tri_try(in, pick, &st) != 0)
            return -1;
    }
    uint32_t us2;
    if (run_on(0, pa + MALI_SET_BASE(0) + SO_FJOB, &us2, &st, "the fragment job of the triangle") != 0)
        return -1;
    in->tri_us += us2;
    const uint8_t *screen = H->screen + (size_t)TRI_Y0 * H->screen_pitch + TRI_X0 * 4;
    in->tri_in = rd32(screen + (size_t)61 * H->screen_pitch + 48 * 4) & 0xffffff;
    in->tri_out = rd32(screen + (size_t)3 * H->screen_pitch + 3 * 4) & 0xffffff;
    ksnprintf(line, sizeof line, "mali: a triangle: inside %06x, outside %06x (the clear is %06x, the colour ff8019), "
              "heap bottom %08x%08x", (unsigned)in->tri_in, (unsigned)in->tri_out, (unsigned)TRI_CLEAR,
              (unsigned)rd32(m + MALI_SET_BASE(0) + SO_HEAPD + 20), (unsigned)rd32(m + MALI_SET_BASE(0) + SO_HEAPD + 16));
    say();
    return in->tri_in != TRI_CLEAR && in->tri_out == TRI_CLEAR ? 0 : 1;
}

static int probed_ok;
static int frames_step(void);

/* why the draws are refused (NULL: good); *tris their triangles */
static const char *draws_check(const mali_draw_t *draws, uint32_t ndraws, uint32_t *tris)
{
    *tris = 0;
    if (ndraws == 0 || ndraws > MALI_MAX_DRAWS)
        return "the number of draws is 0 or over the maximum";
    if (!draws)
        return "no target or no draws";
    for (uint32_t d = 0; d < ndraws; d++) {
        if (draws[d].n == 0 || !draws[d].xyz)
            return "a draw of no triangles";
        if ((*tris += draws[d].n) > MALI_BATCH_MAX)
            return "more than MALI_BATCH_MAX triangles";
    }
    return NULL;
}

int mali_frame(const mali_hw_t *hw, uint8_t *target, uint64_t target_pa, uint32_t pitch, uint32_t w, uint32_t h,
               uint32_t clear, const mali_draw_t *draws, uint32_t ndraws)
{
    const char *why = NULL;
    uint32_t tris = 0;
    if (!probed_ok || hw != H)
        why = "no good probe with this hw";
    else if (hw->mem_size < MALI_SET_BASE(1))
        why = "the GPU memory is smaller than set 0";
    else if (frames_step() != 0)
        why = "frames are in flight (mali_frame_submit): wait for them";
    else if (!target)
        why = "no target or no draws";
    else if (w == 0 || h == 0 || (w & 15) || (h & 15) || w > 1920 || h > 1080 || pitch < w * 4 || (pitch & 3))
        why = "the surface is not as stated (w, h multiples of 16 up to 1920 x 1080, pitch at least 4 w)";
    else if (target_pa & 63)
        why = "target_pa is not 64-aligned";
    if (!why)
        why = draws_check(draws, ndraws, &tris);
    if (why) {
        ksnprintf(line, sizeof line, "mali: frame refused: %s", why);
        say();
        return -1;
    }
    frame_build(0, draws, ndraws, w, h, target_pa, pitch, clear);
    uint32_t us, st, us2;
    if (run_on(1, H->mem_pa + MALI_SET_BASE(0) + SO_DRAWS, &us, &st, "the vertex and tiler jobs of a frame") != 0 ||
        run_on(0, H->mem_pa + MALI_SET_BASE(0) + SO_FJOB, &us2, &st, "the fragment job of a frame") != 0)
        return -1;
    /* the centre of the first triangle's bounding box (transformed by mvp when present) */
    float p0[3][2];
    for (int i = 0; i < 3; i++) {
        const float *v = draws[0].xyz[i], *m = draws[0].mvp;
        if (m) {
            const float wc = m[12] * v[0] + m[13] * v[1] + m[14] * v[2] + m[15];
            const float iw = wc != 0.0f ? 1.0f / wc : 1.0f;
            p0[i][0] = (m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3]) * iw;
            p0[i][1] = (m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7]) * iw;
        } else {
            p0[i][0] = v[0];
            p0[i][1] = v[1];
        }
    }
    float lo[2] = { p0[0][0], p0[0][1] }, hi[2] = { lo[0], lo[1] };
    for (int i = 1; i < 3; i++)
        for (int k = 0; k < 2; k++) {
            if (p0[i][k] < lo[k]) lo[k] = p0[i][k];
            if (p0[i][k] > hi[k]) hi[k] = p0[i][k];
        }
    uint32_t cx = (uint32_t)((lo[0] + hi[0]) / 2), cy = (uint32_t)((lo[1] + hi[1]) / 2);
    if (cx >= w) cx = w - 1;
    if (cy >= h) cy = h - 1;
    const uint32_t px = rd32(target + (size_t)cy * pitch + cx * 4) & 0xffffff;
    ksnprintf(line, sizeof line, "mali: frame of %u draws, %u triangles: %u + %u us, the first triangle's centre is %06x "
              "(the clear is %06x)", (unsigned)ndraws, (unsigned)tris, (unsigned)us, (unsigned)us2, (unsigned)px,
              (unsigned)(clear & 0xffffff));
    say();
    return px != (clear & 0xffffff) ? 0 : 1;
}

int mali_probe_vs3(const mali_hw_t *hw)
{
    if (!probed_ok || hw != H || !H->screen_pa || !H->screen || H->screen_w < 128 || H->screen_h < 208)
        return -1;
    static const float v0[3][3] = { { 4, 44, 0.25f }, { 44, 44, 0.25f }, { 24, 4, 0.25f } };
    static const float v1[3][3] = { { 64, 36, 0.5f }, { 92, 36, 0.5f }, { 78, 8, 0.5f } };
    static const float mvp[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 2.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const mali_draw_t d[2] = {
        { .xyz = v0, .n = 1, .rgb = 0x20a0ff, .mvp = mvp },
        { .xyz = v1, .n = 1, .rgb = 0xff8019, .mvp = NULL },
    };
    uint8_t *target = H->screen + (size_t)TRI_Y0 * H->screen_pitch + TRI_X0 * 4;
    const uint64_t target_pa = H->screen_pa + (uint64_t)TRI_Y0 * H->screen_pitch + TRI_X0 * 4;
    const int r = mali_frame(hw, target, target_pa, H->screen_pitch, TRI, TRI, TRI_CLEAR, d, 2);
    uint32_t out[4];
    for (int k = 0; k < 4; k++)
        out[k] = rd32(H->mem + MALI_FRAME_POSITIONS + 4 * k);
    ksnprintf(line, sizeof line, "mali: BM_VS3 vertex 0: %08x %08x %08x %08x (8.0 88.0 0.5 1.0 are 41000000 42b00000 "
              "3f000000 3f800000)", (unsigned)out[0], (unsigned)out[1], (unsigned)out[2], (unsigned)out[3]);
    say();
    const uint32_t p_vs3 = rd32(target + (size_t)61 * H->screen_pitch + 48 * 4) & 0xffffff;
    const uint32_t p_vs = rd32(target + (size_t)26 * H->screen_pitch + 78 * 4) & 0xffffff;
    const uint32_t p_out = rd32(target + (size_t)3 * H->screen_pitch + 3 * 4) & 0xffffff;
    const int ok = r == 0 && p_vs3 == 0x20a0ff && p_vs == 0xff8019 && p_out == TRI_CLEAR;
    ksnprintf(line, sizeof line, "mali: phase 3 (BM_VS3 4x4 matrix): vs3 %06x (want 20a0ff), vs %06x (want ff8019), "
              "outside %06x (clear %06x): %s", (unsigned)p_vs3, (unsigned)p_vs, (unsigned)p_out, (unsigned)TRI_CLEAR,
              ok ? "ok" : "failed");
    say();
    return ok ? 0 : -1;
}

int mali_probe_async(const mali_hw_t *hw)
{
    if (!probed_ok || hw != H || hw->mem_size < MALI_SET_BASE(2))
        return -1;
    static const float v0[3][3] = { { 8, 88, 0.5f }, { 88, 88, 0.5f }, { 48, 8, 0.5f } };
    static const float v1[3][3] = { { 4, 44, 0.25f }, { 44, 44, 0.25f }, { 24, 4, 0.25f } };
    static const float mvp[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 2.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const mali_draw_t d0 = { .xyz = v0, .n = 1, .rgb = 0x40e080, .mvp = NULL };
    const mali_draw_t d1 = { .xyz = v1, .n = 1, .rgb = 0xe040a0, .mvp = mvp };
    uint8_t *s0 = NULL, *s1 = NULL;
    uint32_t p0 = 0, p1 = 0;
    const uint32_t t0 = H->us();
    const int r0 = mali_frame_submit(hw, TRI, TRI, 0x102050, &d0, 1, &s0, &p0);
    const int r1 = mali_frame_submit(hw, TRI, TRI, 0x301040, &d1, 1, &s1, &p1);
    const int rw = (r0 == 0 && r1 == 0) ? mali_frame_wait(100000) : -1;
    const uint32_t dt = H->us() - t0;
    const uint32_t s0_in = s0 ? (rd32(s0 + (size_t)61 * p0 + 48 * 4) & 0xffffff) : 0;
    const uint32_t s0_out = s0 ? (rd32(s0 + (size_t)3 * p0 + 3 * 4) & 0xffffff) : 0;
    const uint32_t s1_in = s1 ? (rd32(s1 + (size_t)61 * p1 + 48 * 4) & 0xffffff) : 0;
    const uint32_t s1_out = s1 ? (rd32(s1 + (size_t)3 * p1 + 3 * 4) & 0xffffff) : 0;
    const int ok = rw == 0 && s0_in == 0x40e080 && s0_out == 0x102050 && s1_in == 0xe040a0 && s1_out == 0x301040;
    ksnprintf(line, sizeof line, "mali: phase 4 (2 async frames in flight, sets 0 & 1): %u us, s0 %06x/%06x (want "
              "40e080/102050), s1 %06x/%06x (want e040a0/301040): %s", (unsigned)dt, (unsigned)s0_in,
              (unsigned)s0_out, (unsigned)s1_in, (unsigned)s1_out, ok ? "ok" : "failed");
    say();
    return ok ? 0 : -1;
}

/* --- frames in flight (M41 phase 4) ---
 * A set goes QUEUED (built, waiting for slot 1) -> VT (the vertex and tiler jobs on slot 1) -> VT_DONE (waiting for slot 0)
 * -> FRAG (the fragment job on slot 0) -> DONE. A slot runs one set's chain at a time, so the second set's vertex work
 * overlaps the first one's fragment job, and the ARM builds the next frame meanwhile. The state machine moves only when
 * the caller polls (mali_frame_busy / mali_frame_wait / the next submit). */
enum { SET_IDLE, SET_QUEUED, SET_VT, SET_VT_DONE, SET_FRAG, SET_DONE };

static struct {
    int state;
    uint32_t seq;                       /* the order of submission: the older set gets the slot first */
    uint32_t pitch;
} sets[2];
static uint32_t set_seq;
static int slot_owner[2] = { -1, -1 };  /* the set whose chain runs on the slot (-1: none) */
static int fault_seen;                  /* a job failed since mali_frame_wait last said so */

static void set_fail(int s, int js, const char *what, uint32_t status, uint32_t raw)
{
    job_fail(js, what, status, raw);
    sets[s].state = SET_DONE;
    slot_owner[js] = -1;
    fault_seen = 1;
}

/* the set to start next on a slot: the oldest in `state` */
static int oldest(int state)
{
    int best = -1;
    for (int s = 0; s < 2; s++)
        if (sets[s].state == state && (best < 0 || (int32_t)(sets[s].seq - sets[best].seq) < 0))
            best = s;
    return best;
}

static void set_start_vt(int s)
{
    job_kick(1, H->mem_pa + MALI_SET_BASE(s) + SO_DRAWS);
    sets[s].state = SET_VT;
    slot_owner[1] = s;
}

/* moves every set as far as the GPU has got; the number of sets still drawing */
static int frames_step(void)
{
    uint32_t st = 0, raw = 0;
    for (int s = 0; s < 2; s++) {
        if (sets[s].state == SET_VT) {
            const int r = job_poll(1, &st, &raw);
            if (r > 0) {
                sets[s].state = SET_VT_DONE;
                slot_owner[1] = -1;
            } else if (r < 0) {
                set_fail(s, 1, "the vertex and tiler jobs of a queued frame", st, raw);
            }
        } else if (sets[s].state == SET_FRAG) {
            const int r = job_poll(0, &st, &raw);
            if (r > 0) {
                sets[s].state = SET_DONE;
                slot_owner[0] = -1;
            } else if (r < 0) {
                set_fail(s, 0, "the fragment job of a queued frame", st, raw);
            }
        }
    }
    int s = oldest(SET_VT_DONE);
    if (s >= 0 && slot_owner[0] < 0) {                  /* slot 0 is free: this set's fragment job */
        job_kick(0, H->mem_pa + MALI_SET_BASE(s) + SO_FJOB);
        sets[s].state = SET_FRAG;
        slot_owner[0] = s;
    }
    s = oldest(SET_QUEUED);
    if (s >= 0 && slot_owner[1] < 0)
        set_start_vt(s);
    int n = 0;
    for (s = 0; s < 2; s++)
        n += sets[s].state != SET_IDLE && sets[s].state != SET_DONE;
    return n;
}

int mali_frame_busy(void)
{
    return probed_ok ? frames_step() : 0;
}

int mali_frame_wait(uint32_t timeout_us)
{
    if (!probed_ok)
        return 0;
    const uint32_t t0 = H->us();
    while (frames_step() != 0)
        if (H->us() - t0 >= timeout_us)
            return -1;
    const int bad = fault_seen;
    fault_seen = 0;
    return bad ? -1 : 0;
}

int mali_frame_submit(const mali_hw_t *hw, uint32_t w, uint32_t h, uint32_t clear, const mali_draw_t *draws,
                      uint32_t ndraws, uint8_t **surface, uint32_t *pitch)
{
    const char *why = NULL;
    uint32_t tris = 0;
    const uint32_t pit = (w * 4 + 63) & ~63u;
    if (!probed_ok || hw != H)
        why = "no good probe with this hw";
    else if (hw->mem_size < MALI_SET_BASE(2))
        why = "the GPU memory is smaller than two sets";
    else if (!surface || !pitch)
        why = "no surface or pitch pointer";
    else if (w == 0 || h == 0 || (w & 15) || (h & 15) || w > 1920 || h > 1080)
        why = "the surface is not as stated (w, h multiples of 16)";
    else if ((uint64_t)pit * h > MALI_SET_SURF_MAX)
        why = "the surface is too big for a set (368 x 368 at most): use mali_frame";
    else
        why = draws_check(draws, ndraws, &tris);
    if (why) {
        ksnprintf(line, sizeof line, "mali: frame submit refused: %s", why);
        say();
        return -1;
    }
    frames_step();                                      /* retires the frames that have ended */
    int s = -1;
    for (int i = 0; i < 2 && s < 0; i++)
        if (sets[i].state == SET_IDLE || sets[i].state == SET_DONE)
            s = i;
    if (s < 0)
        return -1;                                      /* both sets busy */
    sets[s].seq = set_seq++;
    sets[s].pitch = pit;
    uint8_t *surf = H->mem + MALI_SET_BASE(s) + SO_SURF;
    frame_build(s, draws, ndraws, w, h, H->mem_pa + MALI_SET_BASE(s) + SO_SURF, pit, clear);
    sets[s].state = SET_QUEUED;
    if (slot_owner[1] < 0 && oldest(SET_QUEUED) == s)   /* slot 1 is free: the vertex and tiler jobs start now */
        set_start_vt(s);
    *surface = surf;
    *pitch = pit;
    return 0;
}

int mali_probe(const mali_hw_t *hw, mali_info_t *info)
{
    static mali_info_t dummy;
    mali_info_t *in = info ? info : &dummy;
    memset(in, 0, sizeof *in);
    H = hw;
    memset(sets, 0, sizeof sets);                       /* (a new probe resets the GPU: nothing is in flight) */
    slot_owner[0] = slot_owner[1] = -1;
    fault_seen = 0;
    if (supply(in) != 0) {
        in->step = MALI_E_SUPPLY;
        return MALI_E_SUPPLY;
    }
    int r = MALI_E_POWER;
    if (power() == 0 && (r = MALI_E_ID, identify(in) == 0) && (r = MALI_E_RESET, reset() == 0) &&
        (r = MALI_E_CORES, cores(in) == 0) && (r = MALI_E_MMU, mmu() == 0) && (r = jobs(in)) == MALI_OK)
        r = fragment(in);
    probed_ok = r == MALI_OK;
    if (r == MALI_OK && H->triangle && H->screen_pa && H->screen && H->screen_w >= 128 && H->screen_h >= 208) {
        const int t = triangle(in);     /* (the clear's square stays usable even if this fails) */
        ksnprintf(line, sizeof line, "mali: a triangle (vertex, tiler and fragment jobs): %s",
                  t == 0 ? "drawn by the GPU" : t > 0 ? "the jobs ended, the triangle is not there" : "failed");
        say();
        if (t != 0)
            r = MALI_E_TRIANGLE;
    }
    in->step = r;
    return r;
}

/* --- the r3d backend (M41 phases 2, 3, 4) ---
 * r3d hands this backend flat-coloured, depth-written triangles and meshes (colour_only, W13b). With the vertex shader on
 * (be_vs_on), be_mesh culls back faces and lights faces per bone in object space, passing the 4x4 bone matrix in mvp so
 * BM_VS3 transforms the vertices on the GPU. Triangles are sorted far to near (painter's order: the Mali draws without a
 * depth test here), drawn into an XRGB surface cleared to a key colour, and the covered pixels are copied into the RGB565
 * page. With the queue on (be_q_on), mali_backend_submit kicks the Fragment job on slot 0 and returns so the ARM can run
 * game logic in parallel until mali_backend_sync resolves the surface. */
#define BE_SURF_OFF     0x200000    /* the surface in hw->mem when it does not fit set 0's (frames are never in flight then) */
#define BE_KEY          0xFF00FF    /* "not covered" */
#define BE_MAX_TRIS     32768
#define BE_MAX_MVPS     2048
/* G1, textures: the arena's tail (after the draw slots, 0x320000) holds the textured draws' vertices (32 bytes each: position
 * and u v k) and varyings (16 bytes each), BE_TEXV_MAX vertices (slots of the frame's vertex index) at most; the sheets'
 * texture blocks go to a pool of 2 MB below the arena. */
#define BE_TV_OFF       0x320000
#define BE_TVY_OFF      0x3a0000
#define BE_TEXV_MAX     16384
/* Q2: the lit meshes' data is in 2 MB below the texture pool, [mem - 10 MB, mem - 8 MB): the uniform words of the draws, 448 bytes
 * each (1024 draws, 0x70000), then the vertices, 48 or 64 bytes each (BE_TEXV_MAX slots, to 0x100000). The arena stops at mem - 2 MB, where
 * the resolve surface starts. build_draws gets the area as offsets from the textured vertices (tv_m = arena + BE_TV_OFF). */
#define BE_LIT_FROM_TV  0x720000    /* the lit area is this many bytes below tv_m: 4 MB + BE_TV_OFF */
#define BE_LIT_V_OFF    0x80000     /* the vertices after the uniform words (1024 draws of 448 bytes: 0x70000) */
#define BE_TPOOL_SIZE   (2u << 20)
#define BE_TEX_SLOTS    16

typedef struct { float v[3][3]; float key; uint32_t rgb; const float *mvp; float uvk[3][3]; uint64_t tex_pa; uint8_t proj, nb; const float *light; } be_tri_t;

static be_tri_t be_tris[BE_MAX_TRIS];
static float be_mvps[BE_MAX_MVPS][16];
/* the face cache: the vertices of the faces of a lit mesh made once (the mesh, its version, the detail, the kind of light and the colours
 * name the entry), so a frame only tests the faces and copies the visible ones. Kind 0 (sun and ambient light) has vertices of 12
 * floats, kinds 1 and 2 (the effects, the skeleton) of 16. Kind 3 (a textured mesh, BM_VS_TEXM) has 12 floats: position, normal and
 * emissive flag, then u v of the sheet in 0..1. A block is the 3 vertices of a face. */
#define BE_RM_SLOTS     12
#define BE_RM_POOL      (3u << 18)              /* floats: 3 MB */
#define BE_INST_MAX     256
typedef struct {
    const r3d_mesh_t *m;
    uint32_t version, colsum;
    int nfaces, detail, kind, unlit;
    uint32_t nshown;
    float *blk;
    const void *tex;                            /* kind 3: the sheet, and the scale of its u v */
    float iw, ih;
    int smooth;                                 /* kind 3: the normals are the vertices' (Gouraud light) */
} be_rm_t;
typedef struct {
    const be_rm_t *rm;
    const float *mvp, *light;
    int kind, nb;
    uint32_t nvis, vis0;
    uint64_t tex_pa;                            /* kind 3: the sheet's descriptor */
} be_inst_t;
static be_rm_t be_rms[BE_RM_SLOTS];
static int be_nrm;
static uint32_t be_rm_used;
static float be_rm_pool[BE_RM_POOL] __attribute__((aligned(64)));
static be_inst_t be_insts[BE_INST_MAX];
static int be_ninst;
#define BE_VIS_MAX      65536                   /* the faces of the cached meshes in a batch (a frame holds 10922 visible ones: the flush splits) */
static uint16_t be_vis[BE_VIS_MAX];
static uint32_t be_nvis;
static int be_res_on = 1;                       /* mali_res=0: no face cache */
static int be_msaa;                             /* mali_msaa=1: 4x MSAA */
void mali_backend_set_msaa(int on)      { be_msaa = on != 0; md_set_msaa(be_msaa); }
void mali_backend_set_res(int on)       { be_res_on = on != 0; }

static float be_lit[BE_MAX_TRIS][12];             /* Q2: the face data of a lit triangle (normal, emissive, base colour), by triangle */
static float be_lights[BE_MAX_MVPS][88];         /* Q2: the light of the bone with the matrix be_mvps[i] */
static int be_lit_on = 1;                        /* mali_lit=0: the ARM lights the faces */
static float be_lit_min = 100.0f;                 /* mali_lit_min: the smallest average face (pixels) lit by the vertex shader */
void mali_backend_set_lit(int on, int min_px) { be_lit_on = on != 0; be_lit_min = min_px > 0 ? (float)min_px : 100.0f; }
static int be_n, be_nmvp;
static int be_2d_on;                            /* mali_2d=1: the Mali draws the 2D over the 3D (off: the ARM is faster at it) */
void mali_backend_set_2d(int on)        { be_2d_on = on != 0; }
#define BE_2D_MAX       3072
typedef struct {                                /* an item of the 2D over the 3D: a rectangle or a sprite */
    int kind;                                   /* 1 rectangle, 2 sprite */
    float x0, y0, x1, y1;                       /* the page's pixels, cut to the clip rectangle */
    float r, g, b;                              /* a rectangle's colour 0..1; a sprite's u0, v0, u1 .. see v1 */
    float u0, v0, u1, v1;
    uint64_t tex_pa;
} be_2d_t;
static be_2d_t be_2ds[BE_2D_MAX];
static int be_n2d;
static int be_vs_on = 2, be_q_on = 1;
static int be_z_on;                             /* mali_z=1: the hardware depth buffer replaces sort_tris (G2) */
static int be_zfr;                              /* the frame being run has the depth test */
void mali_backend_set_zbuffer(int on)   { be_z_on = on != 0; }
static gpu3d_stats_t be_stats;

static uint32_t be_wait_us;
static int be_pend;
static g16_t be_pend_gc;                        /* the page of the frame in flight as it was at the submit (the live g16_t moves to the next page) */
static g16_t *be_pend_g;
static uint8_t *be_pend_surf;
static uint32_t be_pend_pitch, be_pend_t0, be_pend_us, be_pend_tris;
static const mali_hw_t *be_hw;
static int (*be_frame)(const mali_hw_t *, uint8_t *, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                       const mali_draw_t *, uint32_t);

void mali_backend_set_frame(int (*frame)(const mali_hw_t *, uint8_t *, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                         const mali_draw_t *, uint32_t))
{
    be_frame = frame;
}

void mali_backend_set_vshader(int on)   { be_vs_on = on < 0 ? 0 : on > 2 ? 2 : on; }
int mali_backend_vshader(void)          { return be_vs_on; }
void mali_backend_set_queue(int on)     { be_q_on = on < 0 ? 0 : on > 2 ? 2 : on; }
int mali_backend_queue(void)            { return be_q_on; }
int mali_backend_inflight(void)         { return be_pend; }
/* waits for the frame in flight only when it is on this page: with each frame shown a frame later, the frame held is waited for and the
 * next one keeps drawing */
int mali_backend_sync_page(const void *px)
{
    if (!be_pend || (const void *)be_pend_gc.px != px)
        return 0;
    return mali_backend_sync();
}
uint32_t mali_backend_wait_us(void)     { return be_wait_us; }

void mali_backend_stats(gpu3d_stats_t *s, int reset)
{
    if (s) {
        *s = be_stats;
        uint64_t f = cnt_freq();
        if (!f)
            f = 24000000u;
        s->mesh_us = (uint32_t)(t_mesh * 1000000u / f);
        s->flush_us = (uint32_t)(t_flush * 1000000u / f);
        s->resolve_us = (uint32_t)(t_resolve * 1000000u / f);
        s->sort_us = (uint32_t)(t_sort * 1000000u / f);
        s->build_us = (uint32_t)(t_build * 1000000u / f);
        s->jobs_us = (uint32_t)(t_jobs * 1000000u / f);
        s->b1_us = (uint32_t)(t_b1 * 1000000u / f);
        s->b2_us = (uint32_t)(t_b2 * 1000000u / f);
        s->b3_us = (uint32_t)(t_b3 * 1000000u / f);
    }
    if (reset) {
        memset(&be_stats, 0, sizeof be_stats);
        t_mesh = t_flush = t_resolve = t_sort = t_build = t_jobs = t_b1 = t_b2 = t_b3 = 0;
        be_wait_us = 0;
    }
}

int mali_backend_pending(void)
{
    return be_n + be_n2d + be_ninst;
}

/* drains any in-flight slot 0 fragment job without resolving */
static void be_drain_pend(void)
{
    if (!be_pend)
        return;
    if (probed_ok && H) {
        uint32_t st = 0, raw = 0;
        const uint32_t t0 = H->us();
        while (!job_poll(0, &st, &raw) && H->us() - t0 < 100000)
            ;
        gw(MMU_INT_CLEAR, 0xffffffffu);
    }
    be_pend = 0;
}

void mali_backend_drop(void)
{
    be_drain_pend();
    be_n = 0;
    be_n2d = 0;
    be_ninst = 0;
    be_nvis = 0;
    be_nmvp = 0;
}

/* the pixels of the surface that are not the key, into the page (8 pixels at a time via 64-bit loads) */
static void be_resolve_impl(const g16_t *g, const uint8_t *surf, uint32_t pitch);
static void be_resolve_aa(const g16_t *g, const uint8_t *surf, uint32_t pitch);
static void be_resolve(const g16_t *g, const uint8_t *surf, uint32_t pitch);
static void be_resolve_x(const g16_t *g, const uint8_t *surf, uint32_t pitch)
{
    if (be_aa_frame)
        be_resolve_aa(g, surf, pitch);
    else
        be_resolve_impl(g, surf, pitch);
}
/* A cls() with the GPU drawing the 3D leaves the page to the backend (runtime.c cls_settle): the first resolve after it fills the page
 * with the colour, then copies the covered pixels. A frame split in several jobs resolves its first jobs before the ARM would fill the
 * page, and the fill would cover them. */
static int be_page_u, be_page_cleared;
static uint16_t be_page_c;
static int be_pend_u, be_res_u;                 /* the clear of the frame in flight, and of the frame being resolved: a request belongs to its frame */
static uint16_t be_pend_c, be_res_c;

static void be_take_page(void)
{
    be_res_u = be_page_u;
    be_res_c = be_page_c;
    be_page_u = 0;
}

void mali_backend_page(int uniform, uint16_t c)
{
    be_page_u = uniform != 0;
    be_page_c = c;
}

int mali_backend_cleared(void)
{
    const int r = be_page_cleared;
    be_page_cleared = 0;
    be_page_u = 0;                              /* (the caller fills the page itself when this says 0) */
    return r;
}

static void be_resolve(const g16_t *g, const uint8_t *surf, uint32_t pitch)
{
    const uint64_t t0 = cnt_now();
    if (be_res_u) {
        g16_cls((g16_t *)g, be_res_c);
        be_res_u = 0;
        if (!be_page_u)                         /* (a newer request, for the next page, is not answered by this one) */
            be_page_cleared = 1;
    }
    be_resolve_x(g, surf, pitch);
    t_resolve += cnt_now() - t0;
}

static void be_resolve_impl(const g16_t *g, const uint8_t *surf, uint32_t pitch)
{
    const uint64_t key2 = (uint64_t)BE_KEY | ((uint64_t)BE_KEY << 32);
    const uint64_t mask = 0x00FFFFFF00FFFFFFull;
    const int w = g->w, h = g->h, w8 = w & ~7;
#ifdef PLAT_RK3566
    if (!be_frame && H && surf < H->mem) {
        const uintptr_t s0 = (uintptr_t)surf & ~63ull;
        const uintptr_t s1 = ((uintptr_t)surf + (size_t)pitch * (size_t)h + 63ull) & ~63ull;
        __asm__ volatile("dsb sy" ::: "memory");
        for (uintptr_t a = s0; a < s1; a += 64)
            __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
        __asm__ volatile("dsb sy" ::: "memory");
    }
#endif
    for (int y = 0; y < h; y++) {
        const uint8_t *row = surf + (size_t)y * pitch;
        uint16_t *dst = g->px + (size_t)y * g->stride;
        int x = 0;
        for (; x < w8; x += 8) {
            uint64_t a, b, c, d;
            memcpy(&a, row + 4 * (size_t)x, 8);
            memcpy(&b, row + 4 * (size_t)x + 8, 8);
            memcpy(&c, row + 4 * (size_t)x + 16, 8);
            memcpy(&d, row + 4 * (size_t)x + 24, 8);
            a &= mask;
            b &= mask;
            c &= mask;
            d &= mask;
            if (((a ^ key2) | (b ^ key2) | (c ^ key2) | (d ^ key2)) == 0)
                continue;
            if (((a ^ key2) | (b ^ key2)) != 0) {
                const uint32_t p0 = (uint32_t)a, p1 = (uint32_t)(a >> 32);
                const uint32_t p2 = (uint32_t)b, p3 = (uint32_t)(b >> 32);
                if (p0 != BE_KEY)
                    dst[x + 0] = (uint16_t)((p0 >> 19 & 31) << 11 | (p0 >> 10 & 63) << 5 | (p0 >> 3 & 31));
                if (p1 != BE_KEY)
                    dst[x + 1] = (uint16_t)((p1 >> 19 & 31) << 11 | (p1 >> 10 & 63) << 5 | (p1 >> 3 & 31));
                if (p2 != BE_KEY)
                    dst[x + 2] = (uint16_t)((p2 >> 19 & 31) << 11 | (p2 >> 10 & 63) << 5 | (p2 >> 3 & 31));
                if (p3 != BE_KEY)
                    dst[x + 3] = (uint16_t)((p3 >> 19 & 31) << 11 | (p3 >> 10 & 63) << 5 | (p3 >> 3 & 31));
            }
            if (((c ^ key2) | (d ^ key2)) != 0) {
                const uint32_t p4 = (uint32_t)c, p5 = (uint32_t)(c >> 32);
                const uint32_t p6 = (uint32_t)d, p7 = (uint32_t)(d >> 32);
                if (p4 != BE_KEY)
                    dst[x + 4] = (uint16_t)((p4 >> 19 & 31) << 11 | (p4 >> 10 & 63) << 5 | (p4 >> 3 & 31));
                if (p5 != BE_KEY)
                    dst[x + 5] = (uint16_t)((p5 >> 19 & 31) << 11 | (p5 >> 10 & 63) << 5 | (p5 >> 3 & 31));
                if (p6 != BE_KEY)
                    dst[x + 6] = (uint16_t)((p6 >> 19 & 31) << 11 | (p6 >> 10 & 63) << 5 | (p6 >> 3 & 31));
                if (p7 != BE_KEY)
                    dst[x + 7] = (uint16_t)((p7 >> 19 & 31) << 11 | (p7 >> 10 & 63) << 5 | (p7 >> 3 & 31));
            }
        }
        for (; x < w; x++) {
            const uint32_t p = rd32(row + 4 * (size_t)x) & 0xFFFFFF;
            if (p != BE_KEY)
                dst[x] = (uint16_t)((p >> 19 & 31) << 11 | (p >> 10 & 63) << 5 | (p >> 3 & 31));
        }
    }
}

/* 4x MSAA: byte 3 of a surface pixel is the coverage (0: not covered, 255: covered), the colour is premultiplied by it: a pixel
 * on an edge is the colour plus the page's pixel times what is left */
static inline void be_aa_pixel(uint16_t *d, uint32_t p)
{
    const uint32_t a = p >> 24;
    if (!a)
        return;
    if (a >= 254) {
        *d = (uint16_t)((p >> 19 & 31) << 11 | (p >> 10 & 63) << 5 | (p >> 3 & 31));
        return;
    }
    const uint32_t q = *d, inv = 256 - a;        /* (x * inv) >> 8: within 1 of the exact blend, and no division */
    const uint32_t pr = (q >> 11) << 3 | (q >> 13), pg = (q >> 5 & 63) << 2 | (q >> 9 & 3), pb = (q & 31) << 3 | (q >> 2 & 7);
    uint32_t r = (p >> 16 & 255) + ((pr * inv) >> 8);
    uint32_t gg = (p >> 8 & 255) + ((pg * inv) >> 8);
    uint32_t b = (p & 255) + ((pb * inv) >> 8);
    r = r > 255 ? 255 : r;
    gg = gg > 255 ? 255 : gg;
    b = b > 255 ? 255 : b;
    *d = (uint16_t)((r >> 3) << 11 | (gg >> 2) << 5 | (b >> 3));
}

static void be_resolve_aa(const g16_t *g, const uint8_t *surf, uint32_t pitch)
{
    const int w = g->w, h = g->h, w8 = w & ~7;
#ifdef PLAT_RK3566
    if (H && surf < H->mem) {
        const uintptr_t s0 = (uintptr_t)surf & ~63ull;
        const uintptr_t s1 = ((uintptr_t)surf + (size_t)pitch * (size_t)h + 63ull) & ~63ull;
        __asm__ volatile("dsb sy" ::: "memory");
        for (uintptr_t a = s0; a < s1; a += 64)
            __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
        __asm__ volatile("dsb sy" ::: "memory");
    }
#endif
    const uint64_t amask = 0xFF000000FF000000ull, abit = 0x0100000001000000ull;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = surf + (size_t)y * pitch;
        uint16_t *dst = g->px + (size_t)y * g->stride;
        int x = 0;
        for (; x < w8; x += 8) {
            uint64_t v[4];
            memcpy(&v[0], row + 4 * (size_t)x, 8);
            memcpy(&v[1], row + 4 * (size_t)x + 8, 8);
            memcpy(&v[2], row + 4 * (size_t)x + 16, 8);
            memcpy(&v[3], row + 4 * (size_t)x + 24, 8);
            if (!((v[0] | v[1] | v[2] | v[3]) & amask))
                continue;
            if ((((v[0] | abit) & (v[1] | abit) & (v[2] | abit) & (v[3] | abit)) & amask) == amask) {
                /* every pixel is covered (alpha 254 or 255, the same as be_aa_pixel): no tests */
#ifdef PLAT_RK3566
                const uint8x8x4_t q = vld4_u8(row + 4 * (size_t)x);        /* B G R A of 8 pixels */
                const uint16x8_t o = vorrq_u16(vorrq_u16(vshlq_n_u16(vmovl_u8(vshr_n_u8(q.val[2], 3)), 11),
                                                         vshlq_n_u16(vmovl_u8(vshr_n_u8(q.val[1], 2)), 5)),
                                               vmovl_u8(vshr_n_u8(q.val[0], 3)));
                vst1q_u16(dst + x, o);
#else
                for (int k = 0; k < 4; k++) {
                    const uint32_t lo = (uint32_t)v[k], hi = (uint32_t)(v[k] >> 32);
                    dst[x + 2 * k] = (uint16_t)((lo >> 19 & 31) << 11 | (lo >> 10 & 63) << 5 | (lo >> 3 & 31));
                    dst[x + 2 * k + 1] = (uint16_t)((hi >> 19 & 31) << 11 | (hi >> 10 & 63) << 5 | (hi >> 3 & 31));
                }
#endif
                continue;
            }
            for (int k = 0; k < 4; k++) {
                be_aa_pixel(dst + x + 2 * k, (uint32_t)v[k]);
                be_aa_pixel(dst + x + 2 * k + 1, (uint32_t)(v[k] >> 32));
            }
        }
        for (; x < w; x++)
            be_aa_pixel(dst + x, rd32(row + 4 * (size_t)x));
    }
}

int mali_backend_sync(void)
{
    if (!be_pend)
        return 0;
    be_pend = 0;
    if (!probed_ok || !H)
        return -1;
    uint32_t st = 0, raw = 0;
    const uint32_t w0 = H->us();
    int r;
    while (!(r = job_poll(0, &st, &raw)) && H->us() - w0 < 100000)
        ;
    const uint32_t wait = H->us() - w0;
    const uint32_t us2 = H->us() - be_pend_t0;
    if (!r)
        st = gr(JS(0, JS_STATUS));
    if (r <= 0) {
        job_fail(0, "the fragment job of a frame", st, raw);
        return -1;
    }
    be_stats.jobs++;
    be_stats.tris += be_pend_tris;
    be_stats.bin_us += be_pend_us;
    be_stats.render_us += us2;
    if (be_pend_us + us2 > be_stats.max_us)
        be_stats.max_us = be_pend_us + us2;
    be_wait_us += be_pend_us + wait;
    be_res_u = be_pend_u;
    be_res_c = be_pend_c;
    be_pend_u = 0;
    be_resolve(be_pend_g, be_pend_surf, be_pend_pitch);
    return 0;
}

#define BE_HW_MAX_DRAWS 1024

/* one frame of the draws so far, resolved into the page (or kicked on slot 0 when async); 0, or -1 */
static int be_run(const g16_t *g, uint8_t *surf, uint64_t pa, uint32_t pitch, uint32_t sw, uint32_t sh,
                  const mali_draw_t *dr, uint32_t nd, int async)
{
    uint32_t btris = 0;
    for (uint32_t d = 0; d < nd; d++)
        btris += dr[d].n;
    if (be_frame) {
        if (be_frame(be_hw, surf, pa, pitch, sw, sh, BE_KEY, dr, nd) < 0)   /* 0 and 1 both count as done */
            return -1;
        be_stats.jobs++;
        be_stats.draws += nd;
        be_stats.tris += btris;
        be_take_page();
        be_resolve(g, surf, pitch);
        return 0;
    }
    if (!probed_ok || be_hw != H || H->mem_size < MALI_SET_BASE(1) || frames_step() != 0)
        return -1;
    uint64_t vt_head = H->mem_pa + MALI_SET_BASE(0) + SO_DRAWS;
#ifdef PLAT_RK3566
    if (H->map_pa > 0 && H->mem_pa >= H->map_pa + (6u << 20)) {
        uint8_t *ab = H->mem - (6u << 20);
        const uint64_t apa = H->mem_pa - (6u << 20);
        const uint64_t tb = cnt_now();
        be_aa_frame = be_msaa;
        frame_build_ex(0, dr, nd, sw, sh, pa, pitch, BE_KEY,
                       ab + 0x000000, apa + 0x000000, apa + 0x080000,
                       ab + 0x100000, apa + 0x100000, ab + 0x110000, apa + 0x110000,
                       ab + 0x120000, apa + 0x120000, 1, ab + BE_TV_OFF, apa + BE_TV_OFF, apa + BE_TVY_OFF, be_zfr);
        t_build += cnt_now() - tb;
        vt_head = apa + 0x120000;
    } else
#endif
    {
        const uint64_t tb = cnt_now();
        be_aa_frame = 0;
        frame_build(0, dr, nd, sw, sh, pa, pitch, BE_KEY);
        t_build += cnt_now() - tb;
    }
    uint32_t us = 0, st = 0, us2 = 0;
    const uint64_t tj0 = cnt_now();
    if (run_on(1, vt_head, &us, &st, "the vertex and tiler jobs of a frame") != 0)
        return -1;
    t_jobs += cnt_now() - tj0;
    if (async && be_q_on) {
        gw(MMU_INT_CLEAR, 0xffffffffu);
        be_pend_t0 = H->us();
        job_kick(0, H->mem_pa + MALI_SET_BASE(0) + SO_FJOB);
        be_pend = 1;
        be_pend_u = be_page_u;
        be_pend_c = be_page_c;
        be_page_u = 0;
        be_pend_gc = *g;
        be_pend_g = &be_pend_gc;
        be_pend_surf = surf;
        be_pend_pitch = pitch;
        be_pend_us = us;
        be_pend_tris = btris;
        return 0;
    }
    const uint64_t tj1 = cnt_now();
    if (run_on(0, H->mem_pa + MALI_SET_BASE(0) + SO_FJOB, &us2, &st, "the fragment job of a frame") != 0)
        return -1;
    t_jobs += cnt_now() - tj1;
    be_stats.jobs++;
    be_stats.draws += nd;
    be_stats.tris += btris;
    be_stats.bin_us += us;
    be_stats.render_us += us2;
    if (us + us2 > be_stats.max_us)
        be_stats.max_us = us + us2;
    be_wait_us += us + us2;
    be_take_page();
    be_resolve(g, surf, pitch);
    return 0;
}

static int tri_after_mvp(const be_tri_t *a, const be_tri_t *b)
{
    if (a->key != b->key)
        return a->key > b->key;
    if (a->mvp != b->mvp)
        return (uintptr_t)a->mvp > (uintptr_t)b->mvp;
    if (a->mvp != NULL)
        return a->rgb > b->rgb;
    return 0;
}

static int tri_after_rgb(const be_tri_t *a, const be_tri_t *b)
{
    if (a->key != b->key)
        return a->key > b->key;
    return a->rgb > b->rgb;
}

static int tri_after_rgbonly(const be_tri_t *a, const be_tri_t *b)
{
    if (a->rgb != b->rgb)
        return a->rgb > b->rgb;
    return a->mvp > b->mvp;
}

static void sort_tris(int *idx, int *tmp, int n, int (*after)(const be_tri_t *, const be_tri_t *))
{
    if (n <= 32) {
        for (int i = 1; i < n; i++) {
            const int k = idx[i];
            int j = i - 1;
            while (j >= 0 && after(&be_tris[idx[j]], &be_tris[k])) {
                idx[j + 1] = idx[j];
                j--;
            }
            idx[j + 1] = k;
        }
        return;
    }
    int *src = idx, *dst = tmp;
    for (int width = 1; width < n; width <<= 1) {
        for (int i = 0; i < n; i += 2 * width) {
            int l = i, r = i + width < n ? i + width : n;
            const int rend = i + 2 * width < n ? i + 2 * width : n, lend = r;
            int out = i;
            while (l < lend && r < rend) {
                if (after(&be_tris[src[l]], &be_tris[src[r]]))
                    dst[out++] = src[r++];
                else
                    dst[out++] = src[l++];
            }
            while (l < lend)
                dst[out++] = src[l++];
            while (r < rend)
                dst[out++] = src[r++];
        }
        int *swp = src;
        src = dst;
        dst = swp;
    }
    if (src != idx)
        memcpy(idx, src, (size_t)n * sizeof(int));
}

/* G2: the depth test works when the backend has the arena (the frame tables of 6 MB) */
static int be_zmode(void)
{
#ifdef PLAT_RK3566
    return be_z_on && !be_frame && be_hw && be_hw->map_pa > 0 && be_hw->mem_pa >= be_hw->map_pa + (6u << 20);
#else
    return 0;
#endif
}

/* the triangles with a matrix go to screen space (x, y, 1/depth): same-colour triangles of different meshes can merge */
static void be_project_mvp(int n)
{
    for (int i = 0; i < n; i++) {
        be_tri_t *t = &be_tris[i];
        const float *m = t->mvp;
        if (!m || t->light)                     /* a lit triangle has no colour of its own: the shader makes it */
            continue;
        for (int k = 0; k < 3; k++) {
            const float vx = t->v[k][0], vy = t->v[k][1], vz = t->v[k][2];
            const float wc = m[12] * vx + m[13] * vy + m[14] * vz + m[15];
            const float iw = wc != 0.0f ? 1.0f / wc : 1.0f;
            t->v[k][0] = (m[0] * vx + m[1] * vy + m[2] * vz + m[3]) * iw;
            t->v[k][1] = (m[4] * vx + m[5] * vy + m[6] * vz + m[7]) * iw;
            t->v[k][2] = iw;                        /* 1/depth, as the triangles of be_tri */
        }
        t->mvp = NULL;
        t->proj = 1;
    }
}

/* the bytes of a lit draw's uniform words (64-aligned): the push order of BM_VS_LIT and the 416 bytes of BM_VS_LITX take 448, a skinned
 * mesh takes its mesh block (240) and 192 a bone */
static uint32_t be_lit_bytes(int litx, int nb)
{
    return litx == 2 ? (240u + 192u * (uint32_t)nb + 63u) & ~63u : 448u;
}

/* the triangles in the order idx fit one frame: the draws (runs of one colour and matrix), the vertex slots */
static int z_fits(const int *idx, int n, uint32_t max_draws, uint32_t x_runs, uint32_t x_slots, uint32_t x_lu)
{
    uint32_t runs = x_runs, slots = x_slots, run_n = 0, lu = x_lu;
    int tex = x_runs != 0;
    for (int i = 0; i < n; i++) {
        const be_tri_t *t = &be_tris[idx[i]];
        const be_tri_t *p = i ? &be_tris[idx[i - 1]] : NULL;
        tex |= t->tex_pa != 0 || t->light != NULL;
        if (p && p->rgb == t->rgb && p->mvp == t->mvp) {
            slots -= (3 * run_n + 3) & ~3u;
            run_n++;
        } else {
            runs++;
            run_n = 1;
            if (t->light)
                lu += be_lit_bytes(t->rgb == 0x03000000u ? 1 : t->rgb == 0x04000000u ? 2 : 0, t->nb);
        }
        slots += (3 * run_n + 3) & ~3u;
    }
    return runs <= max_draws && (!tex || slots <= BE_TEXV_MAX) && lu <= BE_LIT_V_OFF;
}

static int be_flush_mode_impl(const g16_t *g, int async);
static int be_flush_mode(const g16_t *g, int async)
{
    const uint64_t t0 = cnt_now();
    const int r = be_flush_mode_impl(g, async);
    t_flush += cnt_now() - t0;
    return r;
}

static int be_flush_mode_impl(const g16_t *g, int async)
{
    static float vbuf[BE_MAX_TRIS * 3][3];
    static float uvbuf[BE_MAX_TRIS * 3][3];
    static float litbuf[BE_MAX_TRIS * 3][12];
    static mali_draw_t dr[BE_HW_MAX_DRAWS];
    static int idx[BE_MAX_TRIS], tmp_idx[BE_MAX_TRIS];
    static uint64_t spr_tex[BE_HW_MAX_DRAWS][16];     /* the sheets of the sprite draws */
    if (be_pend && mali_backend_sync() != 0)
        return -1;
    const int n = be_n, n2 = be_n2d, ninst = be_ninst;
    if (n == 0 && n2 == 0 && ninst == 0)
        return 0;
    be_n = 0;
    be_n2d = 0;
    be_ninst = 0;
    be_nvis = 0;
    be_nmvp = 0;
    const mali_hw_t *hw = be_hw;
    const uint32_t sw = ((uint32_t)g->w + 15) & ~15u, sh = ((uint32_t)g->h + 15) & ~15u;
    const uint32_t pitch = (sw * 4 + 63) & ~63u;
    if (!hw || sw > 1920 || sh > 1080)
        return -1;
    const int in_set = (uint64_t)pitch * sh <= MALI_SET_SURF_MAX && hw->mem_size >= MALI_SET_BASE(1);
    const uint32_t surf_off = in_set ? MALI_SET_BASE(0) + MALI_SET_SURFACE : BE_SURF_OFF;
    if (surf_off + (uint64_t)pitch * sh > hw->mem_size)
        return -1;
    uint32_t max_draws = MALI_MAX_DRAWS, max_tris = MALI_BATCH_MAX;
#ifdef PLAT_RK3566
    if (!be_frame && hw->map_pa > 0 && hw->mem_pa >= hw->map_pa + (6u << 20)) {
        max_draws = BE_HW_MAX_DRAWS;
        max_tris = BE_MAX_TRIS;
    }
#endif
    int any_mvp = 0;
    for (int i = 0; i < n; i++) {
        idx[i] = i;
        be_tris[i].proj = 0;
        if (be_tris[i].mvp)
            any_mvp = 1;
    }
    /* G2: with the depth buffer the order of the triangles does not matter, but one frame has to hold them all (a second
     * frame has no depth of the first). Try the order as it came, then grouped by colour; else sort far to near. */
    int zfr = 0;
    uint32_t x_slots = 0, x_lu = 0;                 /* what the cached meshes take of a frame */
    for (int ii = 0; ii < ninst; ii++) {
        /* (the vertices of the cached meshes are in the area R2: 32768 of them, checked where the draws are made) */
        x_lu += be_lit_bytes(be_insts[ii].kind, be_insts[ii].nb);
    }
    if (be_zmode()) {
        zfr = z_fits(idx, n, max_draws, (uint32_t)ninst, x_slots, x_lu);
        if (!zfr) {
            be_project_mvp(n);
            any_mvp = 0;
            const uint64_t tz = cnt_now();
            sort_tris(idx, tmp_idx, n, tri_after_rgbonly);
            t_sort += cnt_now() - tz;
            zfr = z_fits(idx, n, max_draws, (uint32_t)ninst, x_slots, x_lu);
            if (!zfr)                           /* back to the sort: the projected triangles had z = 0.5 before */
                for (int i = 0; i < n; i++)
                    if (be_tris[i].proj)
                        for (int k = 0; k < 3; k++)
                            be_tris[i].v[k][2] = 0.5f;
        }
    }
    be_zfr = zfr;
    if (!zfr) {
    const uint64_t ts = cnt_now();
    sort_tris(idx, tmp_idx, n, tri_after_mvp);
    t_sort += cnt_now() - ts;
    }
    if (any_mvp && !zfr) {
        int runs = 1;
        for (int i = 1; i < n; i++) {
            const be_tri_t *p = &be_tris[idx[i - 1]], *c = &be_tris[idx[i]];
            if (p->rgb != c->rgb || p->mvp != c->mvp)
                runs++;
        }
        /* When many small meshes overflow max_draws only because each mesh has its own mvp matrix,
         * projecting their vertices into screen space lets same-colour triangles merge across meshes into one
         * GPU frame instead of running multiple full-surface clear + resolve passes. */
        if (runs > (int)max_draws) {
            be_project_mvp(n);
            for (int i = 0; i < n; i++)
                if (be_tris[i].proj)
                    for (int k = 0; k < 3; k++)
                        be_tris[i].v[k][2] = 0.5f;
            const uint64_t ts2 = cnt_now();
            sort_tris(idx, tmp_idx, n, tri_after_rgb);
            t_sort += cnt_now() - ts2;
        }
    }
    uint8_t *surf = hw->mem + surf_off;
    uint64_t pa = hw->mem_pa + surf_off;
#ifdef PLAT_RK3566
    if (!be_frame && hw->map_pa > 0 && hw->mem_pa >= hw->map_pa + (2u << 20) && (uint64_t)pitch * sh <= (2u << 20)) {
        surf = hw->mem - (2u << 20);
        pa = hw->mem_pa - (2u << 20);
    }
#endif
    uint32_t nd = 0, nv = 0, btri = 0, slots = 0;   /* slots: the frame's vertex index (draws padded to 4 vertices) */
    uint32_t lu_used = 0;                           /* bytes of the lit area's uniform words used by the draws */
    for (int i = 0; i < n; i++) {
        const be_tri_t *t = &be_tris[idx[i]];
        const int tex = t->tex_pa != 0 || t->light != NULL;
        /* a textured or lit draw's vertices live in arrays of BE_TEXV_MAX slots: the frame ends before it passes them */
        const uint32_t dslots = nd > 0 && dr[nd - 1].n ? (3 * dr[nd - 1].n + 3) & ~3u : 0;
        const uint32_t next_slots = (nd > 0 && dr[nd - 1].rgb == t->rgb && dr[nd - 1].mvp == t->mvp)
                                        ? slots - dslots + ((3 * (dr[nd - 1].n + 1) + 3) & ~3u)
                                        : slots + 4;
        if (btri == max_tris || next_slots > (tex ? BE_TEXV_MAX : 32768u)) {   /* (flat vertices: 32768 slots at the arena start) */
            if (be_run(g, surf, pa, pitch, sw, sh, dr, nd, 0) != 0)
                return -1;
            nd = 0;
            nv = 0;
            btri = 0;
            slots = 0;
        }
        if (nd > 0 && dr[nd - 1].rgb == t->rgb && dr[nd - 1].mvp == t->mvp) {
            slots -= (3 * dr[nd - 1].n + 3) & ~3u;
            dr[nd - 1].n++;
            slots += (3 * dr[nd - 1].n + 3) & ~3u;
        } else {
            if (nd == max_draws || (t->light && lu_used + be_lit_bytes(t->rgb == 0x03000000u ? 1 : t->rgb == 0x04000000u ? 2 : 0, t->nb) > BE_LIT_V_OFF)) {   /* a full frame: draw it synchronously, go on with a fresh one */
                if (be_run(g, surf, pa, pitch, sw, sh, dr, nd, 0) != 0)
                    return -1;
                nd = 0;
                nv = 0;
                btri = 0;
                slots = 0;
                lu_used = 0;
            }
            dr[nd].xyz = (const float (*)[3])vbuf[nv];
            dr[nd].d2 = 0;
            dr[nd].res_blk = NULL;
            dr[nd].ntex = 0;
            dr[nd].n = 1;
            dr[nd].rgb = t->rgb;
            dr[nd].mvp = t->mvp;
            dr[nd].uvk = tex ? (const float (*)[3])uvbuf[nv] : NULL;
            dr[nd].tex_pa = t->tex_pa;
            dr[nd].litv = t->light ? (const float (*)[12])litbuf[nv] : NULL;
            dr[nd].litx = t->rgb == 0x03000000u ? 1 : t->rgb == 0x04000000u ? 2 : 0;
            dr[nd].nbones = t->nb;
            dr[nd].uoff = lu_used;
            lu_used += t->light ? be_lit_bytes(dr[nd].litx, t->nb) : 0;
            dr[nd].light = t->light;
            nd++;
            slots += 4;
        }
        memcpy(vbuf[nv], t->v, sizeof t->v);
        if (t->tex_pa)
            memcpy(uvbuf[nv], t->uvk, sizeof t->uvk);
        if (t->light)
            for (int q = 0; q < 3; q++)
                memcpy(litbuf[nv + q], be_lit[idx[i]], sizeof be_lit[0]);
        nv += 3;
        btri++;
    }
    /* the cached meshes: a draw each, with the depth test (the frame has it) */
    if (ninst) {
        if (!zfr && nd > 0) {                       /* the sorted triangles end their frames first */
            if (be_run(g, surf, pa, pitch, sw, sh, dr, nd, 0) != 0)
                return -1;
            nd = nv = btri = slots = lu_used = 0;
        }
        be_zfr = 1;
        /* Far to near: when the meshes need more than one frame, a later frame has no depth of the earlier ones and the nearer
         * meshes must come last. m[15] is the depth of the mesh origin (insertion sort: a stable order for the same depth). */
        for (int ii = 1; ii < ninst; ii++) {
            const be_inst_t t = be_insts[ii];
            int jj = ii;
            while (jj > 0 && be_insts[jj - 1].mvp[15] < t.mvp[15]) {
                be_insts[jj] = be_insts[jj - 1];
                jj--;
            }
            be_insts[jj] = t;
        }
        for (int ii = 0; ii < ninst; ii++) {
            const be_inst_t *q = &be_insts[ii];
            const uint32_t need = be_lit_bytes(q->kind, q->nb), sl = (3 * q->nvis + 3) & ~3u;
            if (nd == max_draws || slots + sl > 32768u || btri + q->nvis > BE_VIS_MAX || lu_used + need > BE_LIT_V_OFF) {
                if (nd > 0 && be_run(g, surf, pa, pitch, sw, sh, dr, nd, 0) != 0)
                    return -1;
                nd = nv = btri = slots = lu_used = 0;
            }
            dr[nd].xyz = (const float (*)[3])vbuf[0];       /* (not read) */
            dr[nd].n = q->nvis;
            dr[nd].rgb = 0x06000000u;
            dr[nd].mvp = q->mvp;
            dr[nd].uvk = NULL;
            dr[nd].tex_pa = q->tex_pa;
            dr[nd].litv = (const float (*)[12])litbuf[0];   /* (not read: marks a lit draw) */
            dr[nd].light = q->light;
            dr[nd].litx = q->kind == 3 ? 0 : q->kind;
            dr[nd].nbones = q->nb;
            dr[nd].d2 = 0;
            dr[nd].ntex = 0;
            dr[nd].uoff = lu_used;
            dr[nd].res_blk = q->rm->blk;
            dr[nd].res_idx = be_vis + q->vis0;
            lu_used += need;
            slots += sl;
            btri += q->nvis;
            nd++;
        }
    }
    /* the 2D, in the order called, after the 3D and without a depth test: the quads of one colour kind or one sheet make a draw */
    for (int i = 0; i < n2; i++) {
        const be_2d_t *q = &be_2ds[i];
        int same = nd > 0 && dr[nd - 1].d2 == q->kind;
        int li = -1;                            /* the sprite's sheet in the draw's list */
        if (same && q->kind == 2) {
            for (uint32_t kx = 0; kx < dr[nd - 1].ntex; kx++)
                if (spr_tex[nd - 1][kx] == q->tex_pa)
                    li = (int)kx;
            if (li < 0 && dr[nd - 1].ntex >= 16)
                same = 0;                       /* the list is full: a new draw */
        }
        uint32_t nslots = same ? slots - ((3 * dr[nd - 1].n + 3) & ~3u) + ((3 * (dr[nd - 1].n + 2) + 3) & ~3u) : slots + 8;
        if (btri + 2 > max_tris || nslots > BE_TEXV_MAX || (!same && (nd == max_draws || (q->kind == 2 && nd >= 880)))) {   /* a full frame: the 2D has no depth to keep */
            if (nd > 0 && be_run(g, surf, pa, pitch, sw, sh, dr, nd, 0) != 0)
                return -1;
            nd = nv = btri = slots = lu_used = 0;
            same = 0;
            li = -1;
            nslots = 8;
        }
        if (same) {
            dr[nd - 1].n += 2;
            if (q->kind == 2 && li < 0) {
                li = (int)dr[nd - 1].ntex++;
                spr_tex[nd - 1][li] = q->tex_pa;
            }
        } else {
            dr[nd].xyz = (const float (*)[3])vbuf[nv];
            dr[nd].n = 2;
            dr[nd].rgb = 0x05000000u | (uint32_t)q->kind;
            dr[nd].mvp = NULL;
            dr[nd].uvk = (const float (*)[3])uvbuf[nv];
            dr[nd].tex_pa = q->tex_pa;
            dr[nd].litv = NULL;
            dr[nd].light = NULL;
            dr[nd].litx = 0;
            dr[nd].nbones = 0;
            dr[nd].d2 = q->kind;
            dr[nd].res_blk = NULL;
            dr[nd].ntex = 0;
            dr[nd].texs = spr_tex[nd];
            if (q->kind == 2) {
                spr_tex[nd][0] = q->tex_pa;
                dr[nd].ntex = 1;
                li = 0;
            }
            nd++;
        }
        slots = nslots;
        const float xs[6] = { q->x0, q->x1, q->x1, q->x0, q->x1, q->x0 }, ys[6] = { q->y0, q->y0, q->y1, q->y0, q->y1, q->y1 };
        const float us[6] = { q->u0, q->u1, q->u1, q->u0, q->u1, q->u0 }, vs[6] = { q->v0, q->v0, q->v1, q->v0, q->v1, q->v1 };
        for (int k = 0; k < 6; k++) {
            vbuf[nv + k][0] = xs[k];
            vbuf[nv + k][1] = ys[k];
            vbuf[nv + k][2] = 1.0f;
            if (q->kind == 1) {
                uvbuf[nv + k][0] = q->r;
                uvbuf[nv + k][1] = q->g;
                uvbuf[nv + k][2] = q->b;
            } else {
                uvbuf[nv + k][0] = us[k];
                uvbuf[nv + k][1] = vs[k];
                uvbuf[nv + k][2] = (float)li;
            }
        }
        nv += 6;
        btri += 2;
        be_stats.quads2d++;
    }
    if (nd == 0)
        return 0;
    return be_run(g, surf, pa, pitch, sw, sh, dr, nd, async);
}

static int be_flush(const g16_t *g)
{
    return be_flush_mode(g, 0);
}

int mali_backend_flush(g16_t *g)
{
    return be_flush_mode(g, 0);
}

int mali_backend_submit(g16_t *g)
{
    return be_flush_mode(g, 1);
}

/* --- G1: sheets as textures. A sheet (opaque, RGB565) is copied to the pool as a block of 64-byte alignment: the Texture
 * descriptor at +0x00, a Surface With Stride at +0x40, the Sampler at +0x80, the texels at +0xc0. A sheet with transparent
 * texels, a pool that cannot hold it, or any other platform than the RGB30's arena stays on the ARM (tex_ok says 0). */

typedef struct {
    const g16_sheet_t *sheet;
    uint32_t version;
    int ok, spr;                                    /* spr: the block is for the 2D (texels with alpha, nearest) */
    int alpha;                                  /* a 3D block of a sheet with transparent texels (RGB5A1): BM_FS_TEXA kills them */
    uint64_t pa;
    float iw, ih;
} be_texslot_t;

static be_texslot_t be_texs[BE_TEX_SLOTS];
static int be_ntex;
static uint32_t be_tpool_used;

static int be_tex_on = 1;                           /* mali_tex=0 (bm_port.c): textures stay on the ARM */
static int be_split_on;                             /* mali_split=1 (bm_port.c): a mesh of textured and flat faces is drawn in two parts; default 0 */
void mali_backend_set_split(int on)     { be_split_on = on != 0; }
static int be_alpha_on;                             /* mali_alpha=1 (bm_port.c): a 3D sheet with transparent texels goes to the Mali (BM_FS_TEXA) */
void mali_backend_set_alpha(int on)     { be_alpha_on = on != 0; }
void mali_backend_set_textures(int on)  { be_tex_on = on != 0; }
static int be_bilinear;
void mali_backend_set_bilinear(int on)
{
    on = on != 0;
    if (on == be_bilinear)
        return;
    be_bilinear = on;
    if (be_pend)
        (void)mali_backend_sync();              /* the GPU reads the samplers: wait for it */
    for (int i = 0; i < be_ntex; i++)           /* the sheets uploaded filter the new way */
        if (be_texs[i].ok && !be_texs[i].spr && !be_texs[i].alpha && H) {
            uint8_t blk[32] __attribute__((aligned(16)));
            zero16(blk, sizeof blk);
            md_sampler(blk, on);
            copy_nc16(H->mem + (be_texs[i].pa - H->mem_pa) + 0x80, blk, sizeof blk);
        }                                       /* (the barrier before the next job makes the writes visible) */
}

static int be_arena(void)
{
#ifdef PLAT_RK3566
    return !be_frame && H && H->map_pa > 0 && H->mem_pa >= H->map_pa + (15u << 20);
#else
    return 0;
#endif
}

/* the slot of a sheet (uploaded now if new or changed), or -1: the ARM draws it. The 3D takes opaque sheets as RGB565; the 2D
 * (spr) takes any sheet: with transparent texels as RGB5A1 (green loses its last bit), else as RGB565 */
static int be_tex_find_ex(const g16_t *g, const g16_sheet_t *sh, int spr)
{
    if (!sh || !sh->px || !(spr ? be_2d_on : be_tex_on) || !be_arena())
        return -1;
    for (int i = be_ntex - 1; i >= 0; i--)
        if (be_texs[i].sheet == sh && be_texs[i].spr == spr) {
            if (be_texs[i].version == sh->version)
                return be_texs[i].ok ? i : -1;
            break;                                  /* the sheet changed: a new block */
        }
    const uint32_t w = (uint32_t)sh->w, h = (uint32_t)sh->h;
    int ok = w >= 8 && h >= 8 && w <= 4096 && h <= 4096 && (w & 7) == 0;
    int opaque = 1;                                 /* no transparent texel: BM_FS_TEX draws every texel */
    if (ok) {
        if (sh->cell_opaque) {
            for (uint32_t c = 0; c < (w / 8) * (h / 8) && opaque; c++)
                opaque = sh->cell_opaque[c] != 0;
        } else if (sh->alpha) {
            for (uint32_t i = 0; i < w * h && opaque; i++)
                opaque = sh->alpha[i] != 0;
        }
    }
    if (ok && !spr && !opaque && !be_alpha_on)
        ok = 0;                                     /* holes in a 3D sheet: the ARM, unless mali_alpha=1 */
    const uint32_t size = ok ? ((0xc0u + w * h * 2u + 63u) & ~63u) : 0;
    if (ok && size > BE_TPOOL_SIZE)
        ok = 0;
    if (ok && (be_tpool_used + size > BE_TPOOL_SIZE || be_ntex == BE_TEX_SLOTS)) {
        be_flush(g);                                /* what is waiting uses the pool: draw it before the blocks go */
        be_ntex = 0;
        be_tpool_used = 0;
    }
    if (be_ntex == BE_TEX_SLOTS)
        be_ntex = 0;
    be_texslot_t *t = &be_texs[be_ntex];
    t->sheet = sh;
    t->version = sh->version;
    t->ok = ok;
    t->spr = spr;
    t->alpha = !spr && !opaque;
    if (ok) {
        uint8_t *blk = H->mem - (8u << 20) + be_tpool_used;
        const uint64_t bpa = H->mem_pa - (8u << 20) + be_tpool_used;
        uint8_t hdr[0xc0] __attribute__((aligned(16)));
        zero16(hdr, sizeof hdr);
        const int a5 = !opaque;                       /* 2D and 3D: a sheet with transparent texels is RGB5A1 */
        md_texture(hdr, w, h, a5 ? MD_FMT_RGB5A1 : MD_FMT_RGB565, bpa + 0x40);
        md_surface(hdr + 0x40, bpa + 0xc0, w * 2, w * h * 2);
        md_sampler(hdr + 0x80, (spr || !opaque) ? 0 : be_bilinear);
        copy_nc16(blk, hdr, sizeof hdr);
        if (a5) {                                   /* R 4:0, G 9:5, B 14:10, A 15 from R 15:11, G 10:5, B 4:0 and the alpha plane */
            uint16_t row[4096] __attribute__((aligned(16)));
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    const uint16_t c = sh->px[(size_t)y * w + x];
                    const int op = sh->alpha ? sh->alpha[(size_t)y * w + x] != 0 : 1;
                    row[x] = (uint16_t)((c >> 11) | ((c >> 6 & 31) << 5) | ((c & 31) << 10) | (op ? 0x8000 : 0));
                }
                copy_nc16(blk + 0xc0 + (size_t)y * w * 2, row, (size_t)w * 2);
            }
        } else {
            copy_nc16(blk + 0xc0, sh->px, (size_t)w * h * 2);
        }
        __asm__ volatile("dsb sy" ::: "memory");
        t->pa = bpa;
        t->iw = 1.0f / (float)w;
        t->ih = 1.0f / (float)h;
        be_tpool_used += size;
    }
    be_ntex++;
    return ok ? be_ntex - 1 : -1;
}

static int be_tex_find(const g16_t *g, const g16_sheet_t *sh)
{
    return be_tex_find_ex(g, sh, 0);
}

/* a colour of the page as the GPU draws it: the 8-bit values that the resolve truncates back to the same RGB565 */
static void be_565_to_f(uint16_t c, float *r, float *g, float *b)
{
    const uint32_t r5 = c >> 11, g6 = c >> 5 & 63, b5 = c & 31;
    const uint32_t r8 = (r5 << 3) | (r5 >> 2), b8 = (b5 << 3) | (b5 >> 2);
    uint32_t g8 = (g6 << 2) | (g6 >> 4);
    if (r8 == 255 && g8 == 0 && b8 == 255)
        g8 = 1;                                     /* not the key colour */
    *r = (float)r8 / 255.0f;
    *g = (float)g8 / 255.0f;
    *b = (float)b8 / 255.0f;
}

int mali_backend_rect2d(const g16_t *g, int x0, int y0, int x1, int y1, uint16_t c)
{
    if (!be_2d_on || !be_arena() || !be_hw)
        return 0;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x1 <= x0 || y1 <= y0)
        return 1;                                   /* nothing to draw */
    if (be_n2d == BE_2D_MAX)
        be_flush(g);
    be_2d_t *q = &be_2ds[be_n2d++];
    q->kind = 1;
    q->x0 = (float)x0; q->y0 = (float)y0; q->x1 = (float)x1; q->y1 = (float)y1;
    be_565_to_f(c, &q->r, &q->g, &q->b);
    q->u0 = q->v0 = q->u1 = q->v1 = 0.0f;
    q->tex_pa = 0;
    return 1;
}

int mali_backend_blit2d(const g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh, int dx, int dy, int zoom,
                        int flip_x, int flip_y)
{
    if (!be_2d_on || !be_arena() || !be_hw || !s || zoom < 1 || sw <= 0 || sh <= 0 || sx < 0 || sy < 0 || sx + sw > s->w ||
        sy + sh > s->h)
        return 0;
    const int slot = be_tex_find_ex(g, s, 1);
    if (slot < 0)
        return 0;
    const int dw = sw * zoom, dh = sh * zoom;
    int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x1 <= x0 || y1 <= y0)
        return 1;
    if (be_n2d == BE_2D_MAX)
        be_flush(g);
    const be_texslot_t *t = &be_texs[slot];
    /* the source texels along the destination: u(x) = ul + (x - dx) / dw * (ur - ul), the texel of a pixel centre */
    const float ul = (float)(flip_x ? sx + sw : sx), ur = (float)(flip_x ? sx : sx + sw);
    const float vt = (float)(flip_y ? sy + sh : sy), vb = (float)(flip_y ? sy : sy + sh);
    be_2d_t *q = &be_2ds[be_n2d++];
    q->kind = 2;
    q->x0 = (float)x0; q->y0 = (float)y0; q->x1 = (float)x1; q->y1 = (float)y1;
    q->u0 = (ul + (float)(x0 - dx) / (float)dw * (ur - ul)) * t->iw;
    q->u1 = (ul + (float)(x1 - dx) / (float)dw * (ur - ul)) * t->iw;
    q->v0 = (vt + (float)(y0 - dy) / (float)dh * (vb - vt)) * t->ih;
    q->v1 = (vt + (float)(y1 - dy) / (float)dh * (vb - vt)) * t->ih;
    q->r = q->g = q->b = 0.0f;
    q->tex_pa = t->pa;
    return 1;
}

static int be_tex_ok(void *ctx, const g16_t *g, const g16_sheet_t *sh)
{
    (void)ctx;
    return be_tex_find(g, sh) >= 0;
}

static void be_tri(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind, const g16_sheet_t *tex, int depth)
{
    (void)ctx;
    const int textured = (kind & ~R3D_INSIDE) == R3D_KIND_TEXTURE;
    if (((kind & ~R3D_INSIDE) != R3D_KIND_COLOUR && !textured) || depth != R3D_DEPTH_WRITE)  /* r3d sends nothing else (colour_only): the guard */
        return;
    if (be_n2d)                                     /* a 3D face after 2D: the job so far has to end, the 2D is under it */
        be_flush(g);
    int ts = -1;
    if (textured && (ts = be_tex_find(g, tex)) < 0)
        return;
    const int cap = be_frame ? MALI_BATCH_MAX : BE_MAX_TRIS;
    if (be_n == cap) {
        be_flush(g);                                /* (g16_t holds the page by pointer: const does not stop the copy) */
        if (textured && (ts = be_tex_find(g, tex)) < 0)
            return;
    }
    be_tri_t *t = &be_tris[be_n++];
    if (textured) {
        const be_texslot_t *sl = &be_texs[ts];
        for (int i = 0; i < 3; i++) {
            t->v[i][0] = v[i].x;
            t->v[i][1] = v[i].y;
            t->v[i][2] = v[i].z;                    /* 1/depth */
            t->uvk[i][0] = v[i].a * sl->iw;
            t->uvk[i][1] = v[i].b * sl->ih;
            t->uvk[i][2] = v[i].c;
        }
        t->rgb = 0x01000000u | (uint32_t)ts;        /* a colour no flat draw has: the draws of one texture merge */
        t->key = (v[0].z + v[1].z + v[2].z) / 3;
        t->mvp = NULL;
        t->tex_pa = sl->pa | (sl->alpha ? 1u : 0u);   /* bit 0: the texture has holes (BM_FS_TEXA) */
        t->light = NULL;
        return;
    }
    t->tex_pa = 0;
    t->light = NULL;
    float r = 0, gr = 0, b = 0, z = 0;
    for (int i = 0; i < 3; i++) {
        t->v[i][0] = v[i].x;
        t->v[i][1] = v[i].y;
        t->v[i][2] = 0.5f;
        r += v[i].a;
        gr += v[i].b;
        b += v[i].c;
        z += v[i].z;
    }
    const uint32_t cr = (uint32_t)(r / 3 * 255 + 0.5f), cg = (uint32_t)(gr / 3 * 255 + 0.5f),
                   cb = (uint32_t)(b / 3 * 255 + 0.5f);
    uint32_t rgb = (cr > 255 ? 255 : cr) << 16 | (cg > 255 ? 255 : cg) << 8 | (cb > 255 ? 255 : cb);
    if (rgb == BE_KEY)
        rgb = 0xFF01FF;                             /* the key means "not covered" */
    t->rgb = rgb;
    t->key = z / 3;                                 /* 1/depth: larger is nearer */
    t->mvp = NULL;
}

static int be_mesh_impl(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*M)[12], const float (*N)[9],
                        int nbones, const r3d_env_t *env, int depth);
/* A mesh of textured and flat faces (a palm: leaves and trunk) is drawn in two parts, each with its own cache entry and shader:
 * the textured faces (kind 3), then the flat ones (kind 1 or 2). be_part says which faces the functions below see. */
static int be_part;                             /* 0: every shown face; 1: the textured faces; 2: the flat ones */
#define BE_SHOWS(c, det) (R3D_LOD_SHOWS((c), (det)) && (be_part == 0 || (((c) & R3D_TEXTURED) != 0) == (be_part == 1)))

static int be_mesh(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*M)[12], const float (*N)[9],
                   int nbones, const r3d_env_t *env, int depth)
{
    const uint64_t t0 = cnt_now();
    int mixed = 0;
    if (m && be_tex_on && be_split_on && m->uv && m->tex) {
        int ntex = 0, nflat = 0;
        for (int t = 0; t < m->nfaces; t++) {
            if (!R3D_LOD_SHOWS(m->colors[t], env->detail))
                continue;
            if (m->colors[t] & R3D_TEXTURED)
                ntex++;
            else
                nflat++;
        }
        mixed = ntex > 0 && nflat > 0;
    }
    int r;
    if (mixed) {
        be_part = 1;
        r = be_mesh_impl(ctx, g, m, M, N, nbones, env, depth);
        if (r) {
            be_part = 2;
            r = be_mesh_impl(ctx, g, m, M, N, nbones, env, depth);     /* 0: the ARM draws all, the textured part twice */
        }
        be_part = 0;
    } else {
        r = be_mesh_impl(ctx, g, m, M, N, nbones, env, depth);
    }
    t_mesh += cnt_now() - t0;
    return r;
}

/* the light block of a bone: the uniform buffer of BM_VS_LITX after the matrix, in floats (vec4 each): sun 0, up 4, view 8, half 12,
 * A 16, B 20, D 24, rim R 28, gloss S 32, fog (colour * 255, amount) 36, misc 40 (x: squarings of the gloss power, y: cel levels,
 * z: fog on, w: cel floor), the
 * camera matrix (3 rows) 44, four lamps 56 (position and 1 / r^2, colour). BM_VS_LIT reads sun, up, A, B, D from the same places. */
static void be_set_light(const float *mvp, const r3d_env_t *env, v3_t sun, v3_t up, v3_t view, v3_t half, const float *Mb, float cz)
{
    float *L = be_lights[(const float (*)[16])mvp - be_mvps];
    memset(L, 0, 88 * sizeof(float));
    L[0] = sun.x; L[1] = sun.y; L[2] = sun.z;
    L[4] = up.x;  L[5] = up.y;  L[6] = up.z;
    L[8] = view.x; L[9] = view.y; L[10] = view.z;
    L[12] = half.x; L[13] = half.y; L[14] = half.z;
    for (int c = 0; c < 3; c++) {
        L[16 + c] = env->A[c];
        L[20 + c] = env->B[c];
        L[24 + c] = env->D[c];
        L[28 + c] = env->R[c];
        L[32 + c] = env->S[c];
        L[36 + c] = env->fog[c] * 255.0f;
    }
    if (env->fog_k > 0.0f) {
        float ff = (cz - env->fog_near) * env->fog_k;
        L[39] = ff < 0.0f ? 0.0f : ff > 1.0f ? 1.0f : ff;
        L[42] = 1.0f;
    }
    int k = 0;
    for (float p = 1.0f; p < env->spec_p && k < 8; p *= 2.0f)
        k++;
    L[40] = (float)k;
    L[41] = (float)env->cel;                    /* cel levels (0: none) and the floor */
    L[43] = env->cel_floor;
    memcpy(L + 44, Mb, 12 * sizeof(float));
    for (int i = 0; i < env->nlamps && i < 4; i++) {
        memcpy(L + 56 + 8 * i, env->lamp[i], 4 * sizeof(float));        /* x y z and 1 / r^2 */
        memcpy(L + 60 + 8 * i, env->lamp[i] + 4, 3 * sizeof(float));    /* colour */
    }
}

static int be_arena(void);

/* the cache entry of a lit mesh, made now if there is none, or NULL (the faces go one by one) */
static const be_rm_t *be_rm_get(const r3d_mesh_t *m, const r3d_env_t *env, int kind, int skinned, const be_texslot_t *sl)
{
    uint32_t cs = 0;
    for (int t = 0; t < m->nfaces; t++)
        cs = (cs << 1 | cs >> 31) ^ m->colors[t];
    const int unlit = env->unlit != 0;
    const int smooth = sl && env->smooth && m->vnormals;
    for (int i = 0; i < be_nrm; i++) {
        const be_rm_t *r = &be_rms[i];
        if (r->m == m && r->version == m->version && r->nfaces == m->nfaces && r->detail == env->detail && r->kind == kind &&
            r->unlit == unlit && r->colsum == cs && r->smooth == smooth && r->tex == (sl ? (const void *)m->tex : NULL) && (!sl || (r->iw == sl->iw && r->ih == sl->ih)))
            return r;
    }
    const unsigned stride = (kind == 1 || kind == 2) ? 16 : 12;
    uint32_t shown = 0;
    for (int t = 0; t < m->nfaces; t++)
        shown += BE_SHOWS(m->colors[t], env->detail) ? 1 : 0;
    const uint32_t need = shown * 3 * stride;
    if (need > BE_RM_POOL || !shown)
        return NULL;
    if (be_nrm == BE_RM_SLOTS || be_rm_used + need > BE_RM_POOL) {
        if (be_ninst)
            return NULL;                        /* the batch waiting holds blocks: this mesh goes one face at a time */
        be_nrm = 0;                             /* the pool is full: start again (a frame copies its blocks when it is built) */
        be_rm_used = 0;
    }
    be_rm_t *r = &be_rms[be_nrm++];
    float *o = be_rm_pool + be_rm_used;
    be_rm_used += need;
    for (int t = 0; t < m->nfaces; t++) {
        const uint32_t c = m->colors[t];
        if (!BE_SHOWS(c, env->detail))
            continue;
        const uint16_t *fc = m->faces + t * 3;
        const v3_t v[3] = { m->verts[fc[0]], m->verts[fc[1]], m->verts[fc[2]] }, n = m->normals[t];
        const float emi = (env->unlit || (c & R3D_EMISSIVE)) ? 1.0f : 0.0f;
        const float cx = (v[0].x + v[1].x + v[2].x) * (1.0f / 3.0f), cy = (v[0].y + v[1].y + v[2].y) * (1.0f / 3.0f),
                    cz = (v[0].z + v[1].z + v[2].z) * (1.0f / 3.0f);
        for (int k = 0; k < 3; k++) {
            float *q = o + k * stride;
            q[0] = v[k].x; q[1] = v[k].y; q[2] = v[k].z; q[3] = 1.0f;
            const v3_t nk = (smooth && !(c & R3D_FLAT)) ? m->vnormals[fc[k]] : n;   /* (r3d lights a textured corner from its vertex) */
            q[4] = nk.x; q[5] = nk.y; q[6] = nk.z; q[7] = emi;
            if (sl) {
                q[8] = m->uv[t * 6 + 2 * k] * sl->iw; q[9] = m->uv[t * 6 + 2 * k + 1] * sl->ih;
                q[10] = q[11] = 0.0f;
            } else {
                q[8] = (float)(c >> 16 & 255); q[9] = (float)(c >> 8 & 255); q[10] = (float)(c & 255);
                q[11] = (c & R3D_GLOSSY) ? 1.0f : 0.0f;
            }
            if (kind == 1 || kind == 2) {
                q[12] = cx; q[13] = cy; q[14] = cz;
                q[15] = skinned ? (float)m->vbone[fc[0]] : 0.0f;
            }
        }
        o += 3 * stride;
    }
    r->m = m; r->version = m->version; r->nfaces = m->nfaces; r->detail = env->detail; r->kind = kind; r->unlit = unlit;
    r->colsum = cs; r->nshown = shown; r->blk = be_rm_pool + be_rm_used - need;
    r->smooth = smooth;
    r->tex = sl ? (const void *)m->tex : NULL; r->iw = sl ? sl->iw : 0.0f; r->ih = sl ? sl->ih : 0.0f;
    return r;
}

static int be_mesh_impl(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*M)[12], const float (*N)[9],
                        int nbones, const r3d_env_t *env, int depth)
{
    (void)ctx;
    if (!be_vs_on || (env->lit && be_vs_on < 2) || depth != R3D_DEPTH_WRITE || !m ||
        m->nfaces <= 0 || m->nverts <= 0 || m->clight)
        return 0;
    const int skinned = m->bones && m->nbones > 0 && m->vbone;
    const int nb = skinned ? (nbones < 32 ? nbones : 32) : 1;
    const unsigned dbit = 1u << env->detail;
    int ntexf = 0, nflatf = 0;                  /* the shown faces with and without a texture */
    for (int t = 0; t < m->nfaces; t++) {
        const uint32_t c = m->colors[t];
        if (!BE_SHOWS(c, env->detail))
            continue;
        if (c & R3D_SCREEN)
            return 0;
        if (c & R3D_TEXTURED)
            ntexf++;
        else
            nflatf++;
        if (skinned) {
            const uint16_t *fc = m->faces + t * 3;
            const uint8_t b0 = m->vbone[fc[0]];
            if (b0 >= (unsigned)nb || m->vbone[fc[1]] != b0 || m->vbone[fc[2]] != b0)
                return 0;
        }
    }
    if (!env->inside) {
        for (int i = 0; i < m->nverts; i++) {
            if (m->vlod && !(m->vlod[i] & dbit))
                continue;
            const float *Mb = M[skinned ? m->vbone[i] : 0];
            const v3_t o = m->verts[i];
            const float cz = Mb[8] * o.x + Mb[9] * o.y + Mb[10] * o.z + Mb[11];
            if (cz < R3D_NEAR)
                return 0;
        }
    }
    /* a textured mesh: the cache and the vertex shader light it as r3d does a textured face (the grey of the sun and sky light, no fog),
     * so the faces all have the texture and the light is the sun and sky alone */
    const be_texslot_t *tsl = NULL;
    if (ntexf) {
        if (nflatf || skinned || env->front || !m->uv || !m->tex || !be_res_on || !be_zmode() || !be_arena() ||
            m->nfaces < 16 || (env->lit && (!N || env->cel > 0 || env->nlamps > 0 || env->R[0] > 0.0f || env->R[1] > 0.0f || env->R[2] > 0.0f)))
            return 0;
        const int ts = be_tex_find(g, m->tex);
        if (ts < 0)
            return 0;
        tsl = &be_texs[ts];
    }
    const int cap = be_frame ? MALI_BATCH_MAX : BE_MAX_TRIS;
    if (be_n2d || be_n + m->nfaces > cap || (int)be_nvis + m->nfaces > BE_VIS_MAX || be_nmvp + nb > BE_MAX_MVPS ||
        be_ninst >= BE_INST_MAX)
        be_flush(g);
    const float zmul = env->front ? 0.1f : 1.0f;
    const int zmode = be_zmode();
    /* Q2: the vertex shader lights the faces when the light is the sun and the ambient light of a rigid mesh */
    int vlit = be_lit_on && be_arena() && env->lit && N;
    int vx = env->cel > 0 || env->nlamps > 0 || env->fog_k > 0.0f || env->R[0] > 0.0f || env->R[1] > 0.0f || env->R[2] > 0.0f;
    if (vlit && (env->S[0] > 0.0f || env->S[1] > 0.0f || env->S[2] > 0.0f))
        for (int t = 0; t < m->nfaces && !vx; t++)
            if (BE_SHOWS(m->colors[t], env->detail) && (m->colors[t] & R3D_GLOSSY))
                vx = 1;
    float mvp_local[32][16];
    float cam_s2[32], cam_at[32][3], cz_bone[32];
    v3_t l_sun[32], l_up[32], l_view[32], l_half[32];
    const float *mvp_bone[32];
    for (int b = 0; b < nb; b++) {
        const float *Mb = M[b];
        float *mvp = mvp_local[b];
        for (int j = 0; j < 4; j++) {
            mvp[0 + j]  = env->f * Mb[0 + j] + env->cx * Mb[8 + j];
            mvp[4 + j]  = -env->f * Mb[4 + j] + env->cy * Mb[8 + j];
            mvp[8 + j]  = zmode ? Mb[8 + j] - (j == 3 ? R3D_NEAR : 0.0f) : 0.5f * Mb[8 + j];  /* z / w = 1 - near / depth */
            mvp[12 + j] = Mb[8 + j];
        }
        if (be_nmvp < BE_MAX_MVPS) {
            memcpy(be_mvps[be_nmvp], mvp, sizeof mvp_local[b]);
            mvp_bone[b] = be_mvps[be_nmvp++];
        } else {
            mvp_bone[b] = NULL;
        }
        cam_s2[b] = Mb[0] * Mb[0] + Mb[1] * Mb[1] + Mb[2] * Mb[2];
        cam_at[b][0] = Mb[0] * Mb[3] + Mb[4] * Mb[7] + Mb[8] * Mb[11];
        cam_at[b][1] = Mb[1] * Mb[3] + Mb[5] * Mb[7] + Mb[9] * Mb[11];
        cam_at[b][2] = Mb[2] * Mb[3] + Mb[6] * Mb[7] + Mb[10] * Mb[11];
        const v3_t c = m->centre;
        float cz = skinned ? 0.0f : (Mb[8] * c.x + Mb[9] * c.y + Mb[10] * c.z + Mb[11]);
        if (!skinned && cz < R3D_NEAR)
            cz = R3D_NEAR;
        cz_bone[b] = cz;
        if (env->lit && N) {
            const float *Nb = N[b];
            l_sun[b]  = (v3_t){ Nb[0] * env->sun[0]  + Nb[3] * env->sun[1]  + Nb[6] * env->sun[2],
                                Nb[1] * env->sun[0]  + Nb[4] * env->sun[1]  + Nb[7] * env->sun[2],
                                Nb[2] * env->sun[0]  + Nb[5] * env->sun[1]  + Nb[8] * env->sun[2] };
            l_up[b]   = (v3_t){ Nb[3], Nb[4], Nb[5] };
            l_view[b] = (v3_t){ Nb[0] * env->view[0] + Nb[3] * env->view[1] + Nb[6] * env->view[2],
                                Nb[1] * env->view[0] + Nb[4] * env->view[1] + Nb[7] * env->view[2],
                                Nb[2] * env->view[0] + Nb[5] * env->view[1] + Nb[8] * env->view[2] };
            l_half[b] = (v3_t){ Nb[0] * env->half[0] + Nb[3] * env->half[1] + Nb[6] * env->half[2],
                                Nb[1] * env->half[0] + Nb[4] * env->half[1] + Nb[7] * env->half[2],
                                Nb[2] * env->half[0] + Nb[5] * env->half[1] + Nb[8] * env->half[2] };
        }
    }
    if (skinned) {
        int bcnt[32] = { 0 };
        float bsum[32] = { 0 };
        for (int i = 0; i < m->nverts; i++) {
            const int b = m->vbone[i];
            if (b < nb) {
                const float *Mb = M[b];
                const v3_t o = m->verts[i];
                bsum[b] += Mb[8] * o.x + Mb[9] * o.y + Mb[10] * o.z + Mb[11];
                bcnt[b]++;
            }
        }
        for (int b = 0; b < nb; b++) {
            float cz = bcnt[b] > 0 ? bsum[b] / (float)bcnt[b] : M[b][11];
            if (cz < R3D_NEAR)
                cz = R3D_NEAR;
            cz_bone[b] = cz;
        }
    }
    if (vlit && m->nfaces < 16)                 /* a draw costs 15-25 us on the ARM: a mesh of a few faces is lit by the ARM */
        vlit = 0;
    if (vlit && !skinned) {                     /* many tiny faces cost more to send than to light: the ARM keeps them (a skinned mesh is 16 draws otherwise) */
        const float rpx = m->radius > 0.0f ? m->radius * env->f / cz_bone[0] : 1e9f;
        if (3.14f * rpx * rpx < be_lit_min * 0.5f * (float)m->nfaces)
            vlit = 0;
    }
    if (vlit)
        for (int b = 0; b < nb; b++)
            if (!mvp_bone[b])                   /* the bones\' matrices have to be in a row */
                vlit = 0;
    if (vlit && skinned)
        vx = 2;
    if (vlit)
        for (int b = 0; b < nb; b++)
            be_set_light(mvp_bone[b], env, l_sun[b], l_up[b], l_view[b], l_half[b], M[b], cz_bone[b]);
    if (tsl) {                                  /* a textured mesh: one matrix, the light block of BM_VS_TEXM (sun, up, A, B, D) */
        vlit = 0;
        const float rpx = m->radius > 0.0f ? m->radius * env->f / cz_bone[0] : 1e9f;
        if (!mvp_bone[0] || 3.14f * rpx * rpx < be_lit_min * 0.5f * (float)m->nfaces)
            return 0;                           /* (the ARM draws the faces one by one) */
        if (env->lit)
            be_set_light(mvp_bone[0], env, l_sun[0], l_up[0], l_view[0], l_half[0], M[0], cz_bone[0]);
        else
            memset(be_lights[(const float (*)[16])mvp_bone[0] - be_mvps], 0, 88 * sizeof(float));
    }
    if ((vlit || tsl) && be_res_on && zmode) {  /* the face cache: only the visible faces' numbers go on */
        const int kind = tsl ? 3 : vx == 2 ? 2 : vx ? 1 : 0;
        const be_rm_t *rm = be_rm_get(m, env, kind, skinned, tsl);
        if (rm) {
            const unsigned stride = (kind == 1 || kind == 2) ? 16 : 12;
            const uint32_t vis0 = be_nvis;
            for (uint32_t j = 0; j < rm->nshown; j++) {
                const float *f = rm->blk + (size_t)j * 3 * stride;
                const int b = skinned ? (int)f[15] : 0;
                const float s2 = cam_s2[b], *at = cam_at[b];
                if ((s2 * f[0] + at[0]) * f[4] + (s2 * f[1] + at[1]) * f[5] + (s2 * f[2] + at[2]) * f[6] >= 0.0f)
                    continue;
                be_vis[be_nvis++] = (uint16_t)j;
            }
            const uint32_t nvis = be_nvis - vis0;
            if (nvis) {
                be_inst_t *q = &be_insts[be_ninst++];
                q->rm = rm;
                q->mvp = mvp_bone[0];
                q->light = be_lights[(const float (*)[16])mvp_bone[0] - be_mvps];
                q->kind = kind;
                q->nb = nb;
                q->nvis = nvis;
                q->vis0 = vis0;
                q->tex_pa = tsl ? (tsl->pa | (tsl->alpha ? 1u : 0u)) : 0;
            }
            be_stats.glmeshes++;
            be_stats.gltris += nvis;
            be_stats.glverts += (uint32_t)m->nverts;
            return 1;
        }
    }
    if (tsl)
        return 0;                               /* no cache entry: r3d draws the faces one by one */
    uint32_t nsent = 0;
    for (int t = 0; t < m->nfaces; t++) {
        const uint32_t c = m->colors[t];
        if (!BE_SHOWS(c, env->detail))
            continue;
        const uint16_t *fc = m->faces + t * 3;
        const int b = skinned ? m->vbone[fc[0]] : 0;
        const v3_t v0 = m->verts[fc[0]], v1 = m->verts[fc[1]], v2 = m->verts[fc[2]];
        const v3_t n = m->normals[t];
        const float s2 = cam_s2[b], *at = cam_at[b];
        if ((s2 * v0.x + at[0]) * n.x + (s2 * v0.y + at[1]) * n.y + (s2 * v0.z + at[2]) * n.z >= 0.0f)
            continue;
        if (be_n == cap) {
            be_flush(g);
            for (int kb = 0; kb < nb; kb++) {
                memcpy(be_mvps[be_nmvp], mvp_local[kb], sizeof mvp_local[kb]);
                mvp_bone[kb] = be_mvps[be_nmvp++];
                if (vlit)
                    be_set_light(mvp_bone[kb], env, l_sun[kb], l_up[kb], l_view[kb], l_half[kb], M[kb], cz_bone[kb]);
            }
        }
        uint32_t rgb = c & 0xFFFFFFu;
        const int emissive = env->unlit || (c & R3D_EMISSIVE) != 0;
        if (!vlit && !emissive && env->lit) {
            const float d = n.x * l_sun[b].x + n.y * l_sun[b].y + n.z * l_sun[b].z;
            const float ny = n.x * l_up[b].x + n.y * l_up[b].y + n.z * l_up[b].z;
            float dd = d > 0.0f ? d : 0.0f;
            if (env->cel > 0) {
                int ci = (int)(dd * 64.0f);
                if (ci > 63) ci = 63;
                const int cb = ci * env->cel / 64;
                const float cv = env->cel_floor + (1.0f - env->cel_floor) * (float)cb / (float)(env->cel - 1);
                dd = (float)((int)(cv * 255.0f + 0.5f)) * (1.0f / 255.0f);
            }
            float lr = env->A[0] + env->B[0] * ny + env->D[0] * dd;
            float lg = env->A[1] + env->B[1] * ny + env->D[1] * dd;
            float lb = env->A[2] + env->B[2] * ny + env->D[2] * dd;
            if (env->R[0] > 0.0f || env->R[1] > 0.0f || env->R[2] > 0.0f) {
                const float nv = n.x * l_view[b].x + n.y * l_view[b].y + n.z * l_view[b].z;
                const float e = 1.0f - (nv > 0.0f ? nv : 0.0f), ee = e * e;
                lr += ee * env->R[0];
                lg += ee * env->R[1];
                lb += ee * env->R[2];
            }
            if (env->nlamps > 0) {
                const float *Mb = M[b];
                const float ox = (v0.x + v1.x + v2.x) * (1.0f / 3.0f),
                            oy = (v0.y + v1.y + v2.y) * (1.0f / 3.0f),
                            oz = (v0.z + v1.z + v2.z) * (1.0f / 3.0f);
                const float mx = Mb[0] * ox + Mb[1] * oy + Mb[2] * oz + Mb[3],
                            my = Mb[4] * ox + Mb[5] * oy + Mb[6] * oz + Mb[7],
                            mz = Mb[8] * ox + Mb[9] * oy + Mb[10] * oz + Mb[11];
                for (int li = 0; li < env->nlamps; li++) {
                    const float *L = env->lamp[li];
                    const float dx = mx - L[0], dy = my - L[1], dz = mz - L[2];
                    const float d2 = (dx * dx + dy * dy + dz * dz) * L[3];
                    if (d2 < 1.0f) {
                        const float k = 1.0f - d2;
                        lr += k * L[4];
                        lg += k * L[5];
                        lb += k * L[6];
                    }
                }
            }
            float sr = 0.0f, sg = 0.0f, sb = 0.0f;
            if ((c & R3D_GLOSSY) && d > 0.0f && (env->S[0] > 0.0f || env->S[1] > 0.0f || env->S[2] > 0.0f)) {
                float nh = n.x * l_half[b].x + n.y * l_half[b].y + n.z * l_half[b].z;
                if (nh > 0.0f) {
                    for (float p = 1.0f; p < env->spec_p; p *= 2.0f)
                        nh *= nh;
                    const float ks = env->cel > 0 ? (nh > 0.5f ? 1.0f : 0.0f) : nh;
                    sr = ks * env->S[0];
                    sg = ks * env->S[1];
                    sb = ks * env->S[2];
                }
            }
            float cr = (float)(rgb >> 16 & 255) * lr + 255.0f * sr;
            float cg = (float)(rgb >> 8 & 255) * lg + 255.0f * sg;
            float cb = (float)(rgb & 255) * lb + 255.0f * sb;
            if (cr > 255.0f) cr = 255.0f;
            if (cg > 255.0f) cg = 255.0f;
            if (cb > 255.0f) cb = 255.0f;
            if (env->fog_k > 0.0f) {
                float ff = (cz_bone[b] - env->fog_near) * env->fog_k;
                if (ff < 0.0f) ff = 0.0f;
                if (ff > 1.0f) ff = 1.0f;
                cr += (env->fog[0] * 255.0f - cr) * ff;
                cg += (env->fog[1] * 255.0f - cg) * ff;
                cb += (env->fog[2] * 255.0f - cb) * ff;
            }
            const uint32_t r5 = (uint32_t)(cr + 0.5f) >> 3, g6 = (uint32_t)(cg + 0.5f) >> 2, b5 = (uint32_t)(cb + 0.5f) >> 3;
            const uint32_t r8 = (r5 << 3) | (r5 >> 2), g8 = (g6 << 2) | (g6 >> 4), b8 = (b5 << 3) | (b5 >> 2);
            rgb = (r8 > 255 ? 255 : r8) << 16 | (g8 > 255 ? 255 : g8) << 8 | (b8 > 255 ? 255 : b8);
        } else if (env->fog_k > 0.0f) {
            float ff = (cz_bone[b] - env->fog_near) * env->fog_k;
            if (ff < 0.0f) ff = 0.0f;
            if (ff > 1.0f) ff = 1.0f;
            float cr = (float)(rgb >> 16 & 255), cg = (float)(rgb >> 8 & 255), cb = (float)(rgb & 255);
            cr += (env->fog[0] * 255.0f - cr) * ff;
            cg += (env->fog[1] * 255.0f - cg) * ff;
            cb += (env->fog[2] * 255.0f - cb) * ff;
            const uint32_t r5 = (uint32_t)(cr + 0.5f) >> 3, g6 = (uint32_t)(cg + 0.5f) >> 2, b5 = (uint32_t)(cb + 0.5f) >> 3;
            const uint32_t r8 = (r5 << 3) | (r5 >> 2), g8 = (g6 << 2) | (g6 >> 4), b8 = (b5 << 3) | (b5 >> 2);
            rgb = (r8 > 255 ? 255 : r8) << 16 | (g8 > 255 ? 255 : g8) << 8 | (b8 > 255 ? 255 : b8);
        }
        if (rgb == BE_KEY)
            rgb = 0xFF01FF;
        be_tri_t *tr = &be_tris[be_n++];
        tr->v[0][0] = v0.x; tr->v[0][1] = v0.y; tr->v[0][2] = v0.z;
        tr->v[1][0] = v1.x; tr->v[1][1] = v1.y; tr->v[1][2] = v1.z;
        tr->v[2][0] = v2.x; tr->v[2][1] = v2.y; tr->v[2][2] = v2.z;
        tr->rgb = rgb;
        tr->key = zmul / cz_bone[b];
        tr->mvp = mvp_bone[b];
        tr->tex_pa = 0;
        tr->light = NULL;
        if (vlit) {                             /* Q2: the vertex shader lights this face */
            if (skinned)
                tr->mvp = mvp_bone[0];          /* one draw a mesh: the vertex shader picks the bone's matrix */
            tr->light = be_lights[(const float (*)[16])tr->mvp - be_mvps];
            tr->nb = (uint8_t)nb;
            float *lf = be_lit[tr - be_tris];
            lf[0] = n.x; lf[1] = n.y; lf[2] = n.z;
            lf[3] = emissive ? 1.0f : 0.0f;
            lf[4] = (float)(c >> 16 & 255); lf[5] = (float)(c >> 8 & 255); lf[6] = (float)(c & 255);
            lf[7] = (c & R3D_GLOSSY) ? 1.0f : 0.0f;
            lf[8] = (v0.x + v1.x + v2.x) * (1.0f / 3.0f); lf[9] = (v0.y + v1.y + v2.y) * (1.0f / 3.0f);
            lf[10] = (v0.z + v1.z + v2.z) * (1.0f / 3.0f); lf[11] = skinned ? (float)b : 0.0f;
            tr->rgb = vx == 2 ? 0x04000000u : vx ? 0x03000000u : 0x02000000u;   /* a colour no flat draw has: the faces of one mesh merge */
        }
        nsent++;
    }
    be_stats.glmeshes++;
    be_stats.gltris += nsent;
    be_stats.glverts += (uint32_t)m->nverts;
    return 1;
}

static void be_zclear(void *ctx, const g16_t *g)
{
    (void)ctx;
    (void)g;
    mali_backend_drop();
}

const r3d_backend_t *mali_backend(const mali_hw_t *hw)
{
    static const r3d_backend_t be = {
        .tri = be_tri, .mesh = be_mesh, .zclear = be_zclear, .tex_ok = be_tex_ok,
        .guard = 2048.0f, .cel = 1, .colour_only = 1,
    };
    if (!probed_ok || hw != H)
        return NULL;
    be_hw = hw;
    return &be;
}
