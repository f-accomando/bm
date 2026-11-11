/*
 * snes_spc.c: the sound CPU of the Super Nintendo, the SPC700, and the boot protocol of its ROM. The game sends a
 * sound program through four ports, then talks to it through the same ports. The SPC700 runs all 256 opcodes
 * and the three timers. The boot ROM (the transfer loop) is played by spc_port_write as a state machine: it
 * takes the upload of the program and the jump, with no firmware. The DSP makes no sound: its registers are
 * stored and read back. Original code.
 */
#include "snes_int.h"

#include <string.h>

static uint8_t ram[65536];
static uint8_t dsp[128];
static uint8_t apu_in[4], apu_out[4];
static uint8_t rA, rX, rY, rSP;
static uint16_t rPC;
static int fN, fV, fP, fB, fH, fI, fZ, fC;
static uint8_t ctl, dspaddr;
static uint8_t tgt[3], tout[3];
static unsigned tstage[3];
static int tenable[3];
static unsigned acc16, acc128;
static int running, halted;
static int ipl_state, ipl_expect, ipl_pending;
static uint16_t ipl_dest;
static uint16_t tr[12], tr_s[12];               /* the last port events: 0x1nvv CPU wrote port n, 0x2nvv the sound CPU did */
static int tr_i, tr_si;
static void trace(int who, int n, uint8_t v)
{
    const uint16_t e = (uint16_t)((who << 12) | (n << 8) | v);
    if (who == 1) { tr[tr_i] = e; tr_i = (tr_i + 1) % 12; } else { tr_s[tr_si] = e; tr_si = (tr_si + 1) % 12; }
}
static uint32_t nwrites, nreads;       /* the port traffic, for the debug line */

uint8_t *spc_ram_ptr(void) { return ram; }
uint8_t *spc_dsp_ptr(void) { return dsp; }

static uint8_t rd(uint16_t a)
{
    if (a < 0xF0 || a > 0xFF) return ram[a];
    switch (a) {
    case 0xF2: return dspaddr;
    case 0xF3: return dsp[dspaddr & 0x7F];
    case 0xF4: case 0xF5: case 0xF6: case 0xF7: return apu_in[a - 0xF4];
    case 0xFD: case 0xFE: case 0xFF: { uint8_t v = tout[a - 0xFD]; tout[a - 0xFD] = 0; return v; }
    default: return ram[a];
    }
}

static void wr(uint16_t a, uint8_t v)
{
    ram[a] = v;
    if (a < 0xF0 || a > 0xFF) return;
    switch (a) {
    case 0xF1:
        for (int i = 0; i < 3; i++) {
            int on = (v >> i) & 1;
            if (on && !tenable[i]) { tstage[i] = 0; tout[i] = 0; }
            tenable[i] = on;
        }
        if (v & 0x10) apu_in[0] = apu_in[1] = 0;
        if (v & 0x20) apu_in[2] = apu_in[3] = 0;
        ctl = v;
        break;
    case 0xF2: dspaddr = v; break;
    case 0xF3: if (dspaddr < 128) dsp_write(dspaddr, v); break;
    case 0xF4: case 0xF5: case 0xF6: case 0xF7: apu_out[a - 0xF4] = v; trace(2, a - 0xF4, v); break;
    case 0xFA: case 0xFB: case 0xFC: tgt[a - 0xFA] = v; break;
    default: break;
    }
}

static inline uint8_t f8(void) { return rd(rPC++); }
static inline uint16_t f16(void) { uint16_t l = f8(); return l | (uint16_t)(f8() << 8); }
static inline uint16_t dpa(uint8_t d) { return (uint16_t)((fP << 8) | d); }
static inline uint16_t dp16(uint8_t d) { return (uint16_t)(rd(dpa(d)) | (rd(dpa((uint8_t)(d + 1))) << 8)); }
static inline uint8_t nz(uint8_t v) { fN = v >> 7; fZ = v == 0; return v; }
static inline void push(uint8_t v) { wr((uint16_t)(0x100 | rSP), v); rSP--; }
static inline uint8_t pop(void) { rSP++; return rd((uint16_t)(0x100 | rSP)); }
static uint8_t getP(void)
{
    return (uint8_t)((fN << 7) | (fV << 6) | (fP << 5) | (fB << 4) | (fH << 3) | (fI << 2) | (fZ << 1) | fC);
}
static void setP(uint8_t v)
{
    fN = v >> 7 & 1; fV = v >> 6 & 1; fP = v >> 5 & 1; fB = v >> 4 & 1;
    fH = v >> 3 & 1; fI = v >> 2 & 1; fZ = v >> 1 & 1; fC = v & 1;
}
static void rel(int cond) { int8_t r = (int8_t)f8(); if (cond) rPC = (uint16_t)(rPC + r); }

static uint8_t adc(uint8_t a, uint8_t b)
{
    unsigned r = a + b + fC;
    fH = ((a & 15) + (b & 15) + fC) > 15;
    fV = (~(a ^ b) & (a ^ r) & 0x80) != 0;
    fC = r > 255;
    return nz((uint8_t)r);
}
static uint8_t sbc(uint8_t a, uint8_t b) { return adc(a, (uint8_t)(b ^ 255)); }
static void cmp(uint8_t a, uint8_t b) { int r = a - b; fC = r >= 0; nz((uint8_t)r); }

/* the ALU: 0 OR, 1 AND, 2 EOR, 3 CMP, 4 ADC, 5 SBC; the result is stored unless it is CMP */
static uint8_t alu(int k, uint8_t a, uint8_t b)
{
    switch (k) {
    case 0: return nz(a | b);
    case 1: return nz(a & b);
    case 2: return nz(a ^ b);
    case 3: cmp(a, b); return a;
    case 4: return adc(a, b);
    default: return sbc(a, b);
    }
}

/* 0 ASL, 1 ROL, 2 LSR, 3 ROR, 4 DEC, 5 INC */
static uint8_t unary(int k, uint8_t v)
{
    int c;
    switch (k) {
    case 0: fC = v >> 7; return nz((uint8_t)(v << 1));
    case 1: c = fC; fC = v >> 7; return nz((uint8_t)((v << 1) | c));
    case 2: fC = v & 1; return nz(v >> 1);
    case 3: c = fC; fC = v & 1; return nz((uint8_t)((v >> 1) | (c << 7)));
    case 4: return nz((uint8_t)(v - 1));
    default: return nz((uint8_t)(v + 1));
    }
}

/* the address of the operand of the ALU modes of the low 5 bits m */
static uint16_t addr_of(int m)
{
    switch (m) {
    case 0x04: return dpa(f8());
    case 0x05: return f16();
    case 0x06: return dpa(rX);
    case 0x07: return dp16((uint8_t)(f8() + rX));
    case 0x14: return dpa((uint8_t)(f8() + rX));
    case 0x15: return (uint16_t)(f16() + rX);
    case 0x16: return (uint16_t)(f16() + rY);
    default: return (uint16_t)(dp16(f8()) + rY);       /* 0x17 */
    }
}

static void bitaddr(uint16_t *a, int *b) { uint16_t m = f16(); *a = m & 0x1FFF; *b = m >> 13; }

static void step(void)
{
    const uint8_t op = f8();
    const int lo = op & 0x1F;
    uint16_t a;
    uint8_t v;
    int b;

    if (op < 0xC0 && ((lo >= 4 && lo <= 9) || (lo >= 0x14 && lo <= 0x19))) {      /* the ALU on A and on memory */
        const int k = op >> 5, store = k != 3;
        uint8_t r;
        switch (lo) {
        case 0x08: r = alu(k, rA, f8()); if (store) rA = r; return;
        case 0x09: { uint8_t s = rd(dpa(f8())); a = dpa(f8()); r = alu(k, rd(a), s); if (store) wr(a, r); return; }
        case 0x18: { uint8_t i = f8(); a = dpa(f8()); r = alu(k, rd(a), i); if (store) wr(a, r); return; }
        case 0x19: { uint8_t sy = rd(dpa(rY)); a = dpa(rX); r = alu(k, rd(a), sy); if (store) wr(a, r); return; }
        default: r = alu(k, rA, rd(addr_of(lo))); if (store) rA = r; return;
        }
    }
    if (op < 0xC0 && (lo == 0x0B || lo == 0x0C || lo == 0x1B || lo == 0x1C)) {    /* shifts, inc, dec */
        const int k = op >> 5;
        if (lo == 0x1C) { rA = unary(k, rA); return; }
        a = lo == 0x0B ? dpa(f8()) : lo == 0x0C ? f16() : dpa((uint8_t)(f8() + rX));
        wr(a, unary(k, rd(a)));
        return;
    }
    switch (op) {
    case 0x00: return;
    case 0xC8: cmp(rX, f8()); return;
    case 0x3E: cmp(rX, rd(dpa(f8()))); return;
    case 0x1E: cmp(rX, rd(f16())); return;
    case 0xAD: cmp(rY, f8()); return;
    case 0x7E: cmp(rY, rd(dpa(f8()))); return;
    case 0x5E: cmp(rY, rd(f16())); return;
    case 0x3D: rX = nz((uint8_t)(rX + 1)); return;
    case 0x1D: rX = nz((uint8_t)(rX - 1)); return;
    case 0xFC: rY = nz((uint8_t)(rY + 1)); return;
    case 0xDC: rY = nz((uint8_t)(rY - 1)); return;
    /* moves into registers */
    case 0xE8: rA = nz(f8()); return;
    case 0xE4: rA = nz(rd(dpa(f8()))); return;
    case 0xF4: rA = nz(rd(dpa((uint8_t)(f8() + rX)))); return;
    case 0xE5: rA = nz(rd(f16())); return;
    case 0xF5: rA = nz(rd((uint16_t)(f16() + rX))); return;
    case 0xF6: rA = nz(rd((uint16_t)(f16() + rY))); return;
    case 0xE6: rA = nz(rd(dpa(rX))); return;
    case 0xE7: rA = nz(rd(dp16((uint8_t)(f8() + rX)))); return;
    case 0xF7: rA = nz(rd((uint16_t)(dp16(f8()) + rY))); return;
    case 0xBF: rA = nz(rd(dpa(rX))); rX++; return;
    case 0xCD: rX = nz(f8()); return;
    case 0xF8: rX = nz(rd(dpa(f8()))); return;
    case 0xF9: rX = nz(rd(dpa((uint8_t)(f8() + rY)))); return;
    case 0xE9: rX = nz(rd(f16())); return;
    case 0x8D: rY = nz(f8()); return;
    case 0xEB: rY = nz(rd(dpa(f8()))); return;
    case 0xFB: rY = nz(rd(dpa((uint8_t)(f8() + rX)))); return;
    case 0xEC: rY = nz(rd(f16())); return;
    case 0x7D: rA = nz(rX); return;
    case 0xDD: rA = nz(rY); return;
    case 0x5D: rX = nz(rA); return;
    case 0xFD: rY = nz(rA); return;
    case 0x9D: rX = nz(rSP); return;
    case 0xBD: rSP = rX; return;
    /* moves into memory */
    case 0xC4: wr(dpa(f8()), rA); return;
    case 0xD4: wr(dpa((uint8_t)(f8() + rX)), rA); return;
    case 0xC5: wr(f16(), rA); return;
    case 0xD5: wr((uint16_t)(f16() + rX), rA); return;
    case 0xD6: wr((uint16_t)(f16() + rY), rA); return;
    case 0xC6: wr(dpa(rX), rA); return;
    case 0xC7: wr(dp16((uint8_t)(f8() + rX)), rA); return;
    case 0xD7: wr((uint16_t)(dp16(f8()) + rY), rA); return;
    case 0xAF: wr(dpa(rX), rA); rX++; return;
    case 0xD8: wr(dpa(f8()), rX); return;
    case 0xD9: wr(dpa((uint8_t)(f8() + rY)), rX); return;
    case 0xC9: wr(f16(), rX); return;
    case 0xCB: wr(dpa(f8()), rY); return;
    case 0xDB: wr(dpa((uint8_t)(f8() + rX)), rY); return;
    case 0xCC: wr(f16(), rY); return;
    case 0x8F: v = f8(); wr(dpa(f8()), v); return;
    case 0xFA: v = rd(dpa(f8())); wr(dpa(f8()), v); return;
    /* 16 bits */
    case 0xBA: { uint8_t d = f8(); rA = rd(dpa(d)); rY = rd(dpa((uint8_t)(d + 1))); fN = rY >> 7; fZ = rA == 0 && rY == 0; return; }
    case 0xDA: { uint8_t d = f8(); wr(dpa(d), rA); wr(dpa((uint8_t)(d + 1)), rY); return; }
    case 0x7A: { uint8_t d = f8(); fC = 0; rA = adc(rA, rd(dpa(d))); rY = adc(rY, rd(dpa((uint8_t)(d + 1)))); fZ = rA == 0 && rY == 0; return; }
    case 0x9A: { uint8_t d = f8(); fC = 1; rA = sbc(rA, rd(dpa(d))); rY = sbc(rY, rd(dpa((uint8_t)(d + 1)))); fZ = rA == 0 && rY == 0; return; }
    case 0x5A: { unsigned m = dp16(f8()); int r = (int)((rY << 8) | rA) - (int)m; fC = r >= 0; r &= 0xFFFF; fN = r >> 15; fZ = r == 0; return; }
    case 0x3A: { uint8_t d = f8(); unsigned r = (dp16(d) + 1u) & 0xFFFF; wr(dpa(d), (uint8_t)r); wr(dpa((uint8_t)(d + 1)), (uint8_t)(r >> 8)); fN = r >> 15; fZ = r == 0; return; }
    case 0x1A: { uint8_t d = f8(); unsigned r = (dp16(d) - 1u) & 0xFFFF; wr(dpa(d), (uint8_t)r); wr(dpa((uint8_t)(d + 1)), (uint8_t)(r >> 8)); fN = r >> 15; fZ = r == 0; return; }
    /* the stack and the flags */
    case 0x2D: push(rA); return;
    case 0x4D: push(rX); return;
    case 0x6D: push(rY); return;
    case 0x0D: push(getP()); return;
    case 0xAE: rA = pop(); return;
    case 0xCE: rX = pop(); return;
    case 0xEE: rY = pop(); return;
    case 0x8E: setP(pop()); return;
    case 0x20: fP = 0; return;
    case 0x40: fP = 1; return;
    case 0x60: fC = 0; return;
    case 0x80: fC = 1; return;
    case 0xED: fC ^= 1; return;
    case 0xE0: fV = 0; fH = 0; return;
    case 0xA0: fI = 1; return;
    case 0xC0: fI = 0; return;
    case 0xEF: case 0xFF: halted = 1; return;
    /* branches and jumps */
    case 0x10: rel(!fN); return;
    case 0x30: rel(fN); return;
    case 0x50: rel(!fV); return;
    case 0x70: rel(fV); return;
    case 0x90: rel(!fC); return;
    case 0xB0: rel(fC); return;
    case 0xD0: rel(!fZ); return;
    case 0xF0: rel(fZ); return;
    case 0x2F: rel(1); return;
    case 0x5F: rPC = f16(); return;
    case 0x1F: a = (uint16_t)(f16() + rX); rPC = (uint16_t)(rd(a) | (rd((uint16_t)(a + 1)) << 8)); return;
    case 0x3F: a = f16(); push((uint8_t)(rPC >> 8)); push((uint8_t)rPC); rPC = a; return;
    case 0x4F: v = f8(); push((uint8_t)(rPC >> 8)); push((uint8_t)rPC); rPC = (uint16_t)(0xFF00 | v); return;
    case 0x6F: v = pop(); rPC = (uint16_t)(v | (pop() << 8)); return;
    case 0x7F: setP(pop()); v = pop(); rPC = (uint16_t)(v | (pop() << 8)); return;
    case 0x0F: push((uint8_t)(rPC >> 8)); push((uint8_t)rPC); push(getP()); fB = 1; fI = 0;
        rPC = (uint16_t)(rd(0xFFDE) | (rd(0xFFDF) << 8)); return;
    case 0x2E: a = dpa(f8()); rel(rA != rd(a)); return;
    case 0xDE: a = dpa((uint8_t)(f8() + rX)); rel(rA != rd(a)); return;
    case 0x6E: a = dpa(f8()); v = (uint8_t)(rd(a) - 1); wr(a, v); rel(v != 0); return;
    case 0xFE: rY--; rel(rY != 0); return;
    /* bits */
    case 0x0A: bitaddr(&a, &b); if ((rd(a) >> b) & 1) fC = 1; return;
    case 0x2A: bitaddr(&a, &b); if (!((rd(a) >> b) & 1)) fC = 1; return;
    case 0x4A: bitaddr(&a, &b); if (!((rd(a) >> b) & 1)) fC = 0; return;
    case 0x6A: bitaddr(&a, &b); if ((rd(a) >> b) & 1) fC = 0; return;
    case 0x8A: bitaddr(&a, &b); fC ^= (rd(a) >> b) & 1; return;
    case 0xAA: bitaddr(&a, &b); fC = (rd(a) >> b) & 1; return;
    case 0xCA: bitaddr(&a, &b); v = rd(a); wr(a, fC ? (uint8_t)(v | (1 << b)) : (uint8_t)(v & ~(1 << b))); return;
    case 0xEA: bitaddr(&a, &b); wr(a, (uint8_t)(rd(a) ^ (1 << b))); return;
    case 0x0E: a = f16(); v = rd(a); nz((uint8_t)(rA - v)); wr(a, v | rA); return;
    case 0x4E: a = f16(); v = rd(a); nz((uint8_t)(rA - v)); wr(a, (uint8_t)(v & ~rA)); return;
    /* the odd ones */
    case 0x9F: rA = nz((uint8_t)((rA << 4) | (rA >> 4))); return;
    case 0xCF: { unsigned r = rY * rA; rA = (uint8_t)r; rY = (uint8_t)(r >> 8); nz(rY); return; }
    case 0x9E: {
        unsigned ya = (rY << 8) | rA;
        fV = rY >= rX;
        fH = (rY & 15) >= (rX & 15);
        if (rY < (rX << 1)) {
            if (rX == 0) { rA = 255; rY = 255; } else { rA = (uint8_t)(ya / rX); rY = (uint8_t)(ya % rX); }
        } else {
            unsigned d = ya - ((unsigned)rX << 9);
            rA = (uint8_t)(255 - d / (256 - rX));
            rY = (uint8_t)(rX + d % (256 - rX));
        }
        nz(rA);
        return;
    }
    case 0xDF:
        if (fC || rA > 0x99) { rA = (uint8_t)(rA + 0x60); fC = 1; }
        if (fH || (rA & 15) > 9) rA = (uint8_t)(rA + 6);
        nz(rA);
        return;
    case 0xBE:
        if (!fC || rA > 0x99) { rA = (uint8_t)(rA - 0x60); fC = 0; }
        if (!fH || (rA & 15) > 9) rA = (uint8_t)(rA - 6);
        nz(rA);
        return;
    default: break;
    }
    if ((op & 0x0F) == 0x01) {                          /* TCALL n */
        const int n = op >> 4;
        const uint16_t t = (uint16_t)(0xFFDE - 2 * n);
        push((uint8_t)(rPC >> 8)); push((uint8_t)rPC);
        rPC = (uint16_t)(rd(t) | (rd((uint16_t)(t + 1)) << 8));
        return;
    }
    if ((op & 0x0F) == 0x02 || (op & 0x0F) == 0x03) {   /* SET1, CLR1 (bit 4 of op), BBS, BBC */
        const int bit = op >> 5;
        if ((op & 0x0F) == 0x02) {
            a = dpa(f8());
            v = rd(a);
            wr(a, (op & 0x10) ? (uint8_t)(v & ~(1 << bit)) : (uint8_t)(v | (1 << bit)));
        } else {
            v = rd(dpa(f8()));
            rel(((v >> bit) & 1) == ((op & 0x10) ? 0 : 1));
        }
        return;
    }
}

/* ------------------------------------------------------------------ timers, the boot ROM and the ports */
static void timers(unsigned c)
{
    acc16 += c;
    if (acc16 < 16) return;
    const unsigned n = acc16 >> 4;
    acc16 &= 15;
    if (tenable[2]) {
        tstage[2] += n;
        const unsigned t = tgt[2] ? tgt[2] : 256u;
        if (tstage[2] >= t) { tout[2] = (uint8_t)((tout[2] + tstage[2] / t) & 15); tstage[2] %= t; }
    }
    acc128 += n;
    if (acc128 < 8) return;
    const unsigned m = acc128 >> 3;
    acc128 &= 7;
    for (int i = 0; i < 2; i++) {
        if (!tenable[i]) continue;
        tstage[i] += m;
        const unsigned t = tgt[i] ? tgt[i] : 256u;
        if (tstage[i] >= t) { tout[i] = (uint8_t)((tout[i] + tstage[i] / t) & 15); tstage[i] %= t; }
    }
}

static void ipl_start(void)
{
    running = 0;
    ipl_state = 0;
    ipl_pending = 0;
    apu_out[0] = 0xAA; apu_out[1] = 0xBB; apu_out[2] = 0; apu_out[3] = 0;
}

void spc_reset(void)
{
    memset(apu_in, 0, sizeof apu_in);
    memset(ram, 0, sizeof ram);
    memset(dsp, 0, sizeof dsp);
    ctl = 0x80; dspaddr = 0; halted = 0;
    dsp_set_mem(ram, dsp);
    dsp_reset();
    memset(tgt, 0, sizeof tgt); memset(tout, 0, sizeof tout);
    memset(tstage, 0, sizeof tstage); memset(tenable, 0, sizeof tenable);
    acc16 = acc128 = 0;
    rA = rX = rY = 0; rSP = 0xEF;
    setP(0);
    ipl_start();
}

/* The boot ROM as a state machine. A write to port 0 is the command or the next byte; its data is in port 1.
 * A 16-bit store writes port 1 after port 0, so the work is done at the next read or write of another port. */
static void ipl_sync(void)
{
    if (!ipl_pending) return;
    ipl_pending = 0;
    const uint8_t v = apu_in[0];
    const uint16_t addr = (uint16_t)(apu_in[2] | (apu_in[3] << 8));
    if (ipl_state == 0) {
        if (v != 0xCC) return;
        if (apu_in[1] == 0) { apu_out[0] = v; rPC = addr; running = 1; halted = 0; return; }
        ipl_dest = addr; ipl_expect = 0; ipl_state = 1;
        apu_out[0] = 0xCC;
    } else if (v == ipl_expect) {
        ram[ipl_dest++] = apu_in[1];
        apu_out[0] = v;
        ipl_expect = (ipl_expect + 1) & 255;
    } else if (apu_in[1] == 0) {
        apu_out[0] = v;                             /* the boot ROM echoes the command before the jump too */
        rPC = addr; running = 1; halted = 0;
    } else {
        ipl_dest = addr; ipl_expect = 0;
        apu_out[0] = v;
    }
}

void spc_port_write(int n, uint8_t v)
{
    n &= 3;
    nwrites++;
    trace(1, n, v);
    if (!running) {
        if (n != 1) ipl_sync();
        apu_in[n] = v;
        if (n == 0) ipl_pending = 1;
        return;
    }
    apu_in[n] = v;
}

uint8_t spc_port_read(int n)
{
    nreads++;
    if (!running) ipl_sync();
    return apu_out[n & 3];
}

int spc_run(int cycles)
{
    static unsigned dsp_acc;
    if (!running) ipl_sync();
    int n = 0;
    if (running && !halted) {
        while (n < cycles) {
            if (rPC >= 0xFFC0 && (ctl & 0x80)) { ipl_start(); break; }     /* the program went back to the boot ROM */
            step();
            n += 3;
            if (halted) break;
        }
        timers((unsigned)n);
    } else {
        n = cycles;
    }
    dsp_acc += (unsigned)n;                             /* the DSP makes a sample every 32 cycles */
    while (dsp_acc >= 32) { dsp_tick(); dsp_acc -= 32; }
    return n;
}

void spc_trace(uint16_t *out) { for (int i = 0; i < 12; i++) out[i] = tr[(tr_i + i) % 12]; }
void spc_trace_s(uint16_t *out) { for (int i = 0; i < 12; i++) out[i] = tr_s[(tr_si + i) % 12]; }

void spc_info(int *r, int *h, uint16_t *pc) { *r = running; *h = halted; *pc = rPC; }

void spc_peek(uint16_t addr, uint8_t *out, int n, uint8_t *pin, uint8_t *pout, uint8_t *tout_, uint8_t *regs)
{
    for (int i = 0; i < n; i++) out[i] = ram[(uint16_t)(addr + i)];
    memcpy(pin, apu_in, 4);
    memcpy(pout, apu_out, 4);
    memcpy(tout_, tout, 3);
    regs[0] = ctl; regs[1] = tgt[0]; regs[2] = tgt[1]; regs[3] = tgt[2];
    regs[5] = (uint8_t)nwrites; regs[6] = (uint8_t)(nwrites >> 8); regs[7] = (uint8_t)(nwrites >> 16);
    regs[4] = (uint8_t)(tenable[0] | (tenable[1] << 1) | (tenable[2] << 2));
}
