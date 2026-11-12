/*
 * snes_gsu.c: the Super FX co-processor (GSU-1 and GSU-2) of the SNES core. Original code, written from public documents:
 * the design, the register map and the source of each fact are in docs/progress/snes-superfx.md.
 *
 * The GSU reads the ROM and writes the cart RAM (S.sram) itself. It runs lazily: the bus calls gsu_run (from gsu_sync_pos) when
 * the CPU touches it. A jump takes effect after the next opcode (the delay slot): `jt` holds the target.
 * Speed (docs/progress/snes-superfx.md, "Speed"): gsu_run keeps the hot state in locals, fetches through a pointer into the code cache
 * (`ip`, one test of its low bits per byte), dispatches with one table jump, and counts the master clocks straight in `budget`.
 * tests/snes/gsu_ref.c is the plain version it must equal (tests/snes/gsu_diff.c).
 */
#include "snes_int.h"
#include "snes_gsu.h"
#include "snes_gsu_off.h"

#include <stddef.h>
#include <string.h>

/* On the ARMv6 console the loop is assembly (snes_gsu_arm.S): the C loop below is the reference and runs on the PC, on the RGB30 (AArch64) and with -DSNES_GSU_C. */
#if defined(__arm__) && !defined(__aarch64__) && !defined(SNES_GSU_C)
#define GSU_ASM 1
#else
#define GSU_ASM 0
#endif

typedef uint16_t __attribute__((may_alias)) u16a;

typedef struct {                            /* the first 256 bytes are reached by the assembly loop with short offsets: snes_gsu_off.h */
    uint32_t r[16];                         /* R0-R15, each below 65536 (a 32-bit slot: the loop loads it with one instruction) */
    uint32_t sz;                            /* the flags S and Z, lazy: Z = the low 16 bits are 0, S = bit 15 or bit 16 (bit 16: S set apart from the result) */
    uint32_t fcy, fov;
    uint32_t jt;                            /* a pending jump (it takes effect after the delay slot): 0 = none; else bit 16 | target, bit 31 = LJMP with the bank in bits 24-30 */
    int budget;                             /* master clocks left */
    int mul;                                /* master clocks for a GSU cycle: 1 (21 MHz) or 2 (10 MHz) */
    int romcyc, ramcyc;                    /* master clocks of a ROM and a RAM access */
    uint32_t cvalid;                        /* one bit for each line of the cache */
    uint32_t rm;                            /* rmask & $FFFF */
    uint8_t *rbase;                         /* S.sram + the RAMBR page (set at the start of a run and by RAMB) */
    uint8_t *ipn;                           /* the code pointer after a slow fetch (0: the next byte is not in the cache) */
    uint32_t insns;
    uint16_t cbr;
    uint16_t ramaddr;
    uint8_t alt, b, sreg, dreg;             /* alt: bit 0 ALT1, bit 1 ALT2 */
    uint8_t cfgr, rombr, dirty, pbr;
    uint8_t pad_[128];
    uint8_t cache[512] __attribute__((aligned(16)));
    uint32_t rmask;
    uint16_t latch;
    uint8_t rambr, scbr, clsr, scmr, colr, por, bramr;
    uint8_t go, irq, irq_line;
    uint8_t rbuf, rbuf_bank, rbuf_ok;       /* the ROM buffer, and the bank and R14 it was read with (a change of R14 reloads it; ROMB does not) */
    uint16_t rbuf_r14;
    uint8_t pmask, pcx, pcy;                /* the pixel cache: one row of 8 pixels (X & $F8, Y); pw: the bit planes of the 8 pixels, a byte for each plane (planes 0-3 in pw[0], 4-7 in pw[1]) */
    uint32_t pw[2];
} gsu_t;

#if defined(__arm__) && !defined(__aarch64__)
_Static_assert(offsetof(gsu_t, sz) == G_SZ && offsetof(gsu_t, fcy) == G_FCY && offsetof(gsu_t, fov) == G_FOV && offsetof(gsu_t, jt) == G_JT, "gsu_t offsets");
_Static_assert(offsetof(gsu_t, budget) == G_BUDGET && offsetof(gsu_t, mul) == G_MUL && offsetof(gsu_t, romcyc) == G_ROMCYC && offsetof(gsu_t, ramcyc) == G_RAMCYC, "gsu_t offsets");
_Static_assert(offsetof(gsu_t, cvalid) == G_CVALID && offsetof(gsu_t, rm) == G_RM && offsetof(gsu_t, rbase) == G_RBASE && offsetof(gsu_t, ipn) == G_IPN, "gsu_t offsets");
_Static_assert(offsetof(gsu_t, insns) == G_INSNS && offsetof(gsu_t, cbr) == G_CBR && offsetof(gsu_t, ramaddr) == G_RAMADDR && offsetof(gsu_t, alt) == G_ALT, "gsu_t offsets");
_Static_assert(offsetof(gsu_t, b) == G_B && offsetof(gsu_t, sreg) == G_SREG && offsetof(gsu_t, dreg) == G_DREG && offsetof(gsu_t, cfgr) == G_CFGR, "gsu_t offsets");
_Static_assert(offsetof(gsu_t, rombr) == G_ROMBR && offsetof(gsu_t, dirty) == G_DIRTY && offsetof(gsu_t, pbr) == G_PBR && offsetof(gsu_t, cache) == G_CACHE, "gsu_t offsets");
#endif

static gsu_t g __attribute__((aligned(16)));
#if GSU_ASM
void gsu_run_asm(gsu_t *gp, int budget);          /* snes_gsu_arm.S: runs while budget > 0 and the program runs */
#endif

#define FLAG_S(sz) ((((sz) >> 15) | ((sz) >> 16)) & 1)
#define FLAG_Z(sz) ((uint16_t)(sz) == 0)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define JF 0x10000u                         /* a jump is pending */
#define JL 0x80000000u                      /* LJMP */

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

static inline void ram_base(void) { g.rbase = S.sram + (((uint32_t)g.rambr << 16) & g.rmask); }

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

/* a fetch the code pointer cannot do: the pointer is at a line start (or 0 after a jump). Returns the byte and, in bits 8 and up, the extra master clocks;
 * g.ipn gets the pointer to the next byte when the byte is in the cache (the 512 bytes at CBR), else 0 (every byte comes here). */
__attribute__((noinline)) unsigned gsu_fetch_slow(uint16_t pc)
{
    const unsigned off = (uint16_t)(pc - g.cbr);
    if (off < 512) {
        const int c = ((g.cvalid >> (off >> 4)) & 1) ? 0 : cache_fill(off >> 4, off);
        g.ipn = &g.cache[off + 1];
        return g.cache[off] | ((unsigned)c << 8);
    }
    g.ipn = 0;
    return code_byte(g.pbr, pc) | ((unsigned)g.romcyc << 8);
}

/* ------------------------------------------------------------------ the pixel cache */
static const uint8_t bpp_of[4] = { 2, 4, 4, 8 };
static const uint32_t spread[16] = {     /* the bit p of the colour in the byte p */
    0x00000000, 0x00000001, 0x00000100, 0x00000101, 0x00010000, 0x00010001, 0x00010100, 0x00010101,
    0x01000000, 0x01000001, 0x01000100, 0x01000101, 0x01010000, 0x01010001, 0x01010100, 0x01010101
};

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
    const unsigned pm = g.pmask;
    for (unsigned p = 0; p < bpp; p++) {
        const unsigned byte = (g.pw[p >> 2] >> ((p & 3) * 8)) & 0xFF;
        uint8_t *d = &S.sram[(base + (p >> 1) * 16 + (p & 1)) & g.rmask];
        *d = pm == 0xFF ? (uint8_t)byte : (uint8_t)((*d & ~pm) | (byte & pm));
    }
    g.pmask = 0;
    S.sram_dirty = 1;
}

/* PLOT: the pixel (R1, R2) with COLR, then R1 + 1. The mask of the pixels of the row has a bit for each pixel, bit 7 is the pixel 0. */
static void do_plot(void)
{
    const unsigned x = g.r[1] & 0xFF, y = g.r[2] & 0xFF;
    g.r[1] = (g.r[1] + 1) & 0xFFFF;
    const unsigned por = g.por, md = g.scmr & 3;
    unsigned c = g.colr;
    if (!(por & 1)) {
        if (md == 3) { if (por & 8 ? (c & 15) == 0 : c == 0) return; }
        else if ((c & 15) == 0) return;
    }
    if ((por & 2) && md != 3) { if ((x ^ y) & 1) c >>= 4; c &= 15; }
    if (g.pmask && ((x & 0xF8) != g.pcx || y != g.pcy)) pix_flush();
    g.pcx = (uint8_t)(x & 0xF8); g.pcy = (uint8_t)y;
    const unsigned bit = 0x80u >> (x & 7);
    g.pw[0] = (g.pw[0] & ~(bit * 0x01010101u)) | (spread[c & 15] * bit);
    if (md == 3) g.pw[1] = (g.pw[1] & ~(bit * 0x01010101u)) | (spread[c >> 4] * bit);
    g.pmask |= (uint8_t)bit;
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
/* the ROM buffer: GETB reads it. It reloads when R14 or ROMBR changed since the last load (the real chip reloads at each write of R14). Returns the byte and, in bits 8 and up, the extra clocks. */
static unsigned rom_buffer(void)
{
    unsigned c = 0;
    if (!(g.rbuf_ok && g.rbuf_r14 == (uint16_t)g.r[14])) {
        g.rbuf = rom_at(g.rombr, (uint16_t)g.r[14]);
        g.rbuf_r14 = (uint16_t)g.r[14]; g.rbuf_bank = g.rombr; g.rbuf_ok = 1;
        c = (unsigned)g.romcyc << 8;
    }
    return g.rbuf | c;
}
/* ROMB: the chip loads its ROM buffer when R14 is written, so a GETB after ROMB without a new R14 returns the byte of the OLD bank (ares and Snes9x do the same; Doom relies on it). Load the buffer with the old bank before the bank changes. */
static inline void set_rombr(unsigned bank)
{
    if (!(g.rbuf_ok && g.rbuf_r14 == (uint16_t)g.r[14])) {
        g.rbuf = rom_at(g.rombr, (uint16_t)g.r[14]);
        g.rbuf_r14 = (uint16_t)g.r[14]; g.rbuf_bank = g.rombr; g.rbuf_ok = 1;
    }
    g.rombr = (uint8_t)bank;
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
    g.rm = g.rmask & 0xFFFF;
    g.sz = 1;                               /* Z clear, S clear */
    clock_set();
}

/* ------------------------------------------------------------------ the instructions */
/* One opcode: a table jump, then the code of the opcode computes. The common ends are labels after the opcodes (wr_d and wr_n store the result in R(dreg) or R(n),
 * clr clears the prefix, fin takes the cycles): the code of an opcode is a few instructions and the loop is small enough to stay in the instruction cache of the
 * Pi Zero W next to the CPU core. The master clocks of an instruction (`mul` and the extra costs, multiples of `mul`) go straight into `budget`.
 * alt: bit 0 ALT1, bit 1 ALT2, bit 2 the B flag (WITH). sreg is the index of the source register, d4 the byte offset of the destination in the 32-bit slots (4 * dreg). */
#define SZ(v) (sz = (uint16_t)(v))
#define RS (R[sreg])
#define SFLAG FLAG_S(sz)
#define ZFLAG FLAG_Z(sz)
#define WR16(off, val) (*(u16a *)((char *)R + (off)) = (uint16_t)(val))
#define FETCH8() ({                                                                                     \
        unsigned f_;                                                                                    \
        if (UNLIKELY(!((uintptr_t)ip & 15))) { f_ = gsu_fetch_slow((uint16_t)pc); budget -= (int)(f_ >> 8); f_ &= 255; ip = g.ipn; } \
        else f_ = *ip++;                                                                                \
        pc++; f_; })
#define FETCH16() ({ const unsigned l_ = FETCH8(); const unsigned h_ = FETCH8(); l_ | (h_ << 8); })
#define RDW(a) ({ const unsigned i_ = (a) & g.rm; (unsigned)(g.rbase[i_] | (g.rbase[i_ ^ 1] << 8)); })
#define RDB(a) ((unsigned)g.rbase[(a) & g.rm])
#define WRW(a, val) do { const unsigned i_ = (a) & g.rm; g.rbase[i_] = (uint8_t)(val); g.rbase[i_ ^ 1] = (uint8_t)((val) >> 8); g.dirty = 1; } while (0)
#define WRB(a, val) do { g.rbase[(a) & g.rm] = (uint8_t)(val); g.dirty = 1; } while (0)

void gsu_run(int clocks)
{
    if (!g.go) { g.budget = 0; return; }
    ram_base();
    int budget = g.budget + clocks;
    if (budget <= 0) { g.budget = budget; return; }
#if GSU_ASM
    gsu_run_asm(&g, budget);
    if (g.dirty) { g.dirty = 0; S.sram_dirty = 1; }
}
#else
    const int mul = g.mul;
    unsigned alt = g.alt | (g.b << 2), sreg = g.sreg, d4 = g.dreg << 2;
    uint32_t sz = g.sz;
    unsigned fcy = g.fcy, fov = g.fov;
    uint32_t jt = g.jt;
    uint32_t *const R = g.r;
    uint32_t pc = R[15];
    uint32_t cnt = 0;
    const uint8_t *ip = 0;                  /* the code byte at pc when its low 4 bits are not 0 (the line is in the cache); else a fetch goes through fetch_slow */
    unsigned op, n, k, v;

    static const void *const T[256] = {
        [0x00] = &&L_stop, [0x01] = &&L_nop, [0x02] = &&L_cache, [0x03] = &&L_lsr, [0x04] = &&L_rol,
        [0x05] = &&L_bra, [0x06] = &&L_bge, [0x07] = &&L_blt, [0x08] = &&L_bne, [0x09] = &&L_beq, [0x0A] = &&L_bpl, [0x0B] = &&L_bmi,
        [0x0C] = &&L_bcc, [0x0D] = &&L_bcs, [0x0E] = &&L_bvc, [0x0F] = &&L_bvs,
        [0x10 ... 0x1F] = &&L_to, [0x20 ... 0x2F] = &&L_with, [0x30 ... 0x3B] = &&L_st, [0x3C] = &&L_loop,
        [0x3D] = &&L_alt1, [0x3E] = &&L_alt2, [0x3F] = &&L_alt3, [0x40 ... 0x4B] = &&L_ld, [0x4C] = &&L_plot, [0x4D] = &&L_swap,
        [0x4E] = &&L_color, [0x4F] = &&L_not, [0x50 ... 0x5F] = &&L_add, [0x60 ... 0x6F] = &&L_sub, [0x70] = &&L_merge,
        [0x71 ... 0x7F] = &&L_and, [0x80 ... 0x8F] = &&L_mult, [0x90] = &&L_sbk, [0x91 ... 0x94] = &&L_link, [0x95] = &&L_sex,
        [0x96] = &&L_asr, [0x97] = &&L_ror, [0x98 ... 0x9D] = &&L_jmp, [0x9E] = &&L_lob, [0x9F] = &&L_fmult,
        [0xA0 ... 0xAF] = &&L_ibt, [0xB0 ... 0xBF] = &&L_from, [0xC0] = &&L_hib, [0xC1 ... 0xCF] = &&L_or,
        [0xD0 ... 0xDE] = &&L_inc, [0xDF] = &&L_getc, [0xE0 ... 0xEE] = &&L_dec, [0xEF] = &&L_getb, [0xF0 ... 0xFF] = &&L_iwt
    };

    if (jt) goto slot;
    for (;;) {
        op = FETCH8();
        cnt++;
    dispatch:
        *(u16a *)&R[15] = (uint16_t)pc;
        n = op & 15;
        goto *T[op];

    L_stop:                                                     /* STOP */
        pix_flush();
        g.go = 0; g.irq = 1;
        if (!(g.cfgr & 0x80)) { g.irq_line = 1; S.irq_pending = 1; }
        pc++;
        alt = sreg = d4 = 0;
        budget = 0;
        goto out;
    L_nop: goto clr;
    L_cache: {                                                  /* CACHE */
        const uint16_t nb = (uint16_t)((pc - 1) & 0xFFF0);
        if (g.cbr != nb) { g.cbr = nb; g.cvalid = 0; ip = 0; }
        goto clr;
    }
    L_lsr: { const unsigned s = RS; fcy = s & 1; v = s >> 1; SZ(v); goto wr_d; }
    L_rol: { const unsigned s = RS; v = (s << 1) | fcy; fcy = s >> 15; SZ(v); goto wr_d; }
    L_bra: k = 1; goto branch;
    L_bge: k = SFLAG == fov; goto branch;
    L_blt: k = SFLAG != fov; goto branch;
    L_bne: k = !ZFLAG; goto branch;
    L_beq: k = ZFLAG; goto branch;
    L_bpl: k = !SFLAG; goto branch;
    L_bmi: k = SFLAG; goto branch;
    L_bcc: k = !fcy; goto branch;
    L_bcs: k = fcy; goto branch;
    L_bvc: k = !fov; goto branch;
    L_bvs: k = fov; goto branch;
    L_to:                                                       /* TO / MOVE */
        if (alt & 4) { v = RS; goto wr_n; }
        d4 = n << 2;
        goto fin;
    L_with: sreg = n; d4 = n << 2; alt |= 4; goto fin;
    L_st:                                                       /* STW / STB */
        g.ramaddr = (uint16_t)R[n];
        if (alt & 1) { WRB(R[n], RS); } else { WRW(R[n], RS); }
        budget -= mul; goto clr;
    L_loop:                                                     /* LOOP */
        v = (R[12] - 1) & 0xFFFF;
        R[12] = v;
        SZ(v);
        if (v) { jt = R[13] | JF; goto jclr; }
        goto clr;
    L_alt1: alt |= 1; goto fin;
    L_alt2: alt |= 2; goto fin;
    L_alt3: alt |= 3; goto fin;
    L_ld:                                                       /* LDW / LDB */
        g.ramaddr = (uint16_t)R[n];
        v = (alt & 1) ? RDB(R[n]) : RDW(R[n]);
        budget -= g.ramcyc;
        goto wr_d;
    L_plot:                                                     /* PLOT / RPIX */
        if (alt & 1) { v = do_rpix(); sz = v; budget -= g.ramcyc * 3; goto wr_d; }
        do_plot();
        budget -= mul; goto clr;
    L_swap: { const unsigned s = RS; v = ((s << 8) | (s >> 8)) & 0xFFFF; SZ(v); goto wr_d; }
    L_color:                                                    /* COLOR / CMODE */
        if (alt & 1) g.por = RS & 0x1F; else g.colr = color_of((uint8_t)RS);
        goto clr;
    L_not: v = ~RS & 0xFFFF; SZ(v); goto wr_d;
    L_add: {                                                    /* ADD, ADC, ADD #, ADC # */
        const uint32_t s = RS, m = (alt & 2) ? n : R[n];
        const uint32_t res = s + m + ((alt & 1) ? fcy : 0);
        fov = (~(s ^ m) & (s ^ res) & 0x8000) != 0;
        fcy = res > 0xFFFF; SZ(res);
        v = res; goto wr_d;
    }
    L_sub: {                                                    /* SUB, SBC, SUB #, CMP */
        const unsigned a = alt & 3;
        const uint32_t s = RS, m = (a == 2) ? n : R[n];
        const uint32_t res = s - m - (a == 1 ? (fcy ^ 1) : 0);
        fov = ((s ^ m) & (s ^ res) & 0x8000) != 0;
        fcy = (int32_t)res >= 0; SZ(res);
        if (a == 3) goto clr;
        v = res; goto wr_d;
    }
    L_merge:                                                    /* MERGE: S, OV, CY and Z come from the masks, Z = (v & $F0F0) != 0 ([F]) */
        v = (R[7] & 0xFF00) | (R[8] >> 8);
        sz = ((v & 0x8080) ? 0x10000u : 0u) | ((v & 0xF0F0) ? 0u : 1u);   /* the Z flag is set when the low 16 bits are 0: so the flag (v & $F0F0) != 0 means they are 0 */
        fov = (v & 0xC0C0) != 0; fcy = (v & 0xE0E0) != 0;
        goto wr_d;
    L_and: {                                                    /* AND, BIC, AND #, BIC # */
        const unsigned m = (alt & 2) ? n : R[n];
        v = (alt & 1) ? (RS & ~m) : (RS & m);
        SZ(v); goto wr_d;
    }
    L_mult: {                                                   /* MULT, UMULT, MULT #, UMULT # */
        const unsigned m = (alt & 2) ? n : R[n];
        v = (alt & 1) ? (unsigned)((uint8_t)RS * (uint8_t)m) : (unsigned)((int8_t)RS * (int8_t)m);
        SZ(v);
        if (!(g.cfgr & 0x20)) budget -= mul;
        goto wr_d;
    }
    L_sbk: { const unsigned a = g.ramaddr; WRW(a, RS); budget -= mul; goto clr; }   /* SBK */
    L_link: R[11] = (uint16_t)(pc + n); goto clr;               /* LINK */
    L_sex: v = (unsigned)(int)(int8_t)RS & 0xFFFF; SZ(v); goto wr_d;
    L_asr: {                                                    /* ASR / DIV2 */
        const unsigned s = RS;
        v = (unsigned)((int16_t)s >> 1) & 0xFFFF;
        if ((alt & 1) && s == 0xFFFF) v = 0;
        fcy = s & 1; SZ(v); goto wr_d;
    }
    L_ror: { const unsigned s = RS; v = (s >> 1) | (fcy << 15); fcy = s & 1; SZ(v); goto wr_d; }
    L_jmp:                                                      /* JMP / LJMP */
        if (alt & 1) jt = RS | JF | JL | ((R[n] & 0x7F) << 24);
        else jt = R[n] | JF;
        goto jclr;
    L_lob: v = RS & 0xFF; sz = v | ((v & 0x80) ? 0x10000u : 0u); goto wr_d;
    L_fmult: {                                                  /* FMULT / LMULT */
        const int32_t pr = (int32_t)(int16_t)RS * (int16_t)R[6];
        v = (uint32_t)pr >> 16;
        if (alt & 1) R[4] = (uint16_t)pr;
        fcy = ((uint32_t)pr >> 15) & 1; SZ(v);
        budget -= ((g.cfgr & 0x20) ? 3 : 7) * mul;
        goto wr_d;
    }
    L_ibt: {                                                    /* IBT / LMS / SMS */
        const unsigned a = alt & 3;
        k = FETCH8();
        if (a == 1) { g.ramaddr = (uint16_t)(k << 1); v = RDW(k << 1); budget -= g.ramcyc + 2 * mul; }
        else if (a == 2) { g.ramaddr = (uint16_t)(k << 1); WRW(k << 1, R[n]); budget -= 2 * mul; goto clr; }
        else v = (unsigned)(int)(int8_t)k & 0xFFFF;
        goto wr_n;
    }
    L_from:                                                     /* FROM / MOVES */
        if (alt & 4) {
            v = R[n];
            fov = (v >> 7) & 1; SZ(v); goto wr_d;
        }
        sreg = n; goto fin;
    L_hib: v = RS >> 8; sz = v | ((v & 0x80) ? 0x10000u : 0u); goto wr_d;
    L_or: {                                                     /* OR, XOR, OR #, XOR # */
        const unsigned m = (alt & 2) ? n : R[n];
        v = (alt & 1) ? (RS ^ m) : (RS | m);
        SZ(v); goto wr_d;
    }
    L_inc: v = (R[n] + 1) & 0xFFFF; SZ(v); goto wr_n;
    L_getc: {                                                   /* GETC / RAMB / ROMB */
        const unsigned a = alt & 3;
        if (a == 2) { g.rambr = RS & 1; ram_base(); }
        else if (a == 3) set_rombr(RS & 0x7F);
        else { const unsigned x = rom_buffer(); budget -= (int)(x >> 8); g.colr = color_of((uint8_t)x); }
        goto clr;
    }
    L_dec: v = (R[n] - 1) & 0xFFFF; SZ(v); goto wr_n;
    L_getb: {                                                   /* GETB / GETBH / GETBL / GETBS */
        const unsigned x = rom_buffer(), by = x & 255;
        budget -= (int)(x >> 8);
        switch (alt & 3) {
        case 0: v = by; break;
        case 1: v = (by << 8) | (RS & 0xFF); break;
        case 2: v = (RS & 0xFF00) | by; break;
        default: v = (unsigned)(int)(int8_t)by & 0xFFFF; break;
        }
        goto wr_d;
    }
    L_iwt: {                                                    /* IWT / LM / SM */
        const unsigned a = alt & 3;
        k = FETCH16();
        if (a == 1) { g.ramaddr = (uint16_t)k; v = RDW(k); budget -= g.ramcyc + 3 * mul; }
        else if (a == 2) { g.ramaddr = (uint16_t)k; WRW(k, R[n]); budget -= 3 * mul; goto clr; }
        else v = k;
        goto wr_n;
    }
    branch: {
        const int d = (int8_t)FETCH8();
        if (k) { jt = (uint16_t)(pc + d) | JF; goto jfin; }
        goto fin;
    }
    wr_n:
        if (n == 15) { jt = (uint16_t)v | JF; goto jclr; }
        WR16(n << 2, v);
        goto clr;
    wr_d:
        if (d4 == 60) { jt = (uint16_t)v | JF; goto jclr; }
        WR16(d4, v);
    clr:
        alt = sreg = d4 = 0;
    fin:
        if (LIKELY((budget -= mul) > 0)) continue;
        break;
    jclr:                                                       /* a jump is pending: the next opcode is the delay slot */
        alt = sreg = d4 = 0;
    jfin:
        if ((budget -= mul) <= 0) break;
    slot:
        op = FETCH8();
        cnt++;
        pc = (uint16_t)jt;
        if (UNLIKELY(jt >> 31)) {
            g.pbr = (uint8_t)((jt >> 24) & 0x7F); g.cbr = (uint16_t)(pc & 0xFFF0); g.cvalid = 0; ip = 0;
        } else {
            const unsigned off = (uint16_t)(pc - g.cbr);
            ip = (off < 512 && ((g.cvalid >> (off >> 4)) & 1)) ? &g.cache[off] : 0;
        }
        jt = 0;
        goto dispatch;
    }
out:
    *(u16a *)&R[15] = (uint16_t)pc;
    g.budget = budget;
    g.alt = (uint8_t)(alt & 3); g.b = (uint8_t)(alt >> 2); g.sreg = (uint8_t)sreg; g.dreg = (uint8_t)(d4 >> 2);
    g.sz = sz; g.fcy = (uint8_t)fcy; g.fov = (uint8_t)fov;
    g.jt = jt;
    g.insns += cnt;
    if (g.dirty) { g.dirty = 0; S.sram_dirty = 1; }
}
#endif

#if GSU_ASM
/* The calls of the assembly loop for the opcodes that are not worth assembly: they work on the state in g (R0-R15, the pixel cache, COLR, POR, the ROM buffer). */
void gsu_c_stop(void) { pix_flush(); g.go = 0; g.irq = 1; if (!(g.cfgr & 0x80)) { g.irq_line = 1; S.irq_pending = 1; } }
void gsu_c_plot(void) { do_plot(); }
unsigned gsu_c_rpix(void) { return do_rpix(); }
void gsu_c_color(unsigned src, unsigned alt) { if (alt & 1) g.por = src & 0x1F; else g.colr = color_of((uint8_t)src); }
void gsu_c_romb(unsigned src) { set_rombr(src & 0x7F); }
void gsu_c_ramb(unsigned src) { g.rambr = src & 1; ram_base(); }
unsigned gsu_c_getc(void) { const unsigned x = rom_buffer(); g.colr = color_of((uint8_t)x); return x >> 8; }
unsigned gsu_c_getb(unsigned alt, unsigned rs)         /* the value in bits 0-15, the extra clocks above */
{
    const unsigned x = rom_buffer(), by = x & 255;
    unsigned v;
    switch (alt & 3) {
    case 0: v = by; break;
    case 1: v = (by << 8) | (rs & 0xFF); break;
    case 2: v = (rs & 0xFF00) | by; break;
    default: v = (unsigned)(int)(int8_t)by & 0xFFFF; break;
    }
    return v | ((x >> 8) << 16);
}
#endif

/* ------------------------------------------------------------------ the SNES side */
int gsu_go(void) { return g.go; }
int gsu_owns_rom(void) { return g.go && (g.scmr & 0x10); }
int gsu_irq_line(void) { return g.irq_line; }
uint32_t gsu_instr_count(void) { return g.insns; }
uint32_t gsu_ram_mask(void) { return g.rmask; }
uint16_t gsu_reg(int n) { return (uint16_t)g.r[n & 15]; }

void gsu_dbg(gsu_dbg_t *d)
{
    for (int i = 0; i < 16; i++) d->r[i] = (uint16_t)g.r[i];
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
    g.jt = 0;
    g.rbuf_ok = 0;
    g.budget = 0;
}

uint8_t gsu_read(uint16_t off)
{
    if (off >= 0x3100) return g.cache[(off - 0x3100) & 0x1FF];
    off = (uint16_t)(0x3000 | (off & 0x3F));
    if (off < 0x3020) { const uint16_t v = (uint16_t)g.r[(off >> 1) & 15]; return (off & 1) ? (uint8_t)(v >> 8) : (uint8_t)v; }
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
