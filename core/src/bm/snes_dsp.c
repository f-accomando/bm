/*
 * snes_dsp.c: the sound chip of the Super Nintendo, the S-DSP. Eight voices that play BRR samples from the sound RAM: the decoding,
 * the pitch (and pitch modulation), the Gaussian interpolation, the ADSR and GAIN envelopes, the noise, the voice and master volumes,
 * and the echo (a buffer in the sound RAM, an 8 tap FIR filter, the feedback). It makes one stereo sample every 32 cycles of the sound
 * CPU (32 kHz). The samples go into a ring; snes_audio_mix takes them from it at the output's rate (48 kHz) from the output's interrupt.
 * The model follows the order of the chip's pipeline for one sample: the voices 1 to 7, the echo, the clock and the noise, then voice 0
 * (it runs one sample ahead of the others in the chip). The 5 sample start delay of a voice, and KON and KOFF read every second sample,
 * are as in the chip. Checked against ares (tests/snes/dsp_ref.cpp). Original code.
 */
#include "snes_int.h"

#include <string.h>

/* Standalone/shared DSP memory: defaults to internal buffers unless spc_reset() binds its own. */
static uint8_t dsp_fallback_ram[65536];
static uint8_t dsp_fallback_regs[128];
static uint8_t *dsp_ram_ptr = dsp_fallback_ram;
static uint8_t *dsp_reg_ptr = dsp_fallback_regs;

void dsp_set_mem(uint8_t *ram64k, uint8_t *regs128)
{
    dsp_ram_ptr = ram64k ? ram64k : dsp_fallback_ram;
    dsp_reg_ptr = regs128 ? regs128 : dsp_fallback_regs;
}

uint8_t *dsp_ram_mem(void) { return dsp_ram_ptr; }
uint8_t *dsp_reg_mem(void) { return dsp_reg_ptr; }

typedef struct {
    int mode;                   /* 0 release, 1 attack, 2 decay, 3 sustain */
    int env;                    /* 0..0x7FF */
    int env_raw;                /* the envelope before the clamp (GAIN mode 7 reads it) */
    uint16_t goff;              /* the place in the sample: 0x1000 is one sample */
    uint16_t addr;              /* the address of the block being decoded */
    uint8_t off;                /* the byte of the block, 1..8 */
    uint8_t boff;               /* where the next decoded samples go in buf[] */
    uint8_t delay;              /* the key on delay, 5..0 */
    uint8_t hdr;                /* the header of the block */
    uint8_t end, looped;        /* the end flag (ENDX), and a block that ended in this sample */
    uint8_t keylatch, keyon, keyoff, keyoff_w;
    int16_t buf[12];
} voice_t;

static voice_t vc[8];
static int counter;             /* the clock of the envelopes and of the noise */
static unsigned noise = 0x4000;
static uint32_t n_kons;
static int clk_sample = 1;      /* KON and KOFF are read every second sample */
static int out_sample;          /* the output of the voice before (pitch modulation) */
static int mix_out[2], echo_acc[2], echo_in[2];
static int echo_hist[2][8];
static int echo_hoff;
static uint16_t echo_addr, echo_off, echo_len;
static uint8_t echo_page;
static uint8_t v0_outx, v0_envx;
static unsigned endx_mask;
static uint8_t m_mod, m_nz, m_ech;      /* PMON, NON, EON as the voices see them (taken once a sample) */

/* the Gaussian table of the chip */
static const int16_t gauss_tab[512] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10,
    11, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17, 17,
    18, 19, 19, 20, 20, 21, 21, 22, 23, 23, 24, 24, 25, 26, 27, 27,
    28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 36, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
    58, 59, 60, 61, 62, 64, 65, 66, 67, 69, 70, 71, 73, 74, 76, 77,
    78, 80, 81, 83, 84, 86, 87, 89, 90, 92, 94, 95, 97, 99, 100, 102,
    104, 106, 107, 109, 111, 113, 115, 117, 118, 120, 122, 124, 126, 128, 130, 132,
    134, 137, 139, 141, 143, 145, 147, 150, 152, 154, 156, 159, 161, 163, 166, 168,
    171, 173, 175, 178, 180, 183, 186, 188, 191, 193, 196, 199, 201, 204, 207, 210,
    212, 215, 218, 221, 224, 227, 230, 233, 236, 239, 242, 245, 248, 251, 254, 257,
    260, 263, 267, 270, 273, 276, 280, 283, 286, 290, 293, 297, 300, 304, 307, 311,
    314, 318, 321, 325, 328, 332, 336, 339, 343, 347, 351, 354, 358, 362, 366, 370,
    374, 378, 381, 385, 389, 393, 397, 401, 405, 410, 414, 418, 422, 426, 430, 434,
    439, 443, 447, 451, 456, 460, 464, 469, 473, 477, 482, 486, 491, 495, 499, 504,
    508, 513, 517, 522, 527, 531, 536, 540, 545, 550, 554, 559, 563, 568, 573, 577,
    582, 587, 592, 596, 601, 606, 611, 615, 620, 625, 630, 635, 640, 644, 649, 654,
    659, 664, 669, 674, 678, 683, 688, 693, 698, 703, 708, 713, 718, 723, 728, 732,
    737, 742, 747, 752, 757, 762, 767, 772, 777, 782, 787, 792, 797, 802, 806, 811,
    816, 821, 826, 831, 836, 841, 846, 851, 855, 860, 865, 870, 875, 880, 884, 889,
    894, 899, 904, 908, 913, 918, 923, 927, 932, 937, 941, 946, 951, 955, 960, 965,
    969, 974, 978, 983, 988, 992, 997, 1001, 1005, 1010, 1014, 1019, 1023, 1027, 1032, 1036,
    1040, 1045, 1049, 1053, 1057, 1061, 1066, 1070, 1074, 1078, 1082, 1086, 1090, 1094, 1098, 1102,
    1106, 1109, 1113, 1117, 1121, 1125, 1128, 1132, 1136, 1139, 1143, 1146, 1150, 1153, 1157, 1160,
    1164, 1167, 1170, 1174, 1177, 1180, 1183, 1186, 1190, 1193, 1196, 1199, 1202, 1205, 1207, 1210,
    1213, 1216, 1219, 1221, 1224, 1227, 1229, 1232, 1234, 1237, 1239, 1241, 1244, 1246, 1248, 1251,
    1253, 1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269, 1270, 1272, 1274, 1275, 1277, 1279, 1280,
    1282, 1283, 1284, 1286, 1287, 1288, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1297, 1298,
    1299, 1300, 1300, 1301, 1302, 1302, 1303, 1303, 1303, 1304, 1304, 1304, 1304, 1304, 1305, 1305,
};

static const uint16_t rate_period[32] = { 0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
                                          64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1 };
static const uint16_t rate_off[32] = { 0, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
                                       0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 0, 0 };

/* ------------------------------------------------------------------ the ring of stereo samples at 32 kHz */
#define RING 8192
static int16_t ring[RING][2];
static volatile uint32_t ring_rd, ring_wr;
static uint32_t n_under, n_over;
static volatile int audio_live;                 /* the output has asked for samples: real hardware plays the ring (the tests never do) */

static void ring_push(int l, int r)
{
    if (ring_wr - ring_rd >= RING) { n_over++; return; }
    ring[ring_wr & (RING - 1)][0] = (int16_t)l;
    ring[ring_wr & (RING - 1)][1] = (int16_t)r;
    ring_wr++;
}

/* The samples waiting in the ring, or -1 when no output plays the ring (a host test): snes_frame uses it to keep the sound at real time. */
int snes_audio_queued(void) { return audio_live ? (int)(ring_wr - ring_rd) : -1; }

void snes_audio_stats(uint32_t *under, uint32_t *over, uint32_t *kons)
{
    *under = n_under; *over = n_over; *kons = n_kons;
}

/* one sample of the ring (for the tests): 1 if there was one */
int snes_dsp_pop(int16_t out[2])
{
    if (ring_rd == ring_wr) return 0;
    out[0] = ring[ring_rd & (RING - 1)][0]; out[1] = ring[ring_rd & (RING - 1)][1];
    ring_rd++;
    return 1;
}

/* the state of a voice, for the tests: 0 mode, 1 envelope, 2 delay, 3 place, 4 block address */
int snes_dsp_dbg(int v, int what)
{
    const voice_t *p = &vc[v];
    switch (what) { case 0: return p->mode; case 1: return p->env; case 2: return p->delay; case 3: return p->goff; default: return p->addr; }
}

/* the output's side: 32 kHz to 48 kHz by linear interpolation, in the interrupt */
int snes_audio_mix(float *out, unsigned n, float gain)
{
    static float prev_l, prev_r, cur_l, cur_r, phase = 1.0f;
    int sound = 0;
    audio_live = 1;
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

/* Does the envelope step at this value of the clock? For each of the 0x7800 values of `counter`, one bit for each rate: the rate fires when
 * (counter + rate_off) is a multiple of its period. A table, because the ARM1176 has no divide. */
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

/* the start (loop 0) or the loop address (1) of the sample of voice i, from the directory */
static inline uint16_t dir_addr(int i, int loop)
{
    const unsigned a = ((unsigned)dsp_reg_ptr[0x5D] << 8) + (unsigned)dsp_reg_ptr[i * 16 + 4] * 4 + (loop ? 2 : 0);
    return (uint16_t)(dsp_ram_ptr[a & 0xFFFF] | (dsp_ram_ptr[(a + 1) & 0xFFFF] << 8));
}

/* decodes four samples of the block (the byte at off and the next are the 4 nybbles) into buf[] */
static void brr_decode(voice_t *v)
{
    const uint8_t *ram = dsp_ram_ptr;
    unsigned nyb = ((unsigned)ram[(uint16_t)(v->addr + v->off)] << 8) | ram[(uint16_t)(v->addr + v->off + 1)];
    const int filter = (v->hdr >> 2) & 3, scale = v->hdr >> 4;
    for (int n = 0; n < 4; n++) {
        int s = (int16_t)nyb >> 12;
        nyb <<= 4;
        if (scale <= 12) s = (s * (1 << scale)) >> 1;          /* a multiply: a left shift of a negative value is undefined in C */
        else s &= ~0x7FF;
        const int p1 = v->buf[v->boff ? v->boff - 1 : 11];
        const int p2 = v->buf[v->boff > 1 ? v->boff - 2 : v->boff + 10] >> 1;
        switch (filter) {
        case 1: s += (p1 >> 1) + ((-p1) >> 5); break;
        case 2: s += p1 - p2 + (p2 >> 4) + ((p1 * -3) >> 6); break;
        case 3: s += p1 - p2 + ((p1 * -13) >> 7) + ((p2 * 3) >> 4); break;
        default: break;
        }
        s = clamp16(s);
        v->buf[v->boff] = (int16_t)(s * 2);
        if (++v->boff >= 12) v->boff = 0;
    }
}

static void envelope(voice_t *v, const uint8_t *r)
{
    int e = v->env, rate, data = r[6];
    if (v->mode == 0) {                                 /* release: 8 a sample */
        e -= 8;
        v->env = e < 0 ? 0 : e;
        return;
    }
    if (r[5] & 0x80) {                                  /* ADSR */
        if (v->mode >= 2) {
            e--; e -= e >> 8;
            rate = data & 31;
            if (v->mode == 2) rate = ((r[5] >> 4) & 7) * 2 + 16;
        } else {
            rate = (r[5] & 15) * 2 + 1;
            e += rate < 31 ? 0x20 : 0x400;
        }
    } else {                                            /* GAIN */
        data = r[7];
        const int mode = data >> 5;
        if (mode < 4) { e = data << 4; rate = 31; }     /* direct */
        else {
            rate = data & 31;
            if (mode == 4) e -= 0x20;
            else if (mode < 6) { e--; e -= e >> 8; }
            else {
                e += 0x20;
                if (mode > 6 && (unsigned)v->env_raw >= 0x600) e += 8 - 0x20;
            }
        }
    }
    if ((e >> 8) == (data >> 5) && v->mode == 2) v->mode = 3;
    v->env_raw = e;
    if ((unsigned)e > 0x7FF) {
        e = e < 0 ? 0 : 0x7FF;
        if (v->mode == 1) v->mode = 2;
    }
    if (fire(rate)) v->env = e;
}

/* one voice for one sample: the steps 1 to 9 of the chip's pipeline together */
static void voice_run(int i)
{
    voice_t *v = &vc[i];
    uint8_t *d = dsp_reg_ptr, *r = &d[i * 16];
    const uint8_t *ram = dsp_ram_ptr;
    /* 1, 2, 3: the header of the block (the directory is read where it is used) */
    v->hdr = ram[v->addr];
    int pitch = r[2] | ((r[3] & 0x3F) << 8);
    if ((m_mod >> i) & 1) pitch = (pitch + (((out_sample >> 5) * pitch) >> 10)) & 0x7FFF;
    if (v->delay) {
        if (v->delay == 5) { v->addr = dir_addr(i, 0); v->off = 1; v->boff = 0; v->hdr = 0; }
        v->env = 0; v->env_raw = 0;
        v->goff = 0;
        v->delay--;
        if (v->delay & 3) v->goff = 0x4000;
        pitch = 0;
    }
    /* the Gaussian interpolation of the 4 samples before the place (a silent voice only moves on) */
    if (v->env) {
        int s;
        const unsigned o = (v->goff >> 4) & 0xFF;
        const int16_t *fw = gauss_tab + 255 - o, *rv = gauss_tab + o;
        unsigned k = v->boff + (v->goff >> 12);
        if (k >= 12) k -= 12;
        int a = (fw[0] * v->buf[k]) >> 11; if (++k >= 12) k = 0;
        a += (fw[256] * v->buf[k]) >> 11; if (++k >= 12) k = 0;
        a += (rv[256] * v->buf[k]) >> 11; if (++k >= 12) k = 0;
        a = (int16_t)a;
        a += (rv[0] * v->buf[k]) >> 11;
        s = clamp16(a) & ~1;
        if ((m_nz >> i) & 1) s = (int16_t)(noise << 1);
        out_sample = (s * v->env >> 11) & ~1;
    } else {
        out_sample = 0;
    }
    const int envx = v->env >> 4;
    /* the end of a sample without a loop, or the reset: silence at once */
    if ((d[0x6C] & 0x80) || (v->hdr & 3) == 1) { v->mode = 0; v->env = 0; }
    if (clk_sample) {
        if (v->keyoff) v->mode = 0;
        if (v->keyon) { v->delay = 5; v->mode = 1; }
    }
    if (!v->delay) envelope(v, r);
    /* 4: decoding of the next 4 samples, then the pitch */
    v->looped = 0;
    if (v->goff >= 0x4000) {
        brr_decode(v);
        v->off += 2;
        if (v->off >= 9) {
            v->addr = (uint16_t)(v->addr + 9);
            if (v->hdr & 1) { v->addr = dir_addr(i, 1); v->looped = 1; }
            v->off = 1;
        }
    }
    {
        const unsigned g = (v->goff & 0x3FFF) + (unsigned)pitch;
        v->goff = (uint16_t)(g > 0x7FFF ? 0x7FFF : g);
    }
    /* 4, 5: the volumes */
    const int l = (out_sample * (int8_t)r[0]) >> 7, rr = (out_sample * (int8_t)r[1]) >> 7;
    mix_out[0] = clamp16(mix_out[0] + l);
    mix_out[1] = clamp16(mix_out[1] + rr);
    if ((m_ech >> i) & 1) {
        echo_acc[0] = clamp16(echo_acc[0] + l);
        echo_acc[1] = clamp16(echo_acc[1] + rr);
    }
    /* 5, 8, 9: ENDX (in dsp_tick), OUTX, ENVX */
    v->end |= v->looped;
    if (v->delay == 5) v->end = 0;
    if (v->end) endx_mask |= 1u << i; else endx_mask &= ~(1u << i);
    if (i) { r[9] = (uint8_t)(out_sample >> 8); r[8] = (uint8_t)envx; }
    else { v0_outx = (uint8_t)(out_sample >> 8); v0_envx = (uint8_t)envx; }    /* the chip writes them in the next sample */
}

/* ------------------------------------------------------------------ the echo */
static inline int fir(int ch, int idx)
{
    return (echo_hist[ch][(echo_hoff + idx + 1) & 7] * (int8_t)dsp_reg_ptr[idx * 16 + 15]) >> 6;
}

static inline int echo_vol(int ch)
{
    const uint8_t *d = dsp_reg_ptr;
    const int m = (int16_t)(mix_out[ch] * (int8_t)d[ch ? 0x1C : 0x0C] >> 7);
    const int e = (int16_t)(echo_in[ch] * (int8_t)d[ch ? 0x3C : 0x2C] >> 7);
    return clamp16(m + e);
}

static void echo_run(int *outl, int *outr)
{
    uint8_t *d = dsp_reg_ptr, *ram = dsp_ram_ptr;
    const int ro = (d[0x6C] >> 5) & 1;
    /* 22 to 25: read the buffer, the filter */
    echo_hoff = (echo_hoff + 1) & 7;
    echo_addr = (uint16_t)((echo_page << 8) + echo_off);
    for (int ch = 0; ch < 2; ch++) {
        const unsigned a = (uint16_t)(echo_addr + ch * 2);
        echo_hist[ch][echo_hoff] = (int16_t)(ram[a] | (ram[(uint16_t)(a + 1)] << 8)) >> 1;
    }
    for (int ch = 0; ch < 2; ch++) {
        int s = fir(ch, 0) + fir(ch, 1) + fir(ch, 2) + fir(ch, 3) + fir(ch, 4) + fir(ch, 5) + fir(ch, 6);
        s = (int16_t)s;
        s += (int16_t)fir(ch, 7);
        echo_in[ch] = clamp16(s) & ~1;
    }
    /* 26, 27: the output, the feedback */
    *outl = echo_vol(0);
    *outr = echo_vol(1);
    for (int ch = 0; ch < 2; ch++) {
        const int s = echo_acc[ch] + (int16_t)(echo_in[ch] * (int8_t)d[0x0D] >> 7);
        echo_acc[ch] = clamp16(s) & ~1;
    }
    mix_out[0] = mix_out[1] = 0;
    /* 29, 30: the echo buffer is written (unless FLG says no), the place moves on */
    echo_page = d[0x6D];
    if (!echo_off) echo_len = (uint16_t)((d[0x7D] & 15) << 11);
    echo_off = (uint16_t)(echo_off + 4);
    if (echo_off >= echo_len) echo_off = 0;
    for (int ch = 0; ch < 2; ch++) {
        if (!ro) {
            const unsigned a = (uint16_t)(echo_addr + ch * 2);
            ram[a] = (uint8_t)echo_acc[ch];
            ram[(uint16_t)(a + 1)] = (uint8_t)(echo_acc[ch] >> 8);
        }
        echo_acc[ch] = 0;
    }
}

/* ------------------------------------------------------------------ one sample */
/* hooks for the tests (a register write, the end of a sample); NULL in the product */
void (*snes_dsp_wr_hook)(int reg, uint8_t val);
void (*snes_dsp_tick_hook)(void);
void (*snes_dsp_pre_hook)(void);

void dsp_write(int reg, uint8_t val)
{
    uint8_t *d = dsp_reg_ptr;
    if (snes_dsp_wr_hook) snes_dsp_wr_hook(reg, val);
    d[reg] = val;
    switch (reg) {
    case 0x4C: for (int i = 0; i < 8; i++) vc[i].keylatch = (val >> i) & 1; break;
    case 0x5C: for (int i = 0; i < 8; i++) vc[i].keyoff_w = (val >> i) & 1; break;
    case 0x7C: for (int i = 0; i < 8; i++) vc[i].end = 0; endx_mask = 0; d[0x7C] = 0; break;     /* a write to ENDX clears it */
    default: break;
    }
}

void dsp_reset(void)
{
    memset(vc, 0, sizeof vc);
    for (int i = 0; i < 8; i++) vc[i].off = 1;
    counter = 0; noise = 0x4000; clk_sample = 1; out_sample = 0;
    memset(mix_out, 0, sizeof mix_out); memset(echo_acc, 0, sizeof echo_acc); memset(echo_in, 0, sizeof echo_in);
    memset(echo_hist, 0, sizeof echo_hist);
    endx_mask = 0; m_mod = m_nz = m_ech = 0; v0_outx = v0_envx = 0; echo_hoff = 0; echo_addr = echo_off = echo_len = 0; echo_page = 0;
    dsp_reg_ptr[0x6C] = 0xE0;                           /* the chip starts muted, in reset, with the echo write off */
    ring_rd = ring_wr = 0;
}

void dsp_tick(void)
{
    if (!fire_ready) fire_init();
    uint8_t *d = dsp_reg_ptr;
    if (snes_dsp_pre_hook) snes_dsp_pre_hook();
    for (int i = 1; i < 8; i++) voice_run(i);
    int ol, orr;
    echo_run(&ol, &orr);
    if (d[0x6C] & 0x40) ol = orr = 0;                   /* muted */
    ring_push(ol, orr);
    /* 27, 28: the bits that the voices take on the next sample */
    m_mod = d[0x2D] & 0xFE; m_nz = d[0x3D]; m_ech = d[0x4D];
    /* 29, 30: KON and KOFF are read on every second sample; the clock; the noise */
    clk_sample ^= 1;
    if (clk_sample) {
        for (int i = 0; i < 8; i++) {
            vc[i].keylatch &= (uint8_t)!vc[i].keyon;
            vc[i].keyon = vc[i].keylatch;
            vc[i].keyoff = vc[i].keyoff_w;
            n_kons += vc[i].keyon;
        }
    }
    counter = counter ? counter - 1 : 0x77FF;
    if (fire(d[0x6C] & 31)) {
        const unsigned fb = ((noise << 13) ^ (noise << 14)) & 0x4000;
        noise = fb | (noise >> 1);
    }
    {                                                   /* voice 0 is one sample behind in these registers */
        d[0x7C] = (uint8_t)endx_mask;
        d[9] = v0_outx; d[8] = v0_envx;
    }
    voice_run(0);
    if (snes_dsp_tick_hook) snes_dsp_tick_hook();
}
