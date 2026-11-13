/*
 * psx_spu.c: the sound chip of the PlayStation. 24 voices that play ADPCM blocks from the 512 KB sound RAM: the
 * decoder with its 5 filters, the pitch (and pitch modulation), linear interpolation, the ADSR envelope, the voice and
 * main volumes. A sample is made every 768 CPU cycles (44.1 kHz) into a ring; psx_audio_mix takes them at the output's
 * rate from the output's interrupt. Not done: the reverb, the noise generator, the CD audio input, the SPU interrupt.
 * Original code.
 */
#include "psx_int.h"

#define RING 4096

typedef struct {
    int on;                     /* the voice sounds */
    int state;                  /* 1 attack, 2 decay, 3 sustain, 4 release */
    int level;                  /* the envelope, 0..0x7FFF */
    int env_cnt;
    uint32_t addr;              /* byte address of the block being played */
    uint32_t rep;               /* repeat address */
    uint32_t counter;           /* 12 bit fraction of the sample position, plus the sample index in the high bits */
    int16_t smp[28 + 3];        /* decoded block, with the last 3 samples of the previous block in front */
    int old1, old2;
    int out;
} voice_t;

static uint8_t sram[512 * 1024];
static uint16_t regs[0x200];
static voice_t vc[24];
static uint32_t xfer_addr;
static uint32_t endx, cyc_acc;
static int16_t ring[RING][2];
static volatile uint32_t ring_wr, ring_rd;
static uint32_t n_under, n_over, n_kon;

static inline int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

void psx_spu_reset(void)
{
    memset(sram, 0, sizeof sram);
    memset(regs, 0, sizeof regs);
    memset(vc, 0, sizeof vc);
    xfer_addr = 0; endx = 0; cyc_acc = 0;
    ring_wr = ring_rd = 0;
}

/* ------------------------------------------------------------------ the voices */
static void decode_block(voice_t *v)
{
    const uint8_t *b = sram + (v->addr & 0x7FFF0u);
    const int shift = b[0] & 15, filt = (b[0] >> 4) & 7;
    static const int f0[5] = { 0, 60, 115, 98, 122 }, f1[5] = { 0, 0, -52, -55, -60 };
    const int k0 = f0[filt > 4 ? 4 : filt], k1 = f1[filt > 4 ? 4 : filt];
    v->smp[0] = v->smp[28]; v->smp[1] = v->smp[29]; v->smp[2] = v->smp[30];
    for (int i = 0; i < 28; i++) {
        int nib = (b[2 + (i >> 1)] >> ((i & 1) * 4)) & 15;
        int s = (int16_t)(nib << 12) >> (shift > 12 ? 9 : shift);
        s += (v->old1 * k0 + v->old2 * k1 + 32) >> 6;
        s = clamp16(s);
        v->old2 = v->old1; v->old1 = s;
        v->smp[3 + i] = (int16_t)s;
    }
}

static void voice_on(int n)
{
    voice_t *v = &vc[n];
    const uint16_t *r = &regs[(n * 16) / 2];
    v->addr = (uint32_t)r[3] * 8;
    v->rep = (uint32_t)r[7] * 8;
    v->old1 = v->old2 = 0;
    memset(v->smp, 0, sizeof v->smp);
    v->counter = 0;
    v->level = 0; v->env_cnt = 0; v->state = 1; v->on = 1; v->out = 0;
    decode_block(v);
    endx &= ~(1u << n);
    n_kon++;
}

/* one envelope step: rate parameters, direction (1 increase), exponential or linear */
static void env_step(voice_t *v, int shift, int step, int dec, int expo)
{
    int st = dec ? -8 + step : 7 - step;
    int cycles = 1 << (shift > 11 ? shift - 11 : 0);
    if (shift < 11) st <<= (11 - shift);
    if (expo && !dec && v->level > 0x6000) cycles *= 4;
    if (expo && dec) st = (st * v->level) >> 15;
    if (v->env_cnt > 0) v->env_cnt--;
    if (v->env_cnt > 0) return;
    v->env_cnt = cycles;
    v->level += st;
    if (v->level < 0) v->level = 0;
    if (v->level > 0x7FFF) v->level = 0x7FFF;
}

static void env_run(voice_t *v, int n)
{
    const uint16_t lo = regs[(n * 16 + 8) / 2], hi = regs[(n * 16 + 10) / 2];
    switch (v->state) {
    case 1:
        env_step(v, (lo >> 10) & 31, (lo >> 8) & 3, 0, (lo >> 15) & 1);
        if (v->level >= 0x7FFF) v->state = 2;
        break;
    case 2:
        env_step(v, (lo >> 4) & 15, 0, 1, 1);
        if (v->level <= (((lo & 15) + 1) << 11)) v->state = 3;
        break;
    case 3:
        env_step(v, (hi >> 8) & 31, (hi >> 6) & 3, (hi >> 14) & 1, (hi >> 15) & 1);
        break;
    case 4:
        env_step(v, hi & 31, 0, 1, (hi >> 5) & 1);
        if (v->level == 0) { v->on = 0; v->state = 0; }
        break;
    default: break;
    }
}

static inline int vol_of(uint16_t r)
{
    if (r & 0x8000) return 0x4000;                         /* a sweep: taken as half volume */
    return (int16_t)(r << 1);
}

static void make_sample(void)
{
    int l = 0, r = 0, prev_out = 0;
    for (int n = 0; n < 24; n++) {
        voice_t *v = &vc[n];
        if (!v->on) { prev_out = 0; continue; }
        const uint16_t *rg = &regs[(n * 16) / 2];
        int pitch = rg[2] & 0x3FFF;
        if (n > 0 && n < 16 && (regs[(0x1D90 - 0x1C00) / 2] & (1u << n)))
            pitch = (pitch * (prev_out + 0x8000)) >> 15;
        v->counter += (uint32_t)pitch;
        while ((v->counter >> 12) >= 28) {
            v->counter -= 28u << 12;
            const uint8_t flags = sram[(v->addr & 0x7FFF0u) + 1];
            if (flags & 4) v->rep = v->addr;
            if (flags & 1) {
                endx |= 1u << n;
                if (flags & 2) v->addr = v->rep;
                else { v->on = 0; v->state = 0; v->level = 0; break; }
            } else {
                v->addr += 16;
            }
            decode_block(v);
        }
        if (!v->on) { prev_out = 0; continue; }
        const int idx = (int)(v->counter >> 12), fr = (int)(v->counter & 0xFFF);
        const int s0 = v->smp[3 + idx], s1 = idx + 1 < 28 ? v->smp[3 + idx + 1] : v->smp[3 + idx];
        int s = s0 + (((s1 - s0) * fr) >> 12);
        env_run(v, n);
        s = (s * v->level) >> 15;
        v->out = s;
        prev_out = s;
        l += (s * vol_of(rg[0])) >> 15;
        r += (s * vol_of(rg[1])) >> 15;
    }
    const uint16_t cnt = regs[(0x1DAA - 0x1C00) / 2];
    const int mvl = vol_of(regs[(0x1D80 - 0x1C00) / 2]), mvr = vol_of(regs[(0x1D82 - 0x1C00) / 2]);
    if (!(cnt & 0x8000) || (cnt & 0x4000) == 0) { l = r = 0; }   /* the chip off or muted */
    l = clamp16((l * mvl) >> 15);
    r = clamp16((r * mvr) >> 15);
    if (ring_wr - ring_rd >= RING) { ring_rd = ring_wr - RING / 2; n_over++; }
    ring[ring_wr & (RING - 1)][0] = (int16_t)l;
    ring[ring_wr & (RING - 1)][1] = (int16_t)r;
    ring_wr++;
}

void psx_spu_run(uint32_t cycles)
{
    cyc_acc += cycles;
    while (cyc_acc >= 768) {
        cyc_acc -= 768;
        make_sample();
    }
}

/* ------------------------------------------------------------------ registers */
uint16_t psx_spu_read(uint32_t reg)
{
    reg |= 0x1000u;                                        /* the bus gives 0xC00..0xFFF, the registers are documented at 0x1C00 */
    const uint32_t i = (reg - 0x1C00) / 2;
    if (i >= 0x200) return 0;
    switch (reg) {
    case 0x1D9C: return (uint16_t)endx;
    case 0x1D9E: return (uint16_t)(endx >> 16);
    case 0x1DA6: return (uint16_t)(xfer_addr / 8);
    case 0x1DAE: return (uint16_t)(regs[(0x1DAA - 0x1C00) / 2] & 0x3F);
    default: break;
    }
    if (reg < 0x1D80 && (reg & 15) == 12) return (uint16_t)vc[(reg - 0x1C00) / 16].level;       /* the envelope level */
    return regs[i];
}

void psx_spu_write(uint32_t reg, uint16_t v)
{
    reg |= 0x1000u;
    const uint32_t i = (reg - 0x1C00) / 2;
    if (i >= 0x200) return;
    regs[i] = v;
    switch (reg) {
    case 0x1D88: case 0x1D8A:
        for (int b = 0; b < 16; b++) if (v & (1u << b)) voice_on(b + (reg == 0x1D8A ? 16 : 0));
        break;
    case 0x1D8C: case 0x1D8E:
        for (int b = 0; b < 16; b++) {
            voice_t *vo = &vc[b + (reg == 0x1D8E ? 16 : 0)];
            if ((v & (1u << b)) && vo->on) { vo->state = 4; vo->env_cnt = 0; }
        }
        break;
    case 0x1DA6: xfer_addr = (uint32_t)v * 8; break;
    case 0x1DA8:
        sram[xfer_addr & 0x7FFFFu] = (uint8_t)v;
        sram[(xfer_addr + 1) & 0x7FFFFu] = (uint8_t)(v >> 8);
        xfer_addr = (xfer_addr + 2) & 0x7FFFFu;
        break;
    default: break;
    }
}

void psx_spu_dma_write(const uint8_t *src, int n)
{
    for (int k = 0; k < n; k++) sram[(xfer_addr + (uint32_t)k) & 0x7FFFFu] = src[k];
    xfer_addr = (xfer_addr + (uint32_t)n) & 0x7FFFFu;
}

void psx_spu_dma_read(uint8_t *dst, int n)
{
    for (int k = 0; k < n; k++) dst[k] = sram[(xfer_addr + (uint32_t)k) & 0x7FFFFu];
    xfer_addr = (xfer_addr + (uint32_t)n) & 0x7FFFFu;
}

/* ------------------------------------------------------------------ the output's side */
int psx_audio_mix(float *out, unsigned n, float gain)
{
    static float prev_l, prev_r, cur_l, cur_r, phase = 1.0f;
    int sound = 0;
    const float k = gain * (0.8f / 32768.0f);
    if (ring_wr - ring_rd > 3 * 1024) ring_rd = ring_wr - 1024;
    for (unsigned i = 0; i < n; i++) {
        phase += 44100.0f / 48000.0f;
        while (phase >= 1.0f) {
            phase -= 1.0f;
            prev_l = cur_l; prev_r = cur_r;
            if (ring_rd != ring_wr) {
                cur_l = ring[ring_rd & (RING - 1)][0];
                cur_r = ring[ring_rd & (RING - 1)][1];
                ring_rd++;
            } else {
                n_under++;
            }
        }
        const float l = prev_l + (cur_l - prev_l) * phase, r = prev_r + (cur_r - prev_r) * phase;
        out[2 * i] += l * k;
        out[2 * i + 1] += r * k;
        if (l != 0.0f || r != 0.0f) sound = 1;
    }
    return sound;
}

void psx_audio_stats(uint32_t *under, uint32_t *over, uint32_t *kons)
{
    *under = n_under; *over = n_over; *kons = n_kon;
}
