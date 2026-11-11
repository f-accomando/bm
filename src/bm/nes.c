/*
 * nes.c: the Famicom / NES machine of bm NES. Original code, written from the public hardware documents.
 *
 *   - the cartridge: the iNES header, the mappers 0, 1, 2, 3, 4, 7, 11, 34, 66, 71 and 79, the nametable mirroring
 *   - the 2C02 picture unit: one scanline at a time, drawn at the start of the line from the registers at that moment
 *     (the loopy registers v, t, x, w), 8 sprites a line, sprite 0 hit with the dot it happens at, the vblank and the NMI
 *   - the 2A03: a 6502 with no decimal mode; all the official opcodes and the usual unofficial ones
 *   - the sound: two pulse channels, the triangle, the noise and the DMC, the frame counter, a 48 kHz mono ring
 *
 * The time is in CPU cycles; the picture unit has 3 dots to a cycle and a line is 341 dots (262 lines a frame).
 * The machine is plain C with no dependence on the kernel.
 */
#include "nes.h"

#include <string.h>

/* ------------------------------------------------------------------ the cartridge */
#define ROM_MAX (2u * 1024u * 1024u)
static uint8_t rom_buf[ROM_MAX];
static uint8_t *prg, *chr;
static uint32_t prg_size, chr_size;
static unsigned n_prg8, n_chr1;
static int chr_is_ram, mapper, battery, hdr_mirror;
static uint8_t chr_ram[8192];
static uint8_t prg_ram[8192];
static int sram_dirty;
static nes_info_t info;
static char whybuf[48];

static uint8_t *prg_pg[4];                  /* the four 8 KB pages of $8000-$FFFF */
static uint8_t *chr_pg[8];                  /* the eight 1 KB pages of the picture ROM or RAM */
static uint8_t *nt_pg[4];                   /* the four name tables */
static uint8_t vram[4096];                  /* 2 KB of name tables (4 KB with four screens) */
static int map_irq;                         /* the mapper pulls the IRQ line */

static void prg8(int slot, unsigned b) { prg_pg[slot] = prg + (size_t)(b % n_prg8) * 8192u; }
static void prg16(int slot, unsigned b) { prg8(slot * 2, b * 2); prg8(slot * 2 + 1, b * 2 + 1); }
static void prg32(unsigned b) { for (int i = 0; i < 4; i++) prg8(i, b * 4 + (unsigned)i); }
static void chr1(int slot, unsigned b) { chr_pg[slot] = chr + (size_t)(b % n_chr1) * 1024u; }
static void chr2(int slot, unsigned b) { chr1(slot, b); chr1(slot + 1, b + 1); }
static void chr4(int slot4, unsigned b4) { for (int i = 0; i < 4; i++) chr1(slot4 * 4 + i, b4 * 4 + (unsigned)i); }
static void chr8(unsigned b8) { for (int i = 0; i < 8; i++) chr1(i, b8 * 8 + (unsigned)i); }

static void mirror(int mode)
{
    static const uint8_t m[5][4] = { { 0, 0, 1, 1 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 0, 1, 2, 3 } };
    for (int i = 0; i < 4; i++) nt_pg[i] = vram + m[mode][i] * 1024;
    info.mirroring = mode;
}

/* MMC1 */
static uint8_t m1_sr, m1_cnt, m1_ctrl, m1_chr0, m1_chr1, m1_prg;
/* MMC3 */
static uint8_t m3_sel, m3_reg[8], m3_latch, m3_cnt, m3_reload, m3_en;

static void mmc1_sync(void)
{
    mirror(m1_ctrl & 2 ? ((m1_ctrl & 1) ? 0 : 1) : (m1_ctrl & 1));
    const int pm = (m1_ctrl >> 2) & 3;
    const unsigned n16 = n_prg8 / 2;
    if (pm < 2) prg32((m1_prg & 0x0F) >> 1);
    else if (pm == 2) { prg16(0, 0); prg16(1, m1_prg & 0x0F); }
    else { prg16(0, m1_prg & 0x0F); prg16(1, n16 - 1); }
    if (m1_ctrl & 0x10) { chr4(0, m1_chr0); chr4(1, m1_chr1); }
    else { chr4(0, m1_chr0 & 0x1E); chr4(1, m1_chr0 | 1); }
}

static void mmc1_write(uint16_t a, uint8_t v)
{
    if (v & 0x80) { m1_sr = 0; m1_cnt = 0; m1_ctrl |= 0x0C; mmc1_sync(); return; }
    m1_sr = (uint8_t)((m1_sr >> 1) | ((v & 1) << 4));
    if (++m1_cnt < 5) return;
    const uint8_t d = m1_sr;
    m1_sr = 0; m1_cnt = 0;
    switch ((a >> 13) & 3) {
    case 0: m1_ctrl = d; break;
    case 1: m1_chr0 = d; break;
    case 2: m1_chr1 = d; break;
    default: m1_prg = d; break;
    }
    mmc1_sync();
}

static void mmc3_sync(void)
{
    const unsigned last = n_prg8 - 1, last2 = n_prg8 - 2;
    const int inv = (m3_sel & 0x80) != 0;
    chr2(inv ? 4 : 0, m3_reg[0] & 0xFE);
    chr2(inv ? 6 : 2, m3_reg[1] & 0xFE);
    chr1(inv ? 0 : 4, m3_reg[2]); chr1(inv ? 1 : 5, m3_reg[3]);
    chr1(inv ? 2 : 6, m3_reg[4]); chr1(inv ? 3 : 7, m3_reg[5]);
    if (m3_sel & 0x40) { prg8(0, last2); prg8(2, m3_reg[6]); }
    else { prg8(0, m3_reg[6]); prg8(2, last2); }
    prg8(1, m3_reg[7]);
    prg8(3, last);
}

static void mmc3_write(uint16_t a, uint8_t v)
{
    switch (((a >> 13) & 3) * 2 + (a & 1)) {
    case 0: m3_sel = v; mmc3_sync(); break;
    case 1: m3_reg[m3_sel & 7] = v; mmc3_sync(); break;
    case 2: if (hdr_mirror != 4) mirror((v & 1) ? 0 : 1); break;
    case 3: break;                                          /* the RAM protect */
    case 4: m3_latch = v; break;
    case 5: m3_cnt = 0; m3_reload = 1; break;
    case 6: m3_en = 0; map_irq = 0; break;
    default: m3_en = 1; break;
    }
}

static void map_write(uint16_t a, uint8_t v)
{
    switch (mapper) {
    case 1: mmc1_write(a, v); break;
    case 2: prg16(0, v); break;
    case 3: chr8(v); break;
    case 4: mmc3_write(a, v); break;
    case 7: prg32(v & 7); mirror((v & 0x10) ? 3 : 2); break;
    case 11: prg32(v & 3); chr8(v >> 4); break;
    case 34: prg32(v); break;
    case 66: prg32((v >> 4) & 3); chr8(v & 3); break;
    case 71:
        if (a >= 0xC000) prg16(0, v);
        else if (a >= 0x9000 && a < 0xA000) mirror((v & 0x10) ? 3 : 2);
        break;
    default: break;
    }
}

static void map_low_write(uint16_t a, uint8_t v)         /* $4020-$5FFF */
{
    if (mapper == 79 && (a & 0xE100) == 0x4100) { prg32((v >> 3) & 1); chr8(v & 7); }
}

/* a scanline clock for the mapper's counter (the usual way: once a line while the picture is drawn) */
static void map_scanline(void)
{
    if (mapper != 4) return;
    if (m3_cnt == 0 || m3_reload) { m3_cnt = m3_latch; m3_reload = 0; }
    else m3_cnt--;
    if (m3_cnt == 0 && m3_en) map_irq = 1;
}

static void map_init(void)
{
    map_irq = 0;
    memset(vram, 0, sizeof vram);
    mirror(hdr_mirror);
    chr8(0);
    prg32(0);
    switch (mapper) {
    case 1: m1_sr = 0; m1_cnt = 0; m1_ctrl = 0x0C; m1_chr0 = m1_chr1 = m1_prg = 0; mmc1_sync(); break;
    case 2: case 71: prg16(0, 0); prg16(1, n_prg8 / 2 - 1); break;
    case 4:
        memset(m3_reg, 0, sizeof m3_reg);
        m3_reg[0] = 0; m3_reg[1] = 2; m3_reg[2] = 4; m3_reg[3] = 5; m3_reg[4] = 6; m3_reg[5] = 7; m3_reg[6] = 0; m3_reg[7] = 1;
        m3_sel = 0; m3_latch = 0; m3_cnt = 0; m3_reload = 0; m3_en = 0;
        mmc3_sync();
        break;
    case 7: mirror(2); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ the picture unit */
static uint16_t fb[NES_W * NES_H_PIX];
static uint8_t oam[256], pal[32];
static uint8_t p_ctrl, p_mask, p_status, p_oamaddr, p_rdbuf, p_x, p_w, p_bus;
static uint16_t p_v, p_t;
static int s0_pend, s0_dot;
static int dot, scanline;
static uint32_t frames, cpu_cycles;
static int nmi_pending;

static const uint32_t nes_rgb[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00, 0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00, 0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22, 0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5, 0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000 };
static uint16_t rgb565[64];

static inline unsigned pal_index(unsigned a)
{
    a &= 0x1F;
    return (a & 0x13) == 0x10 ? (a & 0x0F) : a;
}

static inline uint8_t ppu_rd(uint16_t a)
{
    a &= 0x3FFF;
    if (a < 0x2000) return chr_pg[a >> 10][a & 0x3FF];
    if (a < 0x3F00) return nt_pg[(a >> 10) & 3][a & 0x3FF];
    return pal[pal_index(a)];
}

static inline void ppu_wr(uint16_t a, uint8_t v)
{
    a &= 0x3FFF;
    if (a < 0x2000) { if (chr_is_ram) chr_pg[a >> 10][a & 0x3FF] = v; }
    else if (a < 0x3F00) nt_pg[(a >> 10) & 3][a & 0x3FF] = v;
    else pal[pal_index(a)] = v & 0x3F;
}

static uint8_t ppu_read(unsigned r)
{
    uint8_t ret = p_bus;
    switch (r) {
    case 2:
        if (s0_pend && dot >= s0_dot) { p_status |= 0x40; s0_pend = 0; }
        ret = (uint8_t)((p_status & 0xE0) | (p_bus & 0x1F));
        p_status &= 0x7F;
        p_w = 0;
        break;
    case 4: ret = oam[p_oamaddr]; break;
    case 7:
        if ((p_v & 0x3FFF) >= 0x3F00) {
            ret = ppu_rd(p_v);
            p_rdbuf = nt_pg[(p_v >> 10) & 3][p_v & 0x3FF];
        } else {
            ret = p_rdbuf;
            p_rdbuf = ppu_rd(p_v);
        }
        p_v = (uint16_t)(p_v + ((p_ctrl & 4) ? 32 : 1));
        break;
    default: break;
    }
    p_bus = ret;
    return ret;
}

static void ppu_write(unsigned r, uint8_t val)
{
    p_bus = val;
    switch (r) {
    case 0:
        if (!(p_ctrl & 0x80) && (val & 0x80) && (p_status & 0x80)) nmi_pending = 1;
        p_ctrl = val;
        p_t = (uint16_t)((p_t & ~0x0C00) | ((val & 3) << 10));
        break;
    case 1: p_mask = val; break;
    case 3: p_oamaddr = val; break;
    case 4: oam[p_oamaddr++] = val; break;
    case 5:
        if (!p_w) { p_t = (uint16_t)((p_t & ~0x1F) | (val >> 3)); p_x = val & 7; p_w = 1; }
        else { p_t = (uint16_t)((p_t & ~0x73E0) | ((val & 7) << 12) | ((val & 0xF8) << 2)); p_w = 0; }
        break;
    case 6:
        if (!p_w) { p_t = (uint16_t)((p_t & 0x00FF) | ((val & 0x3F) << 8)); p_w = 1; }
        else { p_t = (uint16_t)((p_t & 0xFF00) | val); p_v = p_t; p_w = 0; }
        break;
    case 7:
        ppu_wr(p_v, val);
        p_v = (uint16_t)(p_v + ((p_ctrl & 4) ? 32 : 1));
        break;
    default: break;
    }
}

/* draws the line ln into fb (draw 0: only what the game can see, sprite 0 hit and the overflow flag) */
static void ppu_line(int ln, int draw)
{
    static uint8_t bgbuf[264 + 8], spr[256];
    const int bgon = (p_mask & 8) != 0, spon = (p_mask & 0x10) != 0;
    const int h = (p_ctrl & 0x20) ? 16 : 8;
    uint8_t idx[8];
    int cnt = 0;
    if (spon) {
        for (int i = 0; i < 64; i++) {
            const int row = ln - 1 - oam[i * 4];
            if (row < 0 || row >= h) continue;
            if (cnt < 8) idx[cnt++] = (uint8_t)i;
            else { p_status |= 0x20; break; }
        }
    }
    const int s0 = cnt > 0 && idx[0] == 0 && bgon && !(p_status & 0x40) && !s0_pend;
    uint16_t pal565[32];
    if (draw) {
        const uint8_t grey = (p_mask & 1) ? 0x30 : 0x3F;
        for (int i = 0; i < 32; i++) pal565[i] = rgb565[pal[i] & grey];
    }
    if (!bgon && !spon) {
        if (draw) { uint16_t *d = fb + ln * NES_W; for (int x = 0; x < NES_W; x++) d[x] = pal565[0]; }
        return;
    }
    if (!draw && !s0) return;
    /* the background: 33 tiles from the loopy register v, shifted by the fine x */
    memset(bgbuf, 0, sizeof bgbuf);
    if (bgon) {
        unsigned vv = p_v;
        const unsigned bgtab = (p_ctrl & 0x10) ? 0x1000u : 0u;
        for (int tile = 0; tile < 33; tile++) {
            const uint8_t *nt = nt_pg[(vv >> 10) & 3];
            const unsigned tn = nt[vv & 0x3FF];
            const unsigned at = nt[0x3C0 + ((vv >> 4) & 0x38) + ((vv >> 2) & 7)];
            const unsigned pl = ((at >> (((vv >> 4) & 4) | (vv & 2))) & 3) << 2;
            const unsigned a = bgtab + tn * 16 + ((vv >> 12) & 7);
            const uint8_t *pg = chr_pg[a >> 10];
            const unsigned lo = pg[a & 0x3FF], hi = pg[(a & 0x3FF) + 8];
            uint8_t *o = bgbuf + tile * 8;
            for (int b = 0; b < 8; b++) {
                const unsigned px = ((lo >> (7 - b)) & 1) | (((hi >> (7 - b)) & 1) << 1);
                o[b] = px ? (uint8_t)(pl | px) : 0;
            }
            if ((vv & 0x1F) == 31) vv = (vv & ~0x1Fu) ^ 0x400u; else vv++;
        }
    }
    const uint8_t *bg = bgbuf + p_x;
    /* the sprites, the last first so that a lower number wins */
    memset(spr, 0, sizeof spr);
    const unsigned sptab = (p_ctrl & 8) ? 0x1000u : 0u;
    for (int k = cnt - 1; k >= 0; k--) {
        const unsigned i = idx[k];
        const unsigned sy = oam[i * 4], tile = oam[i * 4 + 1], attr = oam[i * 4 + 2], sx = oam[i * 4 + 3];
        int row = ln - 1 - (int)sy;
        if (attr & 0x80) row = h - 1 - row;
        unsigned a;
        if (h == 8) a = sptab + tile * 16 + (unsigned)row;
        else a = ((tile & 1) << 12) + (tile & 0xFE) * 16 + (row >= 8 ? 16u : 0u) + (unsigned)(row & 7);
        const uint8_t *pg = chr_pg[(a & 0x1FFF) >> 10];
        const unsigned lo = pg[a & 0x3FF], hi = pg[(a & 0x3FF) + 8];
        for (int b = 0; b < 8; b++) {
            const int col = (attr & 0x40) ? b : 7 - b;
            const unsigned px = ((lo >> col) & 1) | (((hi >> col) & 1) << 1);
            const unsigned x = sx + (unsigned)b;
            if (!px || x >= 256) continue;
            if (x < 8 && !(p_mask & 4)) continue;
            spr[x] = (uint8_t)(px | ((attr & 3) << 2) | ((attr & 0x20) ? 0x40 : 0));
            if (i == 0 && s0 && x < 255 && !s0_pend) {
                const int bpx = (x < 8 && !(p_mask & 2)) ? 0 : bg[x] & 3;
                if (bpx) { s0_pend = 1; s0_dot = (int)x + 2; }
            }
        }
    }
    if (!draw) return;
    uint16_t *d = fb + ln * NES_W;
    const int bgl = (p_mask & 2) != 0;
    for (int x = 0; x < NES_W; x++) {
        unsigned b = bg[x];
        if (x < 8 && !bgl) b = 0;
        const unsigned s = spr[x];
        if ((s & 3) && (!(s & 0x40) || !(b & 3))) d[x] = pal565[0x10 | (s & 0x0F)];
        else if (b & 3) d[x] = pal565[b & 0x0F];
        else d[x] = pal565[0];
    }
}

static void ppu_yinc(void)
{
    if ((p_v & 0x7000) != 0x7000) { p_v = (uint16_t)(p_v + 0x1000); return; }
    p_v &= ~0x7000;
    unsigned y = (p_v & 0x3E0) >> 5;
    if (y == 29) { y = 0; p_v ^= 0x800; }
    else if (y == 31) y = 0;
    else y++;
    p_v = (uint16_t)((p_v & ~0x3E0) | (y << 5));
}

/* ------------------------------------------------------------------ the sound */
static const uint8_t len_tab[32] = { 10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14, 12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30 };
static const uint8_t duty_tab[4][8] = { { 0, 1, 0, 0, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 0, 0, 0 }, { 0, 1, 1, 1, 1, 0, 0, 0 }, { 1, 0, 0, 1, 1, 1, 1, 1 } };
static const uint16_t noise_tab[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
static const uint16_t dmc_tab[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
static const uint8_t tri_tab[32] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

typedef struct {
    uint8_t en, duty, halt, constv, vol, env_start, env_div, env_decay, sw_en, sw_per, sw_neg, sw_shift, sw_div, sw_reload, seq, len;
    uint16_t period;
    int timer;
} pulse_t;
static pulse_t pu[2];
static uint8_t tri_en, tri_ctrl, tri_rv, tri_lin, tri_rflag, tri_seq, tri_len;
static uint16_t tri_period;
static int tri_timer;
static uint8_t nz_en, nz_halt, nz_constv, nz_vol, nz_env_start, nz_env_div, nz_env_decay, nz_mode, nz_len;
static uint16_t nz_period, nz_lfsr = 1;
static int nz_timer;
static struct {
    uint8_t irq_en, loop, level, buf, empty, shift, bits, silence, irq;
    uint16_t rate, addr, cur, len, remaining;
    int timer;
} dmc;
static int apu_frame_irq, fc_mode, fc_inhibit, fc_cnt, fc_step, fc_next, apu_odd, stall;
static int cur_mix, acc_sum, acc_n, hp_prev_in, hp_prev_out;
static int frac;
static int pulse_tab[31], tnd_tab[203];

/* the output ring: mono samples at 48 kHz */
#define RING 8192
static int16_t ring[RING];
static volatile uint32_t ring_rd, ring_wr;
static uint32_t n_under, n_over;

static const uint16_t ev_cyc[2][6] = { { 7457, 14913, 22371, 29829, 29830, 0 }, { 7457, 14913, 22371, 29829, 37281, 37282 } };
static const uint8_t ev_act[2][6] = { { 1, 3, 1, 3 | 4, 8, 0 }, { 1, 3, 1, 0, 3, 8 } };

static unsigned sweep_target(const pulse_t *p, int ones)
{
    const unsigned ch = p->period >> p->sw_shift;
    return p->sw_neg ? (unsigned)p->period - ch - (ones ? 1u : 0u) : (unsigned)p->period + ch;
}

static int pulse_out(const pulse_t *p, int ones)
{
    if (!p->en || !p->len || !duty_tab[p->duty][p->seq] || p->period < 8) return 0;
    if (!p->sw_neg && sweep_target(p, ones) > 0x7FF) return 0;
    return p->constv ? p->vol : p->env_decay;
}

static void remix(void)
{
    int t = tri_tab[tri_seq];
    int n = (nz_en && nz_len && !(nz_lfsr & 1)) ? (nz_constv ? nz_vol : nz_env_decay) : 0;
    cur_mix = pulse_tab[pulse_out(&pu[0], 1) + pulse_out(&pu[1], 0)] + tnd_tab[3 * t + 2 * n + dmc.level];
}

static void env_clock(uint8_t *start, uint8_t *div, uint8_t *decay, uint8_t vol, uint8_t halt)
{
    if (*start) { *start = 0; *decay = 15; *div = vol; }
    else if (*div == 0) { *div = vol; if (*decay) (*decay)--; else if (halt) *decay = 15; }
    else (*div)--;
}

static void sweep_clock(pulse_t *p, int ones)
{
    const unsigned tgt = sweep_target(p, ones);
    if (p->sw_div == 0 && p->sw_en && p->sw_shift && p->period >= 8 && tgt <= 0x7FF) p->period = (uint16_t)tgt;
    if (p->sw_div == 0 || p->sw_reload) { p->sw_div = p->sw_per; p->sw_reload = 0; }
    else p->sw_div--;
}

static void fc_quarter(void)
{
    for (int i = 0; i < 2; i++) env_clock(&pu[i].env_start, &pu[i].env_div, &pu[i].env_decay, pu[i].vol, pu[i].halt);
    env_clock(&nz_env_start, &nz_env_div, &nz_env_decay, nz_vol, nz_halt);
    if (tri_rflag) tri_lin = tri_rv; else if (tri_lin) tri_lin--;
    if (!tri_ctrl) tri_rflag = 0;
}

static void fc_half(void)
{
    for (int i = 0; i < 2; i++) { if (pu[i].len && !pu[i].halt) pu[i].len--; sweep_clock(&pu[i], i == 0); }
    if (tri_len && !tri_ctrl) tri_len--;
    if (nz_len && !nz_halt) nz_len--;
}

static uint8_t rd(uint16_t a);
static void dmc_fetch(void)
{
    if (!dmc.empty || !dmc.remaining) return;
    dmc.buf = rd(dmc.cur);
    dmc.empty = 0;
    stall += 4;
    dmc.cur = dmc.cur == 0xFFFF ? 0x8000 : (uint16_t)(dmc.cur + 1);
    if (--dmc.remaining == 0) {
        if (dmc.loop) { dmc.cur = dmc.addr; dmc.remaining = dmc.len; }
        else if (dmc.irq_en) dmc.irq = 1;
    }
}

static void ring_push(int s)
{
    if (ring_wr - ring_rd >= RING) { n_over++; return; }
    ring[ring_wr & (RING - 1)] = (int16_t)s;
    ring_wr++;
}

static void fc_event(void)
{
    const uint8_t act = ev_act[fc_mode][fc_step];
    if (act & 1) fc_quarter();
    if (act & 2) fc_half();
    if ((act & 4) && !fc_inhibit) apu_frame_irq = 1;
    if (act & 8) { fc_cnt = 0; fc_step = 0; } else fc_step++;
    fc_next = ev_cyc[fc_mode][fc_step];
    remix();
}

static void apu_run(int n)
{
    while (n-- > 0) {
        int dirty = 0;
        if (++fc_cnt >= fc_next) fc_event();
        if (--tri_timer < 0) {
            tri_timer = tri_period;
            if (tri_lin && tri_len && tri_period >= 2) { tri_seq = (tri_seq + 1) & 31; dirty = 1; }
        }
        if (--nz_timer <= 0) {
            nz_timer += nz_period;
            const unsigned fb2 = (nz_lfsr & 1) ^ ((nz_lfsr >> (nz_mode ? 6 : 1)) & 1);
            nz_lfsr = (uint16_t)((nz_lfsr >> 1) | (fb2 << 14));
            dirty = 1;
        }
        if (--dmc.timer <= 0) {
            dmc.timer += dmc.rate;
            if (!dmc.silence) {
                if (dmc.shift & 1) { if (dmc.level <= 125) dmc.level += 2; }
                else if (dmc.level >= 2) dmc.level -= 2;
                dirty = 1;
            }
            dmc.shift >>= 1;
            if (--dmc.bits == 0) {
                dmc.bits = 8;
                if (dmc.empty) dmc.silence = 1;
                else { dmc.silence = 0; dmc.shift = dmc.buf; dmc.empty = 1; dmc_fetch(); }
            }
        }
        apu_odd ^= 1;
        if (apu_odd) {
            for (int i = 0; i < 2; i++) {
                if (--pu[i].timer < 0) { pu[i].timer = pu[i].period; pu[i].seq = (pu[i].seq + 1) & 7; dirty = 1; }
            }
        }
        if (dirty) remix();
        acc_sum += cur_mix; acc_n++;
        frac += 48000;
        if (frac >= 1789773) {
            frac -= 1789773;
            const int x = acc_sum / acc_n;
            acc_sum = 0; acc_n = 0;
            const int y = x - hp_prev_in + ((hp_prev_out * 32604) >> 15);   /* a high-pass filter: no steady level */
            hp_prev_in = x; hp_prev_out = y;
            ring_push(y);
        }
    }
}

static void apu_write(unsigned r, uint8_t v)
{
    if (r < 8) {
        pulse_t *p = &pu[r >> 2];
        switch (r & 3) {
        case 0: p->duty = v >> 6; p->halt = (v >> 5) & 1; p->constv = (v >> 4) & 1; p->vol = v & 15; break;
        case 1: p->sw_en = v >> 7; p->sw_per = (v >> 4) & 7; p->sw_neg = (v >> 3) & 1; p->sw_shift = v & 7; p->sw_reload = 1; break;
        case 2: p->period = (uint16_t)((p->period & 0x700) | v); break;
        default:
            p->period = (uint16_t)((p->period & 0xFF) | ((v & 7) << 8));
            if (p->en) p->len = len_tab[v >> 3];
            p->seq = 0; p->env_start = 1;
            break;
        }
    } else if (r < 12) {
        switch (r & 3) {
        case 0: tri_ctrl = v >> 7; tri_rv = v & 0x7F; break;
        case 2: tri_period = (uint16_t)((tri_period & 0x700) | v); break;
        case 3:
            tri_period = (uint16_t)((tri_period & 0xFF) | ((v & 7) << 8));
            if (tri_en) tri_len = len_tab[v >> 3];
            tri_rflag = 1;
            break;
        default: break;
        }
    } else if (r < 16) {
        switch (r & 3) {
        case 0: nz_halt = (v >> 5) & 1; nz_constv = (v >> 4) & 1; nz_vol = v & 15; break;
        case 2: nz_mode = v >> 7; nz_period = noise_tab[v & 15]; break;
        case 3: if (nz_en) nz_len = len_tab[v >> 3]; nz_env_start = 1; break;
        default: break;
        }
    } else {
        switch (r - 16) {
        case 0: dmc.irq_en = v >> 7; dmc.loop = (v >> 6) & 1; dmc.rate = dmc_tab[v & 15]; if (!dmc.irq_en) dmc.irq = 0; break;
        case 1: dmc.level = v & 0x7F; break;
        case 2: dmc.addr = (uint16_t)(0xC000 | (v << 6)); break;
        default: dmc.len = (uint16_t)((v << 4) | 1); break;
        }
    }
    remix();
}

static void apu_status_write(uint8_t v)
{
    pu[0].en = v & 1; pu[1].en = (v >> 1) & 1; tri_en = (v >> 2) & 1; nz_en = (v >> 3) & 1;
    if (!pu[0].en) pu[0].len = 0;
    if (!pu[1].en) pu[1].len = 0;
    if (!tri_en) tri_len = 0;
    if (!nz_en) nz_len = 0;
    dmc.irq = 0;
    if (!((v >> 4) & 1)) dmc.remaining = 0;
    else if (!dmc.remaining) { dmc.cur = dmc.addr; dmc.remaining = dmc.len; dmc_fetch(); }
    remix();
}

static uint8_t apu_status_read(void)
{
    const uint8_t r = (uint8_t)((pu[0].len > 0) | ((pu[1].len > 0) << 1) | ((tri_len > 0) << 2) | ((nz_len > 0) << 3) |
                                ((dmc.remaining > 0) << 4) | (apu_frame_irq << 6) | (dmc.irq << 7));
    apu_frame_irq = 0;
    return r;
}

static void apu_frame_write(uint8_t v)
{
    fc_mode = v >> 7;
    fc_inhibit = (v >> 6) & 1;
    if (fc_inhibit) apu_frame_irq = 0;
    fc_cnt = 0; fc_step = 0; fc_next = ev_cyc[fc_mode][0];
    if (fc_mode) { fc_quarter(); fc_half(); }
    remix();
}

static void apu_reset(void)
{
    memset(pu, 0, sizeof pu);
    tri_en = tri_ctrl = tri_rv = tri_lin = tri_rflag = tri_seq = tri_len = 0; tri_period = 0; tri_timer = 0;
    nz_en = nz_halt = nz_constv = nz_vol = nz_env_start = nz_env_div = nz_env_decay = nz_mode = nz_len = 0;
    nz_period = noise_tab[0]; nz_lfsr = 1; nz_timer = nz_period;
    memset(&dmc, 0, sizeof dmc);
    dmc.rate = dmc_tab[0]; dmc.timer = dmc.rate; dmc.bits = 8; dmc.empty = 1; dmc.silence = 1;
    apu_frame_irq = 0; fc_mode = 0; fc_inhibit = 0; fc_cnt = 0; fc_step = 0; fc_next = ev_cyc[0][0]; apu_odd = 0;
    acc_sum = 0; acc_n = 0; frac = 0; hp_prev_in = hp_prev_out = 0;
    ring_rd = ring_wr = 0;
    remix();
}

int nes_audio_mix(float *out, unsigned n, float gain)
{
    int sound = 0;
    static float last;
    const float k = gain * (0.9f / 16384.0f);
    if (ring_wr - ring_rd > 3 * 1024) ring_rd = ring_wr - 1024;       /* a delay the game does not need: keep the last 20 ms */
    for (unsigned i = 0; i < n; i++) {
        if (ring_rd != ring_wr) last = ring[ring_rd++ & (RING - 1)];
        else { n_under++; last *= 0.99f; }                              /* nothing came: fade out */
        out[2 * i] += last * k;
        out[2 * i + 1] += last * k;
        if (last > 1.0f || last < -1.0f) sound = 1;
    }
    return sound;
}

/* ------------------------------------------------------------------ the CPU bus */
static uint8_t ram[2048];
static uint8_t pads[2], pad_shift[2], strobe;

static uint8_t rd_slow(uint16_t a)
{
    if (a < 0x4000) return ppu_read(a & 7);
    if (a == 0x4015) return apu_status_read();
    if (a == 0x4016 || a == 0x4017) {
        const int i = a & 1;
        uint8_t v;
        if (strobe) v = pads[i] & 1;
        else { v = pad_shift[i] & 1; pad_shift[i] = (uint8_t)((pad_shift[i] >> 1) | 0x80); }
        return (uint8_t)(v | 0x40);
    }
    if (a >= 0x6000 && a < 0x8000) return prg_ram[a & 0x1FFF];
    return 0;
}

static inline uint8_t rd(uint16_t a)
{
    if (a < 0x2000) return ram[a & 0x7FF];
    if (a >= 0x8000) return prg_pg[(a >> 13) & 3][a & 0x1FFF];
    return rd_slow(a);
}

static void wr_slow(uint16_t a, uint8_t v)
{
    if (a < 0x4000) { ppu_write(a & 7, v); return; }
    if (a < 0x4014 || a == 0x4015 || a == 0x4017) {
        if (a == 0x4015) apu_status_write(v);
        else if (a == 0x4017) apu_frame_write(v);
        else apu_write(a - 0x4000, v);
        return;
    }
    if (a == 0x4014) {
        for (int i = 0; i < 256; i++) oam[(uint8_t)(p_oamaddr + i)] = rd((uint16_t)((v << 8) | i));
        stall += 513 + (int)(cpu_cycles & 1);
        return;
    }
    if (a == 0x4016) { strobe = v & 1; pad_shift[0] = pads[0]; pad_shift[1] = pads[1]; return; }
    if (a < 0x6000) { map_low_write(a, v); return; }
    if (a < 0x8000) {
        if (battery && prg_ram[a & 0x1FFF] != v) sram_dirty = 1;
        prg_ram[a & 0x1FFF] = v;
        return;
    }
}

static inline void wr(uint16_t a, uint8_t v)
{
    if (a < 0x2000) { ram[a & 0x7FF] = v; return; }
    if (a >= 0x8000) { map_write(a, v); return; }
    wr_slow(a, v);
}

/* ------------------------------------------------------------------ the CPU */
static uint8_t rA, rX, rY, rS, fC, fZ, fI, fD, fV, fN;
static uint16_t rPC;
static int jammed;

static const uint8_t cyc_tab[256] = {
    7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,  2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,  2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7 };

static inline void setnz(uint8_t v) { fZ = v == 0; fN = v >> 7; }
static inline void push(uint8_t v) { ram[0x100 | rS] = v; rS--; }
static inline uint8_t pull(void) { rS++; return ram[0x100 | rS]; }
static inline uint8_t get_p(void) { return (uint8_t)((fN << 7) | (fV << 6) | 0x20 | (fD << 3) | (fI << 2) | (fZ << 1) | fC); }
static inline void set_p(uint8_t p) { fN = p >> 7; fV = (p >> 6) & 1; fD = (p >> 3) & 1; fI = (p >> 2) & 1; fZ = (p >> 1) & 1; fC = p & 1; }
static inline uint16_t rd16(uint16_t a) { return (uint16_t)(rd(a) | (rd((uint16_t)(a + 1)) << 8)); }

static inline void adc(uint8_t m)
{
    const unsigned s = (unsigned)rA + m + fC;
    fV = (uint8_t)((~(rA ^ m) & (rA ^ s) & 0x80) != 0);
    fC = s > 0xFF;
    rA = (uint8_t)s;
    setnz(rA);
}
static inline void cmp(uint8_t r, uint8_t m) { fC = r >= m; setnz((uint8_t)(r - m)); }
static inline uint8_t asl(uint8_t v) { fC = v >> 7; v = (uint8_t)(v << 1); setnz(v); return v; }
static inline uint8_t lsr(uint8_t v) { fC = v & 1; v >>= 1; setnz(v); return v; }
static inline uint8_t rol(uint8_t v) { const uint8_t c = fC; fC = v >> 7; v = (uint8_t)((v << 1) | c); setnz(v); return v; }
static inline uint8_t ror(uint8_t v) { const uint8_t c = fC; fC = v & 1; v = (uint8_t)((v >> 1) | (c << 7)); setnz(v); return v; }

static void interrupt(uint16_t vec, int brk)
{
    push((uint8_t)(rPC >> 8)); push((uint8_t)rPC);
    push((uint8_t)(get_p() | (brk ? 0x10 : 0)));
    fI = 1;
    rPC = rd16(vec);
}

/* the addressing modes: they leave the address in ea; the read modes add a cycle when the index crosses a page */
#define IMM()   ea = rPC++
#define ZP()    ea = rd(rPC++)
#define ZPX()   ea = (uint8_t)(rd(rPC++) + rX)
#define ZPY()   ea = (uint8_t)(rd(rPC++) + rY)
#define ABS()   do { ea = rd16(rPC); rPC += 2; } while (0)
#define ABXR()  do { base = rd16(rPC); rPC += 2; ea = (uint16_t)(base + rX); if ((base ^ ea) & 0xFF00) cyc++; } while (0)
#define ABXW()  do { base = rd16(rPC); rPC += 2; ea = (uint16_t)(base + rX); } while (0)
#define ABYR()  do { base = rd16(rPC); rPC += 2; ea = (uint16_t)(base + rY); if ((base ^ ea) & 0xFF00) cyc++; } while (0)
#define ABYW()  do { base = rd16(rPC); rPC += 2; ea = (uint16_t)(base + rY); } while (0)
#define INX()   do { const uint8_t z = (uint8_t)(rd(rPC++) + rX); ea = (uint16_t)(rd(z) | (rd((uint8_t)(z + 1)) << 8)); } while (0)
#define INYR()  do { const uint8_t z = rd(rPC++); base = (uint16_t)(rd(z) | (rd((uint8_t)(z + 1)) << 8)); ea = (uint16_t)(base + rY); if ((base ^ ea) & 0xFF00) cyc++; } while (0)
#define INYW()  do { const uint8_t z = rd(rPC++); base = (uint16_t)(rd(z) | (rd((uint8_t)(z + 1)) << 8)); ea = (uint16_t)(base + rY); } while (0)

#define LDA()   do { rA = rd(ea); setnz(rA); } while (0)
#define LDX()   do { rX = rd(ea); setnz(rX); } while (0)
#define LDY()   do { rY = rd(ea); setnz(rY); } while (0)
#define ORA()   do { rA |= rd(ea); setnz(rA); } while (0)
#define AND()   do { rA &= rd(ea); setnz(rA); } while (0)
#define EOR()   do { rA ^= rd(ea); setnz(rA); } while (0)
#define ADC()   adc(rd(ea))
#define SBC()   adc((uint8_t)~rd(ea))
#define CMP()   cmp(rA, rd(ea))
#define CPX()   cmp(rX, rd(ea))
#define CPY()   cmp(rY, rd(ea))
#define RMW(f)  wr(ea, f(rd(ea)))
#define BR(c)   do { const int8_t o = (int8_t)rd(rPC++); if (c) { cyc++; if (((rPC + o) ^ rPC) & 0xFF00) cyc++; rPC = (uint16_t)(rPC + o); } } while (0)
/* the unofficial opcodes */
#define LAX()   do { rA = rX = rd(ea); setnz(rA); } while (0)
#define SAX()   wr(ea, rA & rX)
#define DCP()   do { const uint8_t m = (uint8_t)(rd(ea) - 1); wr(ea, m); cmp(rA, m); } while (0)
#define ISB()   do { const uint8_t m = (uint8_t)(rd(ea) + 1); wr(ea, m); adc((uint8_t)~m); } while (0)
#define SLO()   do { const uint8_t m = asl(rd(ea)); wr(ea, m); rA |= m; setnz(rA); } while (0)
#define RLA()   do { const uint8_t m = rol(rd(ea)); wr(ea, m); rA &= m; setnz(rA); } while (0)
#define SRE()   do { const uint8_t m = lsr(rd(ea)); wr(ea, m); rA ^= m; setnz(rA); } while (0)
#define RRA()   do { const uint8_t m = ror(rd(ea)); wr(ea, m); adc(m); } while (0)

static int cpu_step(void)
{
    uint16_t ea = 0, base;
    int cyc;
    if (jammed) return 2;
    if (nmi_pending) { nmi_pending = 0; interrupt(0xFFFA, 0); return 7; }
    if (!fI && (apu_frame_irq || dmc.irq || map_irq)) { interrupt(0xFFFE, 0); return 7; }
    const uint8_t op = rd(rPC++);
    cyc = cyc_tab[op];
    switch (op) {
    case 0x00: rPC++; interrupt(0xFFFE, 1); break;
    case 0x01: INX(); ORA(); break;
    case 0x05: ZP(); ORA(); break;
    case 0x09: IMM(); ORA(); break;
    case 0x0D: ABS(); ORA(); break;
    case 0x11: INYR(); ORA(); break;
    case 0x15: ZPX(); ORA(); break;
    case 0x19: ABYR(); ORA(); break;
    case 0x1D: ABXR(); ORA(); break;
    case 0x21: INX(); AND(); break;
    case 0x25: ZP(); AND(); break;
    case 0x29: IMM(); AND(); break;
    case 0x2D: ABS(); AND(); break;
    case 0x31: INYR(); AND(); break;
    case 0x35: ZPX(); AND(); break;
    case 0x39: ABYR(); AND(); break;
    case 0x3D: ABXR(); AND(); break;
    case 0x41: INX(); EOR(); break;
    case 0x45: ZP(); EOR(); break;
    case 0x49: IMM(); EOR(); break;
    case 0x4D: ABS(); EOR(); break;
    case 0x51: INYR(); EOR(); break;
    case 0x55: ZPX(); EOR(); break;
    case 0x59: ABYR(); EOR(); break;
    case 0x5D: ABXR(); EOR(); break;
    case 0x61: INX(); ADC(); break;
    case 0x65: ZP(); ADC(); break;
    case 0x69: IMM(); ADC(); break;
    case 0x6D: ABS(); ADC(); break;
    case 0x71: INYR(); ADC(); break;
    case 0x75: ZPX(); ADC(); break;
    case 0x79: ABYR(); ADC(); break;
    case 0x7D: ABXR(); ADC(); break;
    case 0xE1: INX(); SBC(); break;
    case 0xE5: ZP(); SBC(); break;
    case 0xE9: case 0xEB: IMM(); SBC(); break;
    case 0xED: ABS(); SBC(); break;
    case 0xF1: INYR(); SBC(); break;
    case 0xF5: ZPX(); SBC(); break;
    case 0xF9: ABYR(); SBC(); break;
    case 0xFD: ABXR(); SBC(); break;
    case 0xC1: INX(); CMP(); break;
    case 0xC5: ZP(); CMP(); break;
    case 0xC9: IMM(); CMP(); break;
    case 0xCD: ABS(); CMP(); break;
    case 0xD1: INYR(); CMP(); break;
    case 0xD5: ZPX(); CMP(); break;
    case 0xD9: ABYR(); CMP(); break;
    case 0xDD: ABXR(); CMP(); break;
    case 0xE0: IMM(); CPX(); break;
    case 0xE4: ZP(); CPX(); break;
    case 0xEC: ABS(); CPX(); break;
    case 0xC0: IMM(); CPY(); break;
    case 0xC4: ZP(); CPY(); break;
    case 0xCC: ABS(); CPY(); break;
    case 0xA1: INX(); LDA(); break;
    case 0xA5: ZP(); LDA(); break;
    case 0xA9: IMM(); LDA(); break;
    case 0xAD: ABS(); LDA(); break;
    case 0xB1: INYR(); LDA(); break;
    case 0xB5: ZPX(); LDA(); break;
    case 0xB9: ABYR(); LDA(); break;
    case 0xBD: ABXR(); LDA(); break;
    case 0xA2: IMM(); LDX(); break;
    case 0xA6: ZP(); LDX(); break;
    case 0xAE: ABS(); LDX(); break;
    case 0xB6: ZPY(); LDX(); break;
    case 0xBE: ABYR(); LDX(); break;
    case 0xA0: IMM(); LDY(); break;
    case 0xA4: ZP(); LDY(); break;
    case 0xAC: ABS(); LDY(); break;
    case 0xB4: ZPX(); LDY(); break;
    case 0xBC: ABXR(); LDY(); break;
    case 0x81: INX(); wr(ea, rA); break;
    case 0x85: ZP(); wr(ea, rA); break;
    case 0x8D: ABS(); wr(ea, rA); break;
    case 0x91: INYW(); wr(ea, rA); break;
    case 0x95: ZPX(); wr(ea, rA); break;
    case 0x99: ABYW(); wr(ea, rA); break;
    case 0x9D: ABXW(); wr(ea, rA); break;
    case 0x86: ZP(); wr(ea, rX); break;
    case 0x8E: ABS(); wr(ea, rX); break;
    case 0x96: ZPY(); wr(ea, rX); break;
    case 0x84: ZP(); wr(ea, rY); break;
    case 0x8C: ABS(); wr(ea, rY); break;
    case 0x94: ZPX(); wr(ea, rY); break;
    case 0x0A: rA = asl(rA); break;
    case 0x06: ZP(); RMW(asl); break;
    case 0x0E: ABS(); RMW(asl); break;
    case 0x16: ZPX(); RMW(asl); break;
    case 0x1E: ABXW(); RMW(asl); break;
    case 0x4A: rA = lsr(rA); break;
    case 0x46: ZP(); RMW(lsr); break;
    case 0x4E: ABS(); RMW(lsr); break;
    case 0x56: ZPX(); RMW(lsr); break;
    case 0x5E: ABXW(); RMW(lsr); break;
    case 0x2A: rA = rol(rA); break;
    case 0x26: ZP(); RMW(rol); break;
    case 0x2E: ABS(); RMW(rol); break;
    case 0x36: ZPX(); RMW(rol); break;
    case 0x3E: ABXW(); RMW(rol); break;
    case 0x6A: rA = ror(rA); break;
    case 0x66: ZP(); RMW(ror); break;
    case 0x6E: ABS(); RMW(ror); break;
    case 0x76: ZPX(); RMW(ror); break;
    case 0x7E: ABXW(); RMW(ror); break;
    case 0xE6: ZP(); wr(ea, (uint8_t)(rd(ea) + 1)); setnz(rd(ea)); break;
    case 0xEE: ABS(); wr(ea, (uint8_t)(rd(ea) + 1)); setnz(rd(ea)); break;
    case 0xF6: ZPX(); wr(ea, (uint8_t)(rd(ea) + 1)); setnz(rd(ea)); break;
    case 0xFE: ABXW(); wr(ea, (uint8_t)(rd(ea) + 1)); setnz(rd(ea)); break;
    case 0xC6: ZP(); wr(ea, (uint8_t)(rd(ea) - 1)); setnz(rd(ea)); break;
    case 0xCE: ABS(); wr(ea, (uint8_t)(rd(ea) - 1)); setnz(rd(ea)); break;
    case 0xD6: ZPX(); wr(ea, (uint8_t)(rd(ea) - 1)); setnz(rd(ea)); break;
    case 0xDE: ABXW(); wr(ea, (uint8_t)(rd(ea) - 1)); setnz(rd(ea)); break;
    case 0xE8: rX++; setnz(rX); break;
    case 0xC8: rY++; setnz(rY); break;
    case 0xCA: rX--; setnz(rX); break;
    case 0x88: rY--; setnz(rY); break;
    case 0x24: ZP(); { const uint8_t m = rd(ea); fZ = (rA & m) == 0; fV = (m >> 6) & 1; fN = m >> 7; } break;
    case 0x2C: ABS(); { const uint8_t m = rd(ea); fZ = (rA & m) == 0; fV = (m >> 6) & 1; fN = m >> 7; } break;
    case 0x10: BR(!fN); break;
    case 0x30: BR(fN); break;
    case 0x50: BR(!fV); break;
    case 0x70: BR(fV); break;
    case 0x90: BR(!fC); break;
    case 0xB0: BR(fC); break;
    case 0xD0: BR(!fZ); break;
    case 0xF0: BR(fZ); break;
    case 0x4C: rPC = rd16(rPC); break;
    case 0x6C: {
        const uint16_t p = rd16(rPC);
        rPC = (uint16_t)(rd(p) | (rd((uint16_t)((p & 0xFF00) | ((p + 1) & 0xFF))) << 8));      /* the page wrap of the 6502 */
        break;
    }
    case 0x20: ea = rd16(rPC); rPC++; push((uint8_t)(rPC >> 8)); push((uint8_t)rPC); rPC = ea; break;
    case 0x60: { const uint8_t lo = pull(); rPC = (uint16_t)((lo | (pull() << 8)) + 1); break; }
    case 0x40: { set_p(pull()); const uint8_t lo = pull(); rPC = (uint16_t)(lo | (pull() << 8)); break; }
    case 0x48: push(rA); break;
    case 0x68: rA = pull(); setnz(rA); break;
    case 0x08: push((uint8_t)(get_p() | 0x10)); break;
    case 0x28: set_p(pull()); break;
    case 0xAA: rX = rA; setnz(rX); break;
    case 0x8A: rA = rX; setnz(rA); break;
    case 0xA8: rY = rA; setnz(rY); break;
    case 0x98: rA = rY; setnz(rA); break;
    case 0xBA: rX = rS; setnz(rX); break;
    case 0x9A: rS = rX; break;
    case 0x18: fC = 0; break;
    case 0x38: fC = 1; break;
    case 0x58: fI = 0; break;
    case 0x78: fI = 1; break;
    case 0xB8: fV = 0; break;
    case 0xD8: fD = 0; break;
    case 0xF8: fD = 1; break;
    case 0xEA: break;
    /* unofficial */
    case 0xA3: INX(); LAX(); break;
    case 0xA7: ZP(); LAX(); break;
    case 0xAF: ABS(); LAX(); break;
    case 0xB3: INYR(); LAX(); break;
    case 0xB7: ZPY(); LAX(); break;
    case 0xBF: ABYR(); LAX(); break;
    case 0xAB: IMM(); LAX(); break;
    case 0x83: INX(); SAX(); break;
    case 0x87: ZP(); SAX(); break;
    case 0x8F: ABS(); SAX(); break;
    case 0x97: ZPY(); SAX(); break;
    case 0xC3: INX(); DCP(); break;
    case 0xC7: ZP(); DCP(); break;
    case 0xCF: ABS(); DCP(); break;
    case 0xD3: INYW(); DCP(); break;
    case 0xD7: ZPX(); DCP(); break;
    case 0xDB: ABYW(); DCP(); break;
    case 0xDF: ABXW(); DCP(); break;
    case 0xE3: INX(); ISB(); break;
    case 0xE7: ZP(); ISB(); break;
    case 0xEF: ABS(); ISB(); break;
    case 0xF3: INYW(); ISB(); break;
    case 0xF7: ZPX(); ISB(); break;
    case 0xFB: ABYW(); ISB(); break;
    case 0xFF: ABXW(); ISB(); break;
    case 0x03: INX(); SLO(); break;
    case 0x07: ZP(); SLO(); break;
    case 0x0F: ABS(); SLO(); break;
    case 0x13: INYW(); SLO(); break;
    case 0x17: ZPX(); SLO(); break;
    case 0x1B: ABYW(); SLO(); break;
    case 0x1F: ABXW(); SLO(); break;
    case 0x23: INX(); RLA(); break;
    case 0x27: ZP(); RLA(); break;
    case 0x2F: ABS(); RLA(); break;
    case 0x33: INYW(); RLA(); break;
    case 0x37: ZPX(); RLA(); break;
    case 0x3B: ABYW(); RLA(); break;
    case 0x3F: ABXW(); RLA(); break;
    case 0x43: INX(); SRE(); break;
    case 0x47: ZP(); SRE(); break;
    case 0x4F: ABS(); SRE(); break;
    case 0x53: INYW(); SRE(); break;
    case 0x57: ZPX(); SRE(); break;
    case 0x5B: ABYW(); SRE(); break;
    case 0x5F: ABXW(); SRE(); break;
    case 0x63: INX(); RRA(); break;
    case 0x67: ZP(); RRA(); break;
    case 0x6F: ABS(); RRA(); break;
    case 0x73: INYW(); RRA(); break;
    case 0x77: ZPX(); RRA(); break;
    case 0x7B: ABYW(); RRA(); break;
    case 0x7F: ABXW(); RRA(); break;
    case 0x0B: case 0x2B: IMM(); rA &= rd(ea); setnz(rA); fC = fN; break;
    case 0x4B: IMM(); rA &= rd(ea); rA = lsr(rA); break;
    case 0x6B: IMM(); rA &= rd(ea); rA = (uint8_t)((rA >> 1) | (fC << 7)); setnz(rA); fC = (rA >> 6) & 1; fV = ((rA >> 6) ^ (rA >> 5)) & 1; break;
    case 0xCB: IMM(); { const uint8_t m = rd(ea), t = rA & rX; fC = t >= m; rX = (uint8_t)(t - m); setnz(rX); } break;
    case 0xBB: ABYR(); rA = rX = rS = (uint8_t)(rd(ea) & rS); setnz(rA); break;
    case 0x8B: IMM(); rA = rX & rd(ea); setnz(rA); break;
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: rPC++; break;
    case 0x04: case 0x44: case 0x64: case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: rPC++; break;
    case 0x0C: rPC += 2; break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: ABXR(); break;
    case 0x93: rPC++; break;
    case 0x9B: case 0x9C: case 0x9E: case 0x9F: rPC += 2; break;
    default:                                                    /* the JAM opcodes: the CPU stops until a reset */
        jammed = 1; rPC--; cyc = 2;
        break;
    }
    return cyc;
}

static int step(void)
{
    int c = cpu_step();
    c += stall;
    stall = 0;
    cpu_cycles += (uint32_t)c;
    dot += 3 * c;
    apu_run(c);
    return c;
}

/* ------------------------------------------------------------------ the frame */
static uint32_t (*now_us)(void);
static uint32_t avg_frame, avg_ppu, avg_cpu;

void nes_set_clock(uint32_t (*us_fn)(void)) { now_us = us_fn; }

static void run_to(int d)
{
    while (dot < d) step();
}

static void scan_line(int ln, int render, int timed, uint32_t *t_ppu)
{
    scanline = ln;
    if (ln == 241) { p_status |= 0x80; if (p_ctrl & 0x80) nmi_pending = 1; }
    if (ln == 261) { p_status &= 0x1F; s0_pend = 0; }
    if (ln < 240) {
        if (timed && render) {
            const uint32_t a = now_us();
            ppu_line(ln, render);
            *t_ppu += now_us() - a;
        } else {
            ppu_line(ln, render);
        }
    }
    const int active = ln < 240 || ln == 261;
    run_to(257);
    if (active && (p_mask & 0x18)) {
        if (ln < 240) ppu_yinc();
        p_v = (uint16_t)((p_v & ~0x041F) | (p_t & 0x041F));
    }
    run_to(261);
    if (active && (p_mask & 0x18)) map_scanline();
    if (ln == 261) {
        run_to(305);
        if (p_mask & 0x18) p_v = (uint16_t)((p_v & ~0x7BE0) | (p_t & 0x7BE0));
    }
    const int len = (ln == 261 && (frames & 1) && (p_mask & 0x18)) ? 340 : 341;
    run_to(len);
    dot -= len;
    if (ln < 240 && s0_pend) { p_status |= 0x40; s0_pend = 0; }
}

void nes_frame(uint8_t pad1, uint8_t pad2, int render)
{
    pads[0] = pad1; pads[1] = pad2;
    if (!prg) return;
    static uint32_t rc10;
    const int timed = now_us && (frames & 7) == 0;
    if (timed && !rc10) {
        const uint32_t a = now_us();
        for (int i = 0; i < 10; i++) (void)now_us();
        rc10 = now_us() - a;
    }
    uint32_t t_ppu = 0;
    const uint32_t t_start = timed ? now_us() : 0;
    for (int ln = 0; ln < 262; ln++) scan_line(ln, render, timed, &t_ppu);
    frames++;
    if (timed) {
        const uint32_t over = (timed && render) ? rc10 * 240 / 10 : 0;
        const uint32_t tot = now_us() - t_start;
#define OFF(v, o) ((v) > (o) ? (v) - (o) : 0)
        const uint32_t f = OFF(tot, 2 * over), p = OFF(t_ppu, over), c = OFF(f, p);
        const int first = avg_frame == 0;
#define AVG(a, v) ((a) = first ? (v) : (uint32_t)((int32_t)(a) + ((int32_t)(v) - (int32_t)(a)) / 2))
        AVG(avg_frame, f); AVG(avg_cpu, c);
#undef AVG
        if (render) avg_ppu = avg_ppu ? (uint32_t)((int32_t)avg_ppu + ((int32_t)p - (int32_t)avg_ppu) / 2) : p;
#undef OFF
    }
}

/* ------------------------------------------------------------------ load and reset */
static void tables_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (int i = 0; i < 64; i++) {
        const uint32_t c = nes_rgb[i];
        rgb565[i] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    }
    pulse_tab[0] = 0;
    for (int i = 1; i < 31; i++) pulse_tab[i] = (int)(95.52 / (8128.0 / i + 100.0) * 16384.0);
    tnd_tab[0] = 0;
    for (int i = 1; i < 203; i++) tnd_tab[i] = (int)(163.67 / (24329.0 / i + 100.0) * 16384.0);
}

void nes_reset(void)
{
    if (!prg) return;
    memset(ram, 0, sizeof ram);
    memset(oam, 0, sizeof oam);
    memset(pal, 0x0F, sizeof pal);
    memset(fb, 0, sizeof fb);
    p_ctrl = p_mask = p_oamaddr = p_rdbuf = p_x = p_w = p_bus = 0;
    p_status = 0;
    p_v = p_t = 0;
    s0_pend = 0; s0_dot = 0; dot = 0; scanline = 0; frames = 0; cpu_cycles = 7;
    avg_frame = avg_ppu = avg_cpu = 0;
    nmi_pending = 0; stall = 0; jammed = 0; strobe = 0;
    pad_shift[0] = pad_shift[1] = 0;
    if (chr_is_ram) memset(chr_ram, 0, sizeof chr_ram);
    map_init();
    apu_reset();
    rA = rX = rY = 0; rS = 0xFD; fC = fZ = fD = fV = fN = 0; fI = 1;
    rPC = rd16(0xFFFC);
}

int nes_load(const uint8_t *data, size_t len, const char **why)
{
    static const char *const bad_header = "not an iNES file (no NES header)";
    tables_init();
    prg = NULL;
    if (len < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A) { *why = bad_header; return -1; }
    const unsigned f6 = data[6], f7 = data[7];
    const int nes2 = (f7 & 0x0C) == 0x08;
    mapper = (int)((f6 >> 4) | (f7 & 0xF0));
    if (nes2) mapper |= (int)(data[8] & 0x0F) << 8;
    const size_t trainer = (f6 & 4) ? 512 : 0;
    const size_t psz = (size_t)data[4] * 16384u, csz = (size_t)data[5] * 8192u;
    if (psz == 0) { *why = "the file has no program ROM"; return -1; }
    if (len < 16 + trainer + psz + csz) { *why = "the file is shorter than its header says"; return -1; }
    if (psz + csz > ROM_MAX) { *why = "the ROM is bigger than 2 MB"; return -1; }
    switch (mapper) {
    case 0: case 1: case 2: case 3: case 4: case 7: case 11: case 34: case 66: case 71: case 79: break;
    default: {
        static const char pre[] = "mapper ";
        static const char post[] = " is not supported";
        char *o = whybuf;
        memcpy(o, pre, sizeof pre - 1); o += sizeof pre - 1;
        char tmp[8]; int n = 0, m = mapper;
        do { tmp[n++] = (char)('0' + m % 10); m /= 10; } while (m && n < 7);
        while (n) *o++ = tmp[--n];
        memcpy(o, post, sizeof post); 
        *why = whybuf;
        return -1;
    }
    }
    memcpy(rom_buf, data + 16 + trainer, psz + csz);
    prg = rom_buf; prg_size = (uint32_t)psz; n_prg8 = (unsigned)(psz / 8192u);
    if (csz) { chr = rom_buf + psz; chr_size = (uint32_t)csz; n_chr1 = (unsigned)(csz / 1024u); chr_is_ram = 0; }
    else { chr = chr_ram; chr_size = 0; n_chr1 = 8; chr_is_ram = 1; }
    battery = (f6 & 2) != 0;
    hdr_mirror = (f6 & 8) ? 4 : ((f6 & 1) ? 1 : 0);
    memset(prg_ram, 0, sizeof prg_ram);
    sram_dirty = 0;
    info.mapper = mapper; info.prg_size = prg_size; info.chr_size = chr_size; info.battery = battery; info.sram_size = battery ? 8192u : 0u;
    nes_reset();
    return 0;
}

const uint16_t *nes_picture(void) { return fb; }
const nes_info_t *nes_info(void) { return &info; }
uint8_t *nes_sram(uint32_t *size) { *size = info.sram_size; return prg_ram; }
int nes_sram_dirty(void) { return sram_dirty; }
void nes_sram_clean(void) { sram_dirty = 0; }

void nes_debug(nes_debug_t *d)
{
    d->pc = rPC; d->a = rA; d->x = rX; d->y = rY; d->s = rS; d->p = get_p();
    d->ctrl = p_ctrl; d->mask = p_mask; d->status = p_status; d->v = p_v; d->t = p_t;
    d->frames = frames; d->cycles = cpu_cycles; d->scanline = scanline; d->jammed = jammed;
    d->audio_under = n_under; d->audio_over = n_over;
    d->us_frame = avg_frame; d->us_ppu = avg_ppu; d->us_cpu = avg_cpu;
}

uint8_t nes_peek(uint16_t a)
{
    if (a < 0x2000) return ram[a & 0x7FF];
    if (a >= 0x8000) return prg_pg[(a >> 13) & 3][a & 0x1FFF];
    if (a >= 0x6000) return prg_ram[a & 0x1FFF];
    return 0;
}
const uint8_t *nes_ram(void) { return ram; }
const uint8_t *nes_oam(void) { return oam; }
const uint8_t *nes_palette(void) { return pal; }

int nes_test_step(void) { return step(); }
void nes_test_set_cpu(uint16_t pc, uint8_t p, uint8_t s, uint32_t cycles)
{
    rPC = pc; set_p(p); rS = s; cpu_cycles = cycles; dot = (int)cycles * 3;
}
