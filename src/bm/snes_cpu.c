/*
 * snes_cpu.c: the 65C816 of the Super Nintendo. Emulation and native mode, 8 and 16 bit registers, decimal
 * mode, all 256 opcodes, interrupts, WAI and STP. The time is counted by the bus (every access adds master
 * clocks) and by 6 clocks for the internal cycles of each instruction. Original code.
 *
 * The registers are the fields of one struct. cpu_run copies it to a local variable, and every helper is
 * always_inline and takes a pointer to that local: the address never leaves the function, so GCC keeps the
 * fields in ARM registers (or in the stack frame) and not in the data segment. The master clock is a field of
 * the struct too ("clk"); a slow bus access (a register, not plain memory) stores it to snes_clk and reads it back.
 */
#include "snes_int.h"

/* On the ARMv6 console (Pi Zero W, Pi One) the hot opcodes run in assembly (snes_cpu_arm.S) and the others come back here (exec_op, out of line).
 * The C loop below is the reference and runs on the PC, on the RGB30 (AArch64) and with -DSNES_CPU_C: the golden test must give the same hashes. */
#if defined(__arm__) && !defined(__aarch64__) && !defined(SNES_CPU_C)
#define SNES_CPU_ASM 1
#endif

#define AI static inline __attribute__((always_inline))

typedef struct {
    uint16_t a, x, y, s, d, pc;
    uint32_t db, pb;                    /* kept shifted left by 16 */
    int n, v, z, cf;                    /* the flags N, V, Z, C: 0 or 1 */
    unsigned m:1, xf:1, dec:1, i:1, e:1;/* the flags M, X, D, I and the emulation bit: changed seldom, packed in one word (xf is X, dec is D) */
    int clk;                            /* the master clock while cpu_run works */
} cpu_t;
_Static_assert(__builtin_offsetof(cpu_t, db) == 12 && __builtin_offsetof(cpu_t, pb) == 16 && __builtin_offsetof(cpu_t, n) == 20 && __builtin_offsetof(cpu_t, clk) == 40, "cpu_t layout is used by snes_cpu_arm.S");

static cpu_t C = { .m = 1, .xf = 1, .i = 1, .e = 1 };
int cpu_waiting, cpu_stopped;
/* a plain-memory page is a table lookup and an add to the local clock; the rest is the slow bus, with the clock handed over */
AI uint8_t rd8(cpu_t *cu, uint32_t a)
{
    a &= 0xFFFFFF;
    const unsigned pg = a >> 12;
    const uint8_t *p = snes_rtab[pg];
    if (__builtin_expect(p != 0, 1)) { cu->clk += snes_ctab[pg]; return p[a & 0xFFF]; }
    snes_clk = cu->clk;
    const uint8_t v = snes_read(a);
    cu->clk = snes_clk;
    return v;
}
AI void wr8(cpu_t *cu, uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
#ifdef SNES_TRACE
    if (snes_trace.wr) snes_trace.wr(a, v, cu->pb | (uint16_t)(cu->pc - 1));
#endif
    const unsigned pg = a >> 12;
    uint8_t *p = snes_wtab[pg];
    if (__builtin_expect(p != 0, 1)) { cu->clk += snes_ctab[pg]; p[a & 0xFFF] = v; return; }
    snes_clk = cu->clk;
    snes_write(a, v);
    cu->clk = snes_clk;
}
AI uint16_t rd16(cpu_t *cu, uint32_t a) { uint16_t l = rd8(cu, a); return l | (uint16_t)(rd8(cu, a + 1) << 8); }
AI void wr16(cpu_t *cu, uint32_t a, uint16_t v) { wr8(cu, a, (uint8_t)v); wr8(cu, a + 1, (uint8_t)(v >> 8)); }
/* a read in bank 0 whose second byte wraps in the bank (pointers of the direct page and the stack) */
AI uint16_t rd16b0(cpu_t *cu, uint16_t a) { uint16_t l = rd8(cu, a); return l | (uint16_t)(rd8(cu, (uint16_t)(a + 1)) << 8); }

/* a 16 bit pointer in the program bank (JMP (a,X), JSR (a,X)): the second byte wraps in the bank */
AI uint16_t rd16pb(cpu_t *cu, uint16_t a) { uint16_t l = rd8(cu, cu->pb | a); return l | (uint16_t)(rd8(cu, cu->pb | (uint16_t)(a + 1)) << 8); }

AI uint8_t fetch8(cpu_t *cu) { uint8_t v = rd8(cu, cu->pb | cu->pc); cu->pc++; return v; }
AI uint16_t fetch16(cpu_t *cu) { uint16_t l = fetch8(cu); return l | (uint16_t)(fetch8(cu) << 8); }
AI uint32_t fetch24(cpu_t *cu) { uint32_t l = fetch16(cu); return l | ((uint32_t)fetch8(cu) << 16); }

AI uint8_t getP(cpu_t *cu)
{
    return (uint8_t)((cu->n << 7) | (cu->v << 6) | (cu->m << 5) | (cu->xf << 4) | (cu->dec << 3) | (cu->i << 2) | (cu->z << 1) | cu->cf);
}
AI void setP(cpu_t *cu, uint8_t v)
{
    cu->n = v >> 7 & 1; cu->v = v >> 6 & 1; cu->dec = v >> 3 & 1; cu->i = v >> 2 & 1; cu->z = v >> 1 & 1; cu->cf = v & 1;
    if (cu->e) { cu->m = 1; cu->xf = 1; } else { cu->m = v >> 5 & 1; cu->xf = v >> 4 & 1; }
    if (cu->xf) { cu->x &= 0xFF; cu->y &= 0xFF; }
}

AI void push8(cpu_t *cu, uint8_t v)
{
    wr8(cu, cu->s, v);
    cu->s--;
    if (cu->e) cu->s = 0x100 | (cu->s & 0xFF);
}
AI uint8_t pull8(cpu_t *cu)
{
    cu->s++;
    if (cu->e) cu->s = 0x100 | (cu->s & 0xFF);
    return rd8(cu, cu->s);
}
AI void push16(cpu_t *cu, uint16_t v) { push8(cu, (uint8_t)(v >> 8)); push8(cu, (uint8_t)v); }
AI uint16_t pull16(cpu_t *cu) { uint16_t l = pull8(cu); return l | (uint16_t)(pull8(cu) << 8); }
/* the instructions that the 65C816 added (PEA PEI PER PHD PLD PLB JSL RTL JSR (a,X)) do not wrap at the page of the stack in emulation mode; S gets its page back at the end */
AI void pushN(cpu_t *cu, uint8_t v) { wr8(cu, cu->s, v); cu->s--; }
AI uint8_t pullN(cpu_t *cu) { cu->s++; return rd8(cu, cu->s); }
AI void stk_fix(cpu_t *cu) { if (cu->e) cu->s = (uint16_t)(0x100 | (cu->s & 0xFF)); }
AI void pushN16(cpu_t *cu, uint16_t v) { pushN(cu, (uint8_t)(v >> 8)); pushN(cu, (uint8_t)v); stk_fix(cu); }

AI void nz8(cpu_t *cu, uint8_t v) { cu->n = v >> 7; cu->z = v == 0; }
AI void nz16(cpu_t *cu, uint16_t v) { cu->n = v >> 15; cu->z = v == 0; }
#define NZM(v) do { if (cu->m) nz8(cu, (uint8_t)(v)); else nz16(cu, (uint16_t)(v)); } while (0)
#define NZX(v) do { if (cu->xf) nz8(cu, (uint8_t)(v)); else nz16(cu, (uint16_t)(v)); } while (0)

AI void interrupt(cpu_t *cu, uint16_t vn, uint16_t ve, int brk)
{
    if (!cu->e) push8(cu, (uint8_t)(cu->pb >> 16));
    push16(cu, cu->pc);
    uint8_t p = getP(cu);
    if (cu->e) p = (uint8_t)((p & ~0x30) | 0x20 | (brk ? 0x10 : 0));
    push8(cu, p);
    cu->i = 1; cu->dec = 0; cu->pb = 0;
    cu->pc = rd16(cu, cu->e ? ve : vn);
    cpu_waiting = 0;
    cu->clk += 12;
}


void cpu_reset(void)
{
    cpu_t c = C, *cu = &c;
    cu->a = cu->x = cu->y = cu->d = 0; cu->s = 0x1FF; cu->db = cu->pb = 0;
    cu->n = cu->v = cu->dec = cu->z = cu->cf = 0; cu->m = cu->xf = 1; cu->i = 1; cu->e = 1;
    cpu_waiting = cpu_stopped = 0;
    cu->clk = snes_clk;
    cu->pc = rd16(cu, 0xFFFC);
    snes_clk = cu->clk;
    C = c;
}

void cpu_get(cpu_regs_t *r)
{
    cpu_t *cu = &C;
    r->a = cu->a; r->x = cu->x; r->y = cu->y; r->s = cu->s; r->d = cu->d; r->pc = cu->pc; r->p = getP(cu); r->e = cu->e;
    r->pb = (uint8_t)(cu->pb >> 16); r->db = (uint8_t)(cu->db >> 16);
}

/* for the tests: set the registers (the flag P as setP does: emulation mode forces M and X) */
void cpu_set(const cpu_regs_t *r)
{
    cpu_t *cu = &C;
    cu->a = r->a; cu->x = r->x; cu->y = r->y; cu->s = r->s; cu->d = r->d; cu->pc = r->pc;
    cu->e = r->e != 0;
    cu->pb = (uint32_t)r->pb << 16; cu->db = (uint32_t)r->db << 16;
    setP(cu, r->p);
    cpu_waiting = cpu_stopped = 0;
}

void cpu_info(uint32_t *pc, int *flags)
{
    *pc = C.pb | C.pc;
    *flags = (cpu_waiting ? 1 : 0) | (cpu_stopped ? 2 : 0);
}

/* ------------------------------------------------------------------ addressing modes (24-bit addresses) */
/* The direct page. In emulation mode with DL = 0 an access wraps inside the page (DPA: a 6502 program in page 0 works as on the 6502); otherwise it
 * wraps in bank 0. The modes below return a bank-0 address with DPF (bit 24) set: a 16 bit data access then wraps its second byte in bank 0 too
 * (rd16f, wr16f). rd8 and wr8 mask the bit. */
#define DPF 0x1000000u
#define DPA(x) ((uint16_t)((cu->e && !(cu->d & 0xFF)) ? (cu->d | ((x) & 0xFF)) : (cu->d + (x))))
AI uint32_t a_dp(cpu_t *cu) { return DPA(fetch8(cu)) | DPF; }
AI uint32_t a_dpx(cpu_t *cu) { return DPA(fetch8(cu) + cu->x) | DPF; }
AI uint32_t a_dpy(cpu_t *cu) { return DPA(fetch8(cu) + cu->y) | DPF; }
/* a pointer of the direct page: the second byte is at DPA(o + 1) */
AI uint32_t dp_ptr(cpu_t *cu, unsigned o) { uint16_t l = rd8(cu, DPA(o)); return l | (uint16_t)(rd8(cu, DPA(o + 1)) << 8); }
AI uint32_t a_ind(cpu_t *cu) { return cu->db | dp_ptr(cu, fetch8(cu)); }
/* (dp,X): in emulation mode with DL != 0 the second byte of the pointer wraps inside the page of the first */
AI uint32_t a_indx(cpu_t *cu)
{
    const unsigned o = fetch8(cu) + cu->x;
    if (cu->e && (cu->d & 0xFF)) {
        const uint16_t b = (uint16_t)(cu->d + o);
        uint16_t l = rd8(cu, b);
        l |= (uint16_t)(rd8(cu, (b & 0xFF00) | ((b + 1) & 0xFF)) << 8);
        return cu->db | l;
    }
    return cu->db | dp_ptr(cu, o);
}
AI uint32_t a_indy(cpu_t *cu) { uint32_t b = cu->db | dp_ptr(cu, fetch8(cu)); return (b + cu->y) & 0xFFFFFF; }
AI uint32_t a_lng(cpu_t *cu) { uint16_t p = (uint16_t)(cu->d + fetch8(cu)); return rd16b0(cu, p) | ((uint32_t)rd8(cu, (uint16_t)(p + 2)) << 16); }
AI uint32_t a_lngy(cpu_t *cu) { return (a_lng(cu) + cu->y) & 0xFFFFFF; }
AI uint32_t a_abs(cpu_t *cu) { return cu->db | fetch16(cu); }
AI uint32_t a_absx(cpu_t *cu) { return ((cu->db | fetch16(cu)) + cu->x) & 0xFFFFFF; }
AI uint32_t a_absy(cpu_t *cu) { return ((cu->db | fetch16(cu)) + cu->y) & 0xFFFFFF; }
AI uint32_t a_long(cpu_t *cu) { return fetch24(cu); }
AI uint32_t a_longx(cpu_t *cu) { return (fetch24(cu) + cu->x) & 0xFFFFFF; }
AI uint32_t a_sr(cpu_t *cu) { return (uint16_t)(cu->s + fetch8(cu)) | DPF; }
AI uint32_t a_sry(cpu_t *cu) { uint32_t b = cu->db | rd16b0(cu, (uint16_t)(cu->s + fetch8(cu))); return (b + cu->y) & 0xFFFFFF; }

AI uint16_t rd16f(cpu_t *cu, uint32_t a) { return (a & DPF) ? rd16b0(cu, (uint16_t)a) : rd16(cu, a); }
AI void wr16f(cpu_t *cu, uint32_t a, uint16_t v)
{
    if (a & DPF) { wr8(cu, a, (uint8_t)v); wr8(cu, (uint16_t)(a + 1), (uint8_t)(v >> 8)); } else wr16(cu, a, v);
}
/* a 16 bit read-modify-write writes the high byte first (the 65C816 does) */
AI void wr16hl(cpu_t *cu, uint32_t a, uint16_t v)
{
    wr8(cu, (a & DPF) ? (uint16_t)(a + 1) : a + 1, (uint8_t)(v >> 8));
    wr8(cu, a, (uint8_t)v);
}
AI uint16_t readM(cpu_t *cu, uint32_t a) { return cu->m ? rd8(cu, a) : rd16f(cu, a); }
AI void writeM(cpu_t *cu, uint32_t a, uint16_t v) { if (cu->m) wr8(cu, a, (uint8_t)v); else wr16f(cu, a, v); }
AI uint16_t readX(cpu_t *cu, uint32_t a) { return cu->xf ? rd8(cu, a) : rd16f(cu, a); }
AI uint16_t fetchM(cpu_t *cu) { return cu->m ? fetch8(cu) : fetch16(cu); }
AI uint16_t fetchX(cpu_t *cu) { return cu->xf ? fetch8(cu) : fetch16(cu); }

/* ------------------------------------------------------------------ the ALU */
/* Decimal add and subtract, a digit at a time (v is already the complement of the operand for the subtract). The lower digits are corrected one by one;
 * the top digit is added raw, V is read from that raw sum, and only then the correction and the carry are made (this is what the 65C816 does, and the
 * result of ares and Snes9x on odd digits too). */
static unsigned dec_add(unsigned a, unsigned v, int c, int nib, int sub, int *carry, int *ov, unsigned top)
{
    unsigned r = 0;
    int t = 0;
    for (int i = 0; i < nib; i++) {
        t = (int)((a >> (i * 4)) & 15) + (int)((v >> (i * 4)) & 15) + c;
        if (i == nib - 1) break;
        if (sub) { if (t < 16) t -= 6; } else if (t > 9) t += 6;
        c = t > 15;
        r |= (unsigned)(t & 15) << (i * 4);
    }
    const unsigned raw = r + ((unsigned)t << ((nib - 1) * 4));
    *ov = (~(a ^ v) & (a ^ raw) & top) != 0;
    if (sub) { if (t < 16) t -= 6; } else if (t > 9) t += 6;
    *carry = t > 15;
    return r | ((unsigned)(t & 15) << ((nib - 1) * 4));
}
AI void do_adc(cpu_t *cu, uint16_t v)
{
    const unsigned top = cu->m ? 0x80 : 0x8000, mask = cu->m ? 0xFF : 0xFFFF;
    unsigned a = cu->a & mask, r;
    int c, ov;
    v &= mask;
    if (cu->dec) r = dec_add(a, v, cu->cf, cu->m ? 2 : 4, 0, &c, &ov, top);
    else { r = a + v + cu->cf; c = (r & ~mask) != 0; r &= mask; ov = (~(a ^ v) & (a ^ r) & top) != 0; }
    cu->v = ov;
    cu->cf = c;
    cu->a = cu->m ? (uint16_t)((cu->a & 0xFF00) | r) : (uint16_t)r;
    if (cu->m) nz8(cu, (uint8_t)r); else nz16(cu, (uint16_t)r);
}
AI void do_sbc(cpu_t *cu, uint16_t v)
{
    const unsigned top = cu->m ? 0x80 : 0x8000, mask = cu->m ? 0xFF : 0xFFFF;
    unsigned a = cu->a & mask, r;
    int c, ov;
    v = (uint16_t)(~v) & mask;
    if (cu->dec) r = dec_add(a, v, cu->cf, cu->m ? 2 : 4, 1, &c, &ov, top);
    else { r = a + v + cu->cf; c = (r & ~mask) != 0; r &= mask; ov = (~(a ^ v) & (a ^ r) & top) != 0; }
    cu->v = ov;
    cu->cf = c;
    cu->a = cu->m ? (uint16_t)((cu->a & 0xFF00) | r) : (uint16_t)r;
    if (cu->m) nz8(cu, (uint8_t)r); else nz16(cu, (uint16_t)r);
}
AI void do_cmp(cpu_t *cu, uint16_t r, uint16_t v, int is8)
{
    if (is8) { uint8_t t = (uint8_t)((r & 0xFF) - (v & 0xFF)); cu->cf = (r & 0xFF) >= (v & 0xFF); nz8(cu, t); }
    else { uint16_t t = (uint16_t)(r - v); cu->cf = r >= v; nz16(cu, t); }
}
AI void alu(cpu_t *cu, int k, uint16_t v)
{
    switch (k) {
    case 0: if (cu->m) { cu->a = (cu->a & 0xFF00) | ((cu->a | v) & 0xFF); nz8(cu, (uint8_t)cu->a); } else { cu->a |= v; nz16(cu, cu->a); } break;
    case 1: if (cu->m) { cu->a = (cu->a & 0xFF00) | (cu->a & v & 0xFF); nz8(cu, (uint8_t)cu->a); } else { cu->a &= v; nz16(cu, cu->a); } break;
    case 2: if (cu->m) { cu->a = (cu->a & 0xFF00) | ((cu->a ^ v) & 0xFF); nz8(cu, (uint8_t)cu->a); } else { cu->a ^= v; nz16(cu, cu->a); } break;
    case 3: do_adc(cu, v); break;
    case 5: if (cu->m) { cu->a = (cu->a & 0xFF00) | (v & 0xFF); nz8(cu, (uint8_t)v); } else { cu->a = v; nz16(cu, v); } break;
    case 6: do_cmp(cu, cu->a, v, cu->m); break;
    case 7: do_sbc(cu, v); break;
    }
}

AI uint16_t shiftval(cpu_t *cu, int kind, uint16_t v)    /* 0 ASL 1 LSR 2 ROL 3 ROR 4 INC 5 DEC; the width follows M */
{
    unsigned top = cu->m ? 0x80 : 0x8000, mask = cu->m ? 0xFF : 0xFFFF, r, c;
    switch (kind) {
    case 0: cu->cf = (v & top) != 0; r = (v << 1) & mask; break;
    case 1: cu->cf = v & 1; r = v >> 1; break;
    case 2: c = cu->cf; cu->cf = (v & top) != 0; r = ((v << 1) | c) & mask; break;
    case 3: c = cu->cf; cu->cf = v & 1; r = (v >> 1) | (c ? top : 0); break;
    case 4: r = (v + 1) & mask; break;
    default: r = (v - 1) & mask; break;
    }
    cu->n = (r & top) != 0;
    cu->z = r == 0;
    return (uint16_t)r;
}
AI void wrMrmw(cpu_t *cu, uint32_t a, uint16_t v) { if (cu->m) wr8(cu, a, (uint8_t)v); else wr16hl(cu, a, v); }
AI void rmw(cpu_t *cu, uint32_t a, int kind) { const uint16_t v = shiftval(cu, kind, readM(cu, a)); cu->clk += 6; wrMrmw(cu, a, v); }
AI void accshift(cpu_t *cu, int kind)
{
    if (cu->m) cu->a = (cu->a & 0xFF00) | (uint8_t)shiftval(cu, kind, cu->a & 0xFF);
    else cu->a = shiftval(cu, kind, cu->a);
}
AI void tsb(cpu_t *cu, uint32_t a, int trb)
{
    uint16_t v = readM(cu, a), m = cu->m ? (cu->a & 0xFF) : cu->a;
    cu->z = (v & m) == 0;
    v = trb ? (uint16_t)(v & ~m) : (uint16_t)(v | m);
    cu->clk += 6;
    wrMrmw(cu, a, v);
}
AI void bit(cpu_t *cu, uint16_t v, int imm)
{
    uint16_t m = cu->m ? (cu->a & 0xFF) : cu->a;
    cu->z = (v & m) == 0;
    if (!imm) {
        if (cu->m) { cu->n = v >> 7 & 1; cu->v = v >> 6 & 1; } else { cu->n = v >> 15 & 1; cu->v = v >> 14 & 1; }
    }
}
AI void branch(cpu_t *cu, int cond)
{
    int8_t o = (int8_t)fetch8(cu);
    if (cond) { cu->pc = (uint16_t)(cu->pc + o); cu->clk += 6; }
}
AI uint16_t setXY(cpu_t *cu, uint16_t v) { if (cu->xf) { v &= 0xFF; nz8(cu, (uint8_t)v); } else nz16(cu, v); return v; }

/* an addressing mode of the ALU families (group 01: bbb of bits 2-4) */
AI uint32_t mode01(cpu_t *cu, int bbb)
{
    switch (bbb) {
    case 0: return a_indx(cu);
    case 1: return a_dp(cu);
    case 3: return a_abs(cu);
    case 4: return a_indy(cu);
    case 5: return a_dpx(cu);
    case 6: return a_absy(cu);
    default: return a_absx(cu);
    }
}
/* group 11: the stack-relative, long and long indirect modes */
AI uint32_t mode11(cpu_t *cu, int bbb)
{
    switch (bbb) {
    case 0: return a_sr(cu);
    case 1: return a_lng(cu);
    case 3: return a_long(cu);
    case 4: return a_sry(cu);
    case 5: return a_lngy(cu);
    default: return a_longx(cu);
    }
}

enum { M_INDX, M_DP, M_ABS, M_INDY, M_DPX, M_ABSY, M_ABSX, M_DPY, M_IND, M_SR, M_LNG, M_LONG, M_SRY, M_LNGY, M_LONGX };
enum { K_SH = 8, K_TSB = 14, K_TRB, K_BIT, K_LDY, K_LDX, K_STY, K_STX, K_STZ, K_CPX, K_CPY };   /* kinds 0..7 are the ALU operations (aaa) */
#define MEM(m, k) do { mode = (m); kind = (k); goto mem; } while (0)

/* one instruction after its opcode is fetched: the C loop inlines it; the assembly loop (snes_cpu_arm.S) calls it out of line for the opcodes it does not do itself */
#ifdef SNES_CPU_ASM
#define EXEC __attribute__((noinline, section(".text.snes_cpu_cold")))
#else
#define EXEC AI
#endif
/* The opcodes that a game runs often and the assembly loop does not do (REP, SEP, PLP, PHP, PHB, PLB, XBA, the transfers with D and S, BIT #).
 * They are in one macro so that exec_op (the C loop) and exec_fast (the assembly path) share the code, and the fast ones sit together in the hot block. */
#define FAST_OPS \
    case 0x89: bit(cu, fetchM(cu), 1); return; \
    case 0x1B: cu->s = cu->e ? (uint16_t)(0x100 | (cu->a & 0xFF)) : cu->a; return; \
    case 0x3B: cu->a = cu->s; nz16(cu, cu->a); return; \
    case 0x5B: cu->d = cu->a; nz16(cu, cu->d); return; \
    case 0x7B: cu->a = cu->d; nz16(cu, cu->a); return; \
    case 0xEB: cu->a = (uint16_t)((cu->a << 8) | (cu->a >> 8)); nz8(cu, (uint8_t)cu->a); return; \
    case 0x08: push8(cu, getP(cu)); return; \
    case 0x28: setP(cu, pull8(cu)); return; \
    case 0x0B: pushN16(cu, cu->d); return; \
    case 0x2B: { uint16_t l = pullN(cu); cu->d = (uint16_t)(l | (pullN(cu) << 8)); stk_fix(cu); nz16(cu, cu->d); return; } \
    case 0x8B: push8(cu, (uint8_t)(cu->db >> 16)); return; \
    case 0xAB: { uint8_t b = pullN(cu); stk_fix(cu); cu->db = (uint32_t)b << 16; nz8(cu, b); return; } \
    case 0x4B: push8(cu, (uint8_t)(cu->pb >> 16)); return; \
    case 0xC2: setP(cu, (uint8_t)(getP(cu) & ~fetch8(cu))); return; \
    case 0xE2: setP(cu, (uint8_t)(getP(cu) | fetch8(cu))); return; \

EXEC void exec_op(cpu_t *cu, uint8_t op)
{
    uint32_t a;
    uint16_t v;
    int kind, mode;
    switch (op) {
    FAST_OPS
    /* the operations on a memory operand: the opcode gives a mode and a kind, the address and the operation are computed once, below */
    case 0x06: MEM(M_DP, K_SH + 0);   case 0x0A: accshift(cu, 0); return;   case 0x0E: MEM(M_ABS, K_SH + 0);
    case 0x16: MEM(M_DPX, K_SH + 0);  case 0x1E: MEM(M_ABSX, K_SH + 0);
    case 0x46: MEM(M_DP, K_SH + 1);   case 0x4A: accshift(cu, 1); return;   case 0x4E: MEM(M_ABS, K_SH + 1);
    case 0x56: MEM(M_DPX, K_SH + 1);  case 0x5E: MEM(M_ABSX, K_SH + 1);
    case 0x26: MEM(M_DP, K_SH + 2);   case 0x2A: accshift(cu, 2); return;   case 0x2E: MEM(M_ABS, K_SH + 2);
    case 0x36: MEM(M_DPX, K_SH + 2);  case 0x3E: MEM(M_ABSX, K_SH + 2);
    case 0x66: MEM(M_DP, K_SH + 3);   case 0x6A: accshift(cu, 3); return;   case 0x6E: MEM(M_ABS, K_SH + 3);
    case 0x76: MEM(M_DPX, K_SH + 3);  case 0x7E: MEM(M_ABSX, K_SH + 3);
    case 0xE6: MEM(M_DP, K_SH + 4);   case 0x1A: accshift(cu, 4); return;   case 0xEE: MEM(M_ABS, K_SH + 4);
    case 0xF6: MEM(M_DPX, K_SH + 4);  case 0xFE: MEM(M_ABSX, K_SH + 4);
    case 0xC6: MEM(M_DP, K_SH + 5);   case 0x3A: accshift(cu, 5); return;   case 0xCE: MEM(M_ABS, K_SH + 5);
    case 0xD6: MEM(M_DPX, K_SH + 5);  case 0xDE: MEM(M_ABSX, K_SH + 5);
    case 0x04: MEM(M_DP, K_TSB);
    case 0x0C: MEM(M_ABS, K_TSB);
    case 0x14: MEM(M_DP, K_TRB);
    case 0x1C: MEM(M_ABS, K_TRB);
    case 0x24: MEM(M_DP, K_BIT);
    case 0x2C: MEM(M_ABS, K_BIT);
    case 0x34: MEM(M_DPX, K_BIT);
    case 0x3C: MEM(M_ABSX, K_BIT);
    case 0xA4: MEM(M_DP, K_LDY);
    case 0xAC: MEM(M_ABS, K_LDY);
    case 0xB4: MEM(M_DPX, K_LDY);
    case 0xBC: MEM(M_ABSX, K_LDY);
    case 0xA6: MEM(M_DP, K_LDX);
    case 0xAE: MEM(M_ABS, K_LDX);
    case 0xB6: MEM(M_DPY, K_LDX);
    case 0xBE: MEM(M_ABSY, K_LDX);
    case 0x84: MEM(M_DP, K_STY);
    case 0x8C: MEM(M_ABS, K_STY);
    case 0x94: MEM(M_DPX, K_STY);
    case 0x86: MEM(M_DP, K_STX);
    case 0x8E: MEM(M_ABS, K_STX);
    case 0x96: MEM(M_DPY, K_STX);
    case 0x64: MEM(M_DP, K_STZ);
    case 0x74: MEM(M_DPX, K_STZ);
    case 0x9C: MEM(M_ABS, K_STZ);
    case 0x9E: MEM(M_ABSX, K_STZ);
    case 0xE4: MEM(M_DP, K_CPX);
    case 0xEC: MEM(M_ABS, K_CPX);
    case 0xC4: MEM(M_DP, K_CPY);
    case 0xCC: MEM(M_ABS, K_CPY);
    case 0xA0: v = fetchX(cu); cu->y = v; NZX(v); return;
    case 0xA2: v = fetchX(cu); cu->x = v; NZX(v); return;
    case 0xE0: v = fetchX(cu); do_cmp(cu, cu->x, v, cu->xf); return;
    case 0xC0: v = fetchX(cu); do_cmp(cu, cu->y, v, cu->xf); return;
    case 0xE8: if (cu->xf) { cu->x = (cu->x + 1) & 0xFF; nz8(cu, (uint8_t)cu->x); } else { cu->x++; nz16(cu, cu->x); } return;
    case 0xC8: if (cu->xf) { cu->y = (cu->y + 1) & 0xFF; nz8(cu, (uint8_t)cu->y); } else { cu->y++; nz16(cu, cu->y); } return;
    case 0xCA: if (cu->xf) { cu->x = (cu->x - 1) & 0xFF; nz8(cu, (uint8_t)cu->x); } else { cu->x--; nz16(cu, cu->x); } return;
    case 0x88: if (cu->xf) { cu->y = (cu->y - 1) & 0xFF; nz8(cu, (uint8_t)cu->y); } else { cu->y--; nz16(cu, cu->y); } return;
    /* transfers */
    case 0xAA: cu->x = setXY(cu, cu->a); return;
    case 0xA8: cu->y = setXY(cu, cu->a); return;
    case 0x9B: cu->y = setXY(cu, cu->x); return;
    case 0xBB: cu->x = setXY(cu, cu->y); return;
    case 0xBA: cu->x = setXY(cu, cu->s); return;
    case 0x8A: if (cu->m) { cu->a = (cu->a & 0xFF00) | (cu->x & 0xFF); nz8(cu, (uint8_t)cu->x); } else { cu->a = cu->x; nz16(cu, cu->a); } return;
    case 0x98: if (cu->m) { cu->a = (cu->a & 0xFF00) | (cu->y & 0xFF); nz8(cu, (uint8_t)cu->y); } else { cu->a = cu->y; nz16(cu, cu->a); } return;
    case 0x9A: cu->s = cu->e ? (uint16_t)(0x100 | (cu->x & 0xFF)) : cu->x; return;
    /* the stack */
    case 0x48: if (cu->m) push8(cu, (uint8_t)cu->a); else push16(cu, cu->a); return;
    case 0x68: if (cu->m) { uint8_t b = pull8(cu); cu->a = (cu->a & 0xFF00) | b; nz8(cu, b); } else { cu->a = pull16(cu); nz16(cu, cu->a); } return;
    case 0xDA: if (cu->xf) push8(cu, (uint8_t)cu->x); else push16(cu, cu->x); return;
    case 0x5A: if (cu->xf) push8(cu, (uint8_t)cu->y); else push16(cu, cu->y); return;
    case 0xFA: if (cu->xf) { cu->x = pull8(cu); nz8(cu, (uint8_t)cu->x); } else { cu->x = pull16(cu); nz16(cu, cu->x); } return;
    case 0x7A: if (cu->xf) { cu->y = pull8(cu); nz8(cu, (uint8_t)cu->y); } else { cu->y = pull16(cu); nz16(cu, cu->y); } return;
    case 0xF4: pushN16(cu, fetch16(cu)); return;
    case 0xD4: { const uint16_t p = (uint16_t)(cu->d + fetch8(cu)); pushN16(cu, rd16b0(cu, p)); return; }
    case 0x62: { uint16_t o = fetch16(cu); pushN16(cu, (uint16_t)(cu->pc + o)); return; }
    /* flags */
    case 0x18: cu->cf = 0; return;
    case 0x38: cu->cf = 1; return;
    case 0x58: cu->i = 0; return;
    case 0x78: cu->i = 1; return;
    case 0xB8: cu->v = 0; return;
    case 0xD8: cu->dec = 0; return;
    case 0xF8: cu->dec = 1; return;
    case 0xFB: { int e = cu->e; cu->e = cu->cf; cu->cf = e; if (cu->e) { cu->m = cu->xf = 1; cu->x &= 0xFF; cu->y &= 0xFF; cu->s = 0x100 | (cu->s & 0xFF); } return; }
    case 0xEA: return;
    case 0x42: fetch8(cu); return;
    case 0xCB: cpu_waiting = 1; return;
    case 0xDB: cpu_stopped = 1; return;
    case 0x00: fetch8(cu); interrupt(cu, 0xFFE6, 0xFFFE, 1); return;
    case 0x02: fetch8(cu); interrupt(cu, 0xFFE4, 0xFFF4, 1); return;
    /* branches and jumps */
    case 0x10: branch(cu, !cu->n); return;
    case 0x30: branch(cu, cu->n); return;
    case 0x50: branch(cu, !cu->v); return;
    case 0x70: branch(cu, cu->v); return;
    case 0x90: branch(cu, !cu->cf); return;
    case 0xB0: branch(cu, cu->cf); return;
    case 0xD0: branch(cu, !cu->z); return;
    case 0xF0: branch(cu, cu->z); return;
    case 0x80: branch(cu, 1); return;
    case 0x82: { uint16_t o = fetch16(cu); cu->pc = (uint16_t)(cu->pc + o); return; }
    case 0x4C: cu->pc = fetch16(cu); return;
    case 0x5C: a = fetch24(cu); cu->pc = (uint16_t)a; cu->pb = a & 0xFF0000; return;
    case 0x6C: cu->pc = rd16b0(cu, fetch16(cu)); return;
    case 0x7C: cu->pc = rd16pb(cu, (uint16_t)(fetch16(cu) + cu->x)); return;
    case 0xDC: { uint16_t p = fetch16(cu); a = rd16b0(cu, p) | ((uint32_t)rd8(cu, (uint16_t)(p + 2)) << 16); cu->pc = (uint16_t)a; cu->pb = a & 0xFF0000; return; }
    case 0x20: { uint16_t t = fetch16(cu); push16(cu, (uint16_t)(cu->pc - 1)); cu->pc = t; return; }
    case 0xFC: { uint16_t l = fetch8(cu); pushN16(cu, cu->pc); uint16_t t = (uint16_t)(l | (fetch8(cu) << 8)); cu->pc = rd16pb(cu, (uint16_t)(t + cu->x)); return; }
    case 0x22: { uint16_t l = fetch16(cu); pushN(cu, (uint8_t)(cu->pb >> 16)); a = l | ((uint32_t)fetch8(cu) << 16); pushN16(cu, (uint16_t)(cu->pc - 1)); cu->pc = (uint16_t)a; cu->pb = a & 0xFF0000; return; }
    case 0x60: cu->pc = (uint16_t)(pull16(cu) + 1); return;
    case 0x6B: { uint16_t l = pullN(cu); l |= (uint16_t)(pullN(cu) << 8); cu->pc = (uint16_t)(l + 1); cu->pb = (uint32_t)pullN(cu) << 16; stk_fix(cu); return; }
    case 0x40: setP(cu, pull8(cu)); cu->pc = pull16(cu); if (!cu->e) cu->pb = (uint32_t)pull8(cu) << 16; return;
    case 0x54: case 0x44: {
        int dir = op == 0x54 ? 1 : -1;
        uint8_t db = fetch8(cu), sb = fetch8(cu);
#ifdef SNES_TRACE
        if (snes_trace.mv) snes_trace.mv(((uint32_t)sb << 16) | cu->x, ((uint32_t)db << 16) | cu->y, (uint32_t)cu->a + 1, op == 0x44, cu->pb | (uint16_t)(cu->pc - 3));
#endif
        wr8(cu, ((uint32_t)db << 16) | cu->y, rd8(cu, ((uint32_t)sb << 16) | cu->x));
        if (cu->xf) { cu->x = (cu->x + dir) & 0xFF; cu->y = (cu->y + dir) & 0xFF; } else { cu->x = (uint16_t)(cu->x + dir); cu->y = (uint16_t)(cu->y + dir); }
        cu->a--;
        cu->db = (uint32_t)db << 16;
        cu->clk += 12;
        if (cu->a != 0xFFFF) cu->pc -= 3;
        return;
    }
    default: break;
    }
    /* the ALU families (group 01, 11 and the indirect mode): the mode from bbb of the opcode, the kind from aaa */
    {
        static const uint8_t m01[8] = { M_INDX, M_DP, 0, M_ABS, M_INDY, M_DPX, M_ABSY, M_ABSX };
        static const uint8_t m11[8] = { M_SR, M_LNG, M_LONGX, M_LONG, M_SRY, M_LNGY, M_LONGX, M_LONGX };
        const int aaa = op >> 5, bbb = (op >> 2) & 7;
        kind = aaa;
        if ((op & 3) == 1) {
            if (bbb == 2) { if (aaa == 4) return; v = fetchM(cu); goto do_alu; }
            mode = m01[bbb];
        } else if ((op & 0x1F) == 0x12) mode = M_IND;
        else if ((op & 3) == 3) mode = m11[bbb];
        else return;
    }
mem:
    switch (mode) {
    case M_INDX: a = a_indx(cu); break;
    case M_DP: a = a_dp(cu); break;
    case M_ABS: a = a_abs(cu); break;
    case M_INDY: a = a_indy(cu); break;
    case M_DPX: a = a_dpx(cu); break;
    case M_ABSY: a = a_absy(cu); break;
    case M_ABSX: a = a_absx(cu); break;
    case M_DPY: a = a_dpy(cu); break;
    case M_IND: a = a_ind(cu); break;
    case M_SR: a = a_sr(cu); break;
    case M_LNG: a = a_lng(cu); break;
    case M_LONG: a = a_long(cu); break;
    case M_SRY: a = a_sry(cu); break;
    case M_LNGY: a = a_lngy(cu); break;
    default: a = a_longx(cu); break;
    }
    if (kind < 8) {
        if (kind == 4) { writeM(cu, a, cu->a); return; }
        v = readM(cu, a);
    do_alu:
        alu(cu, kind, v);
        return;
    }
    switch (kind) {
    case K_SH: case K_SH + 1: case K_SH + 2: case K_SH + 3: case K_SH + 4: case K_SH + 5: rmw(cu, a, kind - K_SH); break;
    case K_TSB: tsb(cu, a, 0); break;
    case K_TRB: tsb(cu, a, 1); break;
    case K_BIT: bit(cu, readM(cu, a), 0); break;
    case K_LDY: v = readX(cu, a); cu->y = v; NZX(v); break;
    case K_LDX: v = readX(cu, a); cu->x = v; NZX(v); break;
    case K_STY: if (cu->xf) wr8(cu, a, (uint8_t)cu->y); else wr16f(cu, a, cu->y); break;
    case K_STX: if (cu->xf) wr8(cu, a, (uint8_t)cu->x); else wr16f(cu, a, cu->x); break;
    case K_STZ: writeM(cu, a, 0); break;
    case K_CPX: v = readX(cu, a); do_cmp(cu, cu->x, v, cu->xf); break;
    default: v = readX(cu, a); do_cmp(cu, cu->y, v, cu->xf); break;      /* K_CPY */
    }
    }

#ifdef SNES_CPU_ASM
int cpu_rc, cpu_pg;                             /* what the slow bus helpers hand back to the assembly: the clock and the attention flag */
static cpu_t *cur;
static int pend_of(const cpu_t *cu) { return S.nmi_pending || (S.irq_pending && !cu->i) || cpu_waiting || cpu_stopped; }
int cpu_run_asm(cpu_t *cu, int limit, int pend);        /* snes_cpu_arm.S: returns the count of instructions */
/* the slow bus (a register, not plain memory), called by the assembly with the clock of the moment */
__attribute__((noinline, section(".text.snes_cpu_sr"))) uint8_t cpu_sr(uint32_t a, int clk, int start)
{
    snes_clk = clk; S.line_clk = start;
    const uint8_t v = snes_read(a);
    cpu_rc = snes_clk; cpu_pg = pend_of(cur);
    return v;
}
__attribute__((noinline, section(".text.snes_cpu_sw"))) void cpu_sw(uint32_t a, uint8_t v, int clk, int start)
{
    snes_clk = clk; S.line_clk = start;
    snes_write(a, v);
    cpu_rc = snes_clk; cpu_pg = pend_of(cur);
}
/* an opcode that the assembly does not do: the registers are in the struct, the clock is after the fetch of the opcode; returns the attention flag */
/* the opcodes of FAST_OPS in a small function (the 32 KB exec_op spreads them over many cache lines); the others go to exec_op */
__attribute__((noinline, section(".text.snes_cpu_fast"))) static void exec_fast(cpu_t *cu, uint8_t op)
{
    switch (op) {
    FAST_OPS
    default: exec_op(cu, op); return;
    }
}
__attribute__((noinline, section(".text.snes_cpu_cc"))) int cpu_cold(cpu_t *cu, int op, int start)
{
    S.line_clk = start;
    exec_fast(cu, (uint8_t)op);
    return pend_of(cu);
}
/* an interrupt, WAI or STP is pending: the top of the C loop; returns 1 when this step only idles; the attention flag goes to cpu_pg */
__attribute__((noinline, section(".text.snes_cpu_attn"))) int cpu_attn(cpu_t *cu, int start)
{
    int skip = 0;
    S.line_clk = start;
    if (S.nmi_pending || (S.irq_pending && !cu->i)) {
        uint16_t ivn = 0xFFEE, ive = 0xFFFE;
        if (S.nmi_pending) { S.nmi_pending = 0; ivn = 0xFFEA; ive = 0xFFFA; }
        interrupt(cu, ivn, ive, 0);
    }
    if (cpu_waiting) {
        if (S.irq_pending) cpu_waiting = 0;       /* an IRQ wakes WAI even when I is set */
        else { cu->clk += 8; skip = 1; }
    }
    if (!skip && cpu_stopped) { cu->clk += 8; skip = 1; }
    cpu_pg = pend_of(cu);
    return skip;
}
int cpu_run(int limit)
{
    cpu_t c = C;
    c.clk = snes_clk;
    cur = &c;
    const int n = cpu_run_asm(&c, limit, pend_of(&c));
    C = c;
    snes_clk = c.clk;
    return n;
}
#else
int cpu_run(int limit)
{
    cpu_t c = C, *cu = &c;
    int n = 0;
    cu->clk = snes_clk;
    while (cu->clk < limit) {
        n++;
#ifdef SNES_TRACE
        if (snes_trace.op) snes_trace.op(cu->pb | cu->pc);
        if (snes_trace.opr) { cpu_regs_t tr = { cu->a, cu->x, cu->y, cu->s, cu->d, cu->pc, getP(cu), cu->e, (uint8_t)(cu->pb >> 16), (uint8_t)(cu->db >> 16) }; snes_trace.opr(&tr, cu->clk); }
#endif
        S.line_clk = cu->clk;                       /* the slow bus reads the position in the line (the H and V counters) */
        if (S.nmi_pending || (S.irq_pending && !cu->i)) {
            uint16_t ivn = 0xFFEE, ive = 0xFFFE;
            if (S.nmi_pending) { S.nmi_pending = 0; ivn = 0xFFEA; ive = 0xFFFA; }
            interrupt(cu, ivn, ive, 0);
        }
    if (cpu_waiting) {
        if (S.irq_pending) cpu_waiting = 0;       /* an IRQ wakes WAI even when I is set */
        else { cu->clk += 8; continue; }
    }
    if (cpu_stopped) { cu->clk += 8; continue; }

    cu->clk += 6;                                  /* the internal cycle of an instruction (on average) */
    exec_op(cu, fetch8(cu));
    }
    C = c;
    snes_clk = cu->clk;
    return n;
}

#endif

void cpu_step(void) { cpu_run(snes_clk + 1); }          /* one instruction: it adds at least 6 clocks */

#ifndef SNES_NO_DBG
/* The debug run path (the debug API of the carts, docs/api/snes.md). It is a separate loop, and snes_frame enters it only while a breakpoint is armed
 * (cpu_nbp > 0) and when a cart steps one instruction. The hot loops above have no check for it, so with debugging off they cost nothing.
 * The loop works on the same registers C, in C on every target (it calls exec_op, which the assembly loop calls too). */
uint32_t cpu_bp[CPU_BP_MAX];                    /* the breakpoints: 24-bit addresses (PB:PC of an instruction) */
int cpu_nbp;                                    /* how many are armed */
int cpu_dbg_hit;                                /* the last cpu_run_dbg stopped on a breakpoint */
static int bp_skip;                             /* the next run starts on a breakpoint: execute that instruction first */

int cpu_run_dbg(int limit, int single)
{
    cpu_t c = C, *cu = &c;
    int n = 0, skip = bp_skip;
    bp_skip = 0;
    cpu_dbg_hit = 0;
    cu->clk = snes_clk;
    while (cu->clk < limit) {
        S.line_clk = cu->clk;
        if (S.nmi_pending || (S.irq_pending && !cu->i)) {
            uint16_t ivn = 0xFFEE, ive = 0xFFFE;
            if (S.nmi_pending) { S.nmi_pending = 0; ivn = 0xFFEA; ive = 0xFFFA; }
            interrupt(cu, ivn, ive, 0);
        }
        if (cpu_waiting) {
            if (S.irq_pending) cpu_waiting = 0;
            else { cu->clk += 8; n++; if (single) break; continue; }
        }
        if (cpu_stopped) { cu->clk += 8; n++; if (single) break; continue; }
        if (!single && !skip) {
            const uint32_t pc = cu->pb | cu->pc;
            int hit = 0;
            for (int i = 0; i < cpu_nbp; i++) if (cpu_bp[i] == pc) hit = 1;
            if (hit) { cpu_dbg_hit = 1; bp_skip = 1; break; }
        }
        skip = 0;
        n++;
        cu->clk += 6;
        exec_op(cu, fetch8(cu));
        if (single) break;
    }
    C = c;
    snes_clk = cu->clk;
    return n;
}
void cpu_dbg_clear_stop(void) { bp_skip = 0; }
#endif
