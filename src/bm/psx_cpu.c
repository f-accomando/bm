/*
 * psx_cpu.c: the R3000A of the PlayStation, an interpreter. MIPS I with the branch delay slot, the load delay slot,
 * the system control coprocessor (COP0: status, cause, EPC, exceptions) and the call into the GTE (COP2). Original code.
 *
 * An instruction costs CPI cycles (the real machine takes about one to two; this is a middle value that keeps the
 * timers and the frame in step with real time on slow hosts).
 */
#include "psx_int.h"

#define CPI PSX_CPI

cpu_t C;
uint8_t psx_ram[2 * 1024 * 1024];
uint8_t psx_scratch[1024];

extern int psx_hooks;                  /* psx_bus.c: 1 when pc must be watched (the shell, the HLE BIOS calls) */
extern void psx_hook(void);

static const uint32_t seg_mask[8] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                      0x7FFFFFFFu, 0x1FFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };

/* the load in flight, as the step sees it */
static uint32_t ar, av;

void psx_cpu_reset(void)
{
    memset(&C, 0, sizeof C);
    C.pc = 0xBFC00000u;
    C.npc = C.pc + 4;
    C.cop0[12] = 0x00400000u;           /* SR: BEV */
    C.cop0[15] = 0x00000002u;           /* PRId */
    ar = 0;
}

void psx_cpu_exception(uint32_t code)
{
    uint32_t sr = C.cop0[12];
    C.cop0[12] = (sr & ~0x3Fu) | ((sr << 2) & 0x3Fu);
    uint32_t cause = C.cop0[13] & ~0x8000007Cu;
    cause |= code << 2;
    if (C.in_bd) {
        cause |= 0x80000000u;
        C.cop0[14] = C.cur_pc - 4;
    } else {
        C.cop0[14] = C.cur_pc;
    }
    C.cop0[13] = cause;
    C.pc = (sr & 0x00400000u) ? 0xBFC00180u : 0x80000080u;
    C.npc = C.pc + 4;
    C.next_bd = 0;
}

/* ------------------------------------------------------------------ memory */
#ifdef PSX_PROBE                       /* the host probe only: report the accesses to one address range */
uint32_t psx_watch_lo, psx_watch_hi;
static void watch(const char *k, uint32_t a, uint32_t v)
{
    const uint32_t p = a & 0x1FFFFFFFu;
    if (psx_probe_trace && psx_watch_hi && p >= psx_watch_lo && p < psx_watch_hi)
        printf("  %s %08x = %08x  pc %08x%c", k, a, v, psx_cpu.cur_pc, 10);
}
#define WATCH(k, a, v) watch(k, a, v)
#else
#define WATCH(k, a, v) ((void)0)
#endif

static inline uint32_t mem_read(uint32_t a, int sz)
{
    const uint32_t p = a & seg_mask[a >> 29];
    if (p < 0x00800000u) {
        const uint8_t *m = psx_ram + (p & 0x1FFFFFu);
        WATCH("R", a, m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24);
        if (sz == 4) return (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24;
        if (sz == 2) return (uint32_t)m[0] | (uint32_t)m[1] << 8;
        return m[0];
    }
    return psx_bus_read(p, sz);
}

static inline void mem_write(uint32_t a, uint32_t v, int sz)
{
    if (C.cop0[12] & 0x10000u)          /* the cache is isolated: a write goes to the cache, not to memory */
        return;
    const uint32_t p = a & seg_mask[a >> 29];
    if (p < 0x00800000u) {
        uint8_t *m = psx_ram + (p & 0x1FFFFFu);
        WATCH("W", a, v);
        if (__builtin_expect(psx_jit_pageflags[(p & 0x1FFFFFu) >> 8], 0)) psx_jit_ram_written(p & 0x1FFFFFu, (uint32_t)sz);
        if (sz == 4) { m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); m[2] = (uint8_t)(v >> 16); m[3] = (uint8_t)(v >> 24); }
        else if (sz == 2) { m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); }
        else m[0] = (uint8_t)v;
        return;
    }
    psx_bus_write(p, v, sz);
}

#define SETR(n, v) do { const uint32_t n_ = (n); if (n_) { C.r[n_] = (v); if (n_ == ar) ar = 0; } } while (0)
#define LOAD(n, v) do { const uint32_t n_ = (n); if (n_) { C.ld_r = n_; C.ld_v = (v); } } while (0)

static void bad_addr(uint32_t a, uint32_t code)
{
    C.cop0[8] = a;
    psx_cpu_exception(code);
}

static void cop_unusable(uint32_t n)
{
    C.cop0[13] = (C.cop0[13] & ~0x30000000u) | (n << 28);
    psx_cpu_exception(11);
}

int psx_irq_poll = 1;      /* set when the interrupt state may have changed: the loop looks at it before the next instruction */

static inline __attribute__((always_inline)) void exec(uint32_t i)
{
    const uint32_t op = i >> 26, rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31, sh = (i >> 6) & 31;
    const uint32_t imm = i & 0xFFFFu;
    const int32_t simm = (int16_t)imm;
    uint32_t *r = C.r;
    switch (op) {
    case 0x00:
        switch (i & 63) {
        case 0x00: SETR(rd, r[rt] << sh); break;
        case 0x02: SETR(rd, r[rt] >> sh); break;
        case 0x03: SETR(rd, (uint32_t)((int32_t)r[rt] >> sh)); break;
        case 0x04: SETR(rd, r[rt] << (r[rs] & 31)); break;
        case 0x06: SETR(rd, r[rt] >> (r[rs] & 31)); break;
        case 0x07: SETR(rd, (uint32_t)((int32_t)r[rt] >> (r[rs] & 31))); break;
        case 0x08: C.next_bd = 1; C.npc = r[rs]; break;                                   /* JR */
        case 0x09: { const uint32_t t = r[rs]; SETR(rd, C.cur_pc + 8); C.next_bd = 1; C.npc = t; break; }   /* JALR */
        case 0x0C: psx_cpu_exception(8); break;                                               /* SYSCALL */
        case 0x0D: psx_cpu_exception(9); break;                                               /* BREAK */
        case 0x10: SETR(rd, C.hi); break;
        case 0x11: C.hi = r[rs]; break;
        case 0x12: SETR(rd, C.lo); break;
        case 0x13: C.lo = r[rs]; break;
        case 0x18: { const int64_t p = (int64_t)(int32_t)r[rs] * (int32_t)r[rt]; C.lo = (uint32_t)p; C.hi = (uint32_t)(p >> 32); break; }
        case 0x19: { const uint64_t p = (uint64_t)r[rs] * r[rt]; C.lo = (uint32_t)p; C.hi = (uint32_t)(p >> 32); break; }
        case 0x1A: {
            const int32_t n = (int32_t)r[rs], d = (int32_t)r[rt];
            if (d == 0) { C.hi = (uint32_t)n; C.lo = n < 0 ? 1u : 0xFFFFFFFFu; }
            else if (n == (int32_t)0x80000000 && d == -1) { C.hi = 0; C.lo = 0x80000000u; }
            else { C.lo = (uint32_t)(n / d); C.hi = (uint32_t)(n % d); }
            break;
        }
        case 0x1B: {
            const uint32_t n = r[rs], d = r[rt];
            if (d == 0) { C.hi = n; C.lo = 0xFFFFFFFFu; }
            else { C.lo = n / d; C.hi = n % d; }
            break;
        }
        case 0x20: { const uint32_t s = r[rs] + r[rt]; if (((r[rs] ^ s) & (r[rt] ^ s)) >> 31) psx_cpu_exception(12); else SETR(rd, s); break; }
        case 0x21: SETR(rd, r[rs] + r[rt]); break;
        case 0x22: { const uint32_t s = r[rs] - r[rt]; if (((r[rs] ^ r[rt]) & (r[rs] ^ s)) >> 31) psx_cpu_exception(12); else SETR(rd, s); break; }
        case 0x23: SETR(rd, r[rs] - r[rt]); break;
        case 0x24: SETR(rd, r[rs] & r[rt]); break;
        case 0x25: SETR(rd, r[rs] | r[rt]); break;
        case 0x26: SETR(rd, r[rs] ^ r[rt]); break;
        case 0x27: SETR(rd, ~(r[rs] | r[rt])); break;
        case 0x2A: SETR(rd, (int32_t)r[rs] < (int32_t)r[rt]); break;
        case 0x2B: SETR(rd, r[rs] < r[rt]); break;
        default: psx_cpu_exception(10); break;
        }
        break;
    case 0x01: {                                                                          /* REGIMM */
        const int ge = (rt & 1), link = (rt & 0x1E) == 0x10;
        const int taken = ge ? (int32_t)r[rs] >= 0 : (int32_t)r[rs] < 0;
        C.next_bd = 1;
        if (link) { r[31] = C.cur_pc + 8; if (ar == 31) ar = 0; }
        if (taken) C.npc = C.pc + ((uint32_t)simm << 2);
        break;
    }
    case 0x02: C.next_bd = 1; C.npc = (C.pc & 0xF0000000u) | ((i & 0x03FFFFFFu) << 2); break;
    case 0x03: r[31] = C.cur_pc + 8; if (ar == 31) ar = 0; C.next_bd = 1; C.npc = (C.pc & 0xF0000000u) | ((i & 0x03FFFFFFu) << 2); break;
    case 0x04: C.next_bd = 1; if (r[rs] == r[rt]) C.npc = C.pc + ((uint32_t)simm << 2); break;
    case 0x05: C.next_bd = 1; if (r[rs] != r[rt]) C.npc = C.pc + ((uint32_t)simm << 2); break;
    case 0x06: C.next_bd = 1; if ((int32_t)r[rs] <= 0) C.npc = C.pc + ((uint32_t)simm << 2); break;
    case 0x07: C.next_bd = 1; if ((int32_t)r[rs] > 0) C.npc = C.pc + ((uint32_t)simm << 2); break;
    case 0x08: { const uint32_t s = r[rs] + (uint32_t)simm; if (((r[rs] ^ s) & ((uint32_t)simm ^ s)) >> 31) psx_cpu_exception(12); else SETR(rt, s); break; }
    case 0x09: SETR(rt, r[rs] + (uint32_t)simm); break;
    case 0x0A: SETR(rt, (int32_t)r[rs] < simm); break;
    case 0x0B: SETR(rt, r[rs] < (uint32_t)simm); break;
    case 0x0C: SETR(rt, r[rs] & imm); break;
    case 0x0D: SETR(rt, r[rs] | imm); break;
    case 0x0E: SETR(rt, r[rs] ^ imm); break;
    case 0x0F: SETR(rt, imm << 16); break;
    case 0x10:                                                                            /* COP0 */
        switch (rs) {
        case 0x00:                                                                        /* MFC0 */
            if (rd == 13) { if (I_STAT & I_MASK) C.cop0[13] |= 0x400u; else C.cop0[13] &= ~0x400u; }
            LOAD(rt, C.cop0[rd]);
            break;
        case 0x04: {                                                                      /* MTC0 */
            const uint32_t v = r[rt];
            switch (rd) {
            case 12: C.cop0[12] = v; psx_jit_sr_changed(); break;
            case 13: C.cop0[13] = (C.cop0[13] & ~0x300u) | (v & 0x300u); break;
            case 3: case 5: case 6: case 7: case 9: case 11: C.cop0[rd] = v; break;
            default: break;
            }
            psx_irq_poll = 1;
            break;
        }
        case 0x10:
            if ((i & 63) == 0x10) { C.cop0[12] = (C.cop0[12] & ~0xFu) | ((C.cop0[12] >> 2) & 0xFu); psx_irq_poll = 1; }   /* RFE */
            else psx_cpu_exception(10);
            break;
        default: psx_cpu_exception(10); break;
        }
        break;
    case 0x11: cop_unusable(1); break;
    case 0x12:                                                                            /* COP2: the GTE */
        if (!(C.cop0[12] & 0x40000000u)) { cop_unusable(2); break; }
        if (i & (1u << 25)) { psx_gte_command(i & 0x1FFFFFFu); break; }
        switch (rs) {
        case 0x00: LOAD(rt, psx_gte_get_data((int)rd)); break;
        case 0x02: LOAD(rt, psx_gte_get_ctl((int)rd)); break;
        case 0x04: psx_gte_set_data((int)rd, r[rt]); break;
        case 0x06: psx_gte_set_ctl((int)rd, r[rt]); break;
        default: psx_cpu_exception(10); break;
        }
        break;
    case 0x13: cop_unusable(3); break;
    case 0x20: { const uint32_t a = r[rs] + (uint32_t)simm; LOAD(rt, (uint32_t)(int32_t)(int8_t)mem_read(a, 1)); break; }
    case 0x21: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 1) bad_addr(a, 4); else LOAD(rt, (uint32_t)(int32_t)(int16_t)mem_read(a, 2)); break; }
    case 0x22: {                                                                          /* LWL */
        const uint32_t a = r[rs] + (uint32_t)simm, w = mem_read(a & ~3u, 4);
        const uint32_t cur = (ar == rt) ? av : r[rt];
        static const uint32_t keep[4] = { 0x00FFFFFFu, 0x0000FFFFu, 0x000000FFu, 0u };
        LOAD(rt, (cur & keep[a & 3]) | (w << (24 - 8 * (a & 3))));
        break;
    }
    case 0x23: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 3) bad_addr(a, 4); else LOAD(rt, mem_read(a, 4)); break; }
    case 0x24: { const uint32_t a = r[rs] + (uint32_t)simm; LOAD(rt, mem_read(a, 1)); break; }
    case 0x25: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 1) bad_addr(a, 4); else LOAD(rt, mem_read(a, 2)); break; }
    case 0x26: {                                                                          /* LWR */
        const uint32_t a = r[rs] + (uint32_t)simm, w = mem_read(a & ~3u, 4);
        const uint32_t cur = (ar == rt) ? av : r[rt];
        static const uint32_t keep[4] = { 0u, 0xFF000000u, 0xFFFF0000u, 0xFFFFFF00u };
        LOAD(rt, (cur & keep[a & 3]) | (w >> (8 * (a & 3))));
        break;
    }
    case 0x28: { const uint32_t a = r[rs] + (uint32_t)simm; mem_write(a, r[rt], 1); break; }
    case 0x29: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 1) bad_addr(a, 5); else mem_write(a, r[rt], 2); break; }
    case 0x2A: {                                                                          /* SWL */
        const uint32_t a = r[rs] + (uint32_t)simm, w = mem_read(a & ~3u, 4);
        static const uint32_t keep[4] = { 0xFFFFFF00u, 0xFFFF0000u, 0xFF000000u, 0u };
        mem_write(a & ~3u, (w & keep[a & 3]) | (r[rt] >> (24 - 8 * (a & 3))), 4);
        break;
    }
    case 0x2B: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 3) bad_addr(a, 5); else mem_write(a, r[rt], 4); break; }
    case 0x2E: {                                                                          /* SWR */
        const uint32_t a = r[rs] + (uint32_t)simm, w = mem_read(a & ~3u, 4);
        static const uint32_t keep[4] = { 0u, 0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu };
        mem_write(a & ~3u, (w & keep[a & 3]) | (r[rt] << (8 * (a & 3))), 4);
        break;
    }
    case 0x32: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 3) bad_addr(a, 4); else psx_gte_set_data((int)rt, mem_read(a, 4)); break; }   /* LWC2 */
    case 0x3A: { const uint32_t a = r[rs] + (uint32_t)simm; if (a & 3) bad_addr(a, 5); else mem_write(a, psx_gte_get_data((int)rt), 4); break; }    /* SWC2 */
    case 0x30: case 0x31: case 0x33: case 0x38: case 0x39: case 0x3B: cop_unusable(op & 3); break;
    default: psx_cpu_exception(10); break;
    }
}

/* ------------------------------------------------------------------ helpers for the dynamic recompiler */
int psx_jit_on;

/* one instruction run by the interpreter for a block: the state the loop would have set, and the load applied at once */
int psx_jit_exec(uint32_t instr, uint32_t pc, int bd)
{
    C.cur_pc = pc;
    C.in_bd = bd;
    C.pc = pc + 4;
    C.npc = pc + 8;
    C.next_bd = 0;
    ar = 0; av = 0; C.ld_r = 0;
    exec(instr);
    if (C.ld_r) { C.r[C.ld_r] = C.ld_v; C.ld_r = 0; }
    return C.pc != pc + 4 || C.npc != pc + 8 || C.next_bd;
}

uint32_t psx_jit_read(uint32_t a, uint32_t kind_bd, uint32_t pc)
{
    static const uint8_t size[5] = { 4, 2, 2, 1, 1 };
    const uint32_t kind = kind_bd & 15;
    const int sz = size[kind];
    if ((sz == 4 && (a & 3)) || (sz == 2 && (a & 1))) {
        C.cur_pc = pc;
        C.in_bd = (kind_bd >> 4) & 1;
        bad_addr(a, 4);
        C.jit_exc = 1;
        return 0;
    }
    const uint32_t v = mem_read(a, sz);
    if (kind == 1) return (uint32_t)(int32_t)(int16_t)v;
    if (kind == 3) return (uint32_t)(int32_t)(int8_t)v;
    return v;
}

int psx_jit_write(uint32_t a, uint32_t v, uint32_t kind_bd, uint32_t pc)
{
    const uint32_t kind = kind_bd & 15;
    const int sz = kind == 0 ? 4 : kind == 1 ? 2 : 1;
    if ((sz == 4 && (a & 3)) || (sz == 2 && (a & 1))) {
        C.cur_pc = pc;
        C.in_bd = (kind_bd >> 4) & 1;
        bad_addr(a, 5);
        C.jit_exc = 1;
        return 1;
    }
    mem_write(a, v, sz);
    return psx_jit_take_stale();
}

void psx_cpu_run(uint32_t cycles)
{
    const uint64_t stop = C.cycles + cycles;
    while (C.cycles < stop) {
        if (__builtin_expect(psx_hooks | psx_irq_poll, 0)) {
            if (psx_hooks)
                psx_hook();
            if (psx_irq_poll) {
                psx_irq_poll = 0;
                if (I_STAT & I_MASK) C.cop0[13] |= 0x400u; else C.cop0[13] &= ~0x400u;
                if ((C.cop0[12] & 1u) && (C.cop0[13] & C.cop0[12] & 0xFF00u)) {
                    C.in_bd = C.next_bd;
                    C.cur_pc = C.pc;
                    C.cycles += CPI;
                    psx_cpu_exception(0);
                    continue;
                }
            }
        }
        if (psx_jit_on && !psx_hooks && C.next_bd == 0 && C.ld_r == 0) {   /* a block of compiled code, when there is one at the PC */
            const int used = psx_jit_run_block();
            if (used) { C.cycles += (uint32_t)used; continue; }
        }
        const uint32_t pc = C.pc;
        C.in_bd = C.next_bd;
        C.cur_pc = pc;
        if (__builtin_expect(pc & 3, 0)) {
            bad_addr(pc, 4);
            C.cycles += CPI;
            continue;
        }
        uint32_t i;
        const uint32_t pp = pc & seg_mask[pc >> 29];
        if (__builtin_expect(pp < 0x00800000u, 1)) i = *(const uint32_t *)(psx_ram + (pp & 0x1FFFFFu));
        else if (pp >= 0x1FC00000u && pp < 0x1FC80000u) i = *(const uint32_t *)(psx_bios + (pp & 0x7FFFFu));
        else i = mem_read(pc, 4);
        C.pc = C.npc;
        C.npc += 4;
        C.next_bd = 0;
        ar = C.ld_r; av = C.ld_v; C.ld_r = 0;
        exec(i);
        if (ar) C.r[ar] = av;
        C.cycles += CPI;
    }
}
