/*
 * snes_dsp.c: the sound chip of the Super Nintendo, the S-DSP. Eight voices that play BRR samples from the sound
 * CPU's RAM: the decoding, the pitch (and pitch modulation), the interpolation, the ADSR and GAIN envelopes, the
 * noise, the voice and master volumes. It makes one stereo sample every 32 cycles of the sound CPU (32 kHz). The
 * samples go into a ring; snes_audio_mix takes them from it at the output's rate (48 kHz) from the output's
 * interrupt. Not done: the echo (its buffer is not written), the exact start delay of a voice, the Gaussian table
 * (a cubic interpolation instead). Original code.
 */
#include "snes_int.h"

#include <string.h>

/* the RAM and the registers of snes_spc.c */
extern uint8_t *spc_ram_ptr(void);
extern uint8_t *spc_dsp_ptr(void);

typedef struct {
    int state;                  /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    int env;                    /* 0..0x7FF */
    uint16_t addr;              /* the address of the block being played */
    int pos;                    /* the next sample of blk to take, 0..16 */
    int frac;                   /* 12 bits: the place between two samples */
    int16_t blk[16];            /* the decoded block */
    int16_t hist[4];            /* the last four samples */
    int p1, p2;                 /* the BRR filter's memory (the last two decoded samples) */
    int endflag, loopflag;      /* the header of the block being played */
    int out;                    /* the last output (for the pitch modulation) */
} voice_t;

static voice_t vc[8];
static int counter = 0x77FF;    /* the envelope's clock */
static unsigned noise = 0x4000;
static int noise_cnt;
static uint8_t kon_latch;
static uint32_t n_kons;

static const uint16_t rate_period[32] = { 0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
                                          64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1 };
static const uint16_t rate_off[32] = { 0, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
                                       0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 0, 0 };

/* ------------------------------------------------------------------ the ring of stereo samples at 32 kHz */
#define RING 8192
static int16_t ring[RING][2];
static volatile uint32_t ring_rd, ring_wr;
static uint32_t n_under, n_over;

static void ring_push(int l, int r)
{
    if (ring_wr - ring_rd >= RING) { n_over++; return; }
    ring[ring_wr & (RING - 1)][0] = (int16_t)l;
    ring[ring_wr & (RING - 1)][1] = (int16_t)r;
    ring_wr++;
}

void snes_audio_stats(uint32_t *under, uint32_t *over, uint32_t *kons)
{
    *under = n_under; *over = n_over; *kons = n_kons;
}

/* the output's side: 32 kHz to 48 kHz by linear interpolation, in the interrupt */
int snes_audio_mix(float *out, unsigned n, float gain)
{
    static float prev_l, prev_r, cur_l, cur_r, phase = 1.0f;
    int sound = 0;
    const float k = gain * (0.8f / 32768.0f);
    /* the ring too full is a delay the game does not need: keep the last 30 ms */
    if (ring_wr - ring_rd > 3 * 1024) ring_rd = ring_wr - 1024;
    for (unsigned i = 0; i < n; i++) {
        phase += 32000.0f / 48000.0f;
        while (phase >= 1.0f) {
            phase -= 1.0f;
            prev_l = cur_l; prev_r = cur_r;
            if (ring_rd != ring_wr) {
                cur_l = ring[ring_rd & (RING - 1)][0];
                cur_r = ring[ring_rd & (RING - 1)][1];
                ring_rd++;
            } else {
                n_under++;                              /* nothing came: the last sample stays */
            }
        }
        const float l = prev_l + (cur_l - prev_l) * phase, r = prev_r + (cur_r - prev_r) * phase;
        out[2 * i] += l * k;
        out[2 * i + 1] += r * k;
        if (l != 0.0f || r != 0.0f) sound = 1;
    }
    return sound;
}

/* ------------------------------------------------------------------ the voices */
static inline int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

/* the start of the sample n of the directory */
static uint16_t dir_addr(int srcn, int loop)
{
    const uint8_t *ram = spc_ram_ptr(), *d = spc_dsp_ptr();
    const unsigned a = ((unsigned)d[0x5D] << 8) + (unsigned)srcn * 4 + (loop ? 2 : 0);
    return (uint16_t)(ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8));
}

/* decodes the block at v->addr into v->blk */
static void brr_decode(voice_t *v)
{
    const uint8_t *ram = spc_ram_ptr();
    const unsigned hdr = ram[v->addr];
    const int range = hdr >> 4, filter = (hdr >> 2) & 3;
    v->endflag = hdr & 1; v->loopflag = (hdr >> 1) & 1;
    int p1 = v->p1, p2 = v->p2;
    for (int i = 0; i < 16; i++) {
        const unsigned b = ram[(uint16_t)(v->addr + 1 + (i >> 1))];
        int s = (i & 1) ? (int)(b & 15) : (int)(b >> 4);
        if (s >= 8) s -= 16;
        if (range <= 12) s = (s << range) >> 1;
        else s = s < 0 ? -2048 : 0;
        switch (filter) {
        case 1: s += (p1 >> 1) + ((-p1) >> 5); break;
        case 2: s += p1 + ((-(p1 + (p1 << 1))) >> 5) - (p2 >> 1) + (p2 >> 5); break;
        case 3: s += p1 + ((-(p1 + (p1 << 2) + (p1 << 3))) >> 7) - (p2 >> 1) + ((p2 + (p2 << 1)) >> 5); break;
        default: break;
        }
        s = clamp16(s);
        s = (int16_t)(s * 2);
        v->blk[i] = (int16_t)s;
        p2 = p1; p1 = s;
    }
    v->p1 = p1; v->p2 = p2;
}

static void voice_key_on(int i)
{
    voice_t *v = &vc[i];
    v->addr = dir_addr(spc_dsp_ptr()[i * 16 + 4], 0);
    v->p1 = v->p2 = 0;
    memset(v->hist, 0, sizeof v->hist);
    brr_decode(v);
    v->pos = 0; v->frac = 0;
    v->env = 0; v->state = 1;
    spc_dsp_ptr()[0x7C] &= (uint8_t)~(1 << i);          /* the end flag of the voice is cleared */
    n_kons++;
}

/* the next sample of the voice's stream; handles the end of a block and the loop */
static int16_t next_sample(voice_t *v, int i)
{
    if (v->pos >= 16) {
        if (v->endflag) {
            spc_dsp_ptr()[0x7C] |= (uint8_t)(1 << i);
            if (v->loopflag) {
                v->addr = dir_addr(spc_dsp_ptr()[i * 16 + 4], 1);
            } else {
                v->state = 0; v->env = 0;               /* a sample that ends without a loop stops the voice */
                return 0;
            }
        } else {
            v->addr = (uint16_t)(v->addr + 9);
        }
        brr_decode(v);
        v->pos = 0;
    }
    return v->blk[v->pos++];
}

/* Does the envelope step at this value of the clock? For each of the 0x7800 values of `counter`, one bit for each rate: the rate fires when
 * (counter + rate_off) is a multiple of its period. A table, because the ARM1176 has no divide: the modulo took a library call for every voice at every sample. */
#define COUNTER_N 0x7800
static uint32_t fire_tab[COUNTER_N];
static int fire_ready;
static void fire_init(void)
{
    for (int c = 0; c < COUNTER_N; c++) {
        uint32_t m = 0;
        for (int r = 1; r < 32; r++)
            if (((c + rate_off[r]) % rate_period[r]) == 0) m |= 1u << r;
        fire_tab[c] = m;
    }
    fire_ready = 1;
}

static inline int fire(int rate)
{
    return rate && ((fire_tab[counter] >> rate) & 1);
}

static void envelope(voice_t *v, const uint8_t *r, int koff)
{
    if (koff && v->state != 0) v->state = 4;
    if (v->state == 4) {                                /* release: 8 a sample */
        v->env -= 8;
        if (v->env <= 0) { v->env = 0; v->state = 0; }
        return;
    }
    if (r[5] & 0x80) {                                  /* ADSR */
        int rate, step;
        switch (v->state) {
        case 1:
            rate = (r[5] & 15) * 2 + 1;
            if (fire(rate)) {
                v->env += rate == 31 ? 1024 : 32;
                if (v->env > 0x7FF) v->env = 0x7FF;
                if (v->env >= 0x7E0) v->state = 2;
            }
            break;
        case 2:
            rate = ((r[5] >> 4) & 7) * 2 + 16;
            if (fire(rate)) { step = ((v->env - 1) >> 8) + 1; v->env -= step; }
            if ((v->env >> 8) <= (r[6] >> 5)) v->state = 3;
            break;
        case 3:
            rate = r[6] & 31;
            if (fire(rate)) { step = ((v->env - 1) >> 8) + 1; v->env -= step; if (v->env < 0) v->env = 0; }
            break;
        default: break;
        }
    } else {                                            /* GAIN */
        const int g = r[7];
        if (g & 0x80) {
            const int rate = g & 31;
            if (fire(rate)) {
                switch ((g >> 5) & 3) {
                case 0: v->env -= 32; break;
                case 1: v->env -= ((v->env - 1) >> 8) + 1; break;
                case 2: v->env += 32; break;
                default: v->env += v->env < 0x600 ? 32 : 8; break;
                }
                if (v->env < 0) v->env = 0;
                if (v->env > 0x7FF) v->env = 0x7FF;
            }
        } else {
            v->env = (g & 0x7F) << 4;
        }
    }
}

/* ------------------------------------------------------------------ one sample */
void dsp_write(int reg, uint8_t v)
{
    uint8_t *d = spc_dsp_ptr();
    if (reg == 0x4C) kon_latch |= v;
    if (reg == 0x7C) { d[0x7C] = 0; return; }           /* a write to ENDX clears it */
    d[reg] = v;
}

void dsp_reset(void)
{
    memset(vc, 0, sizeof vc);
    counter = 0x77FF; noise = 0x4000; noise_cnt = 0; kon_latch = 0;
    ring_rd = ring_wr = 0;
}

void dsp_tick(void)
{
    if (!fire_ready) fire_init();
    uint8_t *d = spc_dsp_ptr();
    const uint8_t flg = d[0x6C];
    const int kon = kon_latch;
    kon_latch = 0;
    counter = counter ? counter - 1 : 0x77FF;
    /* the noise: a 15-bit shift register at the FLG rate */
    if (fire(flg & 31)) {
        const unsigned bit = (noise ^ (noise >> 1)) & 1;
        noise = (noise >> 1) | (bit << 14);
    }
    int sl = 0, sr = 0, prev_out = 0;
    for (int i = 0; i < 8; i++) {
        voice_t *v = &vc[i];
        uint8_t *r = &d[i * 16];
        if (kon & (1 << i)) voice_key_on(i);
        if (flg & 0x80) { v->state = 0; v->env = 0; }
        if (v->state == 0) { v->out = 0; r[8] = 0; r[9] = 0; prev_out = 0; continue; }
        envelope(v, r, (d[0x5C] >> i) & 1);
        int pitch = r[2] | ((r[3] & 0x3F) << 8);
        if (i > 0 && (d[0x2D] & (1 << i))) pitch += ((prev_out >> 5) * pitch) >> 10;
        if (pitch < 0) pitch = 0;
        v->frac += pitch;
        while (v->frac >= 0x1000 && v->state) {
            v->frac -= 0x1000;
            v->hist[0] = v->hist[1]; v->hist[1] = v->hist[2]; v->hist[2] = v->hist[3];
            v->hist[3] = next_sample(v, i);
        }
        int s;
        if (v->env == 0) {
            s = 0;                                      /* silent: the sample times the envelope is 0 (the position above still moved) */
        } else if (d[0x3D] & (1 << i)) {
            s = (int16_t)(noise << 1);
        } else {
            /* a cubic between hist[1] and hist[2]: f = s1 + t/2 * (s2 - s0 + t * (c2 + t * c3)), t = frac / 4096 (Horner's rule, three 32 x 32 to 64 bit
             * products, each rounded; the ARM1176 has a slow floating point unit). Within 2 steps of 16 bit of the float formula that was here before. */
            const int s0 = v->hist[0], s1 = v->hist[1], s2 = v->hist[2], s3 = v->hist[3];
            const int c3 = 3 * (s1 - s2) + s3 - s0, c2 = 2 * s0 - 5 * s1 + 4 * s2 - s3;
            const int t = v->frac;
            const int p1 = c2 + (int)(((int64_t)c3 * t + 2048) >> 12);                  /* c2 + t * c3 */
            const int p2 = (s2 - s0) + (int)(((int64_t)p1 * t + 2048) >> 12);          /* s2 - s0 + t * (...) */
            s = s1 + (int)(((int64_t)p2 * t + 4096) >> 13);                             /* + t / 2 * (...) */
            s = clamp16(s);
        }
        s = (s * v->env) >> 11;
        v->out = s;
        prev_out = s;
        r[8] = (uint8_t)(v->env >> 4);
        r[9] = (uint8_t)(s >> 8);
        sl += (s * (int8_t)r[0]) >> 6;
        sr += (s * (int8_t)r[1]) >> 6;
    }
    sl = clamp16(sl); sr = clamp16(sr);
    sl = (sl * (int8_t)d[0x0C]) >> 7;
    sr = (sr * (int8_t)d[0x1C]) >> 7;
    if (flg & 0x40) sl = sr = 0;                        /* muted */
    ring_push(clamp16(sl), clamp16(sr));
}
