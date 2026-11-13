/* spc_ref.cpp: compares the SPC700 of bm (core/src/bm/snes_spc.c) with the one of ares (ISC licence): every opcode on random registers and memory;
 * the registers, the flags, the bytes written and the cycles must be equal. ares is NOT part of this repository:
 *     git clone --depth 1 https://github.com/ares-emulator/ares.git /path/to/ares
 *     make test-snes-audio-ref ARES=/path/to/ares
 * The program includes the SPC700 of ares from that directory; nothing of ares is copied here.
 *   spc_ref [trials]         the number of trials for each opcode (default 300)
 *   spc_ref table            print the cycle table of ares (the cycles of each opcode with no branch taken)
 * Trials that read or write the I/O area $F0-$FF are skipped (it is not plain memory in bm). Original code. */
#include <nall/nall.hpp>
using namespace nall;
using namespace nall::primitives;
#include <ares/types.hpp>
#define private public
/* the pieces of component/processor/spc700/spc700.cpp (it includes the whole of ares.hpp, which needs the platform code) */
#include <component/processor/spc700/spc700.hpp>
namespace ares {
#define PC r.pc.w
#define YA r.ya.w
#define A r.ya.byte.l
#define X r.x
#define Y r.ya.byte.h
#define S r.s
#define P r.p
#define CF r.p.c
#define ZF r.p.z
#define IF r.p.i
#define HF r.p.h
#define BF r.p.b
#define PF r.p.p
#define VF r.p.v
#define NF r.p.n
#define alu (this->*op)
#include <component/processor/spc700/memory.cpp>
#include <component/processor/spc700/algorithms.cpp>
#include <component/processor/spc700/instructions.cpp>
#include <component/processor/spc700/instruction.cpp>
}

extern "C" {
int spc_test_step(uint8_t r[7]);
uint8_t *spc_ram_ptr(void);
void spc_reset(void);
}

struct Ref : ares::SPC700 {
    uint8_t mem[65536];
    int cyc;
    bool io;
    auto idle() -> void override { cyc++; }
    auto read(n16 a) -> n8 override { cyc++; if ((a & 0xFFF0) == 0xF0) io = true; return mem[a]; }
    auto write(n16 a, n8 d) -> void override { cyc++; if ((a & 0xFFF0) == 0xF0) io = true; mem[a] = d; }
    auto synchronizing() const -> bool override { return false; }
};

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

int main(int argc, char **argv)
{
    static Ref ref;
    static uint8_t base[65536];
    const bool table = argc > 1 && !strcmp(argv[1], "table");
    const int trials = (argc > 1 && !table) ? atoi(argv[1]) : 300;
    int mincyc[256], bad_ops = 0;
    for (int i = 0; i < 256; i++) mincyc[i] = 99;
    spc_reset();
    for (int op = 0; op < 256; op++) {
        if (op == 0xEF || op == 0xFF) { mincyc[op] = 3; continue; }                     /* SLEEP and STOP: not compared */
        int fails = 0, runs = 0;
        for (int t = 0; t < trials; t++) {
            for (int i = 0; i < 65536; i++) base[i] = (uint8_t)rnd();
            const unsigned pc = 0x0200 + (rnd() % 0xE000);
            base[pc] = (uint8_t)op;
            uint8_t r[7] = { (uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)pc, (uint8_t)(pc >> 8), (uint8_t)rnd() };
            r[6] &= (uint8_t)~0x10;                                                      /* B and the rest as the chip keeps them */
            if (rnd() % 3 == 0) r[6] &= (uint8_t)~0x20; else if (rnd() % 2) r[6] |= 0x20;
            memcpy(ref.mem, base, 65536);
            ref.cyc = 0; ref.io = false;
            ref.r.pc.w = pc; ref.r.ya.byte.l = r[0]; ref.r.x = r[1]; ref.r.ya.byte.h = r[2]; ref.r.s = r[3]; ref.r.p = (n8)r[6];
            ref.instruction();
            if (ref.io) continue;
            memcpy(spc_ram_ptr(), base, 65536);
            const int c = spc_test_step(r);
            runs++;
            if (ref.cyc < mincyc[op]) mincyc[op] = ref.cyc;
            const uint8_t e[7] = { (uint8_t)ref.r.ya.byte.l, (uint8_t)ref.r.x, (uint8_t)ref.r.ya.byte.h, (uint8_t)ref.r.s, (uint8_t)ref.r.pc.w,
                                   (uint8_t)(ref.r.pc.w >> 8), (uint8_t)(u32)ref.r.p };
            int bad = memcmp(e, r, 7) != 0 || memcmp(ref.mem, spc_ram_ptr(), 65536) != 0;
            if (!table && c != ref.cyc) bad = 2;
            if (bad && !table) {
                if (fails++ < 2) {
                    printf("op %02X: %s. ares A%02X X%02X Y%02X S%02X PC%02X%02X P%02X cyc %d | bm A%02X X%02X Y%02X S%02X PC%02X%02X P%02X cyc %d\n", op,
                           bad == 2 ? "cycles" : "state", e[0], e[1], e[2], e[3], e[5], e[4], e[6], ref.cyc, r[0], r[1], r[2], r[3], r[5], r[4], r[6], c);
                }
            }
        }
        if (fails) bad_ops++;
        if (!runs) printf("op %02X: no trial\n", op);
    }
    if (table) {
        for (int i = 0; i < 256; i++) printf("%d,%s", mincyc[i], (i & 15) == 15 ? "\n" : " ");
        return 0;
    }
    printf("spc_ref: %d of 256 opcodes differ\n", bad_ops);
    return bad_ops != 0;
}
