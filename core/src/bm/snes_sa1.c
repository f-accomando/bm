/*
 * snes_sa1.c: the SA-1 co-processor: a second 65C816 (snes_sa1_cpu.c) at 10.74 MHz with 2 KB of I-RAM, the BW-RAM (S.sram), a ROM mapper with four 1 MB
 * windows, a DMA engine with two kinds of character conversion, a 16 x 16 math unit (multiply, divide, accumulate), a variable-length bit processor, a timer
 * and the interrupt messages between the two CPUs. Behaviour from the public descriptions of the chip (SNESdev wiki "SA-1 registers") and checked against the
 * SA-1 module of ares for the cases below (docs/progress/snes-sa1.md). Original code.
 *
 * The time: the S-CPU runs the frame; the SA-1 runs lazily (like the Super FX): sa1_run(d) runs it for d master clocks, sa1_sync brings it to the position of
 * the S-CPU before the S-CPU reads or writes anything the two share (registers, I-RAM, BW-RAM). The SA-1 clock is kept in units of 1/3 master clock
 * (a bus cycle of the SA-1 is 2 master clocks = 6 units), so the same CPU core (snes_cpu.c) runs it unchanged. Bus conflicts and wait states are not
 * emulated: an access costs one SA-1 cycle (BW-RAM two).
 */
#include "snes_int.h"
#include "snes_sa1.h"

#include <string.h>

#define LINE_CLK 1364

/* the SA-1 CPU (snes_sa1_cpu.c) and its tables */
int sa1c_run(int limit);
void sa1c_set(const cpu_regs_t *r);
void sa1c_get(cpu_regs_t *r);
extern int sa1c_waiting, sa1c_stopped;
extern struct { int irq_pending, nmi_pending, line_clk; } sa1_s;
extern const uint8_t *sa1_rtab[4096];
extern uint8_t *sa1_wtab[4096];
extern uint8_t sa1_ctab[4096];
extern int sa1_clk;

static struct {
    /* $2200 CCNT, $2209 SCNT: the messages and the interrupt requests between the CPUs */
    int sa1_irq, sa1_rdyb, sa1_resb, sa1_nmi, smeg;     /* S-CPU -> SA-1: IRQ, hold, reset, NMI, message (4 bits) */
    int cpu_irq, cpu_ivsw, cpu_nvsw, cmeg;              /* SA-1 -> S-CPU: IRQ, vector switches, message */
    /* interrupt enables and flags */
    int cpu_irqen, chdma_irqen, cpu_irqfl, chdma_irqfl;                 /* S-CPU side ($2201, $2300) */
    int sa1_irqen, timer_irqen, dma_irqen, sa1_nmien;                   /* SA-1 side ($220A) */
    int sa1_irqfl, timer_irqfl, dma_irqfl, sa1_nmifl;                   /* $2301 */
    int nmi_latch;                                                       /* an NMI is waiting for the SA-1 CPU (an edge) */
    uint16_t crv, cnv, civ, snv, siv;
    /* timer */
    int hen, ven, hvselb;
    uint16_t hcnt, vcnt, hcr, vcr;
    int hcounter, vcounter, scanlines;                                  /* the counters of the SA-1 timer: clocks and lines */
    /* mapping */
    uint8_t cb, db, eb, fb;
    int cbmode, dbmode, ebmode, fbmode;
    uint8_t sbm, cbm;
    int sw46, swen, cwen, bwp;
    uint8_t siwp, ciwp;
    /* DMA */
    int sd, dd, cdsel, cden, dprio, dmaen;
    int dmacb, dmasize, chdend;
    uint32_t dsa, dda;
    uint16_t dtc;
    int bbf;
    uint8_t brf[16];
    int dma_line, bwram_dma;
    /* math and bit processor */
    int md, acm;
    uint16_t ma, mb;
    uint64_t mr;
    int overflow;
    int vb, hl;
    uint32_t va;
    int vbit;
} R;

static uint8_t iram[2048];
static int32_t sa1_done;            /* master clocks of this frame the SA-1 has been run to */
static int sa1_lim;                 /* the clock limit of the SA-1 CPU, in units */
static int cpu_line_old;
static uint32_t n_instr;
static int started;

/* ------------------------------------------------------------------ the S-CPU interrupt line */
static int cpu_line(void) { return (R.cpu_irqfl && R.cpu_irqen) || (R.chdma_irqfl && R.chdma_irqen); }
static void cpu_line_update(void)
{
    const int n = cpu_line();
    if (n && !cpu_line_old) S.irq_pending = 1;
    else if (!n && cpu_line_old) S.irq_pending = S.timeup;
    cpu_line_old = n;
}
int sa1_irq_line(void) { return cpu_line(); }
static void sa1_irq_update(void)
{
    sa1_s.irq_pending = (R.timer_irqfl && R.timer_irqen) || (R.dma_irqfl && R.dma_irqen) || (R.sa1_irqfl && R.sa1_irqen);
    if (R.nmi_latch) { sa1_s.nmi_pending = 1; R.nmi_latch = 0; }
}

/* ------------------------------------------------------------------ the memory */
static inline uint32_t bw_mask(void) { return S.sram_size ? S.sram_size - 1 : 0; }
static inline uint8_t bw_rd(uint32_t a) { return S.sram_size ? S.sram[a & bw_mask()] : 0; }
static inline void bw_wr(uint32_t a, uint8_t v) { if (S.sram_size) { S.sram[a & bw_mask()] = v; S.sram_dirty = 1; } }
static inline int bw_protected(uint32_t a) { return !R.swen && !R.cwen && (a & 0x3FFFF) < (0x100u << R.bwp); }

/* The ROM: banks $00-$3F and $80-$BF at $8000-$FFFF are four windows of 1 MB (LoROM style, 32 KB a bank); $C0-$FF hold the four chunks selected by
 * CXB, DXB, EXB and FXB. A window follows its register only when the mode bit is set; else it is the plain LoROM mapping. */
static int rom_lin(uint32_t a, uint32_t *lin)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (bank >= 0xC0) { *lin = a & 0x3FFFFF; return 0; }
    *lin = ((uint32_t)(bank & 0x80) << 14) | ((uint32_t)(bank & 0x3F) << 15) | (off & 0x7FFF);
    return 1;
}
static uint32_t rom_off(uint32_t lin, int lo)
{
    if (lin < 0x100000) return (lo && !R.cbmode) ? lin : ((uint32_t)R.cb << 20) | (lin & 0xFFFFF);
    if (lin < 0x200000) return (lo && !R.dbmode) ? lin : ((uint32_t)R.db << 20) | (lin & 0xFFFFF);
    if (lin < 0x300000) return (lo && !R.ebmode) ? lin : ((uint32_t)R.eb << 20) | (lin & 0xFFFFF);
    return (lo && !R.fbmode) ? lin : ((uint32_t)R.fb << 20) | (lin & 0xFFFFF);
}
static const uint8_t *rom_ptr(uint32_t a)
{
    uint32_t lin;
    const int lo = rom_lin(a, &lin);
    uint32_t o = rom_off(lin, lo);
    if (o >= S.rom_size) o %= S.rom_size;
    return &S.rom[o];
}
static inline uint8_t rom_rd(uint32_t a) { return *rom_ptr(a); }

/* 2 bits and 4 bits a pixel through the BW-RAM bitmap window of the SA-1 ($60-$6F) */
static uint8_t bitmap_rd(uint32_t a)
{
    if (!R.bbf) { const uint8_t b = bw_rd(a >> 1); return (a & 1) ? b >> 4 : b & 15; }
    return (uint8_t)((bw_rd(a >> 2) >> ((a & 3) * 2)) & 3);
}
static void bitmap_wr(uint32_t a, uint8_t v)
{
    if (!R.bbf) { const uint32_t i = a >> 1; const uint8_t b = bw_rd(i); bw_wr(i, (a & 1) ? (uint8_t)((b & 0x0F) | (v << 4)) : (uint8_t)((b & 0xF0) | (v & 15))); return; }
    const uint32_t i = a >> 2; const unsigned sh = (a & 3) * 2;
    bw_wr(i, (uint8_t)((bw_rd(i) & ~(3u << sh)) | ((v & 3u) << sh)));
}

/* ------------------------------------------------------------------ DMA and character conversion */
static void dma_normal(void)
{
    while (R.dtc) {
        uint8_t v = 0;
        const uint32_t s = R.dsa++ & 0xFFFFFF, d = R.dda++ & 0xFFFFFF;
        R.dtc--;
        if (R.sd == 0) v = rom_rd(s);                       /* source: 0 ROM, 1 BW-RAM, 2 I-RAM */
        else if (R.sd == 1) v = bw_rd(s);
        else v = iram[s & 0x7FF];
        if (R.dd == 0) iram[d & 0x7FF] = v; else bw_wr(d, v);
    }
    R.dma_irqfl = 1;
}
/* type 1: the S-CPU reads the BW-RAM and gets tiles that are converted into the I-RAM first */
static uint8_t dma_cc1_read(uint32_t a)
{
    const uint32_t charmask = (1u << (6 - R.dmacb)) - 1;
    if ((a & charmask) == 0) {
        const unsigned bpp = 2u << (2 - R.dmacb), bpl = (8u << R.dmasize) >> R.dmacb;
        const uint32_t bwmask = bw_mask();
        const uint32_t tile = ((a - R.dsa) & bwmask) >> (6 - R.dmacb);
        const uint32_t ty = tile >> R.dmasize, tx = tile & ((1u << R.dmasize) - 1);
        uint32_t bwaddr = R.dsa + ty * 8 * bpl + tx * bpp;
        for (unsigned y = 0; y < 8; y++) {
            uint64_t data = 0;
            for (unsigned b = 0; b < bpp; b++) data |= (uint64_t)bw_rd((bwaddr + b) & bwmask) << (b << 3);
            bwaddr += bpl;
            uint8_t out[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
            for (unsigned x = 0; x < 8; x++) {
                out[0] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                out[1] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                if (R.dmacb == 2) continue;
                out[2] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                out[3] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                if (R.dmacb == 1) continue;
                out[4] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                out[5] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                out[6] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
                out[7] |= (uint8_t)((data & 1) << (7 - x)); data >>= 1;
            }
            for (unsigned b = 0; b < bpp; b++) iram[(R.dda + (y << 1) + ((b & 6) << 3) + (b & 1)) & 0x7FF] = out[b];
        }
    }
    return iram[(R.dda + (a & charmask)) & 0x7FF];
}
static void dma_cc1_start(void)
{
    R.bwram_dma = 1;
    R.chdma_irqfl = 1;
    cpu_line_update();
}
/* type 2: the SA-1 fills the bitmap register file; a full row of 8 pixels is converted into the I-RAM */
static void dma_cc2(void)
{
    const uint8_t *brf = &R.brf[(R.dma_line & 1) << 3];
    const unsigned bpp = 2u << (2 - R.dmacb);
    uint32_t address = R.dda & 0x7FF;
    address &= ~((1u << (7 - R.dmacb)) - 1);
    address += (unsigned)(R.dma_line & 8) * bpp;
    address += (unsigned)(R.dma_line & 7) * 2;
    for (unsigned b = 0; b < bpp; b++) {
        uint8_t out = 0;
        for (unsigned bit = 0; bit < 8; bit++) out |= (uint8_t)(((brf[bit] >> b) & 1) << (7 - bit));
        iram[(address + ((b & 6) << 3) + (b & 1)) & 0x7FF] = out;
    }
    R.dma_line = (R.dma_line + 1) & 15;
}

/* ------------------------------------------------------------------ the timer of the SA-1 */
static void trigger_timer(void) { R.timer_irqfl = 1; }
static void timer_advance(int d)                    /* d master clocks */
{
    if (R.hvselb == 0 && !R.hen && !R.ven) {
        const int t = R.hcounter + d;
        R.vcounter = (R.vcounter + t / LINE_CLK) % R.scanlines;
        R.hcounter = t % LINE_CLK;
        return;
    }
    if (R.hvselb && !R.hen && !R.ven) {
        R.hcounter += d;
        R.vcounter = (R.vcounter + (R.hcounter >> 11)) & 0x1FF;
        R.hcounter &= 0x7FF;
        return;
    }
    for (int i = 0; i < d; i += 2) {                /* a timer that compares: one step of 2 clocks at a time */
        R.hcounter += 2;
        if (R.hvselb == 0) {
            if (R.hcounter >= LINE_CLK) { R.hcounter = 0; if (++R.vcounter >= R.scanlines) R.vcounter = 0; }
        } else {
            R.vcounter += R.hcounter >> 11; R.hcounter &= 0x7FF; R.vcounter &= 0x1FF;
        }
        switch ((R.hen ? 1 : 0) | (R.ven ? 2 : 0)) {
        case 1: if (R.hcounter == (R.hcnt << 2)) trigger_timer(); break;
        case 2: if (R.vcounter == R.vcnt && R.hcounter == 0) trigger_timer(); break;
        case 3: if (R.vcounter == R.vcnt && R.hcounter == (R.hcnt << 2)) trigger_timer(); break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ the SA-1 CPU: reset and run */
static void sa1_cpu_reset_to(uint16_t pc)
{
    cpu_regs_t r;
    memset(&r, 0, sizeof r);
    r.s = 0x1FF; r.p = 0x34; r.e = 1; r.pc = pc;
    sa1c_set(&r);
    sa1c_stopped = 0;
    sa1_s.irq_pending = sa1_s.nmi_pending = 0;
    R.nmi_latch = 0;
}
int sa1_running(void) { return !R.sa1_resb && !R.sa1_rdyb && !sa1c_stopped; }
uint32_t sa1_instr_count(void) { return n_instr; }

static void sa1_advance(int d)
{
    if (d <= 0) return;
    timer_advance(d);
    sa1_irq_update();
    sa1_lim += 3 * d;
    if (!R.sa1_resb && !R.sa1_rdyb && !sa1c_stopped) {
        if (sa1_clk < sa1_lim - 3 * 4 * LINE_CLK) sa1_clk = sa1_lim - 3 * 4 * LINE_CLK;      /* a CPU that was held does not run for the time it was held */
        n_instr += (uint32_t)sa1c_run(sa1_lim);
    } else sa1_clk = sa1_lim;
}
static void sa1_sync_pos(int32_t pos)
{
    const int32_t d = pos - sa1_done;
    if (d <= 0) return;
    sa1_done = pos;
    sa1_advance(d > 4 * LINE_CLK ? 4 * LINE_CLK : d);
}
void sa1_sync(int force)
{
    (void)force;
    if (!started) return;
    sa1_sync_pos((int32_t)(S.line * LINE_CLK + (snes_clk > 0 ? snes_clk : 0)));
}
void sa1_line_end(int line) { if (started) sa1_sync_pos((int32_t)((line + 1) * LINE_CLK + (snes_clk > 0 ? snes_clk : 0))); }
void sa1_run(int master_clocks) { sa1_advance(master_clocks); }
void sa1_frame_end(void)
{
    sa1_done -= 262 * LINE_CLK;
    sa1_lim -= 3 * 262 * LINE_CLK;
    sa1_clk -= 3 * 262 * LINE_CLK;
}

/* ------------------------------------------------------------------ the page tables of the SA-1 CPU */
static void sa1_map_build(void)
{
    for (unsigned pg = 0; pg < 4096; pg++) {
        const uint32_t base = (uint32_t)pg << 12;
        const unsigned bank = base >> 16, off = base & 0xFFFF;
        sa1_rtab[pg] = NULL; sa1_wtab[pg] = NULL; sa1_ctab[pg] = 0;
        if (!S.rom) continue;
        if (bank >= 0xC0 || ((bank & 0x40) == 0 && off >= 0x8000)) {
            if (bank < 0xC0 && off >= 0xF000) continue;       /* the vectors of the SA-1 come from registers: the slow path */
            sa1_rtab[pg] = rom_ptr(base);
            sa1_ctab[pg] = 6;
        }
    }
}

/* ------------------------------------------------------------------ the bus of the SA-1 CPU */
static uint8_t sa1_io_read(unsigned reg)
{
    switch (reg) {
    case 0x2301: return (uint8_t)(R.smeg | (R.sa1_nmifl << 4) | (R.dma_irqfl << 5) | (R.timer_irqfl << 6) | (R.sa1_irqfl << 7));
    case 0x2302: R.hcr = (uint16_t)(R.hcounter >> 2); R.vcr = (uint16_t)R.vcounter; return (uint8_t)R.hcr;
    case 0x2303: return (uint8_t)(R.hcr >> 8);
    case 0x2304: return (uint8_t)R.vcr;
    case 0x2305: return (uint8_t)(R.vcr >> 8);
    case 0x2306: return (uint8_t)R.mr;
    case 0x2307: return (uint8_t)(R.mr >> 8);
    case 0x2308: return (uint8_t)(R.mr >> 16);
    case 0x2309: return (uint8_t)(R.mr >> 24);
    case 0x230A: return (uint8_t)(R.mr >> 32);
    case 0x230B: return (uint8_t)(R.overflow << 7);
    case 0x230C: case 0x230D: {
        uint8_t b[3];
        for (unsigned i = 0; i < 3; i++) {                  /* the bit processor reads the cartridge ROM, the BW-RAM or the I-RAM at VDA */
            const uint32_t a = R.va + i;
            if ((a & 0x408000) == 0x008000 || (a & 0xC00000) == 0xC00000) b[i] = rom_rd(a);
            else if ((a & 0x40E000) == 0x006000 || (a & 0xF00000) == 0x400000) b[i] = bw_rd((a & 0x40E000) == 0x006000 ? (uint32_t)(R.cbm & 0x1F) * 0x2000 + (a & 0x1FFF) : a);
            else if ((a & 0x40F800) == 0x000000 || (a & 0x40F800) == 0x003000) b[i] = iram[a & 0x7FF];
            else b[i] = 0xFF;
        }
        const uint32_t data = ((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)) >> R.vbit;
        if (reg == 0x230D && R.hl == 1) { R.vbit += R.vb; R.va += (uint32_t)(R.vbit >> 3); R.vbit &= 7; }
        return (uint8_t)(reg == 0x230C ? data : data >> 8);
    }
    default: return 0xFF;
    }
}

static void dma_shared_write(unsigned reg, uint8_t v)
{
    switch (reg) {
    case 0x2231:
        R.dmacb = v & 3; R.dmasize = (v >> 2) & 7; R.chdend = v >> 7;
        if (R.dmacb > 2) R.dmacb = 2;
        if (R.dmasize > 5) R.dmasize = 5;
        if (R.chdend) R.bwram_dma = 0;
        return;
    case 0x2232: R.dsa = (R.dsa & 0xFFFF00) | v; return;
    case 0x2233: R.dsa = (R.dsa & 0xFF00FF) | ((uint32_t)v << 8); return;
    case 0x2234: R.dsa = (R.dsa & 0x00FFFF) | ((uint32_t)v << 16); return;
    case 0x2235: R.dda = (R.dda & 0xFFFF00) | v; return;
    case 0x2236:
        R.dda = (R.dda & 0xFF00FF) | ((uint32_t)v << 8);
        if (R.dmaen) {
            if (!R.cden && R.dd == 0) dma_normal();
            else if (R.cden && R.cdsel == 1) dma_cc1_start();
        }
        return;
    case 0x2237:
        R.dda = (R.dda & 0x00FFFF) | ((uint32_t)v << 16);
        if (R.dmaen && !R.cden && R.dd == 1) dma_normal();
        return;
    default: return;
    }
}

static void sa1_io_write(unsigned reg, uint8_t v)
{
    switch (reg) {
    case 0x2209:
        R.cmeg = v & 15; R.cpu_nvsw = (v >> 4) & 1; R.cpu_ivsw = (v >> 6) & 1; R.cpu_irq = v >> 7;
        if (R.cpu_irq) { R.cpu_irqfl = 1; cpu_line_update(); }
        return;
    case 0x220A:
        if (!R.sa1_nmien && (v & 0x10) && R.sa1_nmifl) R.nmi_latch = 1;
        R.sa1_nmien = (v >> 4) & 1; R.dma_irqen = (v >> 5) & 1; R.timer_irqen = (v >> 6) & 1; R.sa1_irqen = v >> 7;
        sa1_irq_update();
        return;
    case 0x220B:
        if (v & 0x10) R.sa1_nmifl = 0;
        if (v & 0x20) R.dma_irqfl = 0;
        if (v & 0x40) R.timer_irqfl = 0;
        if (v & 0x80) R.sa1_irqfl = 0;
        sa1_irq_update();
        return;
    case 0x220C: R.snv = (R.snv & 0xFF00) | v; return;
    case 0x220D: R.snv = (uint16_t)((R.snv & 0xFF) | (v << 8)); return;
    case 0x220E: R.siv = (R.siv & 0xFF00) | v; return;
    case 0x220F: R.siv = (uint16_t)((R.siv & 0xFF) | (v << 8)); return;
    case 0x2210: R.hen = v & 1; R.ven = (v >> 1) & 1; R.hvselb = v >> 7; return;
    case 0x2211: R.vcounter = 0; R.hcounter = 0; return;
    case 0x2212: R.hcnt = (R.hcnt & 0xFF00) | v; return;
    case 0x2213: R.hcnt = (uint16_t)((R.hcnt & 0xFF) | (v << 8)); return;
    case 0x2214: R.vcnt = (R.vcnt & 0xFF00) | v; return;
    case 0x2215: R.vcnt = (uint16_t)((R.vcnt & 0xFF) | (v << 8)); return;
    case 0x2225: R.cbm = v & 0x7F; R.sw46 = v >> 7; return;
    case 0x2227: R.cwen = v >> 7; return;
    case 0x222A: R.ciwp = v; return;
    case 0x2230:
        R.sd = v & 3; R.dd = (v >> 2) & 1; R.cdsel = (v >> 4) & 1; R.cden = (v >> 5) & 1; R.dprio = (v >> 6) & 1; R.dmaen = v >> 7;
        if (!R.dmaen) R.dma_line = 0;
        return;
    case 0x2231: case 0x2232: case 0x2233: case 0x2234: case 0x2235: case 0x2236: case 0x2237: dma_shared_write(reg, v); return;
    case 0x2238: R.dtc = (R.dtc & 0xFF00) | v; return;
    case 0x2239: R.dtc = (uint16_t)((R.dtc & 0xFF) | (v << 8)); return;
    case 0x223F: R.bbf = v >> 7; return;
    case 0x2240: case 0x2241: case 0x2242: case 0x2243: case 0x2244: case 0x2245: case 0x2246: case 0x2247:
    case 0x2248: case 0x2249: case 0x224A: case 0x224B: case 0x224C: case 0x224D: case 0x224E: case 0x224F:
        R.brf[reg - 0x2240] = v;
        if ((reg == 0x2247 || reg == 0x224F) && R.dmaen && R.cden && !R.cdsel) dma_cc2();
        return;
    case 0x2250: R.md = v & 1; R.acm = (v >> 1) & 1; if (R.acm) R.mr = 0; return;
    case 0x2251: R.ma = (R.ma & 0xFF00) | v; return;
    case 0x2252: R.ma = (uint16_t)((R.ma & 0xFF) | (v << 8)); return;
    case 0x2253: R.mb = (R.mb & 0xFF00) | v; return;
    case 0x2254:
        R.mb = (uint16_t)((R.mb & 0xFF) | (v << 8));
        if (!R.acm) {
            if (!R.md) {                                    /* signed multiply */
                R.mr = (uint32_t)((int32_t)(int16_t)R.ma * (int32_t)(int16_t)R.mb);
                R.mb = 0;
            } else {                                        /* signed dividend by unsigned divisor: quotient and a remainder that is not negative */
                if (R.mb == 0) R.mr = 0;
                else {
                    const int32_t dividend = (int16_t)R.ma;
                    const int32_t divisor = R.mb;
                    int32_t rem = dividend % divisor;
                    if (rem < 0) rem += divisor;
                    const uint32_t q = (uint32_t)((dividend - rem) / divisor) & 0xFFFF;
                    R.mr = ((uint32_t)rem << 16) | q;
                }
                R.ma = 0; R.mb = 0;
            }
        } else {                                            /* accumulate */
            R.mr += (uint64_t)(int64_t)((int32_t)(int16_t)R.ma * (int32_t)(int16_t)R.mb);
            R.overflow = (R.mr >> 40) != 0;                  /* any bit above 40: a negative sum counts too */
            R.mr &= 0xFFFFFFFFFFull;
            R.mb = 0;
        }
        return;
    case 0x2258:
        R.vb = v & 15; R.hl = v >> 7;
        if (R.vb == 0) R.vb = 16;
        if (!R.hl) { R.vbit += R.vb; R.va += (uint32_t)(R.vbit >> 3); R.vbit &= 7; }
        return;
    case 0x2259: R.va = (R.va & 0xFFFF00) | v; return;
    case 0x225A: R.va = (R.va & 0xFF00FF) | ((uint32_t)v << 8); return;
    case 0x225B: R.va = (R.va & 0x00FFFF) | ((uint32_t)v << 16); R.vbit = 0; return;
    default: return;
    }
}

uint8_t sa1_bus_read(uint32_t a)
{
    a &= 0xFFFFFF;
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if ((a & 0x40FE00) == 0x2200) { sa1_clk += 6; return sa1_io_read(a & 0x23FF); }
    if ((a & 0x408000) == 0x8000 || (a & 0xC00000) == 0xC00000) {
        sa1_clk += 6;
        if (bank < 0xC0 && off >= 0xFFEA) {                 /* the vectors of the SA-1 CPU: NMI and IRQ from registers */
            if (off == 0xFFEA || off == 0xFFFA) return (uint8_t)R.cnv;
            if (off == 0xFFEB || off == 0xFFFB) return (uint8_t)(R.cnv >> 8);
            if (off == 0xFFEE || off == 0xFFFE) return (uint8_t)R.civ;
            if (off == 0xFFEF || off == 0xFFFF) return (uint8_t)(R.civ >> 8);
            if (off == 0xFFFC) return (uint8_t)R.crv;
            if (off == 0xFFFD) return (uint8_t)(R.crv >> 8);
        }
        return rom_rd(a);
    }
    if ((a & 0x40E000) == 0x6000 || (a & 0xE00000) == 0x400000 || (a & 0xF00000) == 0x600000) {
        sa1_clk += 12;
        if ((a & 0x600000) == 0x600000) return bitmap_rd(a & 0xFFFFF);
        if (a & 0x400000) return bw_rd(a);
        if (!R.sw46) return bw_rd((uint32_t)(R.cbm & 0x1F) * 0x2000 + (a & 0x1FFF));
        return bitmap_rd((uint32_t)R.cbm * 0x2000 + (a & 0x1FFF));
    }
    if ((a & 0x40F800) == 0x000000 || (a & 0x40F800) == 0x003000) { sa1_clk += 6; return iram[a & 0x7FF]; }
    sa1_clk += 6;
    return 0;
}

void sa1_bus_write(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    if ((a & 0x40FE00) == 0x2200) { sa1_clk += 6; sa1_io_write(a & 0x23FF, v); return; }
    if ((a & 0x408000) == 0x8000 || (a & 0xC00000) == 0xC00000) { sa1_clk += 6; return; }        /* the ROM */
    if ((a & 0x40E000) == 0x6000 || (a & 0xE00000) == 0x400000 || (a & 0xF00000) == 0x600000) {
        sa1_clk += 12;
        if ((a & 0x600000) == 0x600000) { bitmap_wr(a & 0xFFFFF, v); return; }
        uint32_t ad;
        if (a & 0x400000) ad = a;
        else if (!R.sw46) ad = (uint32_t)(R.cbm & 0x1F) * 0x2000 + (a & 0x1FFF);
        else { bitmap_wr((uint32_t)R.cbm * 0x2000 + (a & 0x1FFF), v); return; }
        if (bw_protected(ad & bw_mask())) return;
        bw_wr(ad, v);
        return;
    }
    if ((a & 0x40F800) == 0x000000 || (a & 0x40F800) == 0x003000) {
        sa1_clk += 6;
        if (R.ciwp & (1u << ((a >> 8) & 7))) iram[a & 0x7FF] = v;
        return;
    }
    sa1_clk += 6;
}

/* ------------------------------------------------------------------ the S-CPU side */
static int cpu_owns(uint32_t a)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (bank == 0x7E || bank == 0x7F) return 0;
    if ((bank & 0x7F) < 0x40) return (off >= 0x2200 && off < 0x2400) || (off >= 0x3000 && off < 0x3800) || off >= 0x6000;
    return 1;                                               /* $40-$7D and $C0-$FF */
}

int sa1_cpu_map(uint32_t a, const uint8_t **p)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (!cpu_owns(a)) return 0;
    *p = NULL;
    if (bank >= 0xC0 || ((bank & 0x7F) < 0x40 && off >= 0x8000)) {
        if (bank == 0 && off >= 0xF000) return 1;           /* the vector page: the override is on the slow path */
        *p = rom_ptr(a);
    }
    return 1;
}

int sa1_cpu_read(uint32_t a, uint8_t *v)
{
    a &= 0xFFFFFF;
    if (!cpu_owns(a)) return 0;
    const unsigned bank = a >> 16, off = a & 0xFFFF, b7 = bank & 0x7F;
    if (b7 < 0x40 && off >= 0x2200 && off < 0x2400) {
        sa1_sync(0);
        if (off == 0x2300) *v = (uint8_t)(R.cmeg | (R.cpu_nvsw << 4) | (R.chdma_irqfl << 5) | (R.cpu_ivsw << 6) | (R.cpu_irqfl << 7));
        else *v = 0;
        return 1;
    }
    if (b7 < 0x40 && off >= 0x3000 && off < 0x3800) { sa1_sync(0); *v = iram[off & 0x7FF]; return 1; }
    if ((b7 < 0x40 && off >= 0x6000 && off < 0x8000) || (bank >= 0x40 && bank < 0x50)) {
        sa1_sync(0);
        const uint32_t ad = bank >= 0x40 ? (((uint32_t)(bank & 0xF) << 16) | off) : (uint32_t)R.sbm * 0x2000 + (off & 0x1FFF);
        *v = R.bwram_dma ? dma_cc1_read(ad) : bw_rd(ad);
        return 1;
    }
    if (bank >= 0xC0 || (b7 < 0x40 && off >= 0x8000)) {
        if (bank == 0 && off >= 0xFFEA && off <= 0xFFEF) {      /* the vectors: the SA-1 can replace the NMI and IRQ vectors of the S-CPU */
            if (off == 0xFFEA && R.cpu_nvsw) { *v = (uint8_t)R.snv; return 1; }
            if (off == 0xFFEB && R.cpu_nvsw) { *v = (uint8_t)(R.snv >> 8); return 1; }
            if (off == 0xFFEE && R.cpu_ivsw) { *v = (uint8_t)R.siv; return 1; }
            if (off == 0xFFEF && R.cpu_ivsw) { *v = (uint8_t)(R.siv >> 8); return 1; }
        }
        *v = rom_rd(a);
        return 1;
    }
    *v = 0;
    return 1;
}

int sa1_cpu_write(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    const unsigned bank = a >> 16, off = a & 0xFFFF, b7 = bank & 0x7F;
    if (bank == 0x7E || bank == 0x7F) return 0;
    if (b7 < 0x40 && off >= 0x2200 && off < 0x2400) {
        sa1_sync(1);
        switch (off) {
        case 0x2200: {
            const int was_reset = R.sa1_resb;
            if (was_reset && !(v & 0x20)) { sa1_cpu_reset_to(R.crv); R.ciwp = 0; started = 1; sa1_done = (int32_t)(S.line * LINE_CLK + (snes_clk > 0 ? snes_clk : 0)); sa1_lim = sa1_clk; }
            R.smeg = v & 15; R.sa1_nmi = (v >> 4) & 1; R.sa1_resb = (v >> 5) & 1; R.sa1_rdyb = (v >> 6) & 1; R.sa1_irq = v >> 7;
            if (R.sa1_irq) R.sa1_irqfl = 1;
            if (R.sa1_nmi) { R.sa1_nmifl = 1; if (R.sa1_nmien) R.nmi_latch = 1; }
            sa1_irq_update();
            break;
        }
        case 0x2201:
            R.chdma_irqen = (v >> 5) & 1; R.cpu_irqen = v >> 7;
            cpu_line_update();
            break;
        case 0x2202:
            if (v & 0x20) R.chdma_irqfl = 0;
            if (v & 0x80) R.cpu_irqfl = 0;
            cpu_line_update();
            break;
        case 0x2203: R.crv = (R.crv & 0xFF00) | v; break;
        case 0x2204: R.crv = (uint16_t)((R.crv & 0xFF) | (v << 8)); break;
        case 0x2205: R.cnv = (R.cnv & 0xFF00) | v; break;
        case 0x2206: R.cnv = (uint16_t)((R.cnv & 0xFF) | (v << 8)); break;
        case 0x2207: R.civ = (R.civ & 0xFF00) | v; break;
        case 0x2208: R.civ = (uint16_t)((R.civ & 0xFF) | (v << 8)); break;
        case 0x2220: R.cb = v & 7; R.cbmode = v >> 7; sa1_map_build(); snes_map_build(); break;
        case 0x2221: R.db = v & 7; R.dbmode = v >> 7; sa1_map_build(); snes_map_build(); break;
        case 0x2222: R.eb = v & 7; R.ebmode = v >> 7; sa1_map_build(); snes_map_build(); break;
        case 0x2223: R.fb = v & 7; R.fbmode = v >> 7; sa1_map_build(); snes_map_build(); break;
        case 0x2224: R.sbm = v & 0x1F; break;
        case 0x2226: R.swen = v >> 7; break;
        case 0x2228: R.bwp = v & 15; break;
        case 0x2229: R.siwp = v; break;
        case 0x2231: case 0x2232: case 0x2233: case 0x2234: case 0x2235: case 0x2236: case 0x2237: dma_shared_write(off, v); break;
        default: break;
        }
        return 1;
    }
    if (b7 < 0x40 && off >= 0x3000 && off < 0x3800) {
        sa1_sync(1);
        if (R.siwp & (1u << ((off >> 8) & 7))) iram[off & 0x7FF] = v;
        return 1;
    }
    if ((b7 < 0x40 && off >= 0x6000 && off < 0x8000) || (bank >= 0x40 && bank < 0x50)) {
        sa1_sync(1);
        const uint32_t ad = bank >= 0x40 ? (((uint32_t)(bank & 0xF) << 16) | off) : (uint32_t)R.sbm * 0x2000 + (off & 0x1FFF);
        if (!bw_protected(ad & bw_mask())) bw_wr(ad, v);
        return 1;
    }
    return cpu_owns(a);
}

/* ------------------------------------------------------------------ power on */
void sa1_reset(void)
{
    memset(&R, 0, sizeof R);
    memset(iram, 0, sizeof iram);
    R.sa1_resb = 1;
    R.cb = 0; R.db = 1; R.eb = 2; R.fb = 3;
    R.bwp = 15;
    R.vb = 16;
    R.scanlines = 262;
    sa1_s.irq_pending = sa1_s.nmi_pending = 0;
    sa1_clk = sa1_lim = 0; sa1_done = 0; cpu_line_old = 0; n_instr = 0; started = 0;
    sa1c_stopped = 0; sa1c_waiting = 0;
    sa1_map_build();
}
