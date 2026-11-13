/* cpu_diff.c: one random instruction at a time through two copies of the 65C816: the one in this build (the assembly loop on the ARMv6 console) and
 * the C reference (core/src/bm/snes_cpu.c built with -DSNES_CPU_C and the names ref_*). Both start from the same random registers and the same machine
 * state (S is saved and restored); registers, clock and the whole of S must be equal after the instruction. Prints the first differences.
 *   cpu_diff [trials] [first opcode] [last opcode]
 * On the PC both copies are C and this only checks the test. The real check is on ARM: make test-snes-cpu-arm (QEMU, arm1176). Original code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes_int.h"

void ref_reset(void);
void ref_get(cpu_regs_t *r);
void ref_set(const cpu_regs_t *r);
int ref_run(int limit);

static uint64_t rs = 0xD1B54A32D192ED03ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static uint64_t hash_bytes(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

static snes_t S0;
static int same(const cpu_regs_t *a, const cpu_regs_t *b)
{
    return a->a == b->a && a->x == b->x && a->y == b->y && a->s == b->s && a->d == b->d && a->pc == b->pc && a->p == b->p && a->e == b->e && a->pb == b->pb && a->db == b->db;
}

int main(int argc, char **argv)
{
    const int trials = argc > 1 ? atoi(argv[1]) : 20000;
    const int lo = argc > 2 ? (int)strtol(argv[2], NULL, 0) : 0, hi = argc > 3 ? (int)strtol(argv[3], NULL, 0) : 255;
    enum { LEN = 0x100000 };
    uint8_t *rom = malloc(LEN);
    for (size_t i = 0; i < LEN; i++) rom[i] = (uint8_t)rnd();
    const size_t off = 0x7FC0;
    memcpy(rom + off, "CPU DIFF TEST         ", 21);
    rom[off + 0x15] = 0x20; rom[off + 0x18] = 3;
    rom[off + 0x1C] = 0; rom[off + 0x1D] = 0; rom[off + 0x1E] = 0xFF; rom[off + 0x1F] = 0xFF;
    rom[off + 0x3C] = 0x00; rom[off + 0x3D] = 0x80;
    const char *why = NULL;
    if (snes_load(rom, LEN, &why)) { printf("load failed: %s\n", why); return 1; }
    int bad = 0, done = 0;
    int per[256] = { 0 };
    for (int t = 0; t < trials && bad < 25; t++) {
        for (size_t i = 0; i < sizeof S.wram; i++) S.wram[i] = (uint8_t)rnd();
        S.irq_pending = S.nmi_pending = 0;
        cpu_regs_t r;
        r.a = (uint16_t)rnd(); r.x = (uint16_t)rnd(); r.y = (uint16_t)rnd(); r.d = (rnd() & 3) ? (uint16_t)(rnd() & 0x1FFF) : (uint16_t)rnd();
        r.e = (rnd() & 7) == 0;
        if ((rnd() & 7) == 0) r.d = (uint16_t)(0xFF00 | (rnd() & 0xFF));            /* the direct page at the end of bank 0: 16 bit accesses wrap there */
        if ((rnd() & 7) == 0) { r.x = (uint16_t)(0xFF00 | (rnd() & 0xFF)); r.y = (uint16_t)(0xFF00 | (rnd() & 0xFF)); }
        r.s = r.e ? (uint16_t)(0x100 | (rnd() & 0xFF)) : (uint16_t)(0x100 + (rnd() & 0x1FFF));
        r.p = (uint8_t)rnd();
        if ((rnd() & 3) == 0) r.p &= (uint8_t)~0x08;                               /* mostly no decimal mode: the assembly leaves it to C */
        r.pb = 0; r.pc = (uint16_t)(0x8000 + (rnd() % 0x7FF0));
        const uint32_t k = rnd() & 3;
        r.db = k < 2 ? 0x7E : k == 2 ? 0 : (uint8_t)rnd();
        /* the opcode is the first byte of the code: force it into the range asked for */
        const int op = lo + (int)(rnd() % (unsigned)(hi - lo + 1));
        const uint32_t ra = (uint32_t)(r.pc - 0x8000);
        S.rom[ra] = (uint8_t)op;
        memcpy(&S0, &S, sizeof S);
        cpu_set(&r);
        snes_clk = 0;
        cpu_run(1);
        cpu_regs_t a;
        cpu_get(&a);
        const int clk_a = snes_clk;
        const uint64_t h_a = hash_bytes(0xCBF29CE484222325ull, (const uint8_t *)&S, sizeof S);
        memcpy(&S, &S0, sizeof S);
        ref_set(&r);
        snes_clk = 0;
        ref_run(1);
        cpu_regs_t b;
        ref_get(&b);
        const int clk_b = snes_clk;
        const uint64_t h_b = hash_bytes(0xCBF29CE484222325ull, (const uint8_t *)&S, sizeof S);
        done++;
        if (!same(&a, &b) || clk_a != clk_b || h_a != h_b) {
            per[op]++;
            bad++;
            printf("DIFF op %02X at %02X:%04X  in: a %04X x %04X y %04X s %04X d %04X db %02X p %02X e %d\n", op, r.pb, r.pc, r.a, r.x, r.y, r.s, r.d, r.db, r.p, r.e);
            printf("   asm: a %04X x %04X y %04X s %04X d %04X pc %04X p %02X e %d clk %d mem %s\n", a.a, a.x, a.y, a.s, a.d, a.pc, a.p, a.e, clk_a, h_a == h_b ? "=" : "differs");
            printf("   ref: a %04X x %04X y %04X s %04X d %04X pc %04X p %02X e %d clk %d\n", b.a, b.x, b.y, b.s, b.d, b.pc, b.p, b.e, clk_b);
        }
    }
    printf("cpu diff: %d instructions, %d different%s\n", done, bad, bad ? "" : ": ok");
    return bad != 0;
}
