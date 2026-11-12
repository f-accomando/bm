/* ref_diff_gsu.cpp: our Super FX (src/bm/snes_gsu.c) against the GSU module of the ares emulator (ISC licence), on random straight-line programs.
 *
 * Build: make test-snes-ref ARES=/path/to/ares (see ref_diff.cpp). Nothing of ares is copied here: the processor module is #included from the ARES tree
 * at build time, and the cartridge side that the module needs (ROM and RAM buffers, pixel cache, code cache) is written again below from the public
 * descriptions of the chip (SNESdev wiki "Super FX"), in this file's own structure.
 *
 * One trial: the same random ROM, RAM and registers on both; a program of a few random instructions (all opcodes with the ALT prefixes, TO, FROM and WITH,
 * forward branches, no loops and no jumps) and STOP. After STOP the harness compares R0-R15, the flags Z CY S OV, PBR, ROMBR, RAMBR, COLR, POR, CBR and the
 * whole cart RAM. The instruction count and the cycle count are information only.
 *   ref_diff_gsu [trials] [seed]
 * Output: the first mismatch of each opcode that was in the program (the first opcode that differs), then a table. Exit code 1 on a mismatch. Original code. */
#include <ares/ares.hpp>
#include <ares/component/processor/gsu/gsu.cpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

extern "C" {
#include "ref_diff_ours.h"
}

static uint64_t rs = 0xA24BAED4963EE407ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

struct PxCache { uint16_t off = 0xFFFF; uint8_t data[8] = {0}; uint8_t pend = 0; };

struct Env : ares::GSU {
  static const uint32_t RomSize = 0x20000, RamSize = 0x10000;
  uint8_t rom[RomSize], ram[RamSize];
  long clk = 0;
  uint8_t cbuf[512]; bool cvalid[32];
  PxCache px[2];
  unsigned romcl = 0, ramcl = 0;
  uint8_t romdr = 0;
  uint32_t ramar = 0; uint8_t ramdr = 0;
  bool stopped = false;

  void reset() {
    power();
    memset(cbuf, 0, sizeof cbuf); memset(cvalid, 0, sizeof cvalid);
    px[0] = px[1] = PxCache();
    romcl = ramcl = 0; romdr = 0; ramar = 0; ramdr = 0; clk = 0; stopped = false;
  }
  unsigned memclk() const { return regs.clsr ? 5 : 6; }

  // the bus of the chip: ROM at $00-3F (folded LoROM) and $40-5F, RAM at $70-71
  auto read(n24 a, n8 d = 0x00) -> n8 override {
    uint32_t ad = (uint32_t)a;
    if ((ad & 0xC00000) == 0x000000) return rom[((((ad & 0x3F0000) >> 1) | (ad & 0x7FFF))) & (RomSize - 1)];
    if ((ad & 0xE00000) == 0x400000) return rom[ad & (RomSize - 1)];
    if ((ad & 0xFE0000) == 0x700000) return ram[ad & (RamSize - 1)];
    return d;
  }
  auto write(n24 a, n8 d) -> void override {
    uint32_t ad = (uint32_t)a;
    if ((ad & 0xFE0000) == 0x700000) ram[ad & (RamSize - 1)] = (uint8_t)(uint32_t)d;
  }

  auto step(u32 c) -> void override {
    if (romcl) { romcl -= std::min<unsigned>(c, romcl); if (!romcl) { regs.sfr.r = 0; romdr = read((regs.rombr << 16) + regs.r[14]); } }
    if (ramcl) { ramcl -= std::min<unsigned>(c, ramcl); if (!ramcl) write(0x700000 + (regs.rambr << 16) + ramar, ramdr); }
    clk += c;
  }
  auto stop() -> void override { stopped = true; }

  auto readOpcode(uint16_t address) -> uint8_t {
    uint16_t off = address - (uint16_t)regs.cbr;
    if (off < 512) {
      if (!cvalid[off >> 4]) {
        uint32_t dp = off & 0xFFF0;
        uint32_t sp = ((uint32_t)regs.pbr << 16) + (((uint16_t)regs.cbr + dp) & 0xFFF0);
        for (int n = 0; n < 16; n++) { step(memclk()); cbuf[dp++] = read(sp++); }
        cvalid[off >> 4] = true;
      } else step(regs.clsr ? 1 : 2);
      return cbuf[off];
    }
    syncROMBuffer(); syncRAMBuffer();
    step(memclk());
    return read((uint32_t)regs.pbr << 16 | address);
  }
  auto peekpipe() -> uint8_t { uint8_t r = (uint8_t)(uint32_t)regs.pipeline; regs.pipeline = readOpcode(regs.r[15]); regs.r[15].modified = false; return r; }
  auto pipe() -> n8 override { uint8_t r = (uint8_t)(uint32_t)regs.pipeline; regs.pipeline = readOpcode(++regs.r[15]); regs.r[15].modified = false; return r; }
  auto flushCache() -> void override { memset(cvalid, 0, sizeof cvalid); }
  auto updateROMBuffer() -> void { regs.sfr.r = 1; romcl = memclk(); }
  auto syncROMBuffer() -> void override { if (romcl) step(romcl); }
  auto readROMBuffer() -> n8 override { syncROMBuffer(); return romdr; }
  auto syncRAMBuffer() -> void override { if (ramcl) step(ramcl); }
  auto readRAMBuffer(n16 address) -> n8 override { syncRAMBuffer(); return read(0x700000 + (regs.rambr << 16) + (uint32_t)address); }
  auto writeRAMBuffer(n16 address, n8 data) -> void override { syncRAMBuffer(); ramcl = memclk(); ramar = (uint32_t)address; ramdr = (uint8_t)(uint32_t)data; }

  // the pixels
  auto color(n8 s) -> n8 override {
    uint8_t src = (uint8_t)(uint32_t)s, c = (uint8_t)(uint32_t)regs.colr;
    if (regs.por.highnibble) return (c & 0xF0) | (src >> 4);
    if (regs.por.freezehigh) return (c & 0xF0) | (src & 0x0F);
    return src;
  }
  uint32_t charAddr(uint8_t x, uint8_t y, unsigned bpp) {
    uint32_t cn = 0;
    switch (regs.por.obj ? 3 : regs.scmr.ht) {
    case 0: cn = ((x & 0xF8) << 1) + ((y & 0xF8) >> 3); break;
    case 1: cn = ((x & 0xF8) << 1) + ((x & 0xF8) >> 1) + ((y & 0xF8) >> 3); break;
    case 2: cn = ((x & 0xF8) << 1) + ((x & 0xF8) << 0) + ((y & 0xF8) >> 3); break;
    default: cn = ((y & 0x80) << 2) + ((x & 0x80) << 1) + ((y & 0x78) << 1) + ((x & 0x78) >> 3); break;
    }
    return 0x700000 + (cn * (bpp << 3)) + ((uint32_t)(uint8_t)(uint32_t)regs.scbr << 10) + ((y & 7) * 2);
  }
  unsigned bpp() const { static const unsigned t[4] = {2, 4, 4, 8}; return t[regs.scmr.md]; }
  auto flushPixels(PxCache &c) -> void {
    if (!c.pend) return;
    uint8_t x = (uint8_t)(c.off << 3), y = (uint8_t)(c.off >> 5);
    unsigned bp = bpp();
    uint32_t addr = charAddr(x, y, bp);
    for (unsigned n = 0; n < bp; n++) {
      uint32_t byte = ((n >> 1) << 4) + (n & 1);
      uint8_t data = 0;
      for (unsigned i = 0; i < 8; i++) data |= ((c.data[i] >> n) & 1) << i;
      if (c.pend != 0xFF) { step(memclk()); data &= c.pend; data |= read(addr + byte) & ~c.pend; }
      step(memclk());
      write(addr + byte, data);
    }
    c.pend = 0;
  }
  auto plot(n8 xx, n8 yy) -> void override {
    uint8_t x = (uint8_t)(uint32_t)xx, y = (uint8_t)(uint32_t)yy, colr = (uint8_t)(uint32_t)regs.colr;
    if (!regs.por.transparent) {
      if (regs.scmr.md == 3) { if (regs.por.freezehigh) { if ((colr & 0x0F) == 0) return; } else if (colr == 0) return; }
      else if ((colr & 0x0F) == 0) return;
    }
    uint8_t color = colr;
    if (regs.por.dither && regs.scmr.md != 3) { if ((x ^ y) & 1) color >>= 4; color &= 0x0F; }
    uint16_t offset = (uint16_t)((y << 5) + (x >> 3));
    if (offset != px[0].off) { flushPixels(px[1]); px[1] = px[0]; px[0].pend = 0; px[0].off = offset; }
    x = (x & 7) ^ 7;
    px[0].data[x] = color; px[0].pend |= 1 << x;
    if (px[0].pend == 0xFF) { flushPixels(px[1]); px[1] = px[0]; px[0].pend = 0; }
  }
  auto rpix(n8 xx, n8 yy) -> n8 override {
    uint8_t x = (uint8_t)(uint32_t)xx, y = (uint8_t)(uint32_t)yy;
    flushPixels(px[1]); flushPixels(px[0]);
    unsigned bp = bpp();
    uint32_t addr = charAddr(x, y, bp);
    uint8_t data = 0;
    x = (x & 7) ^ 7;
    for (unsigned n = 0; n < bp; n++) {
      uint32_t byte = ((n >> 1) << 4) + (n & 1);
      step(memclk());
      data |= ((read(addr + byte) >> x) & 1) << n;
    }
    return data;
  }
  void flushAll() { flushPixels(px[1]); flushPixels(px[0]); if (ramcl) step(ramcl); }

  // one program: the chip is started as the CPU does it (R15 written, G set)
  int run(int maxInstr) {
    int n = 0;
    while (regs.sfr.g && n < maxInstr) {
      uint8_t opcode = peekpipe();
      instruction(opcode);
      n++;
      if (regs.r[14].modified) { regs.r[14].modified = false; updateROMBuffer(); }
      if (regs.r[15].modified) regs.r[15].modified = false; else regs.r[15]++;
    }
    return regs.sfr.g ? -1 : n;
  }
};

static Env env;

static uint16_t pick16()
{
  static const uint16_t sp[] = {0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0xFF00, 0x00FE, 0x4000, 0xC000};
  switch (rnd() & 3) { case 0: return sp[rnd() % (sizeof sp / sizeof *sp)]; case 1: return (uint16_t)(rnd() & 0xFF); default: return (uint16_t)rnd(); }
}

struct Trial {
  uint8_t code[64]; int len;
  uint16_t r[15]; uint8_t sfr;   /* flags Z=2 CY=4 S=8 OV=16 */
  uint8_t scbr, scmr, clsr;
  std::vector<int> opcodes;      /* the opcodes in the program, for the report */
  std::vector<std::pair<int, int>> units;   /* (opcode, ALT state) of each unit of the program */
  int alt;                       /* the ALT state when the first opcode runs (0-3), for the report */
  bool pre_to, pre_with, pre_from;
};

static void gen(Trial &t)
{
  t.len = 0; t.opcodes.clear(); t.units.clear();
  if (const char *fixed = getenv("RD_GSU_PROG")) {              /* a fixed program in hex, random registers: RD_GSU_PROG="3D F1 95 68" */
    t.alt = 0; t.pre_to = t.pre_with = t.pre_from = false;
    for (const char *q = fixed; *q;) { while (*q == ' ') q++; if (!*q) break; t.code[t.len++] = (uint8_t)strtoul(q, (char **)&q, 16); }
    t.opcodes.push_back(t.code[0]); t.units.push_back({t.code[0], 0});
    for (int i = 0; i < 6; i++) t.code[t.len++] = (i == 0) ? 0x00 : 0x01;
    for (int i = 0; i < 15; i++) t.r[i] = pick16();
    t.sfr = (uint8_t)(rnd() & 0x1E); t.scbr = (uint8_t)(rnd() & 0x3F);
    t.scmr = (uint8_t)(0x18 | ((rnd() & 1) << 2) | ((rnd() & 1) << 5) | (rnd() & 3)); t.clsr = rnd() & 1;
    return;
  } t.alt = 0; t.pre_to = t.pre_with = t.pre_from = false;
  int carry = 0;
  int units = (rnd() & 1) ? 1 : 1 + rnd() % 5;
  for (int u = 0; u < units; u++) {
    /* RD_GSU_ALT_AFTER=1 lets ALT follow WITH/TO/FROM: ours keeps B there, ares and Snes9x clear it (listed in docs/progress/snes-superfx.md) */
    static const bool altAfter = getenv("RD_GSU_ALT_AFTER") != 0;
    int ualt = carry;
    int nalt = (int)(rnd() % 3 == 0 ? 1 + rnd() % 2 : 0), pre = (int)(rnd() % 3);
    for (int k = 0; k < nalt + pre; k++) {
      int kind = k < nalt ? 0 : 1 + (int)(rnd() % 3);
      if (altAfter && k >= nalt && (rnd() & 3) == 0) kind = 0;
      switch (kind) {
      case 0: { int a = (int)(rnd() % 3); t.code[t.len++] = (uint8_t)(0x3D + a); ualt |= (a == 0 ? 1 : a == 1 ? 2 : 3); if (t.opcodes.empty()) t.alt |= (a == 0 ? 1 : a == 1 ? 2 : 3); break; }   /* ALT1-3 */
      case 1: t.code[t.len++] = 0x10 + rnd() % 15; t.pre_to = true; break;                  /* TO */
      case 2: t.code[t.len++] = 0xB0 + rnd() % 15; t.pre_from = true; break;                  /* FROM */
      default: t.code[t.len++] = 0x20 + rnd() % 15; t.pre_with = true; break;                 /* WITH */
      }
    }
    int op;
    for (;;) {
      op = rnd() & 0xFF;
      if (op == 0x00 || op == 0x3C || (op >= 0x98 && op <= 0x9D)) continue;
      if ((op == 0xAF || op == 0xFF) && ualt != 2) continue;                    /* IBT, IWT and LMS, LM of R15 jump to a random place; SMS and SM of R15 only store it */
      if (op == 0x1F) continue;                                                /* TO R15 is a jump to a random place */
      if (!altAfter && ((op >= 0x20 && op <= 0x2F) || (op >= 0x3D && op <= 0x3F))) continue;      /* WITH and ALT are prefixes, not programs */    /* STOP, LOOP, JMP and LJMP: not in the random stream */
      break;
    }
    t.code[t.len++] = (uint8_t)op;
    t.opcodes.push_back(op);
    t.units.push_back({op, ualt});
    carry = ((op >= 0x10 && op <= 0x1F) || (op >= 0xB0 && op <= 0xBF)) ? ualt : 0;      /* TO and FROM without B are prefixes: ALT goes on to the next opcode */
    if ((op >= 0x05 && op <= 0x0F)) { t.code[t.len++] = (uint8_t)(rnd() % 5); t.code[t.len++] = 0x01; /* the delay slot */ }
    else if ((op >= 0xA0 && op <= 0xAF)) t.code[t.len++] = (uint8_t)rnd();
    else if ((op >= 0xF0 && op <= 0xFF)) { t.code[t.len++] = (uint8_t)rnd(); t.code[t.len++] = (uint8_t)rnd(); }
  }
  for (int i = 0; i < 6; i++) t.code[t.len++] = (i == 0) ? 0x00 : 0x01;           /* STOP, then NOPs for the pipeline */
  for (int i = 0; i < 15; i++) t.r[i] = pick16();
  t.sfr = (uint8_t)(rnd() & 0x1E);
  t.scbr = (uint8_t)(rnd() & 0x3F);
  t.scmr = (uint8_t)(0x18 | ((rnd() & 1) << 2) | ((rnd() & 1) << 5) | (rnd() & 3));
  t.clsr = rnd() & 1;
}

static const char *opname(int op)
{
  static char b[8];
  snprintf(b, sizeof b, "%02X", op);
  return b;
}

int main(int argc, char **argv)
{
  const int trials = argc > 1 ? atoi(argv[1]) : 20000;
  if (argc > 2) rs ^= (uint64_t)strtoull(argv[2], 0, 0) * 0x2545F4914F6CDD1Dull;
  static uint8_t romImg[0x20000], ramImg[0x10000];
  long known1 = 0, known2 = 0, known3 = 0, known4 = 0, unexplained = 0, bad = 0, badopc[256] = {0}, seen[256] = {0}, cycdiff = 0, instdiff = 0, sseen[1024] = {0}, sbad[1024] = {0};
  bool shown[1024] = {false};
  Trial t;
  for (int n = 0; n < trials; n++) {
    gen(t);
    for (size_t i = 0; i < sizeof romImg; i++) romImg[i] = (uint8_t)rnd();
    memcpy(romImg, t.code, t.len);
    /* the cartridge header that snes_load reads (LoROM, Super FX, 128 KB ROM, 64 KB RAM): the same image goes to both sides */
    memset(romImg + 0x7FC0, ' ', 21); memcpy(romImg + 0x7FC0, "GSU REF DIFF", 12);
    romImg[0x7FD5] = 0x20; romImg[0x7FD6] = 0x14; romImg[0x7FD7] = 0x09; romImg[0x7FD8] = 6;
    romImg[0x7FDC] = 0xFF; romImg[0x7FDD] = 0xFF; romImg[0x7FDE] = 0; romImg[0x7FDF] = 0;
    romImg[0x7FFC] = 0x00; romImg[0x7FFD] = 0x80;
    memset(romImg + t.len, 0x00, 96);                              /* a branch that jumps past the end lands on STOP */
    for (size_t i = 0; i < sizeof ramImg; i++) ramImg[i] = (uint8_t)rnd();

    // ours
    ours_gsu_load(romImg);
    memcpy(ours_gsu_ram(), ramImg, sizeof ramImg);
    ours_wr(0x303A, t.scmr); ours_wr(0x3038, t.scbr); ours_wr(0x3039, t.clsr); ours_wr(0x3037, 0x80);
    ours_wr(0x3030, t.sfr);
    for (int i = 0; i < 15; i++) { ours_wr(0x3000 + 2 * i, (uint8_t)t.r[i]); ours_wr(0x3001 + 2 * i, (uint8_t)(t.r[i] >> 8)); }
    ours_wr(0x301E, 0x00); ours_wr(0x301F, 0x80);                    /* R15 = $8000 starts the program */
    int oi = ours_gsu_run(100);
    rd_gsu_t o; ours_gsu_state(&o);

    // ares
    env.reset();
    memcpy(env.rom, romImg, sizeof romImg); memcpy(env.ram, ramImg, sizeof ramImg);
    env.regs.scmr = t.scmr; env.regs.scbr = t.scbr; env.regs.clsr = t.clsr & 1; env.regs.cfgr = 0x80;
    env.regs.sfr = t.sfr;
    for (int i = 0; i < 15; i++) env.regs.r[i] = t.r[i];
    env.regs.r[15] = 0x8000; env.regs.r[15].modified = false;
    env.updateROMBuffer();                                         /* a write of R14 by the CPU fills the ROM buffer */
    env.regs.sfr.g = 1;
    int ri = env.run(5000);
    env.flushAll();

    bool diff = (oi < 0) != (ri < 0);
    char what[160] = "";
    if (!diff && oi >= 0) {
      for (int i = 0; i < 16 && !what[0]; i++) if (o.r[i] != (uint16_t)env.regs.r[i]) snprintf(what, sizeof what, "R%d ours %04X ref %04X", i, o.r[i], (uint16_t)env.regs.r[i]);
      unsigned sr = 0;
      sr = (env.regs.sfr.z << 1) | (env.regs.sfr.cy << 2) | (env.regs.sfr.s << 3) | (env.regs.sfr.ov << 4);
      if (!what[0] && (o.sfr_lo & 0x1E) != sr) snprintf(what, sizeof what, "flags ours %02X ref %02X (ZCSO order 1E)", o.sfr_lo & 0x1E, sr);
      if (!what[0] && o.pbr != (uint8_t)(uint32_t)env.regs.pbr) snprintf(what, sizeof what, "PBR ours %02X ref %02X", o.pbr, (unsigned)(uint32_t)env.regs.pbr);
      if (!what[0] && o.rombr != (uint8_t)(uint32_t)env.regs.rombr) snprintf(what, sizeof what, "ROMBR ours %02X ref %02X", o.rombr, (unsigned)(uint32_t)env.regs.rombr);
      if (!what[0] && o.rambr != (env.regs.rambr ? 1 : 0)) snprintf(what, sizeof what, "RAMBR ours %02X ref %02X", o.rambr, env.regs.rambr ? 1 : 0);
      if (!what[0] && o.colr != (uint8_t)(uint32_t)env.regs.colr) snprintf(what, sizeof what, "COLR ours %02X ref %02X", o.colr, (unsigned)(uint32_t)env.regs.colr);
      if (!what[0] && (o.por & 0x1F) != ((uint32_t)env.regs.por & 0x1F)) snprintf(what, sizeof what, "POR ours %02X ref %02X", o.por, (unsigned)(uint32_t)env.regs.por);
      if (!what[0] && o.cbr != (uint16_t)(uint32_t)env.regs.cbr) snprintf(what, sizeof what, "CBR ours %04X ref %04X", o.cbr, (unsigned)(uint32_t)env.regs.cbr);
      if (!what[0]) {
        const uint8_t *ram = ours_gsu_ram();
        for (unsigned i = 0; i < sizeof ramImg; i++) if (ram[i] != env.ram[i]) { snprintf(what, sizeof what, "RAM[%04X] ours %02X ref %02X", i, ram[i], env.ram[i]); break; }
      }
      diff = what[0] != 0;
      if (oi != ri) instdiff++;
    } else if (diff) snprintf(what, sizeof what, "ours %s, ref %s", oi < 0 ? "does not stop" : "stops", ri < 0 ? "does not stop" : "stops");
    for (int op : t.opcodes) seen[op]++;
    if (t.opcodes.size() == 1) { sseen[t.opcodes[0] * 4 + t.alt]++; if (diff) sbad[t.opcodes[0] * 4 + t.alt]++; }
    if (diff) {
      bad++;
      /* the differences that are already understood (docs/progress/snes-superfx.md, 'Checked against ares'): they are counted apart */
      int cls = 0;
      for (auto &u : t.units) {
        const int op = u.first, al = u.second;
        if (((op >= 0xA0 && op <= 0xAF) || (op >= 0xF0 && op <= 0xFF)) && al == 3) cls |= 1;
        if ((op == 0xAF || op == 0xFF) && al == 2) cls |= 2;
        if (op == 0xDF && al == 3) cls |= 4;                                   /* ROMB: the ROM buffer after a bank change */
        if (op == 0x02 && !strncmp(what, "CBR", 3)) cls |= 8;                /* CACHE: R15 of the CACHE opcode itself or the next byte */
      }
      if (cls & 1) known1++;
      if (cls & 2) known2++;
      if (cls & 4) known3++;
      if (cls & 8) known4++;
      if (!cls) {
        unexplained++;
        if (unexplained <= 12) {
          printf("UNEXPLAINED %s\n   program:", what);
          for (int i = 0; i < t.len - 6; i++) printf(" %02X", t.code[i]);
          printf("\n   in: R0-R14");
          for (int i = 0; i < 15; i++) printf(" %04X", t.r[i]);
          printf(" flags %02X SCBR %02X SCMR %02X CLSR %d\n", t.sfr, t.scbr, t.scmr, t.clsr);
          printf("   RAM at R5: input %02X %02X, ours %02X %02X, ref %02X %02X\n", ramImg[t.r[5]], ramImg[(t.r[5] + 1) & 0xFFFF], ours_gsu_ram()[t.r[5]], ours_gsu_ram()[(t.r[5] + 1) & 0xFFFF], env.ram[t.r[5]], env.ram[(t.r[5] + 1) & 0xFFFF]);
        }
      }
      /* blame the first opcode of the program that is the only one (or the first) */
      std::set<int> uniq(t.opcodes.begin(), t.opcodes.end());
      for (int op : uniq) badopc[op]++;
      int key = t.opcodes.empty() ? 0 : t.opcodes[0] * 4 + t.alt;
      if (t.opcodes.size() == 1 && !shown[key]) {
        shown[key] = true;
        printf("MISMATCH op %02X alt %d: %s\n   program:", t.opcodes[0], t.alt, what);
        for (int i = 0; i < t.len - 6; i++) printf(" %02X", t.code[i]);
        printf("\n   in: R0-R14");
        for (int i = 0; i < 15; i++) printf(" %04X", t.r[i]);
        printf(" flags %02X SCBR %02X SCMR %02X CLSR %d\n", t.sfr, t.scbr, t.scmr, t.clsr);
      }
    }
  }
  printf("\nopcode/ALT state: programs of this one opcode with its prefixes, mismatches / programs\n");
  for (int k = 0; k < 1024; k++) if (sbad[k]) printf("  %02X alt%d: %ld / %ld\n", k >> 2, k & 3, sbad[k], sseen[k]);
  printf("compared %d programs: %ld mismatches (ALT3 on A0-AF and F0-FF: %ld, SMS and SM of R15 in ALT2: %ld, GETB after ROMB without R14: %ld, CBR of CACHE at xxxF: %ld, unexplained: %ld); instruction-count differences %ld\n", trials, bad, known1, known2, known3, known4, unexplained, instdiff);
  (void)cycdiff;
  return unexplained ? 1 : 0;
}
