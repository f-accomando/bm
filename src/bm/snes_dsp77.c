/* snes_dsp77.c: the uPD77C25 core of the DSP-1/2/3/4 cartridges. Original code written from the public description of the chip (see
 * docs/progress/snes-dsp.md; the facts that are not sure are marked there). The chip runs lazily: snes_bus.c brings it up to the CPU's time when the CPU
 * touches DR or SR. It does not run while it waits for the SNES in a one-instruction loop on RQM.
 *
 * Instruction word, 24 bits, top 2 bits = type:
 *   00 OP: PSEL[21:20] ALU[19:16] ASL[15] DPL[14:13] DPH[12:9] RPDCR[8] SRC[7:4] DST[3:0]     01 RT: as OP, then RET
 *   10 JP: BRCH[21:13] NA[12:2]                                                               11 LD: ID[21:6] DST[3:0]
 * The multiplier M:N = K*L is not kept per instruction: M and N are computed when an instruction reads them (the same value, K and L only change at the
 * end of an instruction). */
#include "snes_dsp77.h"

#include <string.h>

enum { FC, FZ, FOV0, FOV1, FS0, FS1 };

static struct {
    uint32_t prog[DSP77_PROG_WORDS];
    uint16_t data[DSP77_DATA_WORDS];
    uint16_t ram[256];
    uint16_t acc[2];                            /* A, B */
    uint16_t tr, trb, dr, si, so, k, l, sr, pc, rp, stk[4];
    uint8_t dp, sp, fl[2][6], idle, loaded;
    int32_t credit;                             /* DSP cycles left over, in 1/65536 */
    uint32_t count;
} D;

#define SR_RQM 0x8000u
#define SR_DRS 0x1000u
#define SR_DRC 0x0400u

void dsp77_reset(void)
{
    memset(D.ram, 0, sizeof D.ram);
    memset(D.fl, 0, sizeof D.fl);
    D.acc[0] = D.acc[1] = D.tr = D.trb = D.dr = D.si = D.so = D.k = D.l = D.sr = D.pc = D.rp = 0;
    memset(D.stk, 0, sizeof D.stk);
    D.dp = D.sp = D.idle = 0;
    D.credit = 0; D.count = 0;
}

int dsp77_ready(void) { return D.loaded; }
void dsp77_unload(void) { D.loaded = 0; }
int dsp77_idle(void) { return D.idle; }
uint32_t dsp77_instr_count(void) { return D.count; }

void dsp77_dbg(dsp77_dbg_t *d)
{
    d->a = D.acc[0]; d->b = D.acc[1]; d->tr = D.tr; d->trb = D.trb; d->dr = D.dr; d->si = D.si; d->so = D.so; d->k = D.k; d->l = D.l; d->sr = D.sr;
    d->pc = D.pc; d->rp = D.rp; d->dp = D.dp; d->sp = D.sp;
    for (int a = 0; a < 2; a++) { uint8_t f = 0; for (int i = 0; i < 6; i++) f |= (uint8_t)(D.fl[a][i] << i); if (a) d->fb = f; else d->fa = f; }
    memcpy(d->ram, D.ram, sizeof D.ram);
}

/* ------------------------------------------------------------------ the firmware */
static uint32_t word3(const uint8_t *p, int be) { return be ? ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2] : ((uint32_t)p[2] << 16) | (p[1] << 8) | p[0]; }
static int jp_score(const uint8_t *p, int be)      /* a jump word (type 10) has its two low bits clear: count the jump words that do */
{
    int s = 0;
    for (int i = 0; i < DSP77_PROG_WORDS; i++) { const uint32_t w = word3(p + 3 * i, be); if ((w >> 22) == 2) s += (w & 3) == 0 ? 1 : -1; }
    return s;
}

int dsp77_load(const uint8_t *prog, size_t nprog, const uint8_t *data, size_t ndata, const char **why)
{
    static const char *dummy;
    if (!why) why = &dummy;
    if (nprog != DSP77_PROG_BYTES || ndata != DSP77_DATA_BYTES) { *why = "the DSP firmware must be 6144 bytes of program and 2048 bytes of data (8192 in one file)"; return -1; }
    const int be = jp_score(prog, 1) > jp_score(prog, 0);
    for (int i = 0; i < DSP77_PROG_WORDS; i++) D.prog[i] = word3(prog + 3 * i, be);
    for (int i = 0; i < DSP77_DATA_WORDS; i++) D.data[i] = be ? (uint16_t)((data[2 * i] << 8) | data[2 * i + 1]) : (uint16_t)((data[2 * i + 1] << 8) | data[2 * i]);
    D.loaded = 1;
    dsp77_reset();
    return 0;
}

int dsp77_load_combined(const uint8_t *d, size_t n, const char **why)
{
    if (n != DSP77_PROG_BYTES + DSP77_DATA_BYTES) { static const char *dummy; (why ? why : &dummy)[0] = "the DSP firmware file must be 8192 bytes"; return -1; }
    return dsp77_load(d, DSP77_PROG_BYTES, d + DSP77_PROG_BYTES, DSP77_DATA_BYTES, why);
}

/* ------------------------------------------------------------------ the SNES side */
uint8_t dsp77_read_sr(void) { return (uint8_t)(D.sr >> 8); }

uint8_t dsp77_read_dr(void)
{
    D.idle = 0;
    if (D.sr & SR_DRC) { D.sr &= ~SR_RQM; return (uint8_t)D.dr; }                 /* 8-bit transfers */
    if (!(D.sr & SR_DRS)) { D.sr |= SR_DRS; return (uint8_t)D.dr; }               /* 16-bit: low byte, then high byte */
    D.sr &= ~(SR_RQM | SR_DRS);
    return (uint8_t)(D.dr >> 8);
}

void dsp77_write_dr(uint8_t v)
{
    D.idle = 0;
    if (D.sr & SR_DRC) { D.sr &= ~SR_RQM; D.dr = (uint16_t)((D.dr & 0xFF00) | v); return; }
    if (!(D.sr & SR_DRS)) { D.sr |= SR_DRS; D.dr = (uint16_t)((D.dr & 0xFF00) | v); return; }
    D.sr &= ~(SR_RQM | SR_DRS);
    D.dr = (uint16_t)((v << 8) | (D.dr & 0xFF));
}

/* ------------------------------------------------------------------ the instructions */
static void put(unsigned dst, unsigned v)                   /* v: the ID bus */
{
    switch (dst) {
    case 1: D.acc[0] = (uint16_t)v; break;
    case 2: D.acc[1] = (uint16_t)v; break;
    case 3: D.tr = (uint16_t)v; break;
    case 4: D.dp = (uint8_t)v; break;
    case 5: D.rp = (uint16_t)(v & 0x3FF); break;
    case 6: D.dr = (uint16_t)v; D.sr |= SR_RQM; break;
    case 7: D.sr = (uint16_t)((D.sr & 0x907C) | (v & ~0x907Cu)); break;
    case 8: case 9: D.so = (uint16_t)v; break;
    case 10: D.k = (uint16_t)v; break;
    case 11: D.k = (uint16_t)v; D.l = D.data[D.rp]; break;
    case 12: D.l = (uint16_t)v; D.k = D.ram[D.dp | 0x40]; break;
    case 13: D.l = (uint16_t)v; break;
    case 14: D.trb = (uint16_t)v; break;
    case 15: D.ram[D.dp] = (uint16_t)v; break;
    default: break;
    }
}

static void do_op(uint32_t op)
{
    const unsigned ps = op >> 20 & 3, alu = op >> 16 & 15, asl = op >> 15 & 1, dpl = op >> 13 & 3, dphm = op >> 9 & 15, src = op >> 4 & 15;
    unsigned idb;
    switch (src) {
    case 0: idb = D.trb; break;
    case 1: idb = D.acc[0]; break;
    case 2: idb = D.acc[1]; break;
    case 3: idb = D.tr; break;
    case 4: idb = D.dp; break;
    case 5: idb = D.rp; break;
    case 6: idb = D.data[D.rp]; break;
    case 7: idb = 0x8000u - D.fl[0][FS1]; break;
    case 8: idb = D.dr; D.sr |= SR_RQM; break;
    case 9: idb = D.dr; break;
    case 10: idb = D.sr; break;
    case 11: case 12: idb = D.si; break;
    case 13: idb = D.k; break;
    case 14: idb = D.l; break;
    default: idb = D.ram[D.dp]; break;
    }
    if (alu) {
        uint8_t *f = D.fl[asl];
        const unsigned oc = D.fl[asl ^ 1][FC], old_ov1 = f[FOV1], q = D.acc[asl];
        unsigned p, r;
        int c = 0, ov = 0, arith = 0;
        if (ps == 0) p = D.ram[D.dp];
        else if (ps == 1) p = idb;
        else { const int32_t m = (int32_t)(int16_t)D.k * (int16_t)D.l; p = ps == 2 ? (uint16_t)(m >> 15) : (uint16_t)(m << 1); }
        switch (alu) {
        case 1: r = q | p; break;
        case 2: r = q & p; break;
        case 3: r = q ^ p; break;
        case 4: case 6: case 8: {                                       /* SUB, SBB, DEC */
            const unsigned cin = alu == 6 ? oc : 0;
            if (alu == 8) p = 1;
            r = q - p - cin; c = q < p + cin; ov = (int)((q ^ p) & (q ^ r) & 0x8000) != 0; arith = 1; break; }
        case 5: case 7: case 9: {                                       /* ADD, ADC, INC */
            const unsigned cin = alu == 7 ? oc : 0;
            if (alu == 9) p = 1;
            r = q + p + cin; c = (r >> 16) & 1; ov = (int)(~(q ^ p) & (q ^ r) & 0x8000) != 0; arith = 1; break; }
        case 10: r = ~q; break;                                         /* CMP: the 1's complement */
        case 11: r = (q >> 1) | (q & 0x8000); c = q & 1; break;        /* SHR1: the sign stays */
        case 12: r = (q << 1) | oc; c = q >> 15; break;
        case 13: r = (q << 2) | 3; c = (q >> 14) & 1; break;
        case 14: r = (q << 4) | 15; c = (q >> 12) & 1; break;
        default: r = (q << 8) | (q >> 8); break;                        /* 15: XCHG the bytes */
        }
        r &= 0xFFFF;
        D.acc[asl] = (uint16_t)r;
        f[FC] = (uint8_t)c; f[FZ] = r == 0; f[FS0] = (uint8_t)(r >> 15);
        f[FOV0] = (uint8_t)ov; f[FOV1] = arith ? (uint8_t)(old_ov1 ^ (unsigned)ov) : 0;
        if (!old_ov1) f[FS1] = f[FS0];
    }
    put(op & 15, idb);
    switch (dpl) {                                                      /* the low nibble of DP */
    case 1: D.dp = (uint8_t)((D.dp & 0xF0) | ((D.dp + 1) & 15)); break;
    case 2: D.dp = (uint8_t)((D.dp & 0xF0) | ((D.dp - 1) & 15)); break;
    case 3: D.dp &= 0xF0; break;
    default: break;
    }
    D.dp = (uint8_t)((D.dp & 0x0F) | ((D.dp ^ (dphm << 4)) & 0xF0));    /* the high nibble: XOR */
    if (op & 0x100) D.rp = (uint16_t)((D.rp - 1) & 0x3FF);
}

static int cond(unsigned br)
{
    if (br >= 0x80 && br < 0xB0) return D.fl[(br >> 2) & 1][((br - 0x80) >> 3)] == ((br >> 1) & 1);     /* group C, Z, OV0, OV1, S0, S1; bit 2: A or B; bit 1: set or clear */
    switch (br) {
    case 0xB0: return (D.dp & 15) == 0;
    case 0xB1: return (D.dp & 15) != 0;
    case 0xB2: return (D.dp & 15) == 15;
    case 0xB3: return (D.dp & 15) != 15;
    case 0xB4: case 0xB8: return 1;                                     /* JNSIAK, JNSOAK: the serial port is not emulated, its acknowledges stay 0 */
    case 0xBC: return !(D.sr & SR_RQM);
    case 0xBE: return (D.sr & SR_RQM) != 0;
    default: return 0;
    }
}

static void step(void)
{
    const uint16_t pc0 = D.pc;
    const uint32_t op = D.prog[pc0];
    D.pc = (uint16_t)((pc0 + 1) & 0x7FF);
    switch (op >> 22) {
    case 0: do_op(op); break;
    case 1: do_op(op); D.sp = (uint8_t)((D.sp - 1) & 3); D.pc = D.stk[D.sp]; break;
    case 2: {
        const unsigned br = op >> 13 & 0x1FF, na = op >> 2 & 0x7FF;
        if (br == 0x140) { D.stk[D.sp] = D.pc; D.sp = (uint8_t)((D.sp + 1) & 3); D.pc = (uint16_t)na; }
        else if (br == 0x100 || cond(br)) {
            D.pc = (uint16_t)na;
            if (na == pc0 && (br == 0xBC || br == 0xBE)) D.idle = 1;     /* waits for the SNES: nothing changes until it touches DR */
        }
        break; }
    default: put(op & 15, op >> 6 & 0xFFFF); break;
    }
}

#define DSP_PER_MASTER 23191u                   /* 7.6 MHz / 21.477 MHz in 1/65536 (one instruction per DSP clock) [F] */
void dsp77_run(int master_clocks)
{
    if (!D.loaded || master_clocks <= 0) return;
    if (D.idle) { D.credit = 0; return; }
    const uint64_t t = (uint64_t)D.credit + (uint64_t)(uint32_t)master_clocks * DSP_PER_MASTER;
    int32_t n = (int32_t)(t >> 16);
    D.credit = (int32_t)(t & 0xFFFF);
    while (n-- > 0) {
        step(); D.count++;
        if (D.idle) { D.credit = 0; break; }
    }
}
