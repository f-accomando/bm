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
                      0u << 19 /* 1 target */ | 1u << 24 /* 1 KB of colour a tile */);
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

static void put_f(uint8_t *p, float v)
{
    memcpy(p, &v, 4);
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* Everything for the frame in the GPU memory: the draws (valid, checked by the caller) into a w x h surface at target_pa,
 * cleared to `clear` first. The jobs are chained from the set's SO_DRAWS (the first Vertex job); the Fragment job is at its SO_FJOB. */
static void frame_build(uint32_t set, const mali_draw_t *draws, uint32_t ndraws, uint32_t w, uint32_t h,
                        uint64_t target_pa, uint32_t pitch, uint32_t clear)
{
    const uint32_t sb = MALI_SET_BASE(set);
    uint8_t *m = H->mem;
    const uint64_t pa = H->mem_pa;
    const uint32_t verts = sb + (MALI_FRAME_VERTS - MALI_SET_BASE(0)), positions = sb + (MALI_FRAME_POSITIONS - MALI_SET_BASE(0)),
                   push = sb + (MALI_FRAME_PUSH - MALI_SET_BASE(0));

    memset(m + sb, 0, 0x1000);
    memset(m + sb + SO_DRAWS, 0, (size_t)MEM_DRAW_SIZE * ndraws);
    memcpy(m + sb + SO_VS, BM_VS_bin, sizeof BM_VS_bin);
    memcpy(m + sb + SO_FS, BM_FS_bin, sizeof BM_FS_bin);
    const uint32_t vpush = sb + (MALI_FRAME_VPUSH - MALI_SET_BASE(0));
    for (uint32_t d = 0; d < ndraws; d++)
        if (draws[d].mvp) {
            memcpy(m + sb + SO_VS3, BM_VS3_bin, sizeof BM_VS3_bin);
            break;
        }

    /* tiler: the heap and the context */
    md_tiler_heap(m + sb + SO_HEAPD, pa + sb + SO_HEAP, HEAP_SIZE);
    md_tiler_context(m + sb + SO_TCTX, w, h, HIERARCHY_MASK, pa + sb + SO_HEAPD);

    /* the shaders' states */
    md_renderer_state_t vrs = { .shader = pa + sb + SO_VS, .attribute_count = BM_VS_ATTRIBUTE_COUNT,
                                .varying_count = BM_VS_VARYING_COUNT, .ubo_count = BM_VS_UBO_COUNT,
                                .work_regs = BM_VS_WORK_REGS, .preload_regs = BM_VS_PRELOAD,
                                .push_count = BM_VS_PUSH_COUNT };
    md_renderer_state(m + sb + SO_VRSD, &vrs);
    md_renderer_state_t frs = { .fragment = 1, .shader = pa + sb + SO_FS, .attribute_count = BM_FS_ATTRIBUTE_COUNT,
                                .varying_count = BM_FS_VARYING_COUNT, .ubo_count = BM_FS_UBO_COUNT,
                                .work_regs = BM_FS_WORK_REGS, .preload_regs = BM_FS_PRELOAD,
                                .push_count = BM_FS_PUSH_COUNT, .pixel_kill = MD_KILL_WEAK_EARLY,
                                .zs_update = MD_KILL_WEAK_EARLY, .wait6 = BM_FS_WAIT6, .wait7 = BM_FS_WAIT7 };
    md_local_storage(m + sb + SO_LS);
    md_viewport(m + sb + SO_VIEWPORT, 0, 0, w, h, 0, 0, w - 1, h - 1);
    md_attribute(m + sb + SO_ATTR, 0, MD_FMT_RGBA32F | MD_ORDER_RGBA, 0, 1);
    md_attribute(m + sb + SO_VATTR, 1, MD_FMT_SNAP4 | MD_ORDER_RGBA, 0, 0);

    uint32_t first = 0;                         /* the draw's first vertex, in the frame's arrays */
    for (uint32_t d = 0; d < ndraws; d++) {
        uint8_t *dm = m + sb + SO_DRAWS + (size_t)MEM_DRAW_SIZE * d;
        const uint64_t dpa = pa + sb + SO_DRAWS + (uint64_t)MEM_DRAW_SIZE * d;
        const uint32_t nv = 3 * draws[d].n;
        const uint32_t rgb = draws[d].rgb;

        /* the data: the positions as (x y z 1), the colour twice (the buffer and the pushed words 4-7) */
        for (uint32_t i = 0; i < nv; i++) {
            for (int k = 0; k < 3; k++)
                put_f(m + verts + 16 * (first + i) + 4 * k, draws[d].xyz[i][k]);
            put_f(m + verts + 16 * (first + i) + 12, 1.0f);
        }
        const float col[4] = { (float)(rgb >> 16 & 0xff) / 255.0f, (float)(rgb >> 8 & 0xff) / 255.0f,
                               (float)(rgb & 0xff) / 255.0f, 1.0f };
        for (int k = 0; k < 4; k++) {
            put_f(dm + D_COLOUR + 4 * k, col[k]);
            put_f(m + push + 64 * d + 16 + 4 * k, col[k]);   /* BM_FS_push says ubo 0 offsets 0, 4, 8, 12 */
        }

        /* the draw's tables */
        md_renderer_state(dm + D_FRSD, &frs);
        md_blend_opaque(dm + D_FRSD + MD_RENDERER_STATE_SIZE, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_RGBA, MD_REG_F32);
        md_attribute_buffer(dm + D_ABUFS, pa + verts + 16 * first, 16, 16 * nv);
        md_attribute_buffer(dm + D_VBUFS, 0, 0, 0);
        md_attribute_buffer(dm + D_VBUFS + 16, pa + positions + 16 * first, 16, 16 * nv);
        md_attribute_buffer(dm + D_VBUFS + 32, 0, 0, 0);
        md_uniform_buffer(dm + D_UBO, dpa + D_COLOUR, 1);
        if (draws[d].mvp) {   /* phase 3: BM_VS3 with the matrix pushed (BM_VS3_push: words 0..15 are bytes 0..60 of ubo 0) */
            md_renderer_state_t vrs3 = { .shader = pa + sb + SO_VS3, .attribute_count = BM_VS3_ATTRIBUTE_COUNT,
                                         .varying_count = BM_VS3_VARYING_COUNT, .ubo_count = BM_VS3_UBO_COUNT,
                                         .work_regs = BM_VS3_WORK_REGS, .preload_regs = BM_VS3_PRELOAD,
                                         .push_count = BM_VS3_PUSH_COUNT };
            md_renderer_state(dm + D_VRSD, &vrs3);
            for (int k = 0; k < 16; k++)
                put_f(m + vpush + 64 * d + BM_VS3_push[k][1], draws[d].mvp[k]);
            md_uniform_buffer(dm + D_VUBO, pa + vpush + 64 * d, 4);
        }

        /* the jobs: Vertex 2d + 1 (after the previous Tiler job), Tiler 2d + 2 (after its Vertex job) */
        md_draw_t vd = { .state = pa + sb + SO_VRSD, .attribute_buffers = dpa + D_ABUFS, .attributes = pa + sb + SO_ATTR,
                         .varying_buffers = dpa + D_VBUFS, .varyings = pa + sb + SO_VATTR, .thread_storage = pa + sb + SO_LS };
        if (draws[d].mvp) {
            vd.state = dpa + D_VRSD;
            vd.uniform_buffers = dpa + D_VUBO;
            vd.push_uniforms = pa + vpush + 64 * d;
        }
        job_header(dm + D_VJOB, MD_JOB_VERTEX, 2 * (int)d + 1, d ? 2 * (int)d : 0, dpa + D_TJOB);
        md_vertex_job(dm + D_VJOB, nv, &vd);
        md_draw_t td = { .position = pa + positions + 16 * first, .state = dpa + D_FRSD,
                         .viewport = pa + sb + SO_VIEWPORT, .uniform_buffers = dpa + D_UBO,
                         .push_uniforms = pa + push + 64 * d, .thread_storage = pa + sb + SO_LS };
        md_primitive_t prim = { .mode = MD_TRIANGLES, .index_count = nv };
        job_header(dm + D_TJOB, MD_JOB_TILER, 2 * (int)d + 2, 2 * (int)d + 1,
                   d + 1 < ndraws ? dpa + MEM_DRAW_SIZE + D_VJOB : 0);
        md_tiler_job(dm + D_TJOB, &prim, pa + sb + SO_TCTX, &td);
        first += (nv + 3) & ~3u;                        /* 64-byte alignment: 4 vertices per step for D_ABUFS/td.position */
    }

    /* the fragment job: the clear's descriptor with the tiler's context */
    samples(m + sb + SO_SAMPLES);
    fbd(m + sb + SO_FBD, pa + sb + SO_SAMPLES, w, h, target_pa, pitch, clear);
    put64(m + sb + SO_FBD + 56, pa + sb + SO_TCTX);
    job_fragment(m + sb + SO_FJOB, w, h, pa + sb + SO_FBD);
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
    /* the centre of the first triangle's bounding box */
    float lo[2] = { draws[0].xyz[0][0], draws[0].xyz[0][1] }, hi[2] = { lo[0], lo[1] };
    for (int i = 1; i < 3; i++)
        for (int k = 0; k < 2; k++) {
            if (draws[0].xyz[i][k] < lo[k]) lo[k] = draws[0].xyz[i][k];
            if (draws[0].xyz[i][k] > hi[k]) hi[k] = draws[0].xyz[i][k];
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

/* --- the r3d backend (M41 phase 2) ---
 * r3d hands this backend flat-coloured, depth-written triangles only (colour_only, W13b). They are buffered, sorted far to
 * near (painter's order: the Mali draws without a depth test here), drawn by mali_frame into an XRGB surface cleared to a key
 * colour, and the pixels the GPU covered are copied into the RGB565 page. */
#define BE_SURF_OFF     0x200000    /* the surface in hw->mem when it does not fit set 0's (frames are never in flight then) */
#define BE_KEY          0xFF00FF    /* "not covered" */

typedef struct { float v[3][3]; float key; uint32_t rgb; } be_tri_t;

static be_tri_t be_tris[MALI_BATCH_MAX];
static int be_n;
static const mali_hw_t *be_hw;
static int (*be_frame)(const mali_hw_t *, uint8_t *, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                       const mali_draw_t *, uint32_t);

void mali_backend_set_frame(int (*frame)(const mali_hw_t *, uint8_t *, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                         const mali_draw_t *, uint32_t))
{
    be_frame = frame;
}

int mali_backend_pending(void)
{
    return be_n;
}

void mali_backend_drop(void)
{
    be_n = 0;
}

/* the pixels of the surface that are not the key, into the page */
static void be_resolve(const g16_t *g, const uint8_t *surf, uint32_t pitch)
{
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const uint32_t p = rd32(surf + (size_t)y * pitch + 4 * (size_t)x) & 0xFFFFFF;
            if (p != BE_KEY)
                g->px[(size_t)y * g->stride + x] = (uint16_t)((p >> 19 & 31) << 11 | (p >> 10 & 63) << 5 | (p >> 3 & 31));
        }
}

/* one frame of the draws so far, resolved into the page; 0, or -1 */
static int be_run(const g16_t *g, uint8_t *surf, uint64_t pa, uint32_t pitch, uint32_t sw, uint32_t sh,
                  const mali_draw_t *dr, uint32_t nd)
{
    int (*frame)(const mali_hw_t *, uint8_t *, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, const mali_draw_t *,
                 uint32_t) = be_frame ? be_frame : mali_frame;
    if (frame(be_hw, surf, pa, pitch, sw, sh, BE_KEY, dr, nd) < 0)      /* 0 and 1 both count as done */
        return -1;
    be_resolve(g, surf, pitch);
    return 0;
}

static int be_flush(const g16_t *g)
{
    static float vbuf[MALI_BATCH_MAX * 3][3];
    static mali_draw_t dr[MALI_MAX_DRAWS];
    static int idx[MALI_BATCH_MAX];
    const int n = be_n;
    if (n == 0)
        return 0;
    be_n = 0;
    const mali_hw_t *hw = be_hw;
    const uint32_t sw = ((uint32_t)g->w + 15) & ~15u, sh = ((uint32_t)g->h + 15) & ~15u;
    const uint32_t pitch = (sw * 4 + 63) & ~63u;
    if (!hw || sw > 1920 || sh > 1080)
        return -1;
    const int in_set = (uint64_t)pitch * sh <= MALI_SET_SURF_MAX && hw->mem_size >= MALI_SET_BASE(1);
    const uint32_t surf_off = in_set ? MALI_SET_BASE(0) + MALI_SET_SURFACE : BE_SURF_OFF;
    if (surf_off + (uint64_t)pitch * sh > hw->mem_size)
        return -1;
    for (int i = 0; i < n; i++)
        idx[i] = i;
    for (int i = 1; i < n; i++) {                   /* stable insertion sort: far (small key) first */
        const int k = idx[i];
        int j = i - 1;
        while (j >= 0 && be_tris[idx[j]].key > be_tris[k].key) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = k;
    }
    uint8_t *surf = hw->mem + surf_off;
    const uint64_t pa = hw->mem_pa + surf_off;
    uint32_t nd = 0, nv = 0;
    for (int i = 0; i < n; i++) {
        const be_tri_t *t = &be_tris[idx[i]];
        if (nd > 0 && dr[nd - 1].rgb == t->rgb) {
            dr[nd - 1].n++;
        } else {
            if (nd == MALI_MAX_DRAWS) {             /* a full frame: draw it, go on with a fresh one */
                if (be_run(g, surf, pa, pitch, sw, sh, dr, nd) != 0)
                    return -1;
                nd = 0;
                nv = 0;
            }
            dr[nd].xyz = (const float (*)[3])vbuf[nv];
            dr[nd].n = 1;
            dr[nd].rgb = t->rgb;
            nd++;
        }
        memcpy(vbuf[nv], t->v, sizeof t->v);
        nv += 3;
    }
    return be_run(g, surf, pa, pitch, sw, sh, dr, nd);
}

int mali_backend_flush(g16_t *g)
{
    return be_flush(g);
}

static void be_tri(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind, const g16_sheet_t *tex, int depth)
{
    (void)ctx;
    (void)tex;
    if (kind != R3D_KIND_COLOUR || depth != R3D_DEPTH_WRITE)    /* r3d sends nothing else (colour_only): the guard */
        return;
    if (be_n == MALI_BATCH_MAX)
        be_flush(g);                                /* (g16_t holds the page by pointer: const does not stop the copy) */
    be_tri_t *t = &be_tris[be_n++];
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
}

static void be_zclear(void *ctx, const g16_t *g)
{
    (void)ctx;
    (void)g;
    mali_backend_drop();
}

const r3d_backend_t *mali_backend(const mali_hw_t *hw)
{
    static const r3d_backend_t be = { .tri = be_tri, .zclear = be_zclear, .colour_only = 1 };
    if (!probed_ok || hw != H)
        return NULL;
    be_hw = hw;
    return &be;
}
