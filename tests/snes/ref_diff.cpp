/* ref_diff.cpp: our 65C816 (src/bm/snes_cpu.c) against the 65C816 module of the ares emulator (ISC licence), one instruction at a time.
 *
 * Build: make test-snes-ref ARES=/path/to/ares   (the make target skips with a message when ARES is not set).
 * The ares sources are NOT in this repository and are NOT copied: this file only #includes the processor module from the ARES tree at build time.
 * Both cores run the same random state on one flat 16 MB memory. After each instruction the harness compares A X Y S D DB PB PC P and E, the
 * WAI and STP latches, and every write (address, value, order). The cycle count is compared as an information line only: the bm core charges an
 * average internal cycle per instruction (docs/progress/snes.md), ares counts every bus and idle cycle.
 *
 *   ref_diff [trials per opcode and mode] [first opcode] [last opcode] [seed]
 * Output: the first mismatch of each opcode and aspect (regs, writes, latches), then a table with the counts. Exit code 1 when a register or write mismatch exists.
 * Original code. */
#include <ares/ares.hpp>
#include <ares/component/processor/wdc65816/wdc65816.cpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ref_diff_ours.h"

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static uint8_t *mem;

struct Wr { uint32_t addr; uint8_t val, old; };

struct Ref : ares::WDC65816 {
  long cyc = 0;
  std::vector<Wr> w;
  auto idle() -> void override { cyc++; }
  auto read(n24 a) -> n8 override { cyc++; return mem[(uint32_t)a & 0xFFFFFF]; }
  auto write(n24 a, n8 d) -> void override {
    cyc++;
    uint32_t ad = (uint32_t)a & 0xFFFFFF;
    w.push_back({ad, (uint8_t)(uint32_t)d, mem[ad]});
    mem[ad] = (uint8_t)(uint32_t)d;
  }
  auto lastCycle() -> void override {}
  auto interruptPending() const -> bool override { return false; }
  auto synchronizing() const -> bool override { return true; }
};

static void undo(const Wr *w, int n) { for (int i = n - 1; i >= 0; i--) mem[w[i].addr] = w[i].old; }

static const char *pstr(uint8_t p, int e)
{
  static char b[4][12];
  static int k;
  char *s = b[k++ & 3];
  const char *n = "nvmxdizc";
  for (int i = 0; i < 8; i++) s[i] = (p >> (7 - i) & 1) ? (n[i] - 32) : n[i];
  s[8] = e ? 'E' : '-';
  s[9] = 0;
  return s;
}
static void print_regs(const char *t, const rd_regs_t &r)
{
  printf("   %s A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X PB=%02X PC=%04X P=%02X %s\n", t, r.a, r.x, r.y, r.s, r.d, r.db, r.pb, r.pc, r.p, pstr(r.p, r.e));
}

static uint16_t pick16()
{
  static const uint16_t sp[] = {0x0000, 0x0001, 0x007F, 0x0080, 0x00FF, 0x0100, 0x01FF, 0x7FFF, 0x8000, 0xFF00, 0xFFFF, 0x00FE, 0x1000};
  switch (rnd() & 3) {
  case 0: return sp[rnd() % (sizeof sp / sizeof *sp)];
  case 1: return (uint16_t)(rnd() & 0xFF);
  default: return (uint16_t)rnd();
  }
}
static uint8_t pick8()
{
  static const uint8_t sp[] = {0x00, 0x01, 0x7F, 0x80, 0xFF, 0xFE, 0x0F, 0x10, 0x99};
  return (rnd() & 3) == 0 ? sp[rnd() % sizeof sp] : (uint8_t)rnd();
}
static uint8_t pickbank()
{
  switch (rnd() & 3) { case 0: return 0; case 1: return 0x7E; case 2: return 0xFF; default: return (uint8_t)rnd(); }
}

struct Stat { long n = 0, regs = 0, writes = 0, latch = 0, cyc = 0; };

int main(int argc, char **argv)
{
  const int trials = argc > 1 ? atoi(argv[1]) : 3000;
  const int lo = argc > 2 ? (int)strtol(argv[2], 0, 0) : 0, hi = argc > 3 ? (int)strtol(argv[3], 0, 0) : 255;
  if (argc > 4) rs ^= (uint64_t)strtoull(argv[4], 0, 0) * 0x2545F4914F6CDD1Dull;
  const bool verbose = getenv("RD_VERBOSE") != 0;
  mem = (uint8_t *)malloc(1u << 24);
  for (size_t i = 0; i < (1u << 24); i++) mem[i] = (uint8_t)rnd();
  ours_init(mem);

  Ref ref;
  static Stat st[256 + 2];     /* 256 opcodes, then NMI and IRQ entries */
  static bool shown[258][3];
  long total = 0;
  const char *aspect[3] = {"registers", "writes", "latches"};
  for (int op = lo; op <= hi + 2; op++) {
    if (op > hi && op < 256) break;
    if (op >= 256 && op - 256 > 1) break;
    if (op >= 256 && hi < 255) break;
    for (int t = 0; t < trials; t++) {
      rd_regs_t in;
      in.e = (rnd() % 6) == 0;
      in.p = (uint8_t)rnd();
      if ((rnd() % 3) == 0) in.p |= 0x08;
      if (in.e) in.p |= 0x30;
      const bool m8 = (in.p & 0x20) != 0, x8 = (in.p & 0x10) != 0;
      (void)m8;
      in.a = pick16(); in.x = pick16(); in.y = pick16();
      if (x8) { in.x &= 0xFF; in.y &= 0xFF; }
      in.d = (rnd() % 3) == 0 ? (uint16_t)(pick16() & 0xFF00) : pick16();
      in.s = pick16();
      if (in.e) in.s = (uint16_t)(0x100 | (in.s & 0xFF));
      in.db = pickbank(); in.pb = pickbank();
      in.pc = (rnd() % 5) == 0 ? (uint16_t)(0xFFF8 + (rnd() & 7)) : (uint16_t)rnd();
      if ((rnd() & 15) == 0) in.pc = (uint16_t)(0x8000 | (rnd() & 0x7FFF));
      /* ares reads the pointer of (dp,X) in emulation mode with DL != 0 from bank 1 when D is above $FE00 (an unmasked add in its source); the
       * hardware wraps in bank 0 like ours: keep D out of that corner so that this one known quirk of the reference is not reported */
      if (in.e && (in.d & 0xFF) && in.d >= 0xFE00) in.d &= 0x7FFF;
      /* the order of the stack writes against the operand fetches of JSL and JSR (a,X) differs only when the stack overlaps the code: avoid that */
      if (in.pb == 0 && (uint16_t)(in.pc - in.s + 12) < 24) in.pb = 1;
      const bool intr = op >= 256;
      int nmi = 0, irq = 0;
      int code = op;
      if (intr) {
        if (op == 256) nmi = 1; else { irq = 1; in.p &= (uint8_t)~0x04; }
        code = 0xEA;     /* a NOP at the vector target */
      }
      /* the code bytes at PB:PC; the memory restores after both runs */
      uint32_t base = (uint32_t)in.pb << 16;
      uint32_t ca[4];
      uint8_t saved[4 + 4];
      uint32_t va[4] = {0, 0, 0, 0};
      for (int i = 0; i < 4; i++) { ca[i] = base | (uint16_t)(in.pc + i); saved[i] = mem[ca[i]]; }
      mem[ca[0]] = (uint8_t)(intr ? 0xEA : code);
      for (int i = 1; i < 4; i++) mem[ca[i]] = pick8();
      uint8_t cb[4];
      for (int i = 0; i < 4; i++) cb[i] = mem[ca[i]];
      uint32_t vec = 0;
      if (intr) {
        /* the vector (bank 0) points into a fixed spot with a NOP */
        vec = op == 256 ? (in.e ? 0xFFFA : 0xFFEA) : (in.e ? 0xFFFE : 0xFFEE);
        for (int i = 0; i < 4; i++) { va[i] = i < 2 ? vec + i : 0; saved[4 + i] = i < 2 ? mem[va[i]] : 0; }
        mem[vec] = 0x34; mem[vec + 1] = 0x92;
        // the target 00:9234 holds a NOP
        va[2] = 0x009234; saved[6] = mem[va[2]]; mem[va[2]] = 0xEA;
      }
      /* the stack range and the direct page may overlap the code: that is allowed, both cores see the same memory */

      // --- ours
      rd_regs_t o;
      int ow = 0, os = 0;
      int oclk = ours_step(&in, &o, nmi, irq, &ow, &os);
      Wr ouw[RD_MAXW];
      int onw = ours_nw > RD_MAXW ? RD_MAXW : ours_nw;
      for (int i = 0; i < onw; i++) ouw[i] = {ours_w[i].addr, ours_w[i].val, ours_w[i].old};
      for (int i = onw - 1; i >= 0; i--) mem[ouw[i].addr] = ouw[i].old;   // restore for the reference run

      // --- ares
      ref.w.clear(); ref.cyc = 0;
      ref.r.a = in.a; ref.r.x = in.x; ref.r.y = in.y; ref.r.s = in.s; ref.r.d = in.d; ref.r.b = in.db;
      ref.r.e = in.e; ref.r.p = (uint8_t)in.p;
      if (in.e) { ref.r.p.m = 1; ref.r.p.x = 1; }
      ref.r.pc.d = ((uint32_t)in.pb << 16) | in.pc;
      ref.r.wai = ref.r.stp = false;
      if (intr) { ref.r.vector = vec & 0xFFFF; ref.interrupt(); }
      ref.instruction();
      rd_regs_t g;
      g.a = ref.r.a.w; g.x = ref.r.x.w; g.y = ref.r.y.w; g.s = ref.r.s.w; g.d = ref.r.d.w; g.db = ref.r.b;
      g.pb = ref.r.pc.b; g.pc = ref.r.pc.w; g.p = (uint8_t)ref.r.p; g.e = ref.r.e;
      std::vector<Wr> rw = ref.w;
      undo(rw.data(), (int)rw.size());
      // memory back to the original
      for (int i = 0; i < 4; i++) mem[ca[i]] = saved[i];
      if (intr) { mem[vec] = saved[4]; mem[vec + 1] = saved[5]; mem[va[2]] = saved[6]; }

      Stat &s = st[op];
      s.n++; total++;
      bool bad_regs = memcmp(&o, &g, 0) != 0;
      bad_regs = !(o.a == g.a && o.x == g.x && o.y == g.y && o.s == g.s && o.d == g.d && o.pc == g.pc && o.p == g.p && o.e == g.e && o.pb == g.pb && o.db == g.db);
      bool bad_w = onw != (int)rw.size() || ours_nw > RD_MAXW;
      if (!bad_w) for (int i = 0; i < onw; i++) if (ouw[i].addr != rw[i].addr || ouw[i].val != rw[i].val) bad_w = true;
      bool bad_l = ow != (ref.r.wai ? 1 : 0) || os != (ref.r.stp ? 1 : 0);
      bool bad_c = (oclk / 6) != (int)ref.cyc;
      s.regs += bad_regs; s.writes += bad_w; s.latch += bad_l; s.cyc += bad_c;
      bool flags[3] = {bad_regs, bad_w, bad_l};
      for (int k = 0; k < 3; k++) {
        if (!flags[k] || shown[op][k]) continue;
        shown[op][k] = true;
        if (intr) printf("MISMATCH (%s) in %s\n", aspect[k], op == 256 ? "NMI entry + NOP" : "IRQ entry + NOP");
        else printf("MISMATCH (%s) opcode %02X  bytes %02X %02X %02X %02X\n", aspect[k], op, cb[0], cb[1], cb[2], cb[3]);
        // memory is restored, but the code bytes were: print what the operands were
        print_regs("in  ", in);
        print_regs("ours", o);
        print_regs("ref ", g);
        if (k == 1 || verbose) {
          printf("   ours writes:"); for (int i = 0; i < onw; i++) printf(" %06X=%02X", ouw[i].addr, ouw[i].val); printf("\n");
          printf("   ref  writes:"); for (size_t i = 0; i < rw.size(); i++) printf(" %06X=%02X", rw[i].addr, rw[i].val); printf("\n");
        }
        printf("   cycles ours %d ref %ld\n", oclk / 6, ref.cyc);
      }
    }
  }
  long br = 0, bw = 0, bl = 0, bc = 0;
  printf("\nop  trials  regs writes latch cycles\n");
  for (int i = 0; i < 258; i++) {
    if (!st[i].n) continue;
    br += st[i].regs; bw += st[i].writes; bl += st[i].latch; bc += st[i].cyc;
    if (st[i].regs || st[i].writes || st[i].latch)
      printf("%s%02X %6ld %6ld %6ld %5ld %6ld\n", i >= 256 ? "I" : " ", i >= 256 ? i - 256 : i, st[i].n, st[i].regs, st[i].writes, st[i].latch, st[i].cyc);
  }
  printf("compared %ld instructions: register mismatches %ld, write mismatches %ld, latch mismatches %ld, cycle differences %ld (information only)\n", total, br, bw, bl, bc);
  return (br || bw || bl) ? 1 : 0;
}
