/*
 * snes_cpu.c: the 65C816 of the Super Nintendo. Emulation and native mode, 8 and 16 bit registers, decimal
 * mode, all 256 opcodes, interrupts, WAI and STP. The time is counted by the bus (every access adds master
 * clocks) and by 6 clocks for the internal cycles of each instruction. Original code.
 */
#include "snes_int.h"

static uint16_t rA, rX, rY, rS, rD, rPC;
static uint32_t rDB, rPB;               /* kept shifted left by 16 */
static int fN, fV, fM = 1, fX = 1, fD, fI = 1, fZ, fC, fE = 1;
int cpu_waiting, cpu_stopped;

static inline uint8_t rd8(uint32_t a) { return snes_read(a & 0xFFFFFF); }
static inline void wr8(uint32_t a, uint8_t v) { snes_write(a & 0xFFFFFF, v); }
static inline uint16_t rd16(uint32_t a) { uint16_t l = rd8(a); return l | (uint16_t)(rd8(a + 1) << 8); }
static inline void wr16(uint32_t a, uint16_t v) { wr8(a, (uint8_t)v); wr8(a + 1, (uint8_t)(v >> 8)); }
/* a read in bank 0 whose second byte wraps in the bank (pointers of the direct page and the stack) */
static inline uint16_t rd16b0(uint16_t a) { uint16_t l = rd8(a); return l | (uint16_t)(rd8((uint16_t)(a + 1)) << 8); }

static inline uint8_t fetch8(void) { uint8_t v = rd8(rPB | rPC); rPC++; return v; }
static inline uint16_t fetch16(void) { uint16_t l = fetch8(); return l | (uint16_t)(fetch8() << 8); }
static inline uint32_t fetch24(void) { uint32_t l = fetch16(); return l | ((uint32_t)fetch8() << 16); }

static inline uint8_t getP(void)
{
    return (uint8_t)((fN << 7) | (fV << 6) | (fM << 5) | (fX << 4) | (fD << 3) | (fI << 2) | (fZ << 1) | fC);
}
static void setP(uint8_t v)
{
    fN = v >> 7 & 1; fV = v >> 6 & 1; fD = v >> 3 & 1; fI = v >> 2 & 1; fZ = v >> 1 & 1; fC = v & 1;
    if (fE) { fM = 1; fX = 1; } else { fM = v >> 5 & 1; fX = v >> 4 & 1; }
    if (fX) { rX &= 0xFF; rY &= 0xFF; }
}

static inline void push8(uint8_t v)
{
    wr8(rS, v);
    rS--;
    if (fE) rS = 0x100 | (rS & 0xFF);
}
static inline uint8_t pull8(void)
{
    rS++;
    if (fE) rS = 0x100 | (rS & 0xFF);
    return rd8(rS);
}
static inline void push16(uint16_t v) { push8((uint8_t)(v >> 8)); push8((uint8_t)v); }
static inline uint16_t pull16(void) { uint16_t l = pull8(); return l | (uint16_t)(pull8() << 8); }

static inline void nz8(uint8_t v) { fN = v >> 7; fZ = v == 0; }
static inline void nz16(uint16_t v) { fN = v >> 15; fZ = v == 0; }
#define NZM(v) do { if (fM) nz8((uint8_t)(v)); else nz16((uint16_t)(v)); } while (0)
#define NZX(v) do { if (fX) nz8((uint8_t)(v)); else nz16((uint16_t)(v)); } while (0)

static void interrupt(uint16_t vn, uint16_t ve, int brk)
{
    if (!fE) push8((uint8_t)(rPB >> 16));
    push16(rPC);
    uint8_t p = getP();
    if (fE) p = (uint8_t)((p & ~0x30) | 0x20 | (brk ? 0x10 : 0));
    push8(p);
    fI = 1; fD = 0; rPB = 0;
    rPC = rd16(fE ? ve : vn);
    cpu_waiting = 0;
    snes_clk += 12;
}

void cpu_reset(void)
{
    rA = rX = rY = rD = 0; rS = 0x1FF; rDB = rPB = 0;
    fN = fV = fD = fZ = fC = 0; fM = fX = 1; fI = 1; fE = 1;
    cpu_waiting = cpu_stopped = 0;
    rPC = rd16(0xFFFC);
}

void cpu_get(cpu_regs_t *r)
{
    r->a = rA; r->x = rX; r->y = rY; r->s = rS; r->d = rD; r->pc = rPC; r->p = getP(); r->e = fE;
    r->pb = (uint8_t)(rPB >> 16); r->db = (uint8_t)(rDB >> 16);
}

void cpu_info(uint32_t *pc, int *flags)
{
    *pc = rPB | rPC;
    *flags = (cpu_waiting ? 1 : 0) | (cpu_stopped ? 2 : 0);
}

/* ------------------------------------------------------------------ addressing modes (24-bit addresses) */
#define DPA(x) ((uint16_t)(rD + (x)))
static inline uint32_t a_dp(void) { return DPA(fetch8()); }
static inline uint32_t a_dpx(void) { return (uint16_t)(rD + fetch8() + rX); }
static inline uint32_t a_dpy(void) { return (uint16_t)(rD + fetch8() + rY); }
static inline uint32_t a_ind(void) { return rDB | rd16b0(DPA(fetch8())); }
static inline uint32_t a_indx(void) { return rDB | rd16b0((uint16_t)(rD + fetch8() + rX)); }
static inline uint32_t a_indy(void) { uint32_t b = rDB | rd16b0(DPA(fetch8())); return (b + rY) & 0xFFFFFF; }
static inline uint32_t a_lng(void) { uint16_t p = DPA(fetch8()); return rd16b0(p) | ((uint32_t)rd8((uint16_t)(p + 2)) << 16); }
static inline uint32_t a_lngy(void) { return (a_lng() + rY) & 0xFFFFFF; }
static inline uint32_t a_abs(void) { return rDB | fetch16(); }
static inline uint32_t a_absx(void) { return ((rDB | fetch16()) + rX) & 0xFFFFFF; }
static inline uint32_t a_absy(void) { return ((rDB | fetch16()) + rY) & 0xFFFFFF; }
static inline uint32_t a_long(void) { return fetch24(); }
static inline uint32_t a_longx(void) { return (fetch24() + rX) & 0xFFFFFF; }
static inline uint32_t a_sr(void) { return (uint16_t)(rS + fetch8()); }
static inline uint32_t a_sry(void) { uint32_t b = rDB | rd16b0((uint16_t)(rS + fetch8())); return (b + rY) & 0xFFFFFF; }

static inline uint16_t readM(uint32_t a) { return fM ? rd8(a) : rd16(a); }
static inline void writeM(uint32_t a, uint16_t v) { if (fM) wr8(a, (uint8_t)v); else wr16(a, v); }
static inline uint16_t readX(uint32_t a) { return fX ? rd8(a) : rd16(a); }
static inline uint16_t fetchM(void) { return fM ? fetch8() : fetch16(); }
static inline uint16_t fetchX(void) { return fX ? fetch8() : fetch16(); }

/* ------------------------------------------------------------------ the ALU */
/* decimal add and subtract, a nibble at a time (v is the complement of the operand for the subtract) */
static unsigned bcd_add(unsigned a, unsigned v, int c, int nib, int *carry)
{
    unsigned r = 0;
    for (int i = 0; i < nib; i++) {
        unsigned t = ((a >> (i * 4)) & 15) + ((v >> (i * 4)) & 15) + (unsigned)c;
        if (t > 9) t += 6;
        c = t > 15;
        r |= (t & 15) << (i * 4);
    }
    *carry = c;
    return r;
}
static unsigned bcd_sub(unsigned a, unsigned v, int c, int nib, int *carry)
{
    unsigned r = 0;
    for (int i = 0; i < nib; i++) {
        unsigned t = ((a >> (i * 4)) & 15) + ((v >> (i * 4)) & 15) + (unsigned)c;
        if (t <= 15) { t -= 6; c = 0; } else c = 1;
        r |= (t & 15) << (i * 4);
    }
    *carry = c;
    return r;
}
static void do_adc(uint16_t v)
{
    const unsigned top = fM ? 0x80 : 0x8000, mask = fM ? 0xFF : 0xFFFF;
    unsigned a = rA & mask, r;
    int c;
    v &= mask;
    if (fD) r = bcd_add(a, v, fC, fM ? 2 : 4, &c);
    else { r = a + v + fC; c = (r & ~mask) != 0; r &= mask; }
    fV = (~(a ^ v) & (a ^ r) & top) != 0;
    fC = c;
    rA = fM ? (uint16_t)((rA & 0xFF00) | r) : (uint16_t)r;
    if (fM) nz8((uint8_t)r); else nz16((uint16_t)r);
}
static void do_sbc(uint16_t v)
{
    const unsigned top = fM ? 0x80 : 0x8000, mask = fM ? 0xFF : 0xFFFF;
    unsigned a = rA & mask, r;
    int c;
    v = (uint16_t)(~v) & mask;
    if (fD) r = bcd_sub(a, v, fC, fM ? 2 : 4, &c);
    else { r = a + v + fC; c = (r & ~mask) != 0; r &= mask; }
    fV = (~(a ^ v) & (a ^ r) & top) != 0;
    fC = c;
    rA = fM ? (uint16_t)((rA & 0xFF00) | r) : (uint16_t)r;
    if (fM) nz8((uint8_t)r); else nz16((uint16_t)r);
}
static void do_cmp(uint16_t r, uint16_t v, int is8)
{
    if (is8) { uint8_t t = (uint8_t)((r & 0xFF) - (v & 0xFF)); fC = (r & 0xFF) >= (v & 0xFF); nz8(t); }
    else { uint16_t t = (uint16_t)(r - v); fC = r >= v; nz16(t); }
}
static void alu(int k, uint16_t v)
{
    switch (k) {
    case 0: if (fM) { rA = (rA & 0xFF00) | ((rA | v) & 0xFF); nz8((uint8_t)rA); } else { rA |= v; nz16(rA); } break;
    case 1: if (fM) { rA = (rA & 0xFF00) | (rA & v & 0xFF); nz8((uint8_t)rA); } else { rA &= v; nz16(rA); } break;
    case 2: if (fM) { rA = (rA & 0xFF00) | ((rA ^ v) & 0xFF); nz8((uint8_t)rA); } else { rA ^= v; nz16(rA); } break;
    case 3: do_adc(v); break;
    case 5: if (fM) { rA = (rA & 0xFF00) | (v & 0xFF); nz8((uint8_t)v); } else { rA = v; nz16(v); } break;
    case 6: do_cmp(rA, v, fM); break;
    case 7: do_sbc(v); break;
    }
}

static uint16_t shiftval(int kind, uint16_t v)    /* 0 ASL 1 LSR 2 ROL 3 ROR 4 INC 5 DEC; the width follows M */
{
    unsigned top = fM ? 0x80 : 0x8000, mask = fM ? 0xFF : 0xFFFF, r, c;
    switch (kind) {
    case 0: fC = (v & top) != 0; r = (v << 1) & mask; break;
    case 1: fC = v & 1; r = v >> 1; break;
    case 2: c = fC; fC = (v & top) != 0; r = ((v << 1) | c) & mask; break;
    case 3: c = fC; fC = v & 1; r = (v >> 1) | (c ? top : 0); break;
    case 4: r = (v + 1) & mask; break;
    default: r = (v - 1) & mask; break;
    }
    fN = (r & top) != 0;
    fZ = r == 0;
    return (uint16_t)r;
}
static void rmw(uint32_t a, int kind) { writeM(a, shiftval(kind, readM(a))); snes_clk += 6; }
static void accshift(int kind)
{
    if (fM) rA = (rA & 0xFF00) | (uint8_t)shiftval(kind, rA & 0xFF);
    else rA = shiftval(kind, rA);
}
static void tsb(uint32_t a, int trb)
{
    uint16_t v = readM(a), m = fM ? (rA & 0xFF) : rA;
    fZ = (v & m) == 0;
    v = trb ? (uint16_t)(v & ~m) : (uint16_t)(v | m);
    writeM(a, v);
    snes_clk += 6;
}
static void bit(uint16_t v, int imm)
{
    uint16_t m = fM ? (rA & 0xFF) : rA;
    fZ = (v & m) == 0;
    if (!imm) {
        if (fM) { fN = v >> 7 & 1; fV = v >> 6 & 1; } else { fN = v >> 15 & 1; fV = v >> 14 & 1; }
    }
}
static void branch(int cond)
{
    int8_t o = (int8_t)fetch8();
    if (cond) { rPC = (uint16_t)(rPC + o); snes_clk += 6; }
}
static uint16_t setXY(uint16_t v) { if (fX) { v &= 0xFF; nz8((uint8_t)v); } else nz16(v); return v; }

/* an addressing mode of the ALU families (group 01: bbb of bits 2-4) */
static uint32_t mode01(int bbb)
{
    switch (bbb) {
    case 0: return a_indx();
    case 1: return a_dp();
    case 3: return a_abs();
    case 4: return a_indy();
    case 5: return a_dpx();
    case 6: return a_absy();
    default: return a_absx();
    }
}
/* group 11: the stack-relative, long and long indirect modes */
static uint32_t mode11(int bbb)
{
    switch (bbb) {
    case 0: return a_sr();
    case 1: return a_lng();
    case 3: return a_long();
    case 4: return a_sry();
    case 5: return a_lngy();
    default: return a_longx();
    }
}
static void alu_at(int aaa, uint32_t a)
{
    if (aaa == 4) writeM(a, rA);
    else alu(aaa, readM(a));
}

void cpu_irq_nmi_check(void)
{
    if (S.nmi_pending) { S.nmi_pending = 0; interrupt(0xFFEA, 0xFFFA, 0); }
    else if (S.irq_pending && !fI) interrupt(0xFFEE, 0xFFFE, 0);
}

void cpu_step(void)
{
    if (S.nmi_pending || (S.irq_pending && !fI)) cpu_irq_nmi_check();
    if (cpu_waiting) {
        if (S.irq_pending) cpu_waiting = 0;       /* an IRQ wakes WAI even when I is set */
        else { snes_clk += 8; return; }
    }
    if (cpu_stopped) { snes_clk += 8; return; }

    snes_clk += 6;                                  /* the internal cycle of an instruction (on average) */
    const uint8_t op = fetch8();
    uint32_t a;
    uint16_t v;
    switch (op) {
    case 0x89: bit(fetchM(), 1); return;
    /* shifts, inc, dec */
    case 0x06: rmw(a_dp(), 0); return;   case 0x0A: accshift(0); return;  case 0x0E: rmw(a_abs(), 0); return;
    case 0x16: rmw(a_dpx(), 0); return;  case 0x1E: rmw(a_absx(), 0); return;
    case 0x46: rmw(a_dp(), 1); return;   case 0x4A: accshift(1); return;  case 0x4E: rmw(a_abs(), 1); return;
    case 0x56: rmw(a_dpx(), 1); return;  case 0x5E: rmw(a_absx(), 1); return;
    case 0x26: rmw(a_dp(), 2); return;   case 0x2A: accshift(2); return;  case 0x2E: rmw(a_abs(), 2); return;
    case 0x36: rmw(a_dpx(), 2); return;  case 0x3E: rmw(a_absx(), 2); return;
    case 0x66: rmw(a_dp(), 3); return;   case 0x6A: accshift(3); return;  case 0x6E: rmw(a_abs(), 3); return;
    case 0x76: rmw(a_dpx(), 3); return;  case 0x7E: rmw(a_absx(), 3); return;
    case 0xE6: rmw(a_dp(), 4); return;   case 0x1A: accshift(4); return;  case 0xEE: rmw(a_abs(), 4); return;
    case 0xF6: rmw(a_dpx(), 4); return;  case 0xFE: rmw(a_absx(), 4); return;
    case 0xC6: rmw(a_dp(), 5); return;   case 0x3A: accshift(5); return;  case 0xCE: rmw(a_abs(), 5); return;
    case 0xD6: rmw(a_dpx(), 5); return;  case 0xDE: rmw(a_absx(), 5); return;
    case 0x04: tsb(a_dp(), 0); return;   case 0x0C: tsb(a_abs(), 0); return;
    case 0x14: tsb(a_dp(), 1); return;   case 0x1C: tsb(a_abs(), 1); return;
    case 0x24: bit(readM(a_dp()), 0); return;   case 0x2C: bit(readM(a_abs()), 0); return;
    case 0x34: bit(readM(a_dpx()), 0); return;  case 0x3C: bit(readM(a_absx()), 0); return;
    /* index registers */
    case 0xA0: v = fetchX(); rY = v; NZX(v); return;
    case 0xA2: v = fetchX(); rX = v; NZX(v); return;
    case 0xA4: v = readX(a_dp()); rY = v; NZX(v); return;
    case 0xAC: v = readX(a_abs()); rY = v; NZX(v); return;
    case 0xB4: v = readX(a_dpx()); rY = v; NZX(v); return;
    case 0xBC: v = readX(a_absx()); rY = v; NZX(v); return;
    case 0xA6: v = readX(a_dp()); rX = v; NZX(v); return;
    case 0xAE: v = readX(a_abs()); rX = v; NZX(v); return;
    case 0xB6: v = readX(a_dpy()); rX = v; NZX(v); return;
    case 0xBE: v = readX(a_absy()); rX = v; NZX(v); return;
    case 0x84: a = a_dp(); if (fX) wr8(a, (uint8_t)rY); else wr16(a, rY); return;
    case 0x8C: a = a_abs(); if (fX) wr8(a, (uint8_t)rY); else wr16(a, rY); return;
    case 0x94: a = a_dpx(); if (fX) wr8(a, (uint8_t)rY); else wr16(a, rY); return;
    case 0x86: a = a_dp(); if (fX) wr8(a, (uint8_t)rX); else wr16(a, rX); return;
    case 0x8E: a = a_abs(); if (fX) wr8(a, (uint8_t)rX); else wr16(a, rX); return;
    case 0x96: a = a_dpy(); if (fX) wr8(a, (uint8_t)rX); else wr16(a, rX); return;
    case 0x64: writeM(a_dp(), 0); return;
    case 0x74: writeM(a_dpx(), 0); return;
    case 0x9C: writeM(a_abs(), 0); return;
    case 0x9E: writeM(a_absx(), 0); return;
    case 0xE0: v = fetchX(); do_cmp(rX, v, fX); return;
    case 0xE4: v = readX(a_dp()); do_cmp(rX, v, fX); return;
    case 0xEC: v = readX(a_abs()); do_cmp(rX, v, fX); return;
    case 0xC0: v = fetchX(); do_cmp(rY, v, fX); return;
    case 0xC4: v = readX(a_dp()); do_cmp(rY, v, fX); return;
    case 0xCC: v = readX(a_abs()); do_cmp(rY, v, fX); return;
    case 0xE8: if (fX) { rX = (rX + 1) & 0xFF; nz8((uint8_t)rX); } else { rX++; nz16(rX); } return;
    case 0xC8: if (fX) { rY = (rY + 1) & 0xFF; nz8((uint8_t)rY); } else { rY++; nz16(rY); } return;
    case 0xCA: if (fX) { rX = (rX - 1) & 0xFF; nz8((uint8_t)rX); } else { rX--; nz16(rX); } return;
    case 0x88: if (fX) { rY = (rY - 1) & 0xFF; nz8((uint8_t)rY); } else { rY--; nz16(rY); } return;
    /* transfers */
    case 0xAA: rX = setXY(rA); return;
    case 0xA8: rY = setXY(rA); return;
    case 0x9B: rY = setXY(rX); return;
    case 0xBB: rX = setXY(rY); return;
    case 0xBA: rX = setXY(rS); return;
    case 0x8A: if (fM) { rA = (rA & 0xFF00) | (rX & 0xFF); nz8((uint8_t)rX); } else { rA = rX; nz16(rA); } return;
    case 0x98: if (fM) { rA = (rA & 0xFF00) | (rY & 0xFF); nz8((uint8_t)rY); } else { rA = rY; nz16(rA); } return;
    case 0x9A: rS = fE ? (uint16_t)(0x100 | (rX & 0xFF)) : rX; return;
    case 0x1B: rS = fE ? (uint16_t)(0x100 | (rA & 0xFF)) : rA; return;
    case 0x3B: rA = rS; nz16(rA); return;
    case 0x5B: rD = rA; nz16(rD); return;
    case 0x7B: rA = rD; nz16(rA); return;
    case 0xEB: rA = (uint16_t)((rA << 8) | (rA >> 8)); nz8((uint8_t)rA); return;
    /* the stack */
    case 0x48: if (fM) push8((uint8_t)rA); else push16(rA); return;
    case 0x68: if (fM) { uint8_t b = pull8(); rA = (rA & 0xFF00) | b; nz8(b); } else { rA = pull16(); nz16(rA); } return;
    case 0xDA: if (fX) push8((uint8_t)rX); else push16(rX); return;
    case 0x5A: if (fX) push8((uint8_t)rY); else push16(rY); return;
    case 0xFA: if (fX) { rX = pull8(); nz8((uint8_t)rX); } else { rX = pull16(); nz16(rX); } return;
    case 0x7A: if (fX) { rY = pull8(); nz8((uint8_t)rY); } else { rY = pull16(); nz16(rY); } return;
    case 0x08: push8(getP()); return;
    case 0x28: setP(pull8()); return;
    case 0x0B: push16(rD); return;
    case 0x2B: rD = pull16(); nz16(rD); return;
    case 0x8B: push8((uint8_t)(rDB >> 16)); return;
    case 0xAB: { uint8_t b = pull8(); rDB = (uint32_t)b << 16; nz8(b); return; }
    case 0x4B: push8((uint8_t)(rPB >> 16)); return;
    case 0xF4: push16(fetch16()); return;
    case 0xD4: push16(rd16b0(DPA(fetch8()))); return;
    case 0x62: { uint16_t o = fetch16(); push16((uint16_t)(rPC + o)); return; }
    /* flags */
    case 0x18: fC = 0; return;
    case 0x38: fC = 1; return;
    case 0x58: fI = 0; return;
    case 0x78: fI = 1; return;
    case 0xB8: fV = 0; return;
    case 0xD8: fD = 0; return;
    case 0xF8: fD = 1; return;
    case 0xC2: setP((uint8_t)(getP() & ~fetch8())); return;
    case 0xE2: setP((uint8_t)(getP() | fetch8())); return;
    case 0xFB: { int e = fE; fE = fC; fC = e; if (fE) { fM = fX = 1; rX &= 0xFF; rY &= 0xFF; rS = 0x100 | (rS & 0xFF); } return; }
    case 0xEA: return;
    case 0x42: fetch8(); return;
    case 0xCB: cpu_waiting = 1; return;
    case 0xDB: cpu_stopped = 1; return;
    case 0x00: fetch8(); interrupt(0xFFE6, 0xFFFE, 1); return;
    case 0x02: fetch8(); interrupt(0xFFE4, 0xFFF4, 1); return;
    /* branches and jumps */
    case 0x10: branch(!fN); return;
    case 0x30: branch(fN); return;
    case 0x50: branch(!fV); return;
    case 0x70: branch(fV); return;
    case 0x90: branch(!fC); return;
    case 0xB0: branch(fC); return;
    case 0xD0: branch(!fZ); return;
    case 0xF0: branch(fZ); return;
    case 0x80: branch(1); return;
    case 0x82: { uint16_t o = fetch16(); rPC = (uint16_t)(rPC + o); return; }
    case 0x4C: rPC = fetch16(); return;
    case 0x5C: a = fetch24(); rPC = (uint16_t)a; rPB = a & 0xFF0000; return;
    case 0x6C: rPC = rd16b0(fetch16()); return;
    case 0x7C: rPC = rd16(rPB | (uint16_t)(fetch16() + rX)); return;
    case 0xDC: { uint16_t p = fetch16(); a = rd16b0(p) | ((uint32_t)rd8((uint16_t)(p + 2)) << 16); rPC = (uint16_t)a; rPB = a & 0xFF0000; return; }
    case 0x20: { uint16_t t = fetch16(); push16((uint16_t)(rPC - 1)); rPC = t; return; }
    case 0xFC: { uint16_t t = fetch16(); push16((uint16_t)(rPC - 1)); rPC = rd16(rPB | (uint16_t)(t + rX)); return; }
    case 0x22: a = fetch24(); push8((uint8_t)(rPB >> 16)); push16((uint16_t)(rPC - 1)); rPC = (uint16_t)a; rPB = a & 0xFF0000; return;
    case 0x60: rPC = (uint16_t)(pull16() + 1); return;
    case 0x6B: rPC = (uint16_t)(pull16() + 1); rPB = (uint32_t)pull8() << 16; return;
    case 0x40: setP(pull8()); rPC = pull16(); if (!fE) rPB = (uint32_t)pull8() << 16; return;
    case 0x54: case 0x44: {
        int dir = op == 0x54 ? 1 : -1;
        uint8_t db = fetch8(), sb = fetch8();
        wr8(((uint32_t)db << 16) | rY, rd8(((uint32_t)sb << 16) | rX));
        if (fX) { rX = (rX + dir) & 0xFF; rY = (rY + dir) & 0xFF; } else { rX = (uint16_t)(rX + dir); rY = (uint16_t)(rY + dir); }
        rA--;
        rDB = (uint32_t)db << 16;
        snes_clk += 12;
        if (rA != 0xFFFF) rPC -= 3;
        return;
    }
    default: break;
    }
    /* the ALU families */
    {
        const int aaa = op >> 5, bbb = (op >> 2) & 7;
        if ((op & 3) == 1) {
            if (bbb == 2) { if (aaa == 4) return; alu(aaa, fetchM()); return; }
            alu_at(aaa, mode01(bbb));
        } else if ((op & 0x1F) == 0x12) {
            alu_at(aaa, a_ind());
        } else if ((op & 3) == 3) {
            alu_at(aaa, mode11(bbb));
        }
    }
}
