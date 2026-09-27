/* Host tests for the synthesizer (s32 APU semantics, lua32 apu.lua) and
 * the IEC 958 subframes sent to the HDMI audio FIFO. */
#include "audio/synth.h"
#include "audio/iec958.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#define RATE 48000
static uint8_t regs[SYNTH_REG_BYTES];
static int16_t buf[RATE * 2];

static void voice(int ch, int freq, int wave, int duty, int vol, int a, int d, int s, int r, int gate)
{
    uint8_t *v = regs + ch * SYNTH_VOICE_BYTES;
    v[SYNTH_FREQ_LO] = freq & 255;
    v[SYNTH_FREQ_HI] = freq >> 8;
    v[SYNTH_WAVEFORM] = wave;
    v[SYNTH_DUTY] = duty;
    v[SYNTH_VOLUME] = vol;
    v[SYNTH_ATTACK] = a;
    v[SYNTH_DECAY] = d;
    v[SYNTH_SUSTAIN] = s;
    v[SYNTH_RELEASE] = r;
    v[SYNTH_CONTROL] = gate ? SYNTH_GATE : 0;
}

static int rising_edges(const int16_t *x, int n)
{
    int e = 0;
    for (int i = 1; i < n; i++)
        e += x[i - 1] < 0 && x[i] >= 0;
    return e;
}

static int peak(const int16_t *x, int n)
{
    int p = 0;
    for (int i = 0; i < n; i++)
        if (abs(x[i]) > p) p = abs(x[i]);
    return p;
}

static void test_synth(void)
{
    synth_t s;

    /* silence when nothing is gated */
    memset(regs, 0, sizeof regs);
    synth_init(&s, RATE);
    synth_render(&s, regs, buf, 1000);
    CHECK(peak(buf, 1000) == 0 && synth_active(&s) == 0);

    /* square 1000 Hz at full volume: 1000 cycles in a second, full scale */
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    int e = rising_edges(buf, RATE);
    CHECK(e >= 999 && e <= 1001);
    CHECK(buf[10] == 32767 || buf[10] == -32767);
    CHECK(peak(buf, RATE) == 32767);

    /* duty 64: a quarter of the cycle high */
    int high = 0;
    voice(0, 1000, SYNTH_SQUARE, 64, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    for (int i = 0; i < RATE; i++) high += buf[i] > 0;
    CHECK(high > RATE / 4 - 200 && high < RATE / 4 + 200);

    /* volume 128: half scale */
    voice(0, 440, SYNTH_SQUARE, 128, 128, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, 4800);
    CHECK(abs(peak(buf, 4800) - 16447) < 200);

    /* release 0: silent right after the gate drops, and the voice is idle */
    regs[SYNTH_CONTROL] = 0;
    synth_render(&s, regs, buf, 480);
    CHECK(peak(buf + 1, 479) == 0 && synth_active(&s) == 0);

    /* triangle and saw: frequency and range */
    synth_init(&s, RATE);
    voice(0, 500, SYNTH_TRIANGLE, 0, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    e = rising_edges(buf, RATE);
    CHECK(e >= 499 && e <= 501);
    CHECK(peak(buf, RATE) > 32000);
    voice(0, 500, SYNTH_SAW, 0, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    e = rising_edges(buf, RATE);
    CHECK(e >= 499 && e <= 501);

    /* attack 255: 2 s to full level, so about half after 1 s */
    synth_init(&s, RATE);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 255, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    CHECK(peak(buf, 480) < 500);
    CHECK(abs(peak(buf + RATE - 480, 480) - 16384) < 400);

    /* decay to a sustain level of 64, then hold */
    synth_init(&s, RATE);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 20, 64, 0, 1);
    synth_render(&s, regs, buf, RATE);
    CHECK(peak(buf, 48) > 30000);
    CHECK(abs(peak(buf + RATE - 480, 480) - 64 * 32767 / 255) < 100);

    /* release 128: about 1 s from full level to silence */
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 128, 1);
    synth_init(&s, RATE);
    synth_render(&s, regs, buf, 480);
    regs[SYNTH_CONTROL] = 0;
    synth_render(&s, regs, buf, RATE * 2);
    CHECK(abs(peak(buf + RATE / 2 - 240, 480) - 16384) < 800);
    CHECK(peak(buf + RATE * 3 / 2, 480) == 0 && synth_active(&s) == 0);

    /* retrigger: a held gate restarts the attack */
    synth_init(&s, RATE);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 255, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    synth_retrigger(&s, 0);
    synth_render(&s, regs, buf, 480);
    CHECK(peak(buf, 480) < 500);

    /* noise: +-full scale, not periodic at the audio rate, LFSR never 0 */
    synth_init(&s, RATE);
    voice(0, 8000, SYNTH_NOISE, 0, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, RATE);
    int pos = 0, changes = 0;
    for (int i = 0; i < RATE; i++) {
        pos += buf[i] > 0;
        changes += i && buf[i] != buf[i - 1];
    }
    CHECK(pos > RATE / 3 && pos < RATE * 2 / 3);
    CHECK(changes > 2000 && changes < 8000);
    CHECK(s.v[0].lfsr != 0);
    /* the first steps of the 15-bit LFSR from 1 (apu.lua noise_step) */
    synth_init(&s, RATE);
    voice(0, RATE, SYNTH_NOISE, 0, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, 3);
    CHECK(buf[0] == 32767 && buf[1] == -32767 && buf[2] == -32767);
    CHECK(s.v[0].lfsr == 0x1000);         /* 1 -> 0x4000 -> 0x2000 -> 0x1000 */

    /* two voices in phase at full volume clip, they are not averaged */
    synth_init(&s, RATE);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    voice(1, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, 480);
    CHECK(peak(buf, 480) == 32767 && synth_active(&s) == 2);
    /* one voice at half volume is not lowered by the others */
    voice(1, 1000, SYNTH_SQUARE, 128, 128, 0, 0, 255, 0, 0);
    voice(0, 1000, SYNTH_SQUARE, 128, 128, 0, 0, 255, 0, 1);
    synth_render(&s, regs, buf, 480);
    CHECK(abs(peak(buf + 10, 470) - 16447) < 200);

    CHECK(synth_rate_increment(0, RATE) == 256.0f);
    CHECK(synth_rate_increment(255, RATE) * 2 * RATE > 254.9f);
}

static void test_iec958(void)
{
    iec958_t e;
    iec958_init(&e, 48000);
    CHECK(e.status[0] == 0x04 && e.status[3] == 2 && e.status[4] == (0x0B | 13 << 4));

    static int16_t in[400];
    static uint32_t out[800];
    for (int i = 0; i < 400; i++) in[i] = (int16_t)(i * 97 - 20000);
    iec958_encode(&e, in, out, 400);
    CHECK(e.frame == 400 % 192);
    for (int i = 0; i < 800; i++) {
        uint32_t w = out[i];
        int frame = (i / 2) % 192;
        CHECK(__builtin_parity(w & ~0xFu) == 0);                /* even parity */
        CHECK((w & 0xF) == (frame == 0 ? 0xFu : 0));            /* B preamble */
        int status = frame < 40 && (e.status[frame / 8] >> (frame % 8) & 1);
        CHECK(!!(w & 0x40000000u) == status);
        CHECK((w >> 4 & 0xFFFFFF) == ((uint32_t)(in[i / 2] * 256) & 0xFFFFFF));
        CHECK(out[i & ~1] == out[i | 1]);                       /* mono on both */
    }
    CHECK(out[2 * 2] & 0x40000000u);            /* status bit 2: PCM consumer */
    CHECK(out[2 * 25] & 0x40000000u);           /* byte 3 bit 1: 48 kHz */
    CHECK(!(out[2 * 24] & 0x40000000u));
}

int main(void)
{
    test_synth();
    test_iec958();
    printf("audio: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
