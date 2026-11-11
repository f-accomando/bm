/*
 * snes_gsu.c: the Super FX co-processor (GSU-1 and GSU-2) of the SNES core. Original code, written from public documents:
 * the design, the register map and the source of each fact are in docs/progress/snes-superfx.md.
 *
 * The GSU reads the ROM and writes the cart RAM (S.sram) itself. It runs lazily: the bus calls gsu_run (from gsu_sync_pos) when
 * the CPU touches it. A jump takes effect after the next opcode (the delay slot): `jpend` holds the target.
 * gsu_run keeps the hot state (prefix, flags, pending jump) in locals, so the loop stays in the registers of the host CPU.
 */
#include "snes_int.h"
#include "snes_gsu.h"

#include <string.h>

typedef struct {
    uint16_t r[16];
    uint16_t cbr, latch;
    uint8_t pbr, rombr, rambr, cfgr, scbr, clsr, scmr, colr, por, bramr;
    uint32_t sz;                            /* the flags S and Z, lazy: Z = the low 16 bits are 0, S = bit 15 or bit 16 (bit 16: S set apart from the result) */
    uint8_t fcy, fov, go, irq, irq_line;
    uint8_t alt, b, sreg, dreg;             /* alt: bit 0 ALT1, bit 1 ALT2 */
    uint8_t rbuf, rbuf_bank, rbuf_ok;       /* the ROM buffer, and the bank and R14 it was read with (a change of either reloads it) */
    uint16_t rbuf_r14;
    uint8_t jpend; int16_t jpbr;            /* a jump waits for the delay slot; jpbr >= 0: LJMP changes the bank too */
    uint16_t jaddr;
    uint16_t ramaddr;
    uint32_t rmask;
    uint32_t cvalid;                        /* one bit for each line of the cache */
    int mul;                                /* master clocks for a GSU cycle: 1 (21 MHz) or 2 (10 MHz) */
    int romcyc, ramcyc;                    /* master clocks of a ROM and a RAM access */
    int budget;                             /* master clocks left */
    uint32_t insns;
    uint8_t pmask, pcx, pcy; uint8_t pdata[8];   /* the pixel cache: one row of 8 pixels (X & $F8, Y) */
    uint8_t cache[512];
} gsu_t;

static gsu_t g;

#define FLAG_S(sz) ((((sz) >> 15) | ((sz) >> 16)) & 1)
#define FLAG_Z(sz) ((uint16_t)(sz) == 0)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define INLINE static inline __attribute__((always_inline))

/* ------------------------------------------------------------------ memory seen by the GSU */
static inline uint8_t rom_at(unsigned bank, uint16_t off)
{
    uint32_t a = bank < 0x40 ? (((uint32_t)bank << 15) | (off & 0x7FFF)) : (((uint32_t)(bank & 0x1F) << 16) | off);
    if (a >= S.rom_size) a %= S.rom_size;
    return S.rom[a];
}

static inline uint8_t code_byte(unsigned bank, uint16_t off)
{
    if (bank < 0x60) return rom_at(bank, off);
    if (bank == 0x70 || bank == 0x71) return S.sram[((((uint32_t)bank & 1) << 16) | off) & g.rmask];
    return 0;
}

INLINE uint8_t ram_rd(uint16_t a) { return S.sram[((((uint32_t)g.rambr) << 16) | a) & g.rmask]; }
INLINE void ram_wr(uint16_t a, uint8_t v) { S.sram[((((uint32_t)g.rambr) << 16) | a) & g.rmask] = v; S.sram_dirty = 1; }
/* a word: the low byte at the address, the high byte at the address with bit 0 flipped (the same RAM page); the mask keeps bit 0 */
INLINE uint16_t ram_rw(uint16_t a) { const uint32_t i = ((((uint32_t)g.rambr) << 16) | a) & g.rmask; return (uint16_t)(S.sram[i] | (S.sram[i ^ 1] << 8)); }
INLINE void ram_ww(uint16_t a, uint16_t v) { const uint32_t i = ((((uint32_t)g.rambr) << 16) | a) & g.rmask; S.sram[i] = (uint8_t)v; S.sram[i ^ 1] = (uint8_t)(v >> 8); S.sram_dirty = 1; }

/* the line of the cache at CBR + 16 ln: 16 bytes, read from the ROM or the RAM (a line never crosses a bank: CBR and the line are 16-aligned) */
static int cache_fill(unsigned ln, unsigned off)
{
    const uint16_t base = (uint16_t)(g.cbr + ln * 16);
    uint8_t *const d = &g.cache[ln * 16];
    const unsigned bank = g.pbr;
    if (bank < 0x60) {
        const uint32_t a = bank < 0x40 ? (((uint32_t)bank << 15) | (base & 0x7FFF)) : (((uint32_t)(bank & 0x1F) << 16) | base);
        if (a + 16 <= S.rom_size) memcpy(d, S.rom + a, 16);
        else for (unsigned i = 0; i < 16; i++) d[i] = rom_at(bank, (uint16_t)(base + i));
    } else if (bank == 0x70 || bank == 0x71) {
        memcpy(d, &S.sram[((((uint32_t)bank & 1) << 16) | base) & g.rmask], 16);
    } else memset(d, 0, 16);
    g.cvalid |= 1u << ln;
    return g.romcyc * (int)(16 - (off & 15));
}

/* a fetch the cache does not have: a line to fill, or an address out of the 512 bytes. Returns the byte and, in bits 8 and up, the extra master clocks. */
static __attribute__((noinline)) unsigned fetch_slow(uint16_t pc)
{
    const unsigned off = (uint16_t)(pc - g.cbr);
    if (off < 512) { const int c = cache_fill(off >> 4, off); return g.cache[off] | ((unsigned)c << 8); }
    return code_byte(g.pbr, pc) | ((unsigned)g.romcyc << 8);
}

/* the opcode and operand bytes: from the cache when the address is in the 512 bytes at CBR and its line is filled; *cyc gets the extra clocks */
INLINE uint8_t fetch(int *cyc, uint16_t pc)
{
    const unsigned off = (uint16_t)(pc - g.cbr);
    if (LIKELY(off < 512 && ((g.cvalid >> (off >> 4)) & 1))) return g.cache[off];
    const unsigned x = fetch_slow(pc);
    *cyc += (int)(x >> 8);
    return (uint8_t)x;
}
/* ------------------------------------------------------------------ the pixel cache */
static const uint8_t bpp_of[4] = { 2, 4, 4, 8 };

static unsigned pix_addr(unsigned x, unsigned y)         /* the RAM offset of the row of the tile at the pixel (x, y), plane 0 */
{
    const unsigned ht = (g.por & 0x10) ? 3 : ((g.scmr >> 2) & 1) | ((g.scmr >> 4) & 2);   /* POR bit 4 forces the OBJ layout ([?] the doc also says bit 3) */
    const unsigned md = g.scmr & 3;
    unsigned tile;
    switch (ht) {
    case 0: tile = (x >> 3) * 0x10 + (y >> 3); break;
    case 1: tile = (x >> 3) * 0x14 + (y >> 3); break;
    case 2: tile = (x >> 3) * 0x18 + (y >> 3); break;
    default: tile = (y >> 7) * 0x200 + (x >> 7) * 0x100 + ((y >> 3) & 15) * 0x10 + ((x >> 3) & 15); break;
    }
    return ((unsigned)g.scbr << 10) + tile * (unsigned)(bpp_of[md] * 8) + (y & 7) * 2;
}

static void pix_flush(void)
{
    if (!g.pmask) return;
    const unsigned bpp = bpp_of[g.scmr & 3];
    const unsigned base = pix_addr(g.pcx, g.pcy);
    for (unsigned p = 0; p < bpp; p++) {
        unsigned byte = 0;
        for (unsigned i = 0; i < 8; i++) byte |= (unsigned)((g.pdata[i] >> p) & 1) << (7 - i);
        uint8_t *d = &S.sram[(base + (p >> 1) * 16 + (p & 1)) & g.rmask];
        *d = g.pmask == 0xFF ? (uint8_t)byte : (uint8_t)((*d & ~g.pmask) | (byte & g.pmask));
    }
    g.pmask = 0;
    S.sram_dirty = 1;
}

/* PLOT: the pixel (R1, R2) with COLR, then R1 + 1. The mask of the pixels of the row has a bit for each pixel, bit 7 is the pixel 0. */
static void do_plot(void)
{
    const unsigned x = g.r[1] & 0xFF, y = g.r[2] & 0xFF;
    g.r[1]++;
    if (!(g.por & 1)) {
        if ((g.scmr & 3) == 3) { if (g.por & 8 ? (g.colr & 15) == 0 : g.colr == 0) return; }
        else if ((g.colr & 15) == 0) return;
    }
    uint8_t c = g.colr;
    if ((g.por & 2) && (g.scmr & 3) != 3) { if ((x ^ y) & 1) c >>= 4; c &= 15; }
    if (g.pmask && ((x & 0xF8) != g.pcx || y != g.pcy)) pix_flush();
    g.pcx = (uint8_t)(x & 0xF8); g.pcy = (uint8_t)y;
    g.pdata[x & 7] = c;
    g.pmask |= (uint8_t)(0x80 >> (x & 7));
    if (g.pmask == 0xFF) pix_flush();
}

static uint8_t do_rpix(void)
{
    pix_flush();
    const unsigned x = g.r[1] & 0xFF, y = g.r[2] & 0xFF;
    const unsigned bpp = bpp_of[g.scmr & 3];
    const unsigned base = pix_addr(x, y);
    unsigned v = 0;
    for (unsigned p = 0; p < bpp; p++)
        v |= (unsigned)((S.sram[(base + (p >> 1) * 16 + (p & 1)) & g.rmask] >> (7 - (x & 7))) & 1) << p;
    return (uint8_t)v;
}

/* ------------------------------------------------------------------ the registers */
/* the ROM buffer: GETB reads it. It reloads when R14 or ROMBR changed since the last load (the real chip reloads at each write of R14) */
INLINE uint8_t rom_buffer(int *cyc, int rom_c)
{
    if (!LIKELY(g.rbuf_ok && g.rbuf_r14 == g.r[14] && g.rbuf_bank == g.rombr)) {
        g.rbuf = rom_at(g.rombr, g.r[14]);
        g.rbuf_r14 = g.r[14]; g.rbuf_bank = g.rombr; g.rbuf_ok = 1;
        *cyc += rom_c;
    }
    return g.rbuf;
}
static inline uint8_t color_of(uint8_t src)
{
    if (g.por & 4) return (uint8_t)((g.colr & 0xF0) | (src >> 4));
    if (g.por & 8) return (uint8_t)((g.colr & 0xF0) | (src & 15));
    return src;
}

static void clock_set(void)
{
    g.mul = (g.clsr & 1) ? 1 : 2;
    g.romcyc = g.mul * (g.mul == 1 ? 5 : 3);       /* master clocks of one ROM and one RAM access */
    g.ramcyc = g.mul * (g.mul == 1 ? 6 : 3);
}

void gsu_reset(uint32_t ram_size)
{
    memset(&g, 0, sizeof g);
    g.rmask = ram_size - 1;
    g.jpbr = -1;
    g.sz = 1;                               /* Z clear, S clear */
    clock_set();
}

/* ------------------------------------------------------------------ the instructions */
/* One opcode: the switch only computes. The common ends are labels after it (wr_d and wr_n store the result in R(dreg) or R(n), clr clears the prefix,
 * fin takes the cycles): the code of an opcode is a few instructions and the loop is small enough to stay in the instruction cache of the Pi Zero W
 * next to the CPU core. `cyc` counts master clocks: the extra costs are multiples of `mul`. */
#define SZ(v) (sz = (uint16_t)(v))
#define RS (r[sreg])
#define SFLAG FLAG_S(sz)
#define ZFLAG FLAG_Z(sz)
#define FETCH8() ({ const unsigned f_ = fetch(&cyc, pc); pc++; (uint8_t)f_; })
#define FETCH16() ({ const unsigned l_ = FETCH8(); const unsigned h_ = FETCH8(); (uint16_t)(l_ | (h_ << 8)); })

void gsu_run(int clocks)
{
    if (!g.go) { g.budget = 0; return; }
    int budget = g.budget + clocks;
    const int mul = g.mul;
    const int rom_c = g.romcyc, ram_c = g.ramcyc;
    unsigned alt = g.alt, b = g.b, sreg = g.sreg, dreg = g.dreg;
    uint32_t sz = g.sz;
    unsigned fcy = g.fcy, fov = g.fov;
    int jpend = g.jpend, jpbr = g.jpbr;
    uint16_t jaddr = g.jaddr;
    uint16_t *const r = g.r;
    uint16_t pc = r[15];
    uint32_t cnt = 0;
    int cyc;
    unsigned n, k;
    uint16_t v;

    while (budget > 0) {
        cyc = mul;
        const uint8_t op = fetch(&cyc, pc);
        pc++;
        cnt++;
        if (jpend) {
            jpend = 0;
            pc = jaddr;
            if (jpbr >= 0) { g.pbr = (uint8_t)jpbr; jpbr = -1; g.cbr = (uint16_t)(jaddr & 0xFFF0); g.cvalid = 0; }
        }
        r[15] = pc;
        n = op & 15;
        switch (op) {
        case 0x00:                                              /* STOP */
            pix_flush();
            g.go = 0; g.irq = 1;
            if (!(g.cfgr & 0x80)) { g.irq_line = 1; S.irq_pending = 1; }
            pc++;
            b = alt = sreg = dreg = 0;
            budget = 0;
            goto out;
        case 0x01: goto clr;                                    /* NOP */
        case 0x02: {                                            /* CACHE */
            const uint16_t nb = (uint16_t)((pc - 1) & 0xFFF0);
            if (g.cbr != nb) { g.cbr = nb; g.cvalid = 0; }
            goto clr;
        }
        case 0x03: { const uint16_t s = RS; fcy = s & 1; v = s >> 1; SZ(v); goto wr_d; }   /* LSR */
        case 0x04: { const uint16_t s = RS; v = (uint16_t)((s << 1) | fcy); fcy = s >> 15; SZ(v); goto wr_d; }   /* ROL */
        case 0x05: k = 1; goto branch;
        case 0x06: k = SFLAG == fov; goto branch;               /* BGE */
        case 0x07: k = SFLAG != fov; goto branch;               /* BLT */
        case 0x08: k = !ZFLAG; goto branch;
        case 0x09: k = ZFLAG; goto branch;
        case 0x0A: k = !SFLAG; goto branch;
        case 0x0B: k = SFLAG; goto branch;
        case 0x0C: k = !fcy; goto branch;
        case 0x0D: k = fcy; goto branch;
        case 0x0E: k = !fov; goto branch;
        case 0x0F: k = fov; goto branch;
        case 0x10 ... 0x1F:                                     /* TO / MOVE */
            if (b) { v = RS; goto wr_n; }
            dreg = n;
            goto fin;
        case 0x20 ... 0x2F: sreg = dreg = n; b = 1; goto fin;   /* WITH */
        case 0x30 ... 0x3B:                                     /* STW / STB */
            g.ramaddr = r[n];
            if (alt & 1) ram_wr(g.ramaddr, (uint8_t)RS); else ram_ww(g.ramaddr, RS);
            cyc += mul; goto clr;
        case 0x3C:                                              /* LOOP */
            v = --r[12];
            SZ(v);
            if (v) { jpend = 1; jaddr = r[13]; jpbr = -1; }
            goto clr;
        case 0x3D: alt |= 1; goto fin;
        case 0x3E: alt |= 2; goto fin;
        case 0x3F: alt = 3; goto fin;
        case 0x40 ... 0x4B:                                     /* LDW / LDB */
            g.ramaddr = r[n];
            v = (alt & 1) ? ram_rd(g.ramaddr) : ram_rw(g.ramaddr);
            cyc += ram_c;
            goto wr_d;
        case 0x4C:                                              /* PLOT / RPIX */
            if (alt & 1) { v = do_rpix(); sz = v; cyc += ram_c * 3; goto wr_d; }
            do_plot();
            cyc += mul; goto clr;
        case 0x4D: { const uint16_t s = RS; v = (uint16_t)((s << 8) | (s >> 8)); SZ(v); goto wr_d; }   /* SWAP */
        case 0x4E:                                              /* COLOR / CMODE */
            if (alt & 1) g.por = RS & 0x1F; else g.colr = color_of((uint8_t)RS);
            goto clr;
        case 0x4F: v = (uint16_t)~RS; SZ(v); goto wr_d;         /* NOT */
        case 0x50 ... 0x5F: {                                   /* ADD, ADC, ADD #, ADC # */
            const uint32_t s = RS, m = (alt & 2) ? n : r[n];
            const uint32_t res = s + m + ((alt & 1) ? fcy : 0);
            fov = (~(s ^ m) & (s ^ res) & 0x8000) != 0;
            fcy = res > 0xFFFF; SZ(res);
            v = (uint16_t)res; goto wr_d;
        }
        case 0x60 ... 0x6F: {                                   /* SUB, SBC, SUB #, CMP */
            const uint32_t s = RS, m = (alt == 2) ? n : r[n];
            const uint32_t res = s - m - (alt == 1 ? (fcy ^ 1) : 0);
            fov = ((s ^ m) & (s ^ res) & 0x8000) != 0;
            fcy = (int32_t)res >= 0; SZ(res);
            if (alt == 3) goto clr;
            v = (uint16_t)res; goto wr_d;
        }
        case 0x70:                                              /* MERGE: S, OV, CY and Z come from the masks, Z = (v & $F0F0) != 0 ([F]) */
            v = (uint16_t)((r[7] & 0xFF00) | (r[8] >> 8));
            sz = ((v & 0x8080) ? 0x10000u : 0u) | ((v & 0xF0F0) ? 0u : 1u);   /* the Z flag is set when the low 16 bits are 0: so the flag (v & $F0F0) != 0 means they are 0 */
            fov = (v & 0xC0C0) != 0; fcy = (v & 0xE0E0) != 0;
            goto wr_d;
        case 0x71 ... 0x7F: {                                   /* AND, BIC, AND #, BIC # */
            const uint16_t m = (alt & 2) ? (uint16_t)n : r[n];
            v = (alt & 1) ? (uint16_t)(RS & ~m) : (uint16_t)(RS & m);
            SZ(v); goto wr_d;
        }
        case 0x80 ... 0x8F: {                                   /* MULT, UMULT, MULT #, UMULT # */
            const uint16_t m = (alt & 2) ? (uint16_t)n : r[n];
            v = (alt & 1) ? (uint16_t)((uint8_t)RS * (uint8_t)m) : (uint16_t)((int8_t)RS * (int8_t)m);
            SZ(v);
            if (!(g.cfgr & 0x20)) cyc += mul;
            goto wr_d;
        }
        case 0x90: ram_ww(g.ramaddr, RS); cyc += mul; goto clr;   /* SBK */
        case 0x91 ... 0x94: r[11] = (uint16_t)(pc + n); goto clr;   /* LINK */
        case 0x95: v = (uint16_t)(int16_t)(int8_t)RS; SZ(v); goto wr_d;   /* SEX */
        case 0x96: {                                            /* ASR / DIV2 */
            const uint16_t s = RS;
            v = (uint16_t)((int16_t)s >> 1);
            if ((alt & 1) && s == 0xFFFF) v = 0;
            fcy = s & 1; SZ(v); goto wr_d;
        }
        case 0x97: { const uint16_t s = RS; v = (uint16_t)((s >> 1) | (fcy << 15)); fcy = s & 1; SZ(v); goto wr_d; }   /* ROR */
        case 0x98 ... 0x9D:                                     /* JMP / LJMP */
            if (alt & 1) { jpend = 1; jaddr = RS; jpbr = r[n] & 0x7F; }
            else { jpend = 1; jaddr = r[n]; jpbr = -1; }
            goto clr;
        case 0x9E: v = RS & 0xFF; sz = v | ((v & 0x80) ? 0x10000u : 0u); goto wr_d;   /* LOB */
        case 0x9F: {                                            /* FMULT / LMULT */
            const int32_t pr = (int32_t)(int16_t)RS * (int16_t)r[6];
            v = (uint16_t)((uint32_t)pr >> 16);
            if (alt & 1) r[4] = (uint16_t)pr;
            fcy = ((uint32_t)pr >> 15) & 1; SZ(v);
            cyc += ((g.cfgr & 0x20) ? 3 : 7) * mul;
            goto wr_d;
        }
        case 0xA0 ... 0xAF:                                     /* IBT / LMS / SMS */
            k = FETCH8();
            if (alt == 1) { g.ramaddr = (uint16_t)(k << 1); v = ram_rw(g.ramaddr); cyc += ram_c + 2 * mul; }
            else if (alt == 2) { g.ramaddr = (uint16_t)(k << 1); ram_ww(g.ramaddr, r[n]); cyc += 2 * mul; goto clr; }
            else v = (uint16_t)(int16_t)(int8_t)k;
            goto wr_n;
        case 0xB0 ... 0xBF:                                     /* FROM / MOVES */
            if (b) {
                v = r[n];
                fov = (v >> 7) & 1; SZ(v); goto wr_d;
            }
            sreg = n; goto fin;
        case 0xC0: v = RS >> 8; sz = v | ((v & 0x80) ? 0x10000u : 0u); goto wr_d;   /* HIB */
        case 0xC1 ... 0xCF: {                                   /* OR, XOR, OR #, XOR # */
            const uint16_t m = (alt & 2) ? (uint16_t)n : r[n];
            v = (alt & 1) ? (uint16_t)(RS ^ m) : (uint16_t)(RS | m);
            SZ(v); goto wr_d;
        }
        case 0xD0 ... 0xDE: v = (uint16_t)(r[n] + 1); SZ(v); goto wr_n;   /* INC */
        case 0xDF:                                              /* GETC / RAMB / ROMB */
            if (alt == 2) g.rambr = RS & 1;
            else if (alt == 3) g.rombr = RS & 0x7F;
            else g.colr = color_of(rom_buffer(&cyc, rom_c));
            goto clr;
        case 0xE0 ... 0xEE: v = (uint16_t)(r[n] - 1); SZ(v); goto wr_n;   /* DEC */
        case 0xEF: {                                            /* GETB / GETBH / GETBL / GETBS */
            const uint8_t by = rom_buffer(&cyc, rom_c);
            switch (alt) {
            case 0: v = by; break;
            case 1: v = (uint16_t)((by << 8) | (RS & 0xFF)); break;
            case 2: v = (uint16_t)((RS & 0xFF00) | by); break;
            default: v = (uint16_t)(int16_t)(int8_t)by; break;
            }
            goto wr_d;
        }
        case 0xF0 ... 0xFF:                                     /* IWT / LM / SM */
            k = FETCH16();
            if (alt == 1) { g.ramaddr = (uint16_t)k; v = ram_rw(g.ramaddr); cyc += ram_c + 3 * mul; }
            else if (alt == 2) { g.ramaddr = (uint16_t)k; ram_ww(g.ramaddr, r[n]); cyc += 3 * mul; goto clr; }
            else v = (uint16_t)k;
            goto wr_n;
        default: __builtin_unreachable();
        }
    branch: {
        const int8_t d = (int8_t)FETCH8();
        if (k) { jpend = 1; jaddr = (uint16_t)(pc + d); jpbr = -1; }
        goto fin;
    }
    wr_n:
        if (n == 15) { jpend = 1; jaddr = v; jpbr = -1; } else r[n] = v;
        goto clr;
    wr_d:
        if (dreg == 15) { jpend = 1; jaddr = v; jpbr = -1; } else r[dreg] = v;
    clr:
        b = alt = sreg = dreg = 0;
    fin:
        budget -= cyc;
    }
out:
    r[15] = pc;
    g.budget = budget;
    g.alt = (uint8_t)alt; g.b = (uint8_t)b; g.sreg = (uint8_t)sreg; g.dreg = (uint8_t)dreg;
    g.sz = sz; g.fcy = (uint8_t)fcy; g.fov = (uint8_t)fov;
    g.jpend = (uint8_t)jpend; g.jpbr = (int16_t)jpbr; g.jaddr = jaddr;
    g.insns += cnt;
}

/* ------------------------------------------------------------------ the SNES side */
int gsu_go(void) { return g.go; }
int gsu_owns_rom(void) { return g.go && (g.scmr & 0x10); }
int gsu_irq_line(void) { return g.irq_line; }
uint32_t gsu_instr_count(void) { return g.insns; }
uint32_t gsu_ram_mask(void) { return g.rmask; }
uint16_t gsu_reg(int n) { return g.r[n & 15]; }

void gsu_dbg(gsu_dbg_t *d)
{
    memcpy(d->r, g.r, sizeof d->r);
    d->pbr = g.pbr; d->rombr = g.rombr; d->rambr = g.rambr; d->cfgr = g.cfgr; d->scbr = g.scbr; d->clsr = g.clsr; d->scmr = g.scmr;
    d->colr = g.colr; d->por = g.por; d->go = g.go; d->irq = g.irq; d->alt = g.alt; d->b = g.b; d->cbr = g.cbr;
    d->sfr_lo = (uint8_t)((FLAG_Z(g.sz) << 1) | (g.fcy << 2) | (FLAG_S(g.sz) << 3) | (g.fov << 4) | (g.go << 5));
}

uint8_t gsu_vector_byte(uint16_t off)
{
    static const uint8_t even[8] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x0C };
    const unsigned n = off & 15;
    return (n & 1) ? 0x01 : even[n >> 1];
}

static void start(void)
{
    g.go = 1;
    g.jpend = 0; g.jpbr = -1;
    g.rbuf_ok = 0;
    g.budget = 0;
}

uint8_t gsu_read(uint16_t off)
{
    if (off >= 0x3100) return g.cache[(off - 0x3100) & 0x1FF];
    off = (uint16_t)(0x3000 | (off & 0x3F));
    if (off < 0x3020) { const uint16_t v = g.r[(off >> 1) & 15]; return (off & 1) ? (uint8_t)(v >> 8) : (uint8_t)v; }
    switch (off) {
    case 0x3030: return (uint8_t)((FLAG_Z(g.sz) << 1) | (g.fcy << 2) | (FLAG_S(g.sz) << 3) | (g.fov << 4) | (g.go << 5));
    case 0x3031: {
        const uint8_t v = (uint8_t)((g.alt & 3) | (g.b << 4) | (g.irq << 7));
        g.irq = 0;
        if (g.irq_line) { g.irq_line = 0; S.irq_pending = S.timeup; }
        return v;
    }
    case 0x3034: return g.pbr;
    case 0x3036: return g.rombr;
    case 0x3037: return g.cfgr;
    case 0x3038: return g.scbr;
    case 0x3039: return g.clsr;
    case 0x303A: return g.scmr;
    case 0x303B: return 4;
    case 0x303C: return g.rambr;
    case 0x303E: return (uint8_t)(g.cbr & 0xF0);
    case 0x303F: return (uint8_t)(g.cbr >> 8);
    default: return 0;
    }
}

void gsu_write(uint16_t off, uint8_t v)
{
    if (off >= 0x3100) {
        const unsigned i = (off - 0x3100) & 0x1FF;
        g.cache[i] = v;
        if ((i & 15) == 15) g.cvalid |= 1u << (i >> 4);
        return;
    }
    off = (uint16_t)(0x3000 | (off & 0x3F));
    if (off < 0x3020) {
        const unsigned n = (off >> 1) & 15;
        if (!(off & 1)) { g.latch = v; return; }
        g.r[n] = (uint16_t)(g.latch | (v << 8));
        if (n == 15) start();
        return;
    }
    switch (off) {
    case 0x3030: {
        const int was = g.go;
        g.sz = ((v & 2) ? 0u : 1u) | ((v & 8) ? 0x10000u : 0u);       /* Z and S as written */
        g.fcy = (v >> 2) & 1; g.fov = (v >> 4) & 1;
        g.go = (v >> 5) & 1;
        if (!g.go) { g.cbr = 0; g.cvalid = 0; pix_flush(); }
        else if (!was) start();
        break;
    }
    case 0x3031: g.alt = v & 3; g.b = (v >> 4) & 1; break;
    case 0x3033: g.bramr = v & 1; break;
    case 0x3034: g.pbr = v & 0x7F; break;
    case 0x3037: g.cfgr = v; break;
    case 0x3038: g.scbr = v; break;
    case 0x3039: g.clsr = v & 1; clock_set(); break;
    case 0x303A: g.scmr = v; break;
    default: break;
    }
}
