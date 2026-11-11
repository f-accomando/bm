/*
 * snes_bus.c: the memory map (LoROM, HiROM, WRAM, battery RAM), the registers of the PPU and the CPU, the
 * joypads, DMA and HDMA, the clock of the frame, and the ROM loader. The time is in master clocks (21.477 MHz):
 * a line is 1364 of them, a frame 262 lines. Original code.
 */
#include "snes_int.h"

#include <stdlib.h>
#include <string.h>

int snes_clk;

#define LINE_CLK 1364

/* ------------------------------------------------------------------ the memory map */
static uint8_t cpu_reg_read(uint16_t o);
static void cpu_reg_write(uint16_t o, uint8_t v);
static uint32_t acc_dma;             /* microseconds of the general DMA in this frame (timed at the write to $420B) */

static inline uint8_t rom_byte(uint32_t o)
{
    if (o >= S.rom_size) o %= S.rom_size;
    return S.rom[o];
}

static uint8_t raw_read(uint32_t a)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (bank == 0x7E) return S.wram[off];
    if (bank == 0x7F) return S.wram[0x10000 + off];
    if ((bank & 0x7F) < 0x40) {
        if (off < 0x2000) return S.wram[off];
        if (off < 0x2100) return 0;
        if (off < 0x2200) return snes_ppu_read((uint16_t)off);
        if (off < 0x4000) return 0;
        if (off == 0x4016) { uint8_t b = S.joyshift1 >> 15 & 1; S.joyshift1 = (uint16_t)((S.joyshift1 << 1) | 1); return b; }
        if (off == 0x4017) { uint8_t b = S.joyshift2 >> 15 & 1; S.joyshift2 = (uint16_t)((S.joyshift2 << 1) | 1); return b | 0x1C; }
        if (off < 0x4200) return 0;
        if (off < 0x4380) return cpu_reg_read((uint16_t)off);
        if (off < 0x6000) return 0;
        if (off < 0x8000) {
            if (S.hirom && S.sram_size && (bank & 0x7F) >= 0x20)
                return S.sram[(((bank & 0x1F) * 0x2000) + (off - 0x6000)) & (S.sram_size - 1)];
            return 0;
        }
        if (S.hirom) return rom_byte(((bank & 0x3F) << 16) | off);
        return rom_byte(((bank & 0x7F) << 15) | (off & 0x7FFF));
    }
    if (S.hirom) return rom_byte(((bank & 0x3F) << 16) | off);
    if ((bank & 0x7F) >= 0x70 && off < 0x8000) {
        if (S.sram_size) return S.sram[off & (S.sram_size - 1)];
        return 0;
    }
    return rom_byte(((bank & 0x7F) << 15) | (off & 0x7FFF));
}

static void raw_write(uint32_t a, uint8_t v)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (bank == 0x7E) { S.wram[off] = v; return; }
    if (bank == 0x7F) { S.wram[0x10000 + off] = v; return; }
    if ((bank & 0x7F) < 0x40) {
        if (off < 0x2000) { S.wram[off] = v; return; }
        if (off < 0x2200) { snes_ppu_write((uint16_t)off, v); return; }
        if (off < 0x4000) return;
        if (off == 0x4016) {
            if (S.joystrobe && !(v & 1)) { S.joyshift1 = S.joy1; S.joyshift2 = S.joy2; }
            S.joystrobe = v & 1;
            if (S.joystrobe) { S.joyshift1 = S.joy1; S.joyshift2 = S.joy2; }
            return;
        }
        if (off < 0x4200) return;
        if (off < 0x4380) { cpu_reg_write((uint16_t)off, v); return; }
        if (off >= 0x6000 && off < 0x8000 && S.hirom && S.sram_size && (bank & 0x7F) >= 0x20) {
            S.sram[(((bank & 0x1F) * 0x2000) + (off - 0x6000)) & (S.sram_size - 1)] = v;
            S.sram_dirty = 1;
        }
        return;
    }
    if (!S.hirom && (bank & 0x7F) >= 0x70 && off < 0x8000 && S.sram_size) {
        S.sram[off & (S.sram_size - 1)] = v;
        S.sram_dirty = 1;
    }
}

/* the time one access takes, in master clocks */
static inline int access_clk(uint32_t a)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (bank == 0x7E || bank == 0x7F) return 8;
    if ((bank & 0x7F) < 0x40) {
        if (off < 0x2000) return 8;
        if (off < 0x4000) return 6;
        if (off < 0x4200) return 12;
        if (off < 0x6000) return 6;
        if (off < 0x8000) return 8;
        return (bank & 0x80) && (S.memsel & 1) ? 6 : 8;
    }
    return (bank & 0x80) && (S.memsel & 1) ? 6 : 8;
}

/* ------------------------------------------------------------------ the memory map as pages */
/* the byte that raw_read returns for plain memory, as a pointer; NULL for the registers and for what reads as 0 (the same cases as raw_read) */
static const uint8_t *map_ptr(uint32_t a)
{
    const unsigned bank = a >> 16, off = a & 0xFFFF;
    if (bank == 0x7E) return &S.wram[off];
    if (bank == 0x7F) return &S.wram[0x10000 + off];
    if ((bank & 0x7F) < 0x40) {
        if (off < 0x2000) return &S.wram[off];
        if (off < 0x6000) return NULL;
        if (off < 0x8000) {
            if (S.hirom && S.sram_size && (bank & 0x7F) >= 0x20)
                return &S.sram[(((bank & 0x1F) * 0x2000) + (off - 0x6000)) & (S.sram_size - 1)];
            return NULL;
        }
        if (S.hirom) { uint32_t o = ((bank & 0x3F) << 16) | off; if (o >= S.rom_size) o %= S.rom_size; return &S.rom[o]; }
        { uint32_t o = ((bank & 0x7F) << 15) | (off & 0x7FFF); if (o >= S.rom_size) o %= S.rom_size; return &S.rom[o]; }
    }
    if (S.hirom) { uint32_t o = ((bank & 0x3F) << 16) | off; if (o >= S.rom_size) o %= S.rom_size; return &S.rom[o]; }
    if ((bank & 0x7F) >= 0x70 && off < 0x8000) return S.sram_size ? &S.sram[off & (S.sram_size - 1)] : NULL;
    { uint32_t o = ((bank & 0x7F) << 15) | (off & 0x7FFF); if (o >= S.rom_size) o %= S.rom_size; return &S.rom[o]; }
}

const uint8_t *snes_rtab[4096];
uint8_t *snes_wtab[4096];
uint8_t snes_ctab[4096];

/* A page is fast when its first and last byte are plain memory at the same distance (no mirror wraps inside the page) and cost the same clocks. */
void snes_map_build(void)
{
    for (unsigned pg = 0; pg < 4096; pg++) {
        const uint32_t base = (uint32_t)pg << 12;
        snes_rtab[pg] = NULL; snes_wtab[pg] = NULL; snes_ctab[pg] = 0;
        if (!S.rom) continue;
        const uint8_t *p0 = map_ptr(base), *p1 = map_ptr(base + 0xFFF);
        const int c0 = access_clk(base), c1 = access_clk(base + 0xFFF);
        if (!p0 || !p1 || p1 != p0 + 0xFFF || c0 != c1) continue;
        snes_rtab[pg] = p0;
        snes_ctab[pg] = (uint8_t)c0;
        if (p0 >= S.wram && p0 < S.wram + sizeof S.wram) snes_wtab[pg] = (uint8_t *)p0;
    }
}

uint8_t snes_read(uint32_t a)
{
    snes_clk += access_clk(a);
    const unsigned off = a & 0xFFFF;
    if (off >= 0x2100 && off < 0x4400 && ((a >> 16) & 0x7F) < 0x40) {
        S.io_idx = (S.io_idx + 1) % 6;
        S.io_log[S.io_idx] = (uint16_t)off;
    }
    return raw_read(a);
}

void snes_write(uint32_t a, uint8_t v)
{
    snes_clk += access_clk(a);
    raw_write(a, v);
}

/* ------------------------------------------------------------------ the PPU registers */
static inline uint16_t vaddr(uint16_t a)
{
    switch ((S.vmain >> 2) & 3) {
    case 0: return a;
    case 1: return (a & 0xFF00) | ((a & 0x1F) << 3) | ((a >> 5) & 7);
    case 2: return (a & 0xFE00) | ((a & 0x3F) << 3) | ((a >> 6) & 7);
    default: return (a & 0xFC00) | ((a & 0x7F) << 3) | ((a >> 7) & 7);
    }
}
static const uint16_t vmstep[4] = { 1, 32, 128, 128 };

static void vprefetch(void)
{
    uint32_t ba = ((uint32_t)vaddr(S.vmaddr & 0x7FFF) << 1) & 0xFFFF;
    S.vmpre = (uint16_t)(S.vram[ba] | (S.vram[ba + 1] << 8));
}
static void vwrite(int hi, uint8_t v)
{
    uint32_t ba = ((uint32_t)vaddr(S.vmaddr & 0x7FFF) << 1) & 0xFFFF;
    S.vram[ba + hi] = v;
    ppu_vram_dirty((uint16_t)(ba + hi));
    if (((S.vmain & 0x80) != 0) == hi) S.vmaddr = (uint16_t)(S.vmaddr + vmstep[S.vmain & 3]);
}
static int16_t sign13(unsigned v) { v &= 0x1FFF; return (int16_t)(v & 0x1000 ? v - 0x2000 : v); }

void snes_ppu_write(uint16_t o, uint8_t v)
{
    switch (o) {
    case 0x2100: S.inidisp = v; break;
    case 0x2101: S.obsel = v; break;
    case 0x2102: S.oamaddr_reload = (uint16_t)((S.oamaddr_reload & 0x200) | (v << 1)); S.oamaddr = S.oamaddr_reload; break;
    case 0x2103: S.oamaddr_reload = (uint16_t)((S.oamaddr_reload & 0x1FE) | ((v & 1) << 9)); S.oamaddr = S.oamaddr_reload; S.oamrot = (uint8_t)(v >> 7); break;
    case 0x2104:
        if (S.oamaddr >= 0x200) S.oam[0x200 + (S.oamaddr & 0x1F)] = v;
        else if (!(S.oamaddr & 1)) S.oam_latch = v;
        else { S.oam[S.oamaddr - 1] = S.oam_latch; S.oam[S.oamaddr] = v; }
        S.oamaddr = (S.oamaddr + 1) & 0x3FF;
        break;
    case 0x2105: S.bgmode = v; break;
    case 0x2106: S.mosaic = v; break;
    case 0x2107: case 0x2108: case 0x2109: case 0x210A: S.sc[o - 0x2107] = v; break;
    case 0x210B: S.nba[0] = (uint16_t)((v & 15) << 13); S.nba[1] = (uint16_t)((v >> 4) << 13); break;
    case 0x210C: S.nba[2] = (uint16_t)((v & 15) << 13); S.nba[3] = (uint16_t)((v >> 4) << 13); break;
    case 0x210D: case 0x210F: case 0x2111: case 0x2113: {
        int n = (o - 0x210D) >> 1;
        if (n == 0) { S.m7hofs = sign13(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; }
        S.hofs[n] = (uint16_t)(((unsigned)v << 8) | (S.bgofs_latch & 0xF8) | ((S.hofs[n] >> 8) & 7));    /* all 16 bits kept: the next write takes bits 8 to 10 of this one; the picture uses the low 10 */
        S.bgofs_latch = v;
        break;
    }
    case 0x210E: case 0x2110: case 0x2112: case 0x2114: {
        int n = (o - 0x210E) >> 1;
        if (n == 0) { S.m7vofs = sign13(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; }
        S.vofs[n] = (uint16_t)((((unsigned)v << 8) | S.bgofs_latch) & 0x3FF);
        S.bgofs_latch = v;
        break;
    }
    case 0x2115: S.vmain = v; break;
    case 0x2116: S.vmaddr = (uint16_t)((S.vmaddr & 0xFF00) | v); vprefetch(); break;
    case 0x2117: S.vmaddr = (uint16_t)((S.vmaddr & 0x00FF) | (v << 8)); vprefetch(); break;
    case 0x2118: vwrite(0, v); break;
    case 0x2119: vwrite(1, v); break;
    case 0x211A: S.m7sel = v; break;
    case 0x211B: S.m7a = (int16_t)(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; break;
    case 0x211C: S.m7b = (int16_t)(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v;
        S.mpy = (int32_t)S.m7a * (int8_t)v; break;
    case 0x211D: S.m7c = (int16_t)(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; break;
    case 0x211E: S.m7d = (int16_t)(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; break;
    case 0x211F: S.m7x = sign13(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; break;
    case 0x2120: S.m7y = sign13(((unsigned)v << 8) | S.m7_latch); S.m7_latch = v; break;
    case 0x2121: S.cgadd = v; S.cgflip = 0; break;
    case 0x2122:
        if (!S.cgflip) { S.cglatch = v; S.cgflip = 1; }
        else {
            S.cgram[S.cgadd * 2] = S.cglatch; S.cgram[S.cgadd * 2 + 1] = v & 0x7F;
            ppu_cgram_write(S.cgadd);
            S.cgadd++; S.cgflip = 0;
        }
        break;
    case 0x2123: S.w12sel = v; break;
    case 0x2124: S.w34sel = v; break;
    case 0x2125: S.wobjsel = v; break;
    case 0x2126: S.wh0 = v; break;
    case 0x2127: S.wh1 = v; break;
    case 0x2128: S.wh2 = v; break;
    case 0x2129: S.wh3 = v; break;
    case 0x212A: S.wbglog = v; break;
    case 0x212B: S.wobjlog = v; break;
    case 0x212C: S.tm = v; break;
    case 0x212D: S.ts = v; break;
    case 0x212E: S.tmw = v; break;
    case 0x212F: S.tsw = v; break;
    case 0x2130: S.cgwsel = v; break;
    case 0x2131: S.cgadsub = v; break;
    case 0x2132: {
        uint8_t c = v & 31;
        if (v & 0x20) S.fixr = c;
        if (v & 0x40) S.fixg = c;
        if (v & 0x80) S.fixb = c;
        break;
    }
    case 0x2133: S.setini = v; break;
    case 0x2140: case 0x2141: case 0x2142: case 0x2143: spc_port_write(o & 3, v); break;
    case 0x2180: S.wram[S.wmadd & 0x1FFFF] = v; S.wmadd = (S.wmadd + 1) & 0x1FFFF; break;
    case 0x2181: S.wmadd = (S.wmadd & 0x1FF00) | v; break;
    case 0x2182: S.wmadd = (S.wmadd & 0x100FF) | ((uint32_t)v << 8); break;
    case 0x2183: S.wmadd = (S.wmadd & 0x0FFFF) | ((uint32_t)(v & 1) << 16); break;
    default:
        if (o >= 0x2144 && o < 0x2180) spc_port_write(o & 3, v);
        break;
    }
}

uint8_t snes_ppu_read(uint16_t o)
{
    switch (o) {
    case 0x2134: return (uint8_t)S.mpy;
    case 0x2135: return (uint8_t)(S.mpy >> 8);
    case 0x2136: return (uint8_t)(S.mpy >> 16);
    case 0x2137: S.hcount = (uint16_t)(S.line_clk / 4); S.vcount = (uint16_t)S.line; return 0;
    case 0x2138: {
        uint8_t v = S.oamaddr >= 0x200 ? S.oam[0x200 + (S.oamaddr & 0x1F)] : S.oam[S.oamaddr];
        S.oamaddr = (S.oamaddr + 1) & 0x3FF;
        return v;
    }
    case 0x2139: case 0x213A: {
        int hi = o == 0x213A;
        uint8_t v = hi ? (uint8_t)(S.vmpre >> 8) : (uint8_t)S.vmpre;
        if (((S.vmain & 0x80) != 0) == hi) { S.vmaddr = (uint16_t)(S.vmaddr + vmstep[S.vmain & 3]); vprefetch(); }
        return v;
    }
    case 0x213B: {
        uint8_t v = S.cgram[S.cgadd * 2 + S.cgflip];
        if (S.cgflip) S.cgadd++;
        S.cgflip ^= 1;
        return v;
    }
    case 0x213C: { S.ophct_flip ^= 1; return S.ophct_flip ? (uint8_t)S.hcount : (uint8_t)(S.hcount >> 8 & 1); }
    case 0x213D: { S.opvct_flip ^= 1; return S.opvct_flip ? (uint8_t)S.vcount : (uint8_t)(S.vcount >> 8 & 1); }
    case 0x213E: return 0x01;
    case 0x213F: S.ophct_flip = S.opvct_flip = 0; return 0x03;
    case 0x2180: { uint8_t v = S.wram[S.wmadd & 0x1FFFF]; S.wmadd = (S.wmadd + 1) & 0x1FFFF; return v; }
    default:
        if (o >= 0x2140 && o < 0x2180) return spc_port_read(o & 3);
        return 0;
    }
}

/* ------------------------------------------------------------------ DMA and HDMA */
static const uint8_t dma_pat[8][4] = { {0,0,0,0}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}, {0,1,2,3}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1} };
static const uint8_t dma_len[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

static void dma_unit(snes_dma_t *d, uint32_t abus, int to_b, int idx)
{
    uint16_t b = (uint16_t)(0x2100 + d->bbad + dma_pat[d->dmap & 7][idx & 3]);
    if (to_b) {
        uint8_t v = raw_read(abus);
        if (b != 0x2180 || 1) {
            if (b < 0x2200 && b >= 0x2100) snes_ppu_write(b, v);
        }
    } else {
        uint8_t v = (b >= 0x2100 && b < 0x2200) ? snes_ppu_read(b) : 0;
        raw_write(abus, v);
    }
}

static void run_dma(uint8_t mask)
{
    for (int c = 0; c < 8; c++) {
        if (!(mask & (1 << c))) continue;
        snes_dma_t *d = &S.dma[c];
        const int to_b = !(d->dmap & 0x80);
        uint32_t count = d->das ? d->das : 0x10000;
        uint16_t a = d->a1t;
        int idx = 0;
        snes_clk += 8;
        while (count--) {
            dma_unit(d, ((uint32_t)d->a1b << 16) | a, to_b, idx++);
            if (!(d->dmap & 0x08)) a = (uint16_t)((d->dmap & 0x10) ? a - 1 : a + 1);
            snes_clk += 8;
        }
        d->a1t = a;
        d->das = 0;
    }
}

void snes_hdma_init(void)
{
    for (int c = 0; c < 8; c++) {
        snes_dma_t *d = &S.dma[c];
        d->active = 0;
        if (!(S.hdmaen & (1 << c))) continue;
        d->tbl = d->a1t;
        d->line = raw_read(((uint32_t)d->a1b << 16) | d->tbl++);
        if (d->dmap & 0x40) {
            d->ind = raw_read(((uint32_t)d->a1b << 16) | d->tbl++);
            d->ind |= (uint16_t)(raw_read(((uint32_t)d->a1b << 16) | d->tbl++) << 8);
        }
        d->active = d->line != 0;
        d->doit = 1;
    }
}

void snes_hdma_line(void)
{
    for (int c = 0; c < 8; c++) {
        snes_dma_t *d = &S.dma[c];
        if (!d->active || !(S.hdmaen & (1 << c))) continue;
        if (d->doit) {
            const int n = dma_len[d->dmap & 7];
            for (int i = 0; i < n; i++) {
                uint32_t src;
                if (d->dmap & 0x40) { src = ((uint32_t)d->dasb << 16) | d->ind; d->ind++; }
                else { src = ((uint32_t)d->a1b << 16) | d->tbl; d->tbl++; }
                dma_unit(d, src, 1, i);
            }
            snes_clk += 8 + 8 * n;
        }
        d->line = (uint8_t)((d->line & 0x80) | ((d->line - 1) & 0x7F));
        d->doit = (d->line & 0x80) != 0;
        if ((d->line & 0x7F) == 0) {
            d->line = raw_read(((uint32_t)d->a1b << 16) | d->tbl++);
            if (d->dmap & 0x40) {
                d->ind = raw_read(((uint32_t)d->a1b << 16) | d->tbl++);
                d->ind |= (uint16_t)(raw_read(((uint32_t)d->a1b << 16) | d->tbl++) << 8);
            }
            if (d->line == 0) d->active = 0;
            d->doit = 1;
        }
    }
}

/* ------------------------------------------------------------------ the CPU registers ($4200..$437F) */
static uint8_t cpu_reg_read(uint16_t o)
{
    if (o >= 0x4300) {
        snes_dma_t *d = &S.dma[(o >> 4) & 7];
        switch (o & 15) {
        case 0: return d->dmap; case 1: return d->bbad;
        case 2: return (uint8_t)d->a1t; case 3: return (uint8_t)(d->a1t >> 8); case 4: return d->a1b;
        case 5: return (uint8_t)d->das; case 6: return (uint8_t)(d->das >> 8); case 7: return d->dasb;
        case 8: return (uint8_t)d->a2a; case 9: return (uint8_t)(d->a2a >> 8);
        case 10: return d->ntrl;
        default: return d->unused;
        }
    }
    switch (o) {
    case 0x4210: { uint8_t v = (uint8_t)((S.nmiflag << 7) | 0x02); S.nmiflag = 0; return v; }
    case 0x4211: { uint8_t v = (uint8_t)(S.timeup << 7); S.timeup = 0; S.irq_pending = 0; return v; }
    case 0x4212: return (uint8_t)((S.vblank ? 0x80 : 0) | (S.line_clk >= 1096 ? 0x40 : 0) | S.joybusy);
    case 0x4213: return 0xC0;
    case 0x4214: return (uint8_t)S.rddiv;
    case 0x4215: return (uint8_t)(S.rddiv >> 8);
    case 0x4216: return (uint8_t)S.rdmpy;
    case 0x4217: return (uint8_t)(S.rdmpy >> 8);
    case 0x4218: return (uint8_t)S.joyauto1;
    case 0x4219: return (uint8_t)(S.joyauto1 >> 8);
    case 0x421A: return (uint8_t)S.joyauto2;
    case 0x421B: return (uint8_t)(S.joyauto2 >> 8);
    default: return 0;
    }
}

static void cpu_reg_write(uint16_t o, uint8_t v)
{
    if (o >= 0x4300) {
        snes_dma_t *d = &S.dma[(o >> 4) & 7];
        switch (o & 15) {
        case 0: d->dmap = v; break; case 1: d->bbad = v; break;
        case 2: d->a1t = (uint16_t)((d->a1t & 0xFF00) | v); break;
        case 3: d->a1t = (uint16_t)((d->a1t & 0x00FF) | (v << 8)); break;
        case 4: d->a1b = v; break;
        case 5: d->das = (uint16_t)((d->das & 0xFF00) | v); break;
        case 6: d->das = (uint16_t)((d->das & 0x00FF) | (v << 8)); break;
        case 7: d->dasb = v; break;
        case 8: d->a2a = (uint16_t)((d->a2a & 0xFF00) | v); break;
        case 9: d->a2a = (uint16_t)((d->a2a & 0x00FF) | (v << 8)); break;
        case 10: d->ntrl = v; break;
        default: d->unused = v; break;
        }
        return;
    }
    switch (o) {
    case 0x4200:
        if (!(S.nmitimen & 0x80) && (v & 0x80) && S.nmiflag) S.nmi_pending = 1;
        S.nmitimen = v;
        if ((v & 0x30) == 0) { S.irq_pending = 0; S.timeup = 0; }
        break;
    case 0x4202: S.wrmpya = v; break;
    case 0x4203: S.wrmpyb = v; S.rdmpy = (uint16_t)(S.wrmpya * v); S.rddiv = v; break;
    case 0x4204: S.wrdiv = (uint16_t)((S.wrdiv & 0xFF00) | v); break;
    case 0x4205: S.wrdiv = (uint16_t)((S.wrdiv & 0x00FF) | (v << 8)); break;
    case 0x4206:
        if (v) { S.rddiv = (uint16_t)(S.wrdiv / v); S.rdmpy = (uint16_t)(S.wrdiv % v); }
        else { S.rddiv = 0xFFFF; S.rdmpy = S.wrdiv; }
        break;
    case 0x4207: S.htime = (uint16_t)((S.htime & 0x100) | v); break;
    case 0x4208: S.htime = (uint16_t)((S.htime & 0xFF) | ((v & 1) << 8)); break;
    case 0x4209: S.vtime = (uint16_t)((S.vtime & 0x100) | v); break;
    case 0x420A: S.vtime = (uint16_t)((S.vtime & 0xFF) | ((v & 1) << 8)); break;
    case 0x420B: S.mdmaen = v;
        if (snes_now_us) { uint32_t a = snes_now_us(); run_dma(v); acc_dma += snes_now_us() - a; }
        else run_dma(v);
        break;
    case 0x420C: S.hdmaen = v; break;
    case 0x420D: S.memsel = v; snes_map_build(); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ the machine */
void snes_bus_reset(void)
{
    memset(S.wram, 0, sizeof S.wram);
    memset(S.vram, 0, sizeof S.vram);
    ppu_vram_dirty_all();
    memset(S.oam, 0, sizeof S.oam);
    memset(S.cgram, 0, sizeof S.cgram);
    memset(S.dma, 0xFF, sizeof S.dma);
    for (int c = 0; c < 8; c++) { S.dma[c].active = 0; S.dma[c].doit = 0; }
    S.inidisp = 0x80; S.obsel = S.bgmode = S.mosaic = S.setini = 0;
    S.tm = S.ts = S.tmw = S.tsw = 0;
    S.nmitimen = S.hdmaen = S.mdmaen = S.memsel = 0;
    S.htime = S.vtime = 0x1FF;
    S.nmiflag = S.timeup = S.joybusy = 0;
    S.irq_pending = S.nmi_pending = 0;
    S.joyshift1 = S.joyshift2 = 0xFFFF;
    S.joyauto1 = S.joyauto2 = 0;
    S.line = 0; S.line_clk = 0; S.frames = 0;
    S.lines = 224; S.vblank = 0;
    S.vmain = 0; S.vmaddr = 0; S.oamaddr = S.oamaddr_reload = 0; S.cgadd = 0; S.cgflip = 0;
    S.wmadd = 0;
    S.rddiv = 0; S.rdmpy = 0; S.wrdiv = 0xFFFF; S.wrmpya = 0xFF;
    ppu_reset();
    snes_map_build();
}

void snes_reset(void)
{
    if (!S.rom) return;
    snes_bus_reset();
    spc_reset();
    snes_clk = 0;
    cpu_reset();
}

#define now_us snes_now_us
static uint32_t avg_part[5];                     /* the parts of the PPU (layers, sprites, windows, compose, mix): a drawn frame, in microseconds */
static uint32_t avg_frame, avg_ppu, avg_spc, avg_cpu, avg_dma, avg_hdma, avg_instr;
/* The timer read costs about 6 us on the Pi, and a frame reads it 3 times a line (more inside the PPU): timing every frame would take a third of the frame.
 * So one frame of 7 is timed (`timed`); `now_us` is the clock in that frame and NULL in the others. The cost of one read is measured at the start of
 * a timed frame and taken off each part (every part holds one read of every line). */
static uint32_t (*clock_fn)(void);
void snes_set_clock(uint32_t (*f)(void)) { clock_fn = f; now_us = f; }

void snes_frame(uint16_t pad1, uint16_t pad2, int render)
{
    if (!S.rom) return;
    const int timed = clock_fn && (S.frames % 7) == 0;   /* 7 and not 8: it must not stay on the frames a picture is (never) made in */
    now_us = timed ? clock_fn : NULL;
    uint32_t rc10 = 0;                             /* the cost of one clock read, in tenths of a microsecond */
    if (timed) { const uint32_t r0 = clock_fn(); for (int i = 0; i < 64; i++) (void)clock_fn(); rc10 = (clock_fn() - r0) * 10 / 65; }
    const uint32_t t_start = timed ? clock_fn() : 0;
    uint32_t t_ppu = 0, t_spc = 0, t_cpu = 0, t_hdma = 0, n_instr = 0, n_hl = 0;
    uint32_t t_prev = t_start;                     /* the end of the last part: the CPU part of a line is the time from here to the start of the sound */
    acc_dma = 0;
    S.joy1 = pad1; S.joy2 = pad2;
    S.lines = (S.setini & 4) ? 239 : 224;
    for (int L = 0; L < 262; L++) {
        S.line = L;
        S.line_clk = 0;
        if (L == 0) {
            S.vblank = 0; S.nmiflag = 0; S.joybusy = 0;
            snes_hdma_init();
        } else if (L == S.lines) {
            S.vblank = 1; S.nmiflag = 1;
            if (S.nmitimen & 0x80) S.nmi_pending = 1;
            if (S.nmitimen & 1) { S.joyauto1 = S.joy1; S.joyauto2 = S.joy2; S.joyshift1 = S.joy1; S.joyshift2 = S.joy2; S.joybusy = 1; }
        } else if (L == S.lines + 3) {
            S.joybusy = 0;
        }
        if (L < S.lines) {
            if (S.hdmaen) {
                if (timed) { uint32_t a = now_us(); snes_hdma_line(); t_hdma += now_us() - a; n_hl++; }
                else snes_hdma_line();
            }
        }
        /* IRQ: V, H or both */
        int ia = -1;
        const int m = (S.nmitimen >> 4) & 3;
        if (m == 1) ia = (S.htime > 339 ? 339 : S.htime) * 4 + 14;
        else if (m == 2 && L == S.vtime) ia = 14;
        else if (m == 3 && L == S.vtime) ia = (S.htime > 339 ? 339 : S.htime) * 4 + 14;
        /* the line: the CPU runs to the IRQ, to the start of the blank, and to the end */
        const int end = LINE_CLK;
        S.line_clk = 0;
        if (ia >= 0 && ia < end) {
            n_instr += (uint32_t)cpu_run(ia);
            S.irq_pending = 1; S.timeup = 1;
        }
        n_instr += (uint32_t)cpu_run(end);
        S.line_clk = 0;
        snes_clk -= end;
        if (now_us) {
            uint32_t a = now_us();
            spc_run(65);
            uint32_t b = now_us();
            if (L < S.lines && render) ppu_line(L);
            const uint32_t c = now_us();
            t_cpu += a - t_prev; t_spc += b - a; t_ppu += c - b; t_prev = c;
        } else {
            spc_run(65);
            if (L < S.lines && render) ppu_line(L);
        }
    }
    S.frames++;
    if (timed) {
        const uint32_t over = rc10 * 262 / 10;                 /* the reads in one part over the frame */
        const uint32_t tot = now_us() - t_start;
#define OFF(v, o) ((v) > (o) ? (v) - (o) : 0)
        const uint32_t f = OFF(tot, 3 * over), p = OFF(t_ppu, over), sp = OFF(t_spc, over), c = OFF(t_cpu, over);
        /* a sample every 8 frames: the average moves by half of the difference; the first sample is taken as it is */
        const int first = avg_frame == 0;
#define AVG(a, v) ((a) = first ? (v) : (uint32_t)((int32_t)(a) + ((int32_t)(v) - (int32_t)(a)) / 2))
        AVG(avg_frame, f); AVG(avg_spc, sp); AVG(avg_cpu, c); AVG(avg_dma, acc_dma); AVG(avg_hdma, OFF(t_hdma, n_hl * rc10 / 10));
#undef AVG
        /* the PPU is averaged over the frames a picture was made in: a drawn frame costs this much (the frames with no picture cost none) */
        if (render) {
            avg_ppu = avg_ppu ? (uint32_t)((int32_t)avg_ppu + ((int32_t)p - (int32_t)avg_ppu) / 2) : p;
            for (int k = 0; k < 5; k++) {                      /* each part: its sum over the lines of this frame, less one clock read for each lap */
                const uint32_t v = OFF(ppu_us[k], ppu_n[k] * rc10 / 10);
                avg_part[k] = avg_part[k] ? (uint32_t)((int32_t)avg_part[k] + ((int32_t)v - (int32_t)avg_part[k]) / 2) : v;
            }
        }
        memset(ppu_us, 0, sizeof ppu_us); memset(ppu_n, 0, sizeof ppu_n);
#undef OFF
    }
    avg_instr = (uint32_t)((int32_t)avg_instr + ((int32_t)n_instr - (int32_t)avg_instr) / 8);   /* the instructions are counted in every frame */
}

/* ------------------------------------------------------------------ the loader and the small API */
static int header_score(const uint8_t *d, size_t len, size_t off, int hi)
{
    if (len < off + 0x40) return -1;
    int sc = 0;
    unsigned sum = d[off + 0x1E] | (d[off + 0x1F] << 8), comp = d[off + 0x1C] | (d[off + 0x1D] << 8);
    if ((sum ^ comp) == 0xFFFF) sc += 4;
    unsigned mm = d[off + 0x15];
    if (mm & 0x20) sc += 1;
    if (((mm & 1) == 1) == hi) sc += 2;
    if ((d[off + 0x3C] | (d[off + 0x3D] << 8)) >= 0x8000) sc += 1;
    int ok = 1;
    for (int i = 0; i < 21; i++) if (d[off + i] < 32 || d[off + i] > 126) ok = 0;
    if (ok) sc += 1;
    return sc;
}

int snes_load(const uint8_t *data, size_t len, const char **why)
{
    static const char *dummy;
    if (!why) why = &dummy;
    if (len % 1024 == 512) { data += 512; len -= 512; }        /* a copier header */
    if (len < 0x8000) { *why = "the file is too small for a SNES ROM"; return -1; }
    if (len > 8u * 1024 * 1024) { *why = "the ROM is bigger than 8 MB"; return -1; }
    uint8_t *copy = malloc(len);
    if (!copy) { *why = "not enough memory for the ROM"; return -1; }
    memcpy(copy, data, len);
    free(S.rom);
    S.rom = copy;
    S.rom_size = (uint32_t)len;
    const int lo = header_score(data, len, 0x7FC0, 0), hi = header_score(data, len, 0xFFC0, 1);
    S.hirom = hi > lo;
    const size_t off = S.hirom ? 0xFFC0 : 0x7FC0;
    memset(&S.info, 0, sizeof S.info);
    for (int i = 0; i < 21; i++) {
        char c = (char)data[off + i];
        S.info.title[i] = (c >= 32 && c < 127) ? c : '?';
    }
    for (int i = 20; i >= 0 && S.info.title[i] == ' '; i--) S.info.title[i] = 0;
    S.info.hirom = S.hirom;
    S.info.rom_size = S.rom_size;
    const unsigned rs = data[off + 0x18];
    S.sram_size = (rs > 0 && rs <= 7) ? (1024u << rs) : 0;
    S.info.sram_size = S.sram_size;
    memset(S.sram, 0, sizeof S.sram);
    S.sram_dirty = 0;
    snes_reset();
    return 0;
}

const uint16_t *snes_picture(int *lines) { if (lines) *lines = S.lines; return S.picture; }
const snes_info_t *snes_info(void) { return &S.info; }
uint8_t *snes_sram(uint32_t *size) { if (size) *size = S.sram_size; return S.sram; }
int snes_sram_dirty(void) { return S.sram_dirty; }
void snes_sram_clean(void) { S.sram_dirty = 0; }

void snes_vram_peek(uint16_t addr, uint8_t *out, int n) { for (int i = 0; i < n; i++) out[i] = S.vram[(uint16_t)(addr + i)]; }
void snes_cgram_peek(uint16_t addr, uint8_t *out, int n) { for (int i = 0; i < n; i++) out[i] = S.cgram[(addr + i) & 511]; }

void snes_debug(snes_debug_t *d)
{
    memset(d, 0, sizeof *d);
    cpu_info(&d->pc, &d->flags);
    for (int i = 0; i < 6; i++) d->io[i] = S.io_log[(S.io_idx + 1 + i) % 6];
    spc_info(&d->spc_running, &d->spc_halted, &d->spc_pc);
    for (int i = 0; i < 12; i++) d->cpu_code[i] = raw_read((d->pc - 8 + (uint32_t)i) & 0xFFFFFF);   /* 8 bytes before the PC and 4 after */
    spc_trace(d->trace);
    spc_trace_s(d->trace_s);
    {
        uint8_t regs[8];
        spc_peek(d->spc_pc, d->spc_code, 12, d->port_in, d->port_out, d->timer_out, regs);
        d->spc_ctl = regs[0]; d->spc_tgt[0] = regs[1]; d->spc_tgt[1] = regs[2]; d->spc_tgt[2] = regs[3]; d->spc_en = regs[4]; d->spc_writes = (uint32_t)(regs[5] | (regs[6] << 8) | (regs[7] << 16));
    }
    d->us_bg = avg_part[0]; d->us_obj = avg_part[1]; d->us_win = avg_part[2]; d->us_comp = avg_part[3]; d->us_mix = avg_part[4];
    d->us_frame = avg_frame; d->us_ppu = avg_ppu; d->us_spc = avg_spc;
    d->us_cpu = avg_cpu; d->us_dma = avg_dma; d->us_hdma = avg_hdma; d->instr = avg_instr;
    d->inidisp = S.inidisp; d->tm = S.tm; d->nmitimen = S.nmitimen; d->frames = S.frames;
    memcpy(d->oam, S.oam, sizeof d->oam);
    d->oamrot = S.oamrot; d->oam_reload = S.oamaddr_reload;
    d->bgmode = S.bgmode; d->obsel = S.obsel; d->ts = S.ts;
    for (int i = 0; i < 4; i++) { d->sc[i] = S.sc[i]; d->hofs[i] = S.hofs[i] & 0x3FF; d->vofs[i] = S.vofs[i]; }
}
