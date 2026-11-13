/* The tests of the uPD77C25 core (core/src/bm/snes_dsp77.c) that runs DSP-1/2/3/4, and of the cartridge ports in the map (snes_bus.c). There is no real firmware in
 * the repository: every program here is a small original one, written with the macros below. Run: make test-snes-dsp77. */
#include "snes_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, count;
#define CHECK(name, cond) do { count++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)
#define CHECKV(name, got, want) do { count++; if ((unsigned)(got) != (unsigned)(want)) { fails++; printf("FAIL %s: got %x want %x\n", name, (unsigned)(got), (unsigned)(want)); } } while (0)

/* the instruction words */
#define OP(ps, alu, asl, dpl, dphm, rpd, src, dst) ((uint32_t)((ps) << 20 | (alu) << 16 | (asl) << 15 | (dpl) << 13 | (dphm) << 9 | (rpd) << 8 | (src) << 4 | (dst)))
#define RT(ps, alu, asl, dpl, dphm, rpd, src, dst) ((uint32_t)(1u << 22) | OP(ps, alu, asl, dpl, dphm, rpd, src, dst))
#define JP(br, na) ((uint32_t)(2u << 22 | (br) << 13 | (na) << 2))
#define LD(v, dst) ((uint32_t)(3u << 22 | ((v) & 0xFFFF) << 6 | (dst)))
/* ALU codes, SRC codes, DST codes */
enum { NOP, OR, AND, XOR, SUB, ADD, SBB, ADC, DEC, INC, CMP, SHR1, SHL1, SHL2, SHL4, XCHG };
enum { S_TRB, S_A, S_B, S_TR, S_DP, S_RP, S_ROM, S_SGN, S_DR, S_DRNF, S_SR, S_SIM, S_SIL, S_K, S_L, S_MEM };
enum { D_NON, D_A, D_B, D_TR, D_DP, D_RP, D_DR, D_SR, D_SOL, D_SOM, D_K, D_KLR, D_KLM, D_L, D_TRB, D_MEM };
#define JMP_SELF(n) JP(0x100, n)
enum { F_C = 1, F_Z = 2, F_OV0 = 4, F_OV1 = 8, F_S0 = 16, F_S1 = 32 };

static uint32_t prog[2048];
static uint16_t dat[1024];
static dsp77_dbg_t d;

static void pack(uint8_t *out, int be)
{
    for (int i = 0; i < 2048; i++) {
        const uint32_t w = prog[i];
        if (be) { out[3 * i] = (uint8_t)(w >> 16); out[3 * i + 1] = (uint8_t)(w >> 8); out[3 * i + 2] = (uint8_t)w; }
        else { out[3 * i] = (uint8_t)w; out[3 * i + 1] = (uint8_t)(w >> 8); out[3 * i + 2] = (uint8_t)(w >> 16); }
    }
    for (int i = 0; i < 1024; i++) {
        if (be) { out[6144 + 2 * i] = (uint8_t)(dat[i] >> 8); out[6144 + 2 * i + 1] = (uint8_t)dat[i]; }
        else { out[6144 + 2 * i] = (uint8_t)dat[i]; out[6144 + 2 * i + 1] = (uint8_t)(dat[i] >> 8); }
    }
}
static int load_prog(const uint32_t *p, int n, int be)
{
    static uint8_t img[8192];
    memset(prog, 0, sizeof prog);
    memcpy(prog, p, (size_t)n * 4);
    for (int i = n; i < 2048; i++) prog[i] = JMP_SELF(i);                 /* what runs past the program waits */
    pack(img, be);
    return dsp77_load_combined(img, sizeof img, NULL);
}
static void go(int instrs) { dsp77_run((int)((uint64_t)instrs * 65536 / 23191) + 2); }
/* run a program (a firmware with the data ROM in dat[]) for a time that is plenty */
#define RUN(...) do { const uint32_t p_[] = { __VA_ARGS__ }; load_prog(p_, (int)(sizeof p_ / 4), 0); go(2000); dsp77_dbg(&d); } while (0)

static void test_alu(void)
{
    /* one ALU operation on A (asl 0) or B (asl 1) with TR as the other operand: the results and the flags */
#define ALU(name, asl, a0, tr, alu, want, wflags) do { \
        RUN(LD(a0, D_A + (asl)), LD(tr, D_TR), OP(1, alu, asl, 0, 0, 0, S_TR, D_NON), JMP_SELF(3)); \
        CHECKV(name " result", (asl) ? d.b : d.a, want); CHECKV(name " flags", ((asl) ? d.fb : d.fa) & 0x1F, wflags); } while (0)
    ALU("or", 0, 0x00F0, 0x0F0F, OR, 0x0FFF, 0);
    ALU("and", 0, 0x0FF0, 0x00FF, AND, 0x00F0, 0);
    ALU("xor", 0, 0xFFFF, 0x0F0F, XOR, 0xF0F0, F_S0);
    ALU("xor zero", 0, 0x1234, 0x1234, XOR, 0, F_Z);
    ALU("or on B", 1, 0x0010, 0x0001, OR, 0x0011, 0);
    ALU("sub borrow", 0, 5, 7, SUB, 0xFFFE, F_C | F_S0);
    ALU("sub zero", 0, 7, 7, SUB, 0, F_Z);
    ALU("sub overflow", 0, 0x8000, 1, SUB, 0x7FFF, F_OV0 | F_OV1);
    ALU("add carry", 0, 0xFFFF, 1, ADD, 0, F_C | F_Z);
    ALU("add overflow", 0, 0x7FFF, 1, ADD, 0x8000, F_OV0 | F_OV1 | F_S0);
    ALU("add on B", 1, 0x0100, 0x0023, ADD, 0x0123, 0);
    ALU("dec", 0, 0, 0, DEC, 0xFFFF, F_C | F_S0);
    ALU("inc", 0, 0xFFFF, 0, INC, 0, F_C | F_Z);
    ALU("inc plain", 0, 41, 0, INC, 42, 0);
    ALU("cmp", 0, 0x00FF, 0, CMP, 0xFF00, F_S0);
    ALU("shr1", 0, 0x8001, 0, SHR1, 0xC000, F_C | F_S0);
    ALU("shl1", 0, 0x8001, 0, SHL1, 0x0002, F_C);
    ALU("shl2", 0, 0x4001, 0, SHL2, 0x0007, 0);
    ALU("shl4", 0, 0x1234, 0, SHL4, 0x234F, 0);
    ALU("xchg", 0, 0x1234, 0, XCHG, 0x3412, 0);
    /* ADC, SBB and SHL1 take the carry of the other accumulator */
    RUN(LD(0xFFFF, D_B), LD(1, D_TR), OP(1, ADD, 1, 0, 0, 0, S_TR, D_NON), LD(1, D_A), OP(1, ADC, 0, 0, 0, 0, S_TR, D_NON), JMP_SELF(5));
    CHECKV("adc", d.a, 3); CHECKV("adc B carry", d.fb & F_C, F_C);
    RUN(LD(0xFFFF, D_B), LD(1, D_TR), OP(1, ADD, 1, 0, 0, 0, S_TR, D_NON), LD(5, D_A), OP(1, SBB, 0, 0, 0, 0, S_TR, D_NON), JMP_SELF(5));
    CHECKV("sbb", d.a, 3);
    RUN(LD(0xFFFF, D_B), LD(1, D_TR), OP(1, ADD, 1, 0, 0, 0, S_TR, D_NON), LD(0x8001, D_A), OP(1, SHL1, 0, 0, 0, 0, S_TR, D_NON), JMP_SELF(5));
    CHECKV("shl1 with carry in", d.a, 3);
    RUN(LD(0x1000, D_A), LD(0x0234, D_TR), OP(1, NOP, 0, 0, 0, 0, S_TR, D_NON), JMP_SELF(3));
    CHECKV("alu 0 changes nothing", d.a, 0x1000);
    /* the sign after an overflow: SGN gives 0x7FFF after a positive overflow and 0x8000 after a negative one */
    RUN(LD(0x7FFF, D_A), LD(1, D_TR), OP(1, ADD, 0, 0, 0, 0, S_TR, D_NON), OP(0, NOP, 0, 0, 0, 0, S_SGN, D_TR), JMP_SELF(4));
    CHECKV("sgn positive", d.tr, 0x7FFF);
    RUN(LD(0x8000, D_A), LD(1, D_TR), OP(1, SUB, 0, 0, 0, 0, S_TR, D_NON), OP(0, NOP, 0, 0, 0, 0, S_SGN, D_TR), JMP_SELF(4));
    CHECKV("sgn negative", d.tr, 0x8000);
}

static void test_mul_and_moves(void)
{
    RUN(LD(0x4000, D_K), LD(0x4000, D_L), OP(2, ADD, 0, 0, 0, 0, S_TRB, D_NON), JMP_SELF(3));         /* 0.5 * 0.5 = 0.25: M = 0x2000 */
    CHECKV("mul M", d.a, 0x2000);
    RUN(LD(3, D_K), LD(5, D_L), OP(3, ADD, 0, 0, 0, 0, S_TRB, D_NON), JMP_SELF(3));                  /* N = 2*15 */
    CHECKV("mul N", d.a, 30);
    RUN(LD(0xC000, D_K), LD(0x4000, D_L), OP(2, ADD, 0, 0, 0, 0, S_TRB, D_NON), JMP_SELF(3));         /* -0.5 * 0.5 */
    CHECKV("mul signed", d.a, 0xE000);
    dat[5] = 0x1111;
    RUN(LD(5, D_RP), LD(0x2222, D_A), OP(0, NOP, 0, 0, 0, 0, S_A, D_KLR), OP(0, NOP, 0, 0, 0, 0, S_ROM, D_TR), JMP_SELF(4));
    CHECKV("klr k", d.k, 0x2222); CHECKV("klr l", d.l, 0x1111); CHECKV("rom src", d.tr, 0x1111);
    RUN(LD(0x42, D_DP), LD(0x7777, D_MEM), LD(2, D_DP), LD(0x1000, D_KLM), JMP_SELF(4));
    CHECKV("klm l", d.l, 0x1000); CHECKV("klm k", d.k, 0x7777);
    RUN(LD(3, D_DP), LD(0xBEEF, D_MEM), OP(0, NOP, 0, 0, 0, 0, S_MEM, D_TR), JMP_SELF(3));
    CHECKV("ram", d.tr, 0xBEEF); CHECKV("ram array", d.ram[3], 0xBEEF);
    RUN(LD(0xFFFF, D_SR), OP(0, NOP, 0, 0, 0, 0, S_SR, D_TR), JMP_SELF(2));
    CHECKV("sr write mask", d.sr, 0xFFFF & ~0x907C);
    RUN(LD(0x0123, D_RP), LD(0x77, D_DP), OP(0, NOP, 0, 0, 0, 0, S_RP, D_A), OP(0, NOP, 0, 0, 0, 0, S_DP, D_B), LD(0xFFFF, D_RP), JMP_SELF(5));
    CHECKV("rp src", d.a, 0x0123); CHECKV("dp src", d.b, 0x77); CHECKV("rp is 10 bits", d.rp, 0x3FF);
    RUN(LD(0x25, D_DP), OP(0, NOP, 0, 1, 0, 0, S_TRB, D_NON), JMP_SELF(2));                       /* DPL inc */
    CHECKV("dpl inc", d.dp, 0x26);
    RUN(LD(0x2F, D_DP), OP(0, NOP, 0, 1, 0, 0, S_TRB, D_NON), JMP_SELF(2));
    CHECKV("dpl inc wraps in the nibble", d.dp, 0x20);
    RUN(LD(0x20, D_DP), OP(0, NOP, 0, 2, 0, 0, S_TRB, D_NON), JMP_SELF(2));
    CHECKV("dpl dec wraps in the nibble", d.dp, 0x2F);
    RUN(LD(0x25, D_DP), OP(0, NOP, 0, 3, 0, 0, S_TRB, D_NON), JMP_SELF(2));
    CHECKV("dpl clear", d.dp, 0x20);
    RUN(LD(0x25, D_DP), OP(0, NOP, 0, 0, 3, 0, S_TRB, D_NON), JMP_SELF(2));
    CHECKV("dph xor", d.dp, 0x15);
    RUN(OP(0, NOP, 0, 0, 0, 1, S_TRB, D_NON), JMP_SELF(1));
    CHECKV("rpdcr", d.rp, 0x3FF);
    RUN(LD(0x1234, D_A), LD(7, D_DP), OP(0, NOP, 0, 1, 0, 0, S_A, D_MEM), JMP_SELF(3));            /* the store uses DP before it changes */
    CHECKV("mem store before dpl", d.ram[7], 0x1234); CHECKV("dp after", d.dp, 8);
    /* a load of DP or RP (the destination field) is not followed by the DP and RP changes of the same instruction (the reference core: DPL and DPH only when the destination is not DP, RPDCR only when it is not RP) */
    RUN(LD(0x0123, D_TR), OP(0, NOP, 0, 1, 3, 0, S_TR, D_DP), JMP_SELF(2));
    CHECKV("load DP: no DPL, no DPH", d.dp, 0x23);
    RUN(LD(0x0123, D_TR), OP(0, NOP, 0, 0, 0, 1, S_TR, D_RP), JMP_SELF(2));
    CHECKV("load RP: no RPDCR", d.rp, 0x0123);
    RUN(LD(0x0123, D_TR), OP(0, NOP, 0, 0, 0, 1, S_TR, D_A), JMP_SELF(2));
    CHECKV("RPDCR with another destination", d.rp, 0x3FF);
    /* OV1 when a second overflow comes while OV1 is set: OV1 is then (S0 == S1) after it, not OV1 xor OV0 (the reference core). The first overflow (0x7FFF + 1) sets OV1 and S1 = 1;
     * additions that do not overflow keep OV1 and S1 (S0 goes 1, 1, 0, 0); the last one overflows to a negative result, S0 = S1 = 1, so OV1 stays set. */
    RUN(LD(0x7FFF, D_A), LD(1, D_TR), OP(1, ADD, 0, 0, 0, 0, S_TR, D_NON), LD(0x7FFF, D_TR), OP(1, ADD, 0, 0, 0, 0, S_TR, D_NON),
        LD(1, D_TR), OP(1, ADD, 0, 0, 0, 0, S_TR, D_NON), LD(0x7FFF, D_TR), OP(1, ADD, 0, 0, 0, 0, S_TR, D_NON), OP(1, ADD, 0, 0, 0, 0, S_TR, D_NON), JMP_SELF(10));
    CHECKV("two overflows: A", d.a, 0xFFFE); CHECKV("two overflows: OV0 and OV1 and S1", d.fa & (F_OV0 | F_OV1 | F_S1), F_OV0 | F_OV1 | F_S1);
    RUN(LD(0x0123, D_TRB), OP(0, NOP, 0, 0, 0, 0, S_TRB, D_B), JMP_SELF(2));
    CHECKV("trb", d.b, 0x0123);
    RUN(LD(0x1111, D_A), LD(0x2222, D_K), LD(0x3333, D_L), OP(0, NOP, 0, 0, 0, 0, S_K, D_TR), OP(0, NOP, 0, 0, 0, 0, S_L, D_TRB), JMP_SELF(5));
    CHECKV("k src", d.tr, 0x2222); CHECKV("l src", d.trb, 0x3333);
}

static void test_jumps(void)
{
    RUN(JP(0x100, 3), LD(1, D_TR), JMP_SELF(2), LD(2, D_TR), JMP_SELF(4));
    CHECKV("jmp", d.tr, 2);
    RUN(JP(0x140, 4), LD(0x1111, D_B), JMP_SELF(2), 0, LD(0x2222, D_A), RT(0, 0, 0, 0, 0, 0, S_TRB, D_NON));
    CHECKV("call a", d.a, 0x2222); CHECKV("ret b", d.b, 0x1111); CHECKV("stack back", d.sp, 0);
    RUN(JP(0x140, 4), JMP_SELF(1), 0, 0, JP(0x140, 6), RT(0, INC, 0, 0, 0, 0, S_TRB, D_NON), JP(0x140, 8), RT(0, INC, 0, 0, 0, 0, S_TRB, D_NON),
        JP(0x140, 10), RT(0, INC, 0, 0, 0, 0, S_TRB, D_NON), RT(0, INC, 0, 0, 0, 0, S_TRB, D_NON));
    CHECKV("four nested calls", d.a, 4); CHECKV("four nested calls pc", d.pc, 1);
    /* the conditions on the flags: a set-up that makes the flags, then the jump; TR is 2 when the jump is taken, 1 when not */
    enum { K_ZERO, K_POS, K_NEG, K_CARRY, K_OV };
    static const uint8_t kflags[5] = { F_Z, 0, F_S0, F_C | F_Z, F_OV0 | F_OV1 | F_S0 };      /* C Z OV0 OV1 S0 of each set-up (S1 is checked below) */
    int bad = 0, n = 0;
    for (int acc = 0; acc < 2; acc++)
        for (int kind = 0; kind < 5; kind++)
            for (int grp = 0; grp < 5; grp++)
                for (int want = 0; want < 2; want++) {
                    static const int fbit[5] = { F_C, F_Z, F_OV0, F_OV1, F_S0 };
                    static const int base[5] = { 0x80, 0x88, 0x90, 0x98, 0xA0 };
                    uint32_t p[8]; int k = 0;
                    static const int a0[5] = { 5, 5, 0x8000, 0xFFFF, 0x7FFF }, t0[5] = { 0, 0, 0, 1, 1 }, al[5] = { XOR, OR, OR, ADD, ADD };
                    p[k++] = LD(a0[kind], D_A + acc);
                    if (kind == K_ZERO) p[k++] = OP(1, XOR, acc, 0, 0, 0, S_A + acc, D_NON);       /* A xor A = 0 */
                    else { p[k++] = LD(t0[kind], D_TR); p[k++] = OP(1, al[kind], acc, 0, 0, 0, S_TR, D_NON); }
                    const int j = k;
                    p[k++] = JP(base[grp] + 4 * acc + 2 * want, j + 3);
                    p[k++] = LD(1, D_TR);
                    p[k++] = JMP_SELF(j + 2);
                    p[k++] = LD(2, D_TR);
                    p[k++] = JMP_SELF(j + 4);
                    load_prog(p, k, 0); go(100); dsp77_dbg(&d);
                    const int flag = (kflags[kind] & fbit[grp]) != 0, taken = flag == want;
                    n++; if (d.tr != (taken ? 2 : 1)) { bad++; printf("  jump acc %d kind %d group %d want %d: TR %x\n", acc, kind, grp, want, d.tr); }
                }
    CHECKV("flag jumps (100 cases)", bad, 0); CHECKV("flag jumps ran", n, 100);
    /* the nibble of DP */
    for (int dp = 0; dp < 2; dp++)
        for (int t = 0; t < 4; t++) {
            const int v = dp ? 0x2F : 0x20, taken = t == 0 ? v == 0x20 : t == 1 ? v != 0x20 : t == 2 ? v == 0x2F : v != 0x2F;
            RUN(LD(v, D_DP), JP(0xB0 + t, 4), LD(1, D_TR), JMP_SELF(3), LD(2, D_TR), JMP_SELF(5));
            CHECKV("dp nibble jump", d.tr, taken ? 2 : 1);
        }
    /* the serial acknowledges stay 0: JNSIAK, JNSOAK are taken, JSIAK, JSOAK not; a BRCH that no one knows is not taken */
    static const int brs[5] = { 0xB4, 0xB6, 0xB8, 0xBA, 0x001 }, tk[5] = { 1, 0, 1, 0, 0 };
    for (int i = 0; i < 5; i++) { RUN(JP(brs[i], 3), LD(1, D_TR), JMP_SELF(2), LD(2, D_TR), JMP_SELF(4)); CHECKV("serial jump", d.tr, tk[i] ? 2 : 1); }
    /* RQM: JNRQM is taken when RQM is 0, JRQM when it is 1 */
    RUN(JP(0xBC, 3), LD(1, D_TR), JMP_SELF(2), LD(2, D_TR), JMP_SELF(4));
    CHECKV("jnrqm taken", d.tr, 2);
    RUN(LD(0, D_DR), JP(0xBC, 4), LD(1, D_TR), JMP_SELF(3), LD(2, D_TR), JMP_SELF(5));
    CHECKV("jnrqm not taken after DR written", d.tr, 1);
    RUN(LD(0, D_DR), JP(0xBE, 4), LD(1, D_TR), JMP_SELF(3), LD(2, D_TR), JMP_SELF(5));
    CHECKV("jrqm taken after DR written", d.tr, 2);
}

/* the SNES side: a firmware that adds 1 to a 16-bit word that the SNES sends, and the handshake with DR and SR */
static void test_handshake(int be)
{
    static const uint32_t echo[] = {
        OP(0, NOP, 0, 0, 0, 0, S_DR, D_NON),            /* 0: read DR: RQM = 1 (the chip asks for data) */
        JP(0xBE, 1),                                    /* 1: JRQM $: wait while the SNES has not taken its turn */
        OP(0, NOP, 0, 0, 0, 0, S_DRNF, D_A),            /* 2: A = DR */
        OP(0, INC, 0, 0, 0, 0, S_TRB, D_NON),           /* 3: A + 1 */
        OP(0, NOP, 0, 0, 0, 0, S_A, D_DR),              /* 4: DR = A: RQM = 1 */
        JP(0xBE, 5),                                    /* 5: wait for the SNES to read it */
        JP(0x100, 0),
    };
    load_prog(echo, 7, be);
    go(50);
    CHECKV("hs: RQM set, waiting", dsp77_read_sr(), 0x80);
    CHECK("hs: idle on the RQM loop", dsp77_idle());
    const uint32_t n0 = dsp77_instr_count();
    dsp77_run(100000);
    CHECKV("hs: idle chip runs no instruction", dsp77_instr_count(), n0);
    dsp77_write_dr(0x34);
    CHECKV("hs: after the low byte RQM stays (DRS set)", dsp77_read_sr(), 0x90);
    dsp77_write_dr(0x12);
    CHECKV("hs: after the high byte RQM is 0", dsp77_read_sr(), 0x00);
    go(20);
    CHECKV("hs: the result is ready", dsp77_read_sr(), 0x80);
    CHECKV("hs: low byte", dsp77_read_dr(), 0x35);
    CHECKV("hs: still RQM after the low byte (DRS set)", dsp77_read_sr(), 0x90);
    CHECKV("hs: high byte", dsp77_read_dr(), 0x12);
    CHECKV("hs: RQM 0 after the high byte", dsp77_read_sr(), 0x00);
    go(20);
    CHECKV("hs: asks for the next word", dsp77_read_sr(), 0x80);
    dsp77_write_dr(0xFF); dsp77_write_dr(0xFF); go(20);
    CHECKV("hs: wraps low", dsp77_read_dr(), 0x00); CHECKV("hs: wraps high", dsp77_read_dr(), 0x00);
    /* 8-bit transfers: DRC = 1 */
    static const uint32_t b8[] = { LD(0x0400, D_SR), OP(0, NOP, 0, 0, 0, 0, S_DR, D_NON), JP(0xBE, 2), OP(0, NOP, 0, 0, 0, 0, S_DRNF, D_A), OP(0, NOP, 0, 0, 0, 0, S_A, D_DR), JP(0xBE, 5), JP(0x100, 1) };
    load_prog(b8, 7, be); go(50);
    dsp77_write_dr(0x5A);
    CHECKV("hs8: one byte takes RQM (DRC set)", dsp77_read_sr(), 0x04);
    go(20);
    CHECKV("hs8: result", dsp77_read_dr(), 0x5A);
    CHECKV("hs8: RQM 0 (DRC set)", dsp77_read_sr(), 0x04);
}

static void test_firmware_load(void)
{
    const char *why = NULL;
    static uint8_t img[8192];
    CHECK("short firmware refused", dsp77_load_combined(img, 100, &why) == -1 && why);
    CHECK("split sizes refused", dsp77_load(img, 6144, img, 100, &why) == -1);
    /* both byte orders read as the same program */
    static const uint32_t p[] = { LD(0x1234, D_A), JP(0x100, 1), 0, JP(0x100, 7), JP(0x140, 9) };
    for (int be = 0; be < 2; be++) {
        load_prog(p, 5, be); dat[9] = 0xABCD; go(10); dsp77_dbg(&d);
        CHECKV(be ? "big endian program" : "little endian program", d.a, 0x1234);
    }
    /* the data ROM follows the same order; two files give the same as one */
    memset(dat, 0, sizeof dat); dat[1] = 0x1357; dat[1000] = 0xFEDC;
    for (int be = 0; be < 2; be++) {
        static const uint32_t q[] = { LD(1000, D_RP), OP(0, NOP, 0, 0, 0, 0, S_ROM, D_A), JMP_SELF(2) };
        load_prog(q, 3, be); go(10); dsp77_dbg(&d);
        CHECKV(be ? "data ROM big endian" : "data ROM little endian", d.a, 0xFEDC);
    }
    uint8_t split[8192];
    pack(split, 0);
    CHECK("two files load", dsp77_load(split, 6144, split + 6144, 2048, NULL) == 0);
    go(10); dsp77_dbg(&d);
    CHECKV("two files run", d.a, 0xFEDC);
    memset(dat, 0, sizeof dat);
}

static void test_speed(void)
{
    static const uint32_t spin[] = { JMP_SELF(0) };
    load_prog(spin, 1, 0);
    dsp77_run(21477);
    CHECK("7.6 MHz: 7600 instructions in 21477 master clocks", dsp77_instr_count() >= 7595 && dsp77_instr_count() <= 7605);
    dsp77_run(21477 * 10);
    CHECK("the rest of a time carries over", dsp77_instr_count() >= 83590 && dsp77_instr_count() <= 83610);
}

/* ------------------------------------------------------------------ the cartridge */
#define ROMN 0x20000
static uint8_t rom[0x200000];
static void make_rom(int hirom, size_t size, const char *title, int chipset)
{
    memset(rom, 0, sizeof rom);
    const size_t h = hirom ? 0xFFC0 : 0x7FC0;
    memset(rom + h, ' ', 21);
    memcpy(rom + h, title, strlen(title));
    rom[h + 0x15] = hirom ? 0x21 : 0x20; rom[h + 0x16] = (uint8_t)chipset; rom[h + 0x17] = 9; rom[h + 0x18] = 0;
    rom[h + 0x1C] = 0xFF; rom[h + 0x1D] = 0xFF;                                    /* the checksum and its complement */
    rom[h + 0x3C] = 0x00; rom[h + 0x3D] = 0x80;
    snes_load(rom, size, NULL);
}
static void load_fw(const uint32_t *p, int n)
{
    static uint8_t img[8192];
    memset(prog, 0, sizeof prog); memcpy(prog, p, (size_t)n * 4);
    for (int i = n; i < 2048; i++) prog[i] = JMP_SELF(i);
    pack(img, 0);
    const char *why = NULL;
    CHECK("snes_dsp_firmware", snes_dsp_firmware(img, sizeof img, NULL, 0, &why) == 0);
}

static void test_cart(void)
{
    static const struct { const char *title; int dsp; } names[] = { { "SUPER MARIO KART", 1 }, { "DUNGEON MASTER", 2 }, { "SD GUNDAM GX", 3 }, { "TOP GEAR 3000", 4 }, { "PILOTWINGS", 1 } };
    for (int i = 0; i < 5; i++) { make_rom(0, ROMN, names[i].title, 3); CHECKV(names[i].title, snes_info()->dsp, names[i].dsp); }
    make_rom(0, ROMN, "PLAIN GAME", 0); CHECKV("no chip", snes_info()->dsp, 0);
    make_rom(0, ROMN, "GSU GAME", 0x15); CHECKV("a Super FX is not a DSP", snes_info()->dsp, 0);
    make_rom(0, ROMN, "RAM GAME", 2); CHECKV("chipset 2 is not a DSP", snes_info()->dsp, 0);

    /* without firmware: the game runs, SR says RQM, DR is 0, and the info says so */
    make_rom(0, ROMN, "SUPER MARIO KART", 3);
    CHECKV("no firmware: info", snes_info()->dsp_fw, 0);
    CHECKV("no firmware: SR", snes_read(0x20C000), 0x80);
    CHECKV("no firmware: DR", snes_read(0x208000), 0);
    snes_write(0x208000, 0x55);                                                    /* no crash */
    CHECK("firmware refused on a ROM with no chip", (make_rom(0, ROMN, "PLAIN GAME", 0), snes_dsp_firmware(rom, 8192, NULL, 0, NULL) == -1));

    /* LoROM up to 1 MB: DR at $20-$3F:$8000-$BFFF, SR at $C000-$FFFF (and the mirrors $A0-$BF) */
    static const uint32_t echo[] = { OP(0, NOP, 0, 0, 0, 0, S_DR, D_NON), JP(0xBE, 1), OP(0, NOP, 0, 0, 0, 0, S_DRNF, D_A), OP(0, INC, 0, 0, 0, 0, S_TRB, D_NON),
                                     OP(0, NOP, 0, 0, 0, 0, S_A, D_DR), JP(0xBE, 5), JP(0x100, 0) };
    make_rom(0, ROMN, "SUPER MARIO KART", 3); load_fw(echo, 7);
    CHECKV("info: firmware loaded", snes_info()->dsp_fw, 1);
    snes_clk = 0; S.line = 0;
    snes_clk = 1000;
    CHECKV("lorom: SR at $20:C000", snes_read(0x20C000), 0x80);
    snes_write(0x2A8000, 0x10); snes_write(0x3F8000 + 0x3FFF, 0x02);               /* DR in two banks and at the top of the range */
    snes_clk = 2000;
    CHECKV("lorom: SR after the word", snes_read(0xBFFFFF), 0x80);                 /* $BF:FFFF: SR, mirror */
    CHECKV("lorom: low byte", snes_read(0xA08000), 0x11);
    CHECKV("lorom: high byte", snes_read(0x20BFFF), 0x02);
    CHECKV("map: ROM still at bank 0", snes_read(0x008000), rom[0]);
    const uint8_t *pg = snes_rtab[0x208];
    CHECK("the port pages are on the slow path", pg == NULL && snes_rtab[0x20C] == NULL && snes_rtab[0x3FB] == NULL && snes_rtab[0x00 * 16 + 8] != NULL);

    /* a chip that computes: the SNES sees RQM when the time has gone, not before (the chip runs lazily) */
    static const uint32_t slow[] = { LD(100, D_A), OP(0, DEC, 0, 0, 0, 0, S_TRB, D_NON), JP(0x88 + 0, 1), LD(0, D_DR), JP(0xBE, 4), JP(0x100, 0) };
    make_rom(0, ROMN, "SUPER MARIO KART", 3); load_fw(slow, 6);
    snes_clk = 100; S.line = 0;
    CHECKV("lazy: not ready after 100 clocks", snes_read(0x20C000), 0x00);
    snes_clk = 1300;
    CHECKV("lazy: ready after the time of the loop", snes_read(0x20C000), 0x80);
    CHECK("lazy: waits now", dsp77_idle());

    /* LoROM over 1 MB, DSP-1: ports at $60-$6F:$0000-$7FFF; the ROM at $20 is ROM */
    make_rom(0, 0x200000, "BIG DSP1", 3); load_fw(echo, 7);
    snes_clk = 1000; S.line = 0;
    CHECKV("big lorom: SR at $60:4000", snes_read(0x604000), 0x80);
    snes_write(0x600000, 0x07); snes_write(0x6F3FFF, 0x01); snes_clk = 2000;
    CHECKV("big lorom: low byte", snes_read(0xE03FFF), 0x08);
    CHECKV("big lorom: high byte", snes_read(0x600000), 0x01);
    S.rom[0x100000] = 0x5A;
    CHECKV("big lorom: $20:8000 is ROM", snes_read(0x208000), 0x5A);
    CHECK("big lorom: pages", snes_rtab[0x600] == NULL && snes_rtab[0x6F7] == NULL && snes_rtab[0x208] != NULL);

    /* HiROM: DR at $00-$1F:$6000-$6FFF, SR at $7000-$7FFF (and $80-$9F) */
    make_rom(1, 0x80000, "HIROM DSP1", 3); load_fw(echo, 7);
    snes_clk = 1000; S.line = 0;
    CHECKV("hirom: SR at $00:7000", snes_read(0x007000), 0x80);
    snes_write(0x806000, 0x21); snes_write(0x1F6FFF, 0x43); snes_clk = 2000;
    CHECKV("hirom: low byte", snes_read(0x9F6000), 0x22);
    CHECKV("hirom: high byte", snes_read(0x006FFF), 0x43);
    CHECK("hirom: pages", snes_rtab[0x006] == NULL && snes_rtab[0x007] == NULL && snes_rtab[0x1F6] == NULL && snes_rtab[0x008] != NULL);

    /* DSP-2, DSP-3 and DSP-4 use the same map as the small LoROM DSP-1 */
    for (int c = 2; c <= 4; c++) {
        make_rom(0, ROMN, c == 2 ? "DUNGEON MASTER" : c == 3 ? "SD GUNDAM GX" : "TOP GEAR 3000", 3); load_fw(echo, 7);
        snes_clk = 1000; S.line = 0;
        snes_write(0x208000, 0x00); snes_write(0x208000, 0x80); snes_clk = 2000;
        CHECKV("dsp 2-4: low byte", snes_read(0x208000), 0x01); CHECKV("dsp 2-4: high byte", snes_read(0x208000), 0x80);
    }

    /* a reset restarts the chip; a new ROM without a chip drops the firmware */
    make_rom(0, ROMN, "SUPER MARIO KART", 3); load_fw(echo, 7);
    snes_clk = 1000; S.line = 0; snes_write(0x208000, 1); snes_write(0x208000, 1);
    snes_reset();
    CHECKV("reset: the chip starts again", dsp77_read_sr(), 0x00);
    make_rom(0, ROMN, "PLAIN GAME", 0);
    CHECK("a ROM with no chip: no firmware", !dsp77_ready());

    /* frames: a chip that never waits runs one instruction per DSP clock, 7.6 MHz, over whole frames */
    static const uint32_t spin[] = { JMP_SELF(0) };
    make_rom(0, ROMN, "SUPER MARIO KART", 3); load_fw(spin, 1);
    for (int i = 0; i < 3; i++) snes_frame(0, 0, 0);
    const uint32_t n = dsp77_instr_count(), want = (uint32_t)(3ull * 262 * 1364 * 23191 / 65536);
    CHECK("frames: 3 frames of DSP time", n > want * 99 / 100 && n < want * 101 / 100);
    if (n <= want * 99 / 100 || n >= want * 101 / 100) printf("  %u instructions, want about %u\n", n, want);
}

int main(void)
{
    test_alu();
    test_mul_and_moves();
    test_jumps();
    test_handshake(0);
    test_handshake(1);
    test_firmware_load();
    test_speed();
    test_cart();
    printf("snes dsp77: %d checks, %d failed\n", count, fails);
    return fails ? 1 : 0;
}
