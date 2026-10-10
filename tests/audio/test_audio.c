/* Host tests for the synthesizer (the chip voice of the first versions,
 * sample for sample, and the clean one), the player of the sound banks,
 * the output's depth (16, 24, 32 bits: the dither's noise and SNR, no DC,
 * no noise in silence), the tone moving within a block without zipper,
 * the IEC 958 subframes sent to the HDMI audio FIFO, the samples (the
 * bank's and the kit's), the effects of the second pass and the presets. */
#include "audio/synth.h"
#include "audio/player.h"
#include "audio/presets.h"
#include "audio/iec958.h"

#include <math.h>
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

/* n frames of the synthesizer; buf gets the left channel */
static int16_t stereo[RATE * 2 * 2];
static void mono(synth_t *s, const uint8_t *r, int16_t *out, int n)
{
    synth_render(s, r, stereo, (unsigned)n);
    for (int i = 0; i < n; i++)
        out[i] = stereo[2 * i];
}

/* the chip of the first versions: every voice raw, no room, no echo */
static void init_retro(synth_t *s)
{
    synth_init(s, RATE);
    s->retro = 1;
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
    static synth_t s;
    const int FULL = (int)(synth_limit(1.0f) * 32767.0f);     /* one voice at full volume */

    /* silence when nothing is gated */
    memset(regs, 0, sizeof regs);
    init_retro(&s);
    mono(&s, regs, buf, 1000);
    CHECK(peak(buf, 1000) == 0 && synth_active(&s) == 0);

    /* square 1000 Hz at full volume: 1000 cycles in a second, full scale */
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    int e = rising_edges(buf, RATE);
    CHECK(e >= 999 && e <= 1001);
    CHECK(buf[10] == FULL || buf[10] == -FULL);
    CHECK(peak(buf, RATE) == FULL);

    /* duty 64: a quarter of the cycle high */
    int high = 0;
    voice(0, 1000, SYNTH_SQUARE, 64, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    for (int i = 0; i < RATE; i++) high += buf[i] > 0;
    CHECK(high > RATE / 4 - 200 && high < RATE / 4 + 200);

    /* volume 128: half scale */
    voice(0, 440, SYNTH_SQUARE, 128, 128, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 4800);
    CHECK(abs(peak(buf, 4800) - 16447) < 200);

    /* release 0: silent right after the gate drops, and the voice is idle */
    regs[SYNTH_CONTROL] = 0;
    mono(&s, regs, buf, 480);
    CHECK(peak(buf + 1, 479) == 0 && synth_active(&s) == 0);

    /* triangle and saw: frequency and range */
    init_retro(&s);
    voice(0, 500, SYNTH_TRIANGLE, 0, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    e = rising_edges(buf, RATE);
    CHECK(e >= 499 && e <= 501);
    CHECK(peak(buf, RATE) > FULL - 300);
    voice(0, 500, SYNTH_SAW, 0, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    e = rising_edges(buf, RATE);
    CHECK(e >= 499 && e <= 501);

    /* attack 255: 2 s to full level, so about half after 1 s */
    init_retro(&s);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 255, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    CHECK(peak(buf, 480) < 500);
    CHECK(abs(peak(buf + RATE - 480, 480) - 16384) < 400);

    /* decay to a sustain level of 64, then hold */
    init_retro(&s);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 20, 64, 0, 1);
    mono(&s, regs, buf, RATE);
    CHECK(peak(buf, 48) > 30000);
    CHECK(abs(peak(buf + RATE - 480, 480) - 64 * 32767 / 255) < 100);

    /* release 128: about 1 s from full level to silence */
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 128, 1);
    init_retro(&s);
    mono(&s, regs, buf, 480);
    regs[SYNTH_CONTROL] = 0;
    mono(&s, regs, buf, RATE * 2);
    CHECK(abs(peak(buf + RATE / 2 - 240, 480) - 16384) < 800);
    CHECK(peak(buf + RATE * 3 / 2, 480) == 0 && synth_active(&s) == 0);

    /* retrigger: a held gate attacks again, from the level it is at (no
     * click), and a silent voice starts its wave from the beginning */
    init_retro(&s);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 255, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    float before = s.v[0].level;
    synth_retrigger(&s, 0);
    mono(&s, regs, buf, 480);
    CHECK(before > 120 && s.v[0].level > before && s.v[0].stage == SYNTH_ATTACK_ST);
    CHECK(peak(buf, 480) > 15000);
    init_retro(&s);
    voice(0, 1000, SYNTH_SINE, 0, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 8);
    CHECK(buf[0] > 0 && buf[0] < 5000 && buf[1] > buf[0]);   /* sin from 0, going up */

    /* noise: +-full scale, not periodic at the audio rate, LFSR never 0 */
    init_retro(&s);
    voice(0, 8000, SYNTH_NOISE, 0, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE);
    int pos = 0, changes = 0;
    for (int i = 0; i < RATE; i++) {
        pos += buf[i] > 0;
        changes += i && buf[i] != buf[i - 1];
    }
    CHECK(pos > RATE / 3 && pos < RATE * 2 / 3);
    CHECK(changes > 2000 && changes < 8000);
    CHECK(s.v[0].lfsr != 0);
    /* the first steps of the 15-bit LFSR from 1 (apu.lua noise_step) */
    init_retro(&s);
    voice(0, RATE, SYNTH_NOISE, 0, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 3);
    CHECK(buf[0] == FULL && buf[1] == -FULL && buf[2] == -FULL);
    CHECK(s.v[0].lfsr == 0x1000);         /* 1 -> 0x4000 -> 0x2000 -> 0x1000 */

    /* two voices in phase at full volume saturate smoothly, they are not
     * averaged */
    init_retro(&s);
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    voice(1, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 480);
    CHECK(peak(buf, 480) > 32000 && peak(buf, 480) < 32767 && synth_active(&s) == 2);
    /* one voice at half volume is not lowered by the others */
    voice(1, 1000, SYNTH_SQUARE, 128, 128, 0, 0, 255, 0, 0);
    voice(0, 1000, SYNTH_SQUARE, 128, 128, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 480);
    CHECK(abs(peak(buf + 10, 470) - 16447) < 200);

    CHECK(synth_rate_increment(0, RATE) == 256.0f);
    CHECK(synth_rate_increment(255, RATE) * 2 * RATE > 254.9f);

    /* the limiter: straight below the knee, smooth and below 1 above */
    CHECK(synth_limit(0.5f) == 0.5f && synth_limit(-0.8f) == -0.8f);
    CHECK(synth_limit(1.0f) > 0.9f && synth_limit(1.0f) < 0.95f);
    CHECK(synth_limit(4.0f) < 1.0f && synth_limit(4.0f) > synth_limit(2.0f));
    CHECK(synth_limit(-2.0f) == -synth_limit(2.0f));

    /* the fraction of a hertz: 440.5 Hz is 881 cycles in 2 s */
    init_retro(&s);
    voice(0, 440, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    regs[SYNTH_FREQ_FRAC] = 128;
    mono(&s, regs, buf, RATE * 2);
    e = rising_edges(buf, RATE * 2);
    CHECK(e >= 880 && e <= 882);
    regs[SYNTH_FREQ_FRAC] = 0;

    /* sine: the shape of sin() within 1% */
    init_retro(&s);
    voice(0, 100, SYNTH_SINE, 0, 128, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 480);
    double worst = 0;
    for (int i = 0; i < 480; i++) {
        double want = sin(2 * M_PI * 100.0 * i / RATE) * 128.0 / 255.0 * 32767.0;
        if (fabs(buf[i] - want) > worst) worst = fabs(buf[i] - want);
    }
    CHECK(worst < 0.01 * 32767);

    /* metal: a noise that repeats after a few dozen steps */
    init_retro(&s);
    voice(0, RATE, SYNTH_METAL, 0, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 2000);
    int period = 0;
    for (int pp = 1; pp < 200 && !period; pp++) {
        int same = 1;
        for (int i = 400; i < 1400 && same; i++) same = buf[i] == buf[i + pp];
        if (same) period = pp;
    }
    CHECK(period > 0 && period < 128);

    /* master volume */
    init_retro(&s);
    s.gain = 0.25f;
    voice(0, 1000, SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 480);
    CHECK(abs(peak(buf, 480) - 8192) < 50);
}

/* ---------------------------------------------------------------- player */

/* a bank under construction */
static uint8_t bank_bytes[8192];
static size_t bank_len;
static void put(int v) { bank_bytes[bank_len++] = (uint8_t)v; }
static void put_name(const char *n) { for (int i = 0; i < 8; i++) put(*n ? *n++ : 0); }
static void put_step(int note, int sound, int vol, int fx) { put(note); put(sound); put(vol); put(fx); }
static void bank_begin(int sounds, int sfx, int patterns, int songs)
{
    bank_len = 0;
    put('B'); put('M'); put('A'); put('U'); put(1);
    put(sounds); put(sfx); put(patterns); put(songs);
    while (bank_len < 16) put(0);
}
static void put_sound(const char *name, int wave, int vol, int a, int d, int s, int r, int pitch, int ptime)
{
    put_name(name);
    put(wave); put(128); put(vol); put(a); put(d); put(s); put(r);
    put(pitch); put(ptime); put(0); put(0); put(0);
    put(0); put(0); put(0); put(0);
}

/* frequency of the samples in [from, from + n): rising edges per second */
static double freq_at(const int16_t *x, int from, int n)
{
    return rising_edges(x + from, n) * (double)RATE / n;
}

/* the first sample (from `from`) louder than `level` */
static int onset(const int16_t *x, int from, int n, int level)
{
    for (int i = from; i < from + n; i++)
        if (abs(x[i]) > level) return i;
    return -1;
}

static au_bank_t bank, bank2;
static synth_t syn;
static player_t pl;
static uint8_t pregs[SYNTH_REG_BYTES];

static void run(int16_t *out, int n)
{
    for (int k = 0; k < n; k += 64) {
        player_advance(&pl, 64);
        mono(&syn, pregs, out + k, 64);
    }
}

static void fresh(au_bank_t *b)
{
    memset(pregs, 0, sizeof pregs);
    init_retro(&syn);
    player_init(&pl, RATE, pregs, &syn);
    player_set_bank(&pl, b);
}

static void test_player(void)
{
    char err[64];

    /* parse errors */
    CHECK(au_parse((const uint8_t *)"XXXX", 4, &bank, err, sizeof err) == -1);
    bank_begin(1, 0, 0, 0);
    CHECK(au_parse(bank_bytes, bank_len, &bank, err, sizeof err) == -1);    /* sound missing */

    /* sounds 0 (square, steady) and 1 (sine kick: +24 semitones gliding
     * down in 50 ms); sfx 0: A4 then A5, 100 ms steps; sfx 1: a looping
     * C5 with an arpeggio; pattern 0: 16 steps, track 0 A3 on steps 0
     * and 4, track 1 nothing; pattern 1: track 2 a glide; song 0: 120
     * BPM, patterns 0 1, loops to 0; song 1: 120 BPM swing 50, pattern 0,
     * stops */
    bank_begin(3, 2, 2, 2);
    put_sound("LEAD", SYNTH_SQUARE, 255, 0, 0, 255, 0, 0, 0);
    put_sound("KICK", SYNTH_SINE, 255, 0, 40, 0, 0, 24, 5);
    put_sound("BLIP", SYNTH_SQUARE, 255, 0, 30, 0, 0, 0, 0);
    put_name("UP"); put(100); put(0); put(2); put(0); put(0); put(0); put(0); put(0);
    put_step(69, 0, 255, 0); put_step(81, 0, 255, 0);
    put_name("ARP"); put(50); put(0); put(2); put(0); put(2); put(0); put(0); put(0);
    put_step(72, 0, 255, AU_FX_CHORD << 4 | 1); put_step(0, 0, 255, 0);
    put(16); put(0x01); put(0); put(0);
    for (int i = 0; i < 16; i++) put_step(i == 0 || i == 4 ? 57 : 0, 2, 255, 0);
    put(16); put(0x04); put(0); put(0);
    for (int i = 0; i < 16; i++) put_step(i == 0 ? 60 : i == 1 ? 72 : 0, 0, 255, i == 1 ? AU_FX_GLIDE << 4 | 3 : 0);
    put_name("THEME"); put(120); put(0); put(2); put(0); put(0); put(0); put(0); put(0); put(0); put(1);
    put_name("SWING"); put(120); put(50); put(1); put(255); put(0); put(0); put(0); put(0); put(0);
    CHECK(au_parse(bank_bytes, bank_len, &bank, err, sizeof err) == 0);
    CHECK(bank.nsounds == 3 && bank.nsfx == 2 && bank.npatterns == 2 && bank.nsongs == 2);
    CHECK(!strcmp(bank.sound[1].name, "KICK") && bank.sound[1].pitch == 24);
    CHECK(bank.song[0].tracks == 0x05 && bank.song[1].loop == AU_NO_LOOP);
    CHECK(bank.sfx[1].loop_end == 2);

    CHECK(fabs(au_note_hz(69) - 440.0f) < 0.01f && fabs(au_note_hz(60) - 261.63f) < 0.01f);
    CHECK(fabs(au_hz_note(880) - 81.0f) < 0.001f);

    /* sfx 0: A4 for 100 ms, then A5; it ends and frees its voice */
    fresh(&bank);
    CHECK(player_sfx(&pl, 0, 3, 0, 1.0f) == 3);
    run(buf, RATE / 4);
    CHECK(fabs(freq_at(buf, 480, 4320) - 440) < 12);
    CHECK(fabs(freq_at(buf, 4800 + 480, 4320) - 880) < 12);
    CHECK(pl.v[3].owner == AU_OWN_NONE && pl.sfx[3].n == -1 && !player_busy(&pl, 3));
    /* transposed down an octave, at half volume */
    fresh(&bank);
    player_sfx(&pl, 0, 0, -12, 0.5f);
    run(buf, 4800);
    CHECK(fabs(freq_at(buf, 480, 4320) - 220) < 12);
    CHECK(abs(peak(buf, 4800) - 16384) < 300);

    /* a looping sfx goes on until stopped; its chord effect runs through
     * C E G at 60 notes a second */
    fresh(&bank);
    int v = player_sfx(&pl, 1, -1, 0, 1.0f);
    CHECK(v == 7);                       /* no music: the highest voice */
    run(buf, RATE);
    CHECK(player_sfx_pos(&pl, 7, NULL) == 1 && player_busy(&pl, 7));
    double f0 = freq_at(buf, 0, 800), f1 = freq_at(buf, 800, 800), f2 = freq_at(buf, 1600, 800);
    CHECK(fabs(f0 - 523) < 70 && fabs(f1 - 659) < 70 && fabs(f2 - 784) < 70);
    player_sfx_stop(&pl, -1);
    run(buf, 4800);
    CHECK(!player_busy(&pl, 7) && pl.v[7].owner == AU_OWN_NONE);

    /* song 0 at 120 BPM: a step is 125 ms (6000 samples); track 0 plays
     * A3 on steps 0 and 4 (0 ms and 500 ms), within one block */
    fresh(&bank);
    player_music(&pl, 0, 0, 0);
    static int16_t song_buf[RATE * 5];
    run(song_buf, RATE * 5);
    CHECK(onset(song_buf, 0, 4000, 1000) < 64);
    CHECK(fabs(freq_at(song_buf, 1000, 4000) - 220) < 12);
    int s, o, st, pt;
    CHECK(player_music_pos(&pl, &s, &o, &st, &pt) == 1 && s == 0);
    /* 5 s = 40 steps: position 0 again (loop), step 8 */
    CHECK(o == 0 && st == 7 && pt == 0);
    /* pattern 1: C4 then a glide up to C5 over a step */
    fresh(&bank);
    player_music(&pl, 0, 1, 0);
    run(song_buf, 6000 * 3);
    CHECK(fabs(freq_at(song_buf, 1000, 4000) - 261.6) < 12);
    double mid = freq_at(song_buf, 6000 + 2000, 2000), end = freq_at(song_buf, 12000 + 1000, 4000);
    CHECK(mid > 300 && mid < 500 && fabs(end - 523) < 15);
    /* while the music plays tracks 0 and 2, an effect avoids them */
    v = player_sfx(&pl, 0, -1, 0, 1.0f);
    CHECK(v == 7);
    v = player_sfx(&pl, 0, -1, 0, 1.0f);
    CHECK(v == 6);
    /* a muted track is silent */
    player_mute(&pl, 2, 1);
    run(song_buf, 6000 * 2);
    CHECK(pl.m.mute == 0x04);

    /* swing 50: the steps of each pair last 1.25 and 0.75 of 125 ms;
     * song 1 stops after its pattern */
    fresh(&bank);
    player_music(&pl, 1, 0, 0);
    run(song_buf, RATE * 3);
    CHECK(onset(song_buf, 0, 1000, 1000) < 64);
    /* step 4 starts after 2 x (7500 + 4500) = 24000 samples, as without swing */
    CHECK(abs(onset(song_buf, 23000, 4000, 1000) - 24000) <= 64);
    CHECK(player_music_pos(&pl, NULL, NULL, NULL, NULL) == 0);       /* stopped */

    /* tempo x2: step 4 after 250 ms */
    fresh(&bank);
    player_tempo(&pl, 2.0f);
    player_music(&pl, 0, 0, 0);
    run(song_buf, RATE);
    CHECK(abs(onset(song_buf, 11000, 2000, 1000) - 12000) <= 64);

    /* fade out: silent after the fade, music stopped */
    fresh(&bank);
    player_music(&pl, 0, 0, 0);
    run(song_buf, 4800);
    player_music_stop(&pl, 200);
    run(song_buf, RATE / 2);
    CHECK(peak(song_buf + RATE / 2 - 2000, 2000) == 0 && !player_music_pos(&pl, NULL, NULL, NULL, NULL));

    /* the kick: its pitch starts 2 octaves up and falls in 50 ms */
    fresh(&bank);
    player_play(&pl, 0, 1, 45, 255, 0, 200);
    run(buf, 9600);
    CHECK(freq_at(buf, 0, 480) > 250 && fabs(freq_at(buf, 4800, 4800) - 110) < 15);

    /* a new bank while the music plays: it goes on from its place */
    fresh(&bank);
    player_music(&pl, 0, 0, 0);
    run(song_buf, 6000 * 3 + 100);
    memcpy(&bank2, &bank, sizeof bank);
    player_set_bank(&pl, &bank2);
    run(song_buf, 6000);
    CHECK(player_music_pos(&pl, &s, &o, &st, &pt) && o == 0 && st == 4);
    player_set_bank(&pl, NULL);
    run(song_buf, RATE / 2);
    CHECK(!player_music_pos(&pl, NULL, NULL, NULL, NULL));
    for (int ch = 0; ch < 8; ch++) CHECK(!player_busy(&pl, ch));

    /* a pattern looping (the editor) */
    fresh(&bank);
    player_music_pattern(&pl, 0, 240, 0, 0);
    run(song_buf, RATE * 2);                /* 240 BPM: 62.5 ms a step, 2 s = 32 steps */
    CHECK(player_music_pos(&pl, &s, &o, &st, &pt) && s == -2 && pt == 0 && st == 15);

    /* Lua notes: slide, vibrato and arpeggio on note()s */
    fresh(NULL);
    memset(pregs, 0, sizeof pregs);
    for (int ch = 0; ch < 2; ch++) {
        pregs[ch * SYNTH_VOICE_BYTES + SYNTH_DUTY] = 128;
        pregs[ch * SYNTH_VOICE_BYTES + SYNTH_VOLUME] = 255;
        pregs[ch * SYNTH_VOICE_BYTES + SYNTH_SUSTAIN] = 255;
    }
    player_lua_note(&pl, 0, 220.0f, 0);
    player_slide(&pl, 0, 440.0f, 100);
    run(buf, 9600);
    CHECK(fabs(freq_at(buf, 6000, 3600) - 440) < 12);
    CHECK(pregs[SYNTH_FREQ_LO] + pregs[SYNTH_FREQ_HI] * 256 == 440);
    static const int8_t fifth[] = { 0, 7 };
    player_arp(&pl, 0, fifth, 2, 50);
    run(buf, 9600);
    double a0 = freq_at(buf, 2400 * 0 + 100, 2200), a1 = freq_at(buf, 2400 + 100, 2200);
    CHECK((fabs(a0 - 440) < 25 && fabs(a1 - 659) < 30) || (fabs(a0 - 659) < 30 && fabs(a1 - 440) < 25));
    player_arp(&pl, 0, NULL, 0, 0);
    player_lua_freq(&pl, 0, 261.63f);
    run(buf, 4800);
    CHECK(fabs(freq_at(buf, 0, 4800) - 261.6) < 12);
    CHECK(pregs[SYNTH_FREQ_LO] + pregs[SYNTH_FREQ_HI] * 256 == 261 && pregs[SYNTH_FREQ_FRAC] > 150);
    player_lua_off(&pl, 0);
    run(buf, 4800);
    CHECK(pl.v[0].owner == AU_OWN_NONE);
    /* a timed note releases itself */
    player_lua_note(&pl, 1, 1000.0f, 20);
    run(buf, 960);
    CHECK(pregs[SYNTH_VOICE_BYTES + SYNTH_CONTROL] & SYNTH_GATE);
    run(buf, 960);
    CHECK(!(pregs[SYNTH_VOICE_BYTES + SYNTH_CONTROL] & SYNTH_GATE));
}

/* ---------------------------------------------------------------- the clean voice */

/* power of x at hz (a Hann-windowed DFT bin) */
static double power_at(const int16_t *x, int n, double hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        re += x[i] * w * cos(2 * M_PI * hz * i / RATE);
        im -= x[i] * w * sin(2 * M_PI * hz * i / RATE);
    }
    return re * re + im * im;
}

/* an alias of a square at f: harmonic k folded back under Nyquist */
static double alias_hz(double f, int k)
{
    double h = f * k;
    while (h > RATE)
        h -= RATE;
    return h > RATE / 2 ? RATE - h : h;
}

static void tone(int ch, int reg, int value)
{
    regs[ch * SYNTH_VOICE_BYTES + reg] = (uint8_t)value;
}

static void clean_voice(int ch, int freq, int wave, int vol, int a, int d, int s, int r, int gate)
{
    voice(ch, freq, wave, 128, vol, a, d, s, r, gate);
    for (int i = SYNTH_CUTOFF; i < SYNTH_VOICE_BYTES; i++)
        tone(ch, i, 0);
}

static int crossings_hz(const int16_t *x, int from, int n)
{
    return (int)(rising_edges(x + from, n) * (double)RATE / n + 0.5);
}

/* the pitch by autocorrelation (a string's harmonics cross zero too) */
static double ac_hz(const int16_t *x, int n)
{
    int best = 0;
    double bv = -1e300;
    for (int lag = RATE / 2000; lag < RATE / 50; lag++) {
        double c = 0;
        for (int i = 0; i + lag < n; i++)
            c += (double)x[i] * x[i + lag];
        if (c > bv) { bv = c; best = lag; }
    }
    /* the peak between samples (a parabola through three) */
    double c0 = 0, c2 = 0;
    for (int i = 0; i + best + 1 < n; i++) {
        c0 += (double)x[i] * x[i + best - 1];
        c2 += (double)x[i] * x[i + best + 1];
    }
    double d = 0.5 * (c0 - c2) / (c0 - 2 * bv + c2);
    return RATE / (best + d);
}

static void test_clean(void)
{
    static synth_t s;

    /* band-limited: a square at 3520 Hz, its folded harmonics 20 dB or
     * more under the chip's */
    static int16_t chip[8192], clean[8192];
    memset(regs, 0, sizeof regs);
    init_retro(&s);
    clean_voice(0, 3520, SYNTH_SQUARE, 200, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 4800);
    mono(&s, regs, chip, 8192);
    synth_init(&s, RATE);
    mono(&s, regs, buf, 4800);
    mono(&s, regs, clean, 8192);
    double worse = 0;
    for (int k = 7; k <= 41; k += 2) {            /* harmonics past 24 kHz folded under 8 kHz */
        double a = alias_hz(3520, k);
        if (k * 3520 < RATE / 2 || a > 8000)
            continue;
        double ratio = power_at(clean, 8192, a) / (power_at(chip, 8192, a) + 1);
        if (ratio > worse) worse = ratio;
    }
    CHECK(worse < 0.01);                          /* -20 dB at least */
    CHECK(fabs(power_at(clean, 8192, 3520) / power_at(chip, 8192, 3520) - 1) < 0.1);  /* the note as loud */

    /* pitch of every wave (the noises aside): rising edges per second */
    static const int waves[] = { SYNTH_SQUARE, SYNTH_TRIANGLE, SYNTH_SAW, SYNTH_SINE, SYNTH_FM, SYNTH_PLUCK,
                                 SYNTH_ORGAN };
    for (unsigned w = 0; w < sizeof waves / sizeof waves[0]; w++) {
        synth_init(&s, RATE);
        clean_voice(0, 220, waves[w], 200, 0, 0, 255, 0, 1);
        if (waves[w] == SYNTH_FM)
            tone(0, SYNTH_MOD2, 16);              /* a light depth: one crossing a cycle */
        if (waves[w] == SYNTH_PLUCK)
            tone(0, SYNTH_MOD1, 40);              /* a dull string: the fundamental leads */
        mono(&s, regs, buf, RATE / 2);
        int hz = waves[w] == SYNTH_PLUCK ? (int)(ac_hz(buf + 2400, 4096) + 0.5) : crossings_hz(buf, 4800, RATE / 2 - 4800);
        CHECK(abs(hz - 220) <= 2);
        if (abs(hz - 220) > 2) printf("  wave %d: %d Hz\n", waves[w], hz);
    }

    /* no click: attack 0 still rises over a few samples */
    synth_init(&s, RATE);
    clean_voice(0, 1000, SYNTH_SQUARE, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 480);
    CHECK(abs(buf[0]) < 4000 && peak(buf, 480) > 25000);

    /* exponential decay: 5% left at the time asked (decay 64 = 0.5 s),
     * then the sustain level */
    synth_init(&s, RATE);
    clean_voice(0, 1000, SYNTH_SQUARE, 80, 0, 64, 0, 0, 1);       /* under the compressor */
    mono(&s, regs, buf, RATE);
    int p0 = peak(buf + 480, 480), p_half = peak(buf + RATE / 2 - 240, 480);
    CHECK(p_half > p0 * 0.03 && p_half < p0 * 0.08);
    CHECK(peak(buf + RATE / 4 - 240, 480) > p0 * 0.15);     /* not straight: about 22% at half way */
    /* release 64: idle when 60 dB down, about 2.3 x 0.5 s */
    synth_init(&s, RATE);
    clean_voice(0, 1000, SYNTH_SQUARE, 255, 0, 0, 255, 64, 1);
    mono(&s, regs, buf, 4800);
    tone(0, SYNTH_CONTROL, 0);
    mono(&s, regs, buf, RATE);
    CHECK(synth_active(&s) == 1);
    mono(&s, regs, buf, RATE / 2);
    CHECK(synth_active(&s) == 0);

    /* the filter: a low pass at 500 Hz takes most of a saw's highs; with
     * resonance its cutoff rings (quiet enough for the compressor to rest) */
    synth_init(&s, RATE);
    clean_voice(0, 110, SYNTH_SAW, 80, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 4800);
    mono(&s, regs, clean, 8192);
    double h20 = power_at(clean, 8192, 2200), h1 = power_at(clean, 8192, 110);
    uint8_t c500 = (uint8_t)(1 + log2(500.0 / 20.0) * 254 / 9.9658 + 0.5);
    CHECK(fabs(synth_cutoff_hz(c500) - 500) < 15);
    tone(0, SYNTH_CUTOFF, c500);
    mono(&s, regs, buf, 4800);
    mono(&s, regs, clean, 8192);
    CHECK(power_at(clean, 8192, 2200) / h20 < 0.01);           /* -20 dB at 2.2 kHz */
    CHECK(power_at(clean, 8192, 110) / h1 > 0.8);               /* the fundamental stays */
    double h5 = power_at(clean, 8192, 550);
    tone(0, SYNTH_RESONANCE, 240);
    mono(&s, regs, buf, 4800);
    mono(&s, regs, clean, 8192);
    CHECK(power_at(clean, 8192, 550) > 10 * h5);               /* the ring near 500 Hz */
    tone(0, SYNTH_FILTER, SYNTH_HIGHPASS);
    tone(0, SYNTH_RESONANCE, 0);
    mono(&s, regs, buf, 4800);
    mono(&s, regs, clean, 8192);
    CHECK(power_at(clean, 8192, 110) / h1 < 0.01 && power_at(clean, 8192, 2200) / h20 > 0.5);

    /* place: hard left is silent on the right */
    synth_init(&s, RATE);
    clean_voice(0, 440, SYNTH_SINE, 200, 0, 0, 255, 0, 1);
    tone(0, SYNTH_PAN, (uint8_t)-127);
    synth_render(&s, regs, stereo, 4800);
    int pl_ = 0, pr = 0;
    for (int i = 0; i < 4800; i++) {
        if (abs(stereo[2 * i]) > pl_) pl_ = abs(stereo[2 * i]);
        if (abs(stereo[2 * i + 1]) > pr) pr = abs(stereo[2 * i + 1]);
    }
    CHECK(pl_ > 15000 && pr < 300);

    /* the room: a dry note ends, a note with the send rings on */
    for (int send = 0; send <= 255; send += 255) {
        synth_init(&s, RATE);
        clean_voice(0, 440, SYNTH_SINE, 200, 0, 10, 0, 0, 1);
        tone(0, SYNTH_REVERB, send);
        mono(&s, regs, buf, RATE / 2);
        int tail = peak(buf + RATE / 4, RATE / 4);
        CHECK(send ? tail > 300 : tail < 30);
    }
    /* the echo: the note again after the echo's time, left then right */
    synth_init(&s, RATE);
    synth_echo(&s, 200, 0.5f, 1.0f);
    clean_voice(0, 440, SYNTH_SINE, 200, 0, 5, 0, 0, 1);
    tone(0, SYNTH_ECHO, 255);
    synth_render(&s, regs, stereo, RATE / 2);
    int l1 = 0, r1 = 0, r2 = 0;
    for (int i = 9600; i < 9600 + 2400; i++) {          /* 200 ms: the first repeat, on the left */
        if (abs(stereo[2 * i]) > l1) l1 = abs(stereo[2 * i]);
        if (abs(stereo[2 * i + 1]) > r1) r1 = abs(stereo[2 * i + 1]);
    }
    for (int i = 19200; i < 19200 + 2400; i++)          /* 400 ms: the second, on the right */
        if (abs(stereo[2 * i + 1]) > r2) r2 = abs(stereo[2 * i + 1]);
    CHECK(l1 > 2000 && r1 < l1 / 4 && r2 > 1000);

    /* eight loud voices: the compressor keeps them under the limiter's bend */
    synth_init(&s, RATE);
    for (int ch = 0; ch < 8; ch++)
        clean_voice(ch, 200 + 37 * ch, SYNTH_SAW, 255, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, RATE / 2);
    int bent = 0;
    for (int i = RATE / 8; i < RATE / 2; i++)
        bent += abs(buf[i]) > (int)(0.85f * 32767);
    CHECK(bent < RATE / 200);                            /* under 1% of the samples */

    /* the raw bit: one voice as the chip, the others clean */
    memset(regs, 0, sizeof regs);
    synth_init(&s, RATE);
    clean_voice(0, 1000, SYNTH_SQUARE, 255, 0, 0, 255, 0, 1);
    tone(0, SYNTH_CONTROL, SYNTH_GATE | SYNTH_RAW);
    mono(&s, regs, buf, 480);
    CHECK(abs(buf[2]) > 20000);                          /* no minimum attack */

    /* the same sound every time: the noise and the string from a fixed seed */
    static int16_t again[4800];
    synth_init(&s, RATE);
    clean_voice(0, 330, SYNTH_PLUCK, 200, 0, 0, 255, 0, 1);
    mono(&s, regs, buf, 4800);
    synth_init(&s, RATE);
    mono(&s, regs, again, 4800);
    CHECK(!memcmp(buf, again, sizeof again));
}

/* ---------------------------------------------------------------- bank version 2 */

static void test_bank2(void)
{
    char err[64];
    /* one sound with a tone (filter, place, room, echo), one song with the
     * echo in steps and the room's size */
    bank_len = 0;
    put('B'); put('M'); put('A'); put('U'); put(2);
    put(1); put(0); put(1); put(1);
    while (bank_len < 16) put(0);
    put_name("ACID");
    put(SYNTH_SAW); put(128); put(255); put(0); put(30); put(100); put(10);
    put(0); put(0); put(0); put(0); put(0);
    put(0); put(0); put(0); put(0);
    uint8_t tone_regs[AU_TONE] = { 0 };
    tone_regs[SYNTH_CUTOFF - SYNTH_CUTOFF] = 90;
    tone_regs[SYNTH_RESONANCE - SYNTH_CUTOFF] = 180;
    tone_regs[SYNTH_PAN - SYNTH_CUTOFF] = (uint8_t)-60;
    tone_regs[SYNTH_REVERB - SYNTH_CUTOFF] = 0;
    tone_regs[SYNTH_ECHO - SYNTH_CUTOFF] = 77;
    for (int i = 0; i < AU_TONE; i++) put(tone_regs[i]);
    put(0); put(0); put(0);
    put(16); put(0x01); put(0); put(0);
    for (int i = 0; i < 16; i++) put_step(i == 0 ? 45 : 0, 0, 255, 0);
    put_name("SONG"); put(100); put(0); put(1); put(0); put(4); put(200); put(0); put(0); put(0);
    CHECK(au_parse(bank_bytes, bank_len, &bank, err, sizeof err) == 0);
    CHECK(bank.sound[0].tone[0] == 90 && bank.sound[0].tone[SYNTH_ECHO - SYNTH_CUTOFF] == 77);
    CHECK(bank.song[0].echo == 4 && bank.song[0].room == 200);

    /* the song sets the echo (4 steps at 100 BPM: 600 ms) and the room;
     * its note carries the tone into the voice's registers */
    memset(pregs, 0, sizeof pregs);
    synth_init(&syn, RATE);
    player_init(&pl, RATE, pregs, &syn);
    player_set_bank(&pl, &bank);
    player_music(&pl, 0, 0, 0);
    run(buf, 4800);
    CHECK(abs((int)syn.echo_len - RATE * 6 / 10) < 2 && fabs(syn.room_size - 200 / 255.0f) < 0.01f);
    CHECK(pregs[SYNTH_CUTOFF] == 90 && pregs[SYNTH_RESONANCE] == 180 && (int8_t)pregs[SYNTH_PAN] == -60);
    CHECK(pregs[SYNTH_ECHO] == 77 && pregs[SYNTH_REVERB] == 0 && pl.v[0].bank_tone);
    /* a Lua note on that voice does not keep the bank sound's tone */
    player_music_stop(&pl, 0);
    player_lua_note(&pl, 0, 440.0f, 100);
    CHECK(pregs[SYNTH_CUTOFF] == 0 && pregs[SYNTH_ECHO] == 0 && pregs[SYNTH_REVERB] == AU_ROOM_SEND);

    /* a version 1 sound: the plain tone with a little room */
    bank_begin(1, 0, 0, 0);
    put_sound("OLD", SYNTH_SQUARE, 255, 0, 0, 255, 0, 0, 0);
    CHECK(au_parse(bank_bytes, bank_len, &bank, err, sizeof err) == 0);
    CHECK(bank.sound[0].tone[SYNTH_REVERB - SYNTH_CUTOFF] == AU_ROOM_SEND && bank.sound[0].tone[0] == 0);
    /* the console's voice at the start */
    uint8_t v[SYNTH_VOICE_BYTES];
    memset(v, 0xAA, sizeof v);
    au_voice_default(v);
    CHECK(v[SYNTH_DUTY] == 128 && v[SYNTH_SUSTAIN] == 255 && v[SYNTH_CONTROL] == 0 && v[SYNTH_CUTOFF] == 0);
    CHECK(v[SYNTH_REVERB] == AU_ROOM_SEND && v[31] == 0);
}

static void test_iec958(void)
{
    iec958_t e;
    iec958_init(&e, 48000);
    CHECK(e.status[0] == 0x04 && e.status[3] == 2 && e.status[4] == (0x0B | 13 << 4));

    static int16_t in[800];
    static uint32_t out[800];
    for (int i = 0; i < 800; i++) in[i] = (int16_t)(i * 97 - 20000 + (i & 1) * 5000);
    iec958_encode(&e, in, out, 400);
    CHECK(e.frame == 400 % 192);
    for (int i = 0; i < 800; i++) {
        uint32_t w = out[i];
        int frame = (i / 2) % 192;
        CHECK(__builtin_parity(w & ~0xFu) == 0);                /* even parity */
        CHECK((w & 0xF) == (frame == 0 ? 0xFu : 0));            /* B preamble */
        int status = frame < 40 && (e.status[frame / 8] >> (frame % 8) & 1);
        CHECK(!!(w & 0x40000000u) == status);
        CHECK((w >> 4 & 0xFFFFFF) == ((uint32_t)(in[i] * 256) & 0xFFFFFF));  /* left, right */
    }
    CHECK(out[2 * 2] & 0x40000000u);            /* status bit 2: PCM consumer */
    CHECK(out[2 * 25] & 0x40000000u);           /* byte 3 bit 1: 48 kHz */
    CHECK(!(out[2 * 24] & 0x40000000u));

    /* 24 bits (audio_render32's words, aligned to the left): all 24 in
     * bits 4..27, the low byte too (not a 16-bit sample times 256) */
    static int32_t in32[800];
    uint32_t r = 12345;
    for (int i = 0; i < 800; i++) {
        r = r * 1664525u + 1013904223u;
        in32[i] = (int32_t)(r & 0xFFFFFF00u);
    }
    in32[0] = (int32_t)0x80000000u;             /* the extremes */
    in32[1] = 0x7FFFFF00;
    iec958_init(&e, 48000);
    iec958_encode32(&e, in32, out, 400);
    int low = 0, good = 1;
    for (int i = 0; i < 800; i++) {
        uint32_t w = out[i];
        int frame = (i / 2) % 192;
        int status = frame < 40 && (e.status[frame / 8] >> (frame % 8) & 1);
        good &= __builtin_parity(w & ~0xFu) == 0;
        good &= (w & 0xF) == (frame == 0 ? 0xFu : 0);
        good &= !!(w & 0x40000000u) == status;
        good &= (w >> 4 & 0xFFFFFF) == ((uint32_t)in32[i] >> 8);
        low += (w >> 4 & 0xFF) != 0;
    }
    CHECK(good);
    CHECK(low > 700);                           /* the low byte reaches the subframe */
    CHECK((out[0] >> 4 & 0xFFFFFF) == 0x800000 && (out[1] >> 4 & 0xFFFFFF) == 0x7FFFFF);
}

/* ---------------------------------------------------------------- the output's depth */

#define FFT_N 32768
static double fre[FFT_N], fim[FFT_N];

static void fft(double *xr, double *xi, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = xr[i]; xr[i] = xr[j]; xr[j] = t;
            t = xi[i]; xi[i] = xi[j]; xi[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                double *ar = &xr[i + k], *ai = &xi[i + k], *br = &xr[i + k + len / 2], *bi = &xi[i + k + len / 2];
                double vr = *br * wr - *bi * wi, vi = *br * wi + *bi * wr;
                *br = *ar - vr; *bi = *ai - vi;
                *ar += vr; *ai += vi;
            }
    }
}

/* n frames of the left channel from `from`, Hann-windowed (and padded with
 * 0 to FFT_N), into fre/fim */
static void spectrum16(const int16_t *st, int from, int n)
{
    for (int i = 0; i < FFT_N; i++) {
        fre[i] = i < n ? st[2 * (from + i)] * (0.5 - 0.5 * cos(2 * M_PI * i / (n - 1))) : 0;
        fim[i] = 0;
    }
    fft(fre, fim, FFT_N);
}

static double band(double lo, double hi)
{
    double p = 0;
    for (int b = (int)(lo * FFT_N / RATE); b <= (int)(hi * FFT_N / RATE); b++)
        p += fre[b] * fre[b] + fim[b] * fim[b];
    return p;
}

/* the sidebands at the block rate (750 Hz x k, k = 1..k_max, each +-w Hz)
 * around a carrier at hz, in dB under it */
static double sidebands(double hz, int k_max, double w)
{
    double car = band(hz - 250, hz + 250), side = 0;
    for (int k = 1; k <= k_max; k++) {
        side += band(hz + 750 * k - w, hz + 750 * k + w);
        if (hz - 750 * k - w > 50)
            side += band(hz - 750 * k - w, hz - 750 * k + w);
    }
    return 10 * log10(side / car);
}

/* the amplitude of a component at hz with whole cycles in n samples */
static double amp_at(const double *x, int n, double hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) {
        re += x[i] * cos(2 * M_PI * hz * i / RATE);
        im += x[i] * sin(2 * M_PI * hz * i / RATE);
    }
    return 2 * sqrt(re * re + im * im) / n;
}

static float fin[RATE * 2];
static int32_t q32[RATE * 2];
static int16_t q16[RATE * 2];
static double dy[RATE], dx[RATE];

/* the left samples of q32 at `bits`, in LSBs of that depth */
static double lsb_of(int32_t w, unsigned bits)
{
    return bits == 32 ? (double)w : (double)(w >> (32 - bits));
}

/* sine of `amp` (1.0 full scale) at hz, quantized: the SNR in dB */
static double snr(synth_t *s, unsigned bits, double amp, double hz)
{
    double full = bits == 32 ? 2147483648.0 : (double)((1u << (bits - 1)) - 1);
    for (int i = 0; i < RATE; i++)
        fin[2 * i] = fin[2 * i + 1] = (float)(amp * sin(2 * M_PI * hz * i / RATE));
    synth_quantize(s, fin, q32, RATE, bits, SYNTH_MIX_SOUND);
    double sig = 0, err = 0;
    for (int i = 0; i < 2 * RATE; i++) {
        double x = fin[i], e = lsb_of(q32[i], bits) / full - x;
        sig += x * x;
        err += e * e;
    }
    return 10 * log10(sig / err);
}

static void test_depth(void)
{
    static synth_t s;
    synth_init(&s, RATE);

    /* a sine at -1 dBFS: 16 bits with TPDF dither 92.3 dB in theory (the
     * rounding's 1/12 LSB^2 and the dither's 1/6), 24 bits 140.5, 32 the
     * float's own (more than 150) */
    double s16 = snr(&s, 16, 0.891, 997), s24 = snr(&s, 24, 0.891, 997), s32 = snr(&s, 32, 0.891, 997);
    printf("  SNR of a 997 Hz sine at -1 dBFS: %.1f dB (16 bits), %.1f dB (24), %.1f dB (32)\n", s16, s24, s32);
    CHECK(s16 > 90.5 && s16 < 94.0);
    CHECK(s24 > 136.0 && s24 < 142.0);
    CHECK(s32 > 150.0);

    /* below one step: a 1 kHz sine of 0.6 LSB. Rounded plainly it is a
     * pulse (its third harmonic 0.42 LSB, the note itself 0.70); with the
     * dither the note is there as it is, 0.6, and no harmonic: noise
     * instead of distortion, at 16 and at 24 bits (the room's tails) */
    for (unsigned bits = 16; bits <= 24; bits += 8) {
        double full = (double)((1u << (bits - 1)) - 1);
        for (int i = 0; i < RATE; i++)
            fin[2 * i] = fin[2 * i + 1] = (float)(0.6 / full * sin(2 * M_PI * 1000.0 * i / RATE));
        synth_quantize(&s, fin, q32, RATE, bits, SYNTH_MIX_SOUND);
        for (int i = 0; i < RATE; i++) {
            dy[i] = lsb_of(q32[2 * i], bits);
            dx[i] = floor(fin[2 * i] * full + 0.5);     /* plain rounding */
        }
        double d1 = amp_at(dy, RATE, 1000), d3 = amp_at(dy, RATE, 3000);
        double p1 = amp_at(dx, RATE, 1000), p3 = amp_at(dx, RATE, 3000);
        printf("  0.6 LSB at %u bits: dithered %.3f / 3rd %.4f LSB, plainly rounded %.3f / 3rd %.3f LSB\n",
               bits, d1, d3, p1, p3);
        CHECK(fabs(d1 - 0.6) < 0.03 && d3 < 0.02);
        CHECK(p3 > 0.3);
    }

    /* the dither's noise: 0.5 LSB rms (1/4 LSB^2), no DC */
    for (unsigned bits = 16; bits <= 24; bits += 8) {
        for (int i = 0; i < 2 * RATE; i++)
            fin[i] = 0;
        synth_quantize(&s, fin, q32, RATE, bits, SYNTH_MIX_SOUND);
        double sum = 0, sq = 0;
        int most = 0;
        for (int i = 0; i < 2 * RATE; i++) {
            double v = lsb_of(q32[i], bits);
            sum += v;
            sq += v * v;
            most = abs((int)v) > most ? abs((int)v) : most;
        }
        double mean = sum / (2 * RATE), rms = sqrt(sq / (2 * RATE));
        CHECK(fabs(mean) < 0.01 && rms > 0.45 && rms < 0.55 && most == 1);
        CHECK((q32[0] & ((1 << (32 - bits)) - 1)) == 0);      /* the bits under the depth 0 */
    }

    /* past full scale: clipped, not wrapped (1.0 is 32767, give or take
     * the dither's step) */
    int clipped = 1;
    for (int t = 0; t < 200; t++) {
        fin[0] = 1.5f; fin[1] = -1.5f; fin[2] = 1.0f; fin[3] = -1.0f;
        synth_quantize(&s, fin, q32, 2, 16, SYNTH_MIX_SOUND);
        clipped &= q32[0] >> 16 >= 32766 && q32[1] >> 16 <= -32766 && q32[2] >> 16 >= 32766 && q32[3] >> 16 <= -32766;
        synth_quantize(&s, fin, q32, 2, 24, SYNTH_MIX_SOUND);
        clipped &= q32[0] >> 8 >= 8388606 && q32[1] >> 8 <= -8388606;
        synth_quantize16(&s, fin, q16, 2, SYNTH_MIX_SOUND);
        clipped &= q16[0] >= 32766 && q16[1] <= -32766;
    }
    synth_quantize(&s, fin, q32, 2, 32, SYNTH_MIX_SOUND);
    CHECK(clipped && q32[0] > 0x7FFFFF00 && q32[1] == (int32_t)0x80000000u);

    /* silence: nothing at any depth, not even the dither's noise */
    memset(regs, 0, sizeof regs);
    synth_init(&s, RATE);
    int quiet = 1;
    for (unsigned bits = 16; bits <= 32; bits += 8) {
        synth_render32(&s, regs, q32, 4800, bits);
        for (int i = 0; i < 9600; i++)
            quiet &= q32[i] == 0;
    }
    synth_render(&s, regs, q16, 4800);
    for (int i = 0; i < 9600; i++)
        quiet &= q16[i] == 0;
    CHECK(quiet);

    /* a note in a big room at 24 bits: the tail follows the float mix
     * within the dither (0.5 LSB rms) all the way down, below 16 bits'
     * step, then the room goes quiet and the output is 0 again */
    synth_init(&s, RATE);
    synth_room(&s, 1.0f, 0.3f, 1.0f);
    clean_voice(0, 440, SYNTH_SAW, 200, 0, 0, 255, 10, 1);
    tone(0, SYNTH_REVERB, 255);
    double worst = 0, under16 = 0;
    int blocks = 0, zeros_after = -1;
    for (int b = 0; b < 12 * RATE / 64; b++) {
        if (b == RATE / 4 / 64)
            tone(0, SYNTH_CONTROL, 0);                  /* released after 250 ms */
        int kind = synth_mix(&s, regs, fin, 64);
        synth_quantize(&s, fin, q32, 64, 24, kind);
        double err = 0, lvl = 0;
        int nz = 0;
        for (int i = 0; i < 128; i++) {
            double e = lsb_of(q32[i], 24) - fin[i] * 8388607.0;
            err += e * e;
            lvl += fin[i] * fin[i];
            nz += q32[i] != 0;
        }
        err = sqrt(err / 128);
        lvl = sqrt(lvl / 128) * 32767.0;                /* in LSBs of 16 bits */
        if (kind != SYNTH_MIX_SILENT && err > worst)
            worst = err;
        if (kind != SYNTH_MIX_SILENT && lvl < 0.5 && lvl > 0.01) {
            under16 += 1;                               /* a block under half a 16-bit step, still there */
            CHECK(nz > 64);
        }
        if (kind == SYNTH_MIX_SILENT) {
            CHECK(nz == 0);
            if (zeros_after < 0)
                zeros_after = b;
        }
        blocks++;
    }
    printf("  the room's tail at 24 bits: %.2f LSB rms from the float at worst, %d blocks under half a "
           "16-bit step, quiet after %.2f s\n", worst, (int)under16, zeros_after * 64.0 / RATE);
    CHECK(worst < 0.75);                                /* 16 bits would be 256 */
    CHECK(under16 > 20);
    CHECK(zeros_after > 0 && zeros_after < 11 * RATE / 64);

    /* the DC blocker: a square a quarter high (its mean half way down)
     * comes out around 0 */
    synth_init(&s, RATE);
    clean_voice(0, 1000, SYNTH_SQUARE, 100, 0, 0, 255, 0, 1);  /* under the compressor: no bending */
    tone(0, SYNTH_DUTY, 64);
    synth_render32(&s, regs, q32, RATE, 24);
    synth_render32(&s, regs, q32, RATE / 2, 24);
    double dc = 0, peak24 = 0;
    for (int i = 0; i < RATE; i++) {
        dc += lsb_of(q32[i], 24);
        peak24 = fabs(lsb_of(q32[i], 24)) > peak24 ? fabs(lsb_of(q32[i], 24)) : peak24;
    }
    dc /= RATE * 8388607.0;
    printf("  DC of a 25%% square after the blocker: %.2e of full scale (peak %.2f)\n", dc, peak24 / 8388607.0);
    CHECK(fabs(dc) < 1e-5 && peak24 > 0.3 * 8388607.0);

    /* the chip (retro): its 16 bits truncated as always, at any depth */
    static synth_t s2;
    init_retro(&s);
    init_retro(&s2);
    clean_voice(0, 330, SYNTH_SAW, 180, 20, 30, 120, 40, 1);
    synth_render(&s, regs, q16, 4800);
    synth_render32(&s2, regs, q32, 4800, 24);
    int same = 1;
    for (int i = 0; i < 9600; i++)
        same &= q32[i] == (int32_t)((uint32_t)(uint16_t)q16[i] << 16);
    CHECK(same && peak(q16, 9600) > 10000);

    /* 32 bits is the 24 without the rounding: the same sound within 2 steps of 24 bits */
    synth_init(&s, RATE);
    synth_init(&s2, RATE);
    clean_voice(0, 330, SYNTH_FM, 200, 0, 30, 120, 40, 1);
    tone(0, SYNTH_REVERB, 120);
    static int32_t q24[9600];
    synth_render32(&s, regs, q32, 4800, 32);
    synth_render32(&s2, regs, q24, 4800, 24);
    int near = 1;
    for (int i = 0; i < 9600; i++)
        near &= abs((q32[i] >> 8) - (q24[i] >> 8)) <= 2;
    CHECK(near);
}

/* ---------------------------------------------------------------- the tone within a block */

static void test_ramps(void)
{
    static synth_t s;
    static int16_t st[RATE * 2 * 2];

    /* a 3 kHz sine through a resonant low pass whose cutoff an 8 Hz LFO
     * moves 4 octaves: with the coefficients updated once a block (64
     * samples) the gain stepped 750 times a second, sidebands at 3 kHz
     * +- 750 Hz x k at -36 dBc; ramped sample by sample they are gone */
    memset(regs, 0, sizeof regs);
    clean_voice(0, 3000, SYNTH_SINE, 200, 0, 0, 255, 0, 1);
    tone(0, SYNTH_CUTOFF, 200);
    tone(0, SYNTH_RESONANCE, 200);
    tone(0, SYNTH_LFO_RATE, 140);
    tone(0, SYNTH_LFO_CUT, 255);
    synth_init(&s, RATE);
    synth_render(&s, regs, st, RATE * 2);
    spectrum16(st, RATE / 2, FFT_N);
    double dbc = sidebands(3000, 4, 100);
    printf("  a cutoff swept by the LFO: block-rate sidebands at %.1f dBc (once a block: -36)\n", dbc);
    CHECK(dbc < -65);

    /* the filter envelope sweeping the same sine's cutoff down from 21 kHz
     * to 300 Hz, over the carrier: 85 ms around the crossing. Once a block
     * the sidebands were at -33 dBc (-25 for an acid decay of 120 ms) */
    tone(0, SYNTH_LFO_RATE, 0);
    tone(0, SYNTH_LFO_CUT, 0);
    tone(0, SYNTH_CUTOFF, 100);
    tone(0, SYNTH_FENV, 127);
    tone(0, SYNTH_FDECAY, 40);
    synth_init(&s, RATE);
    synth_render(&s, regs, st, RATE / 4);
    spectrum16(st, 2048, 4096);
    double env = sidebands(3000, 3, 120);
    tone(0, SYNTH_FDECAY, 15);
    synth_init(&s, RATE);
    synth_render(&s, regs, st, RATE / 4);
    spectrum16(st, 0, 4096);
    double acid = sidebands(3000, 3, 120);
    printf("  the filter envelope's sweep: block-rate sidebands at %.1f dBc, an acid one %.1f dBc\n", env, acid);
    CHECK(env < -60 && acid < -52);

    /* the square's width: a new duty ramps over a block, not at once. A
     * 750 Hz square is 64 samples a cycle: the block after duty 64 -> 192
     * (25% -> 75% high) is about half high, the next one 75% */
    clean_voice(0, 750, SYNTH_SQUARE, 200, 0, 0, 255, 0, 1);
    tone(0, SYNTH_DUTY, 64);
    synth_init(&s, RATE);
    synth_render(&s, regs, st, 640);
    tone(0, SYNTH_DUTY, 192);
    synth_render(&s, regs, st, 128);
    int high1 = 0, high2 = 0;
    for (int i = 0; i < 64; i++) {
        high1 += st[2 * i] > 0;
        high2 += st[2 * (64 + i)] > 0;
    }
    printf("  duty 25%% -> 75%%: %d of 64 high in the block after, %d in the next\n", high1, high2);
    CHECK(high1 > 24 && high1 < 40 && abs(high2 - 48) <= 3);

    /* the ramps are stable whatever they do: the cutoff jumping from the
     * bottom to the top every block with the most resonance, in whole
     * blocks and in odd sizes (the division instead of the Newton step) */
    for (int odd = 0; odd < 2; odd++) {
        clean_voice(0, 220, SYNTH_SAW, 255, 0, 0, 255, 0, 1);
        tone(0, SYNTH_RESONANCE, 255);
        synth_init(&s, RATE);
        int ok = 1, top = 0;
        for (int b = 0; b < 400; b++) {
            tone(0, SYNTH_CUTOFF, b & 1 ? 255 : 1);
            tone(0, SYNTH_FILTER, b % 7 == 3 ? SYNTH_BANDPASS : SYNTH_LOWPASS);
            unsigned n = odd ? 1 + (unsigned)(b * 7 % 13) : 64;
            synth_render(&s, regs, st, n);
            for (unsigned i = 0; i < 2 * n; i++)
                top = abs(st[i]) > top ? abs(st[i]) : top;
            ok &= s.v[0].f1 == s.v[0].f1 && s.v[0].f2 == s.v[0].f2 && fabsf(s.v[0].f1) < 100 && fabsf(s.v[0].f2) < 100;
        }
        CHECK(ok && top > 1000);
    }
}

/* notes at a time (player_at, the pattern language): on time within a
 * block, in order, on the voices allowed, the oldest stolen, cancelled */
static void test_at(void)
{
    fresh(NULL);
    au_sound_t sq = { "SQ", SYNTH_SQUARE, 128, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, { 0 } };
    au_tone_default(pregs);
    for (int i = 0; i < AU_TONE; i++)
        sq.tone[i] = pregs[SYNTH_CUTOFF + i];
    sq.tone[SYNTH_FLAGS - SYNTH_CUTOFF] |= SYNTH_FLAG_RAW;
    CHECK(player_clock(&pl) == 0);
    /* 1000 samples on, 2400 long: silent before, A4 from the block it falls in */
    CHECK(player_at(&pl, 1000, &sq, 69, 255, 2400, 1) == 0);
    CHECK(player_at_waiting(&pl, 0) == 1 && player_at_waiting(&pl, 2) == 0);
    run(buf, 4800);
    CHECK(player_clock(&pl) == 4800);
    int on = onset(buf, 0, 4800, 1000);
    CHECK(on >= 960 && on <= 1024);
    CHECK(peak(buf, 960) == 0);
    CHECK(fabs(freq_at(buf, 1200, 1920) - 440.0) < 30.0);
    CHECK(peak(buf + 3600, 1200) == 0);                 /* released at 3400 */
    CHECK(player_at_waiting(&pl, 0) == 0);
    /* a time gone plays at once; bad notes and tags refused */
    CHECK(player_at(&pl, 10, &sq, 60, 255, 480, 1) == 0);
    CHECK(player_at(&pl, 10, &sq, 0, 255, 480, 1) == -1 && player_at(&pl, 10, &sq, 60, 255, 480, 0) == -1);
    run(buf, 64);
    CHECK(pl.v[7].at_tag == 1 && pl.v[7].owner == AU_OWN_SFX);

    /* the voices allowed: 2 and 5; three notes at once: the third takes
     * the oldest of the two */
    fresh(NULL);
    player_at_voices(&pl, 1u << 2 | 1u << 5);
    CHECK(player_at(&pl, 100, &sq, 60, 255, 9600, 3) == 0);
    CHECK(player_at(&pl, 200, &sq, 64, 255, 9600, 3) == 0);
    run(buf, 512);
    CHECK(pl.v[5].at_tag == 3 && pl.v[2].at_tag == 3 && pl.v[7].owner == AU_OWN_NONE);
    CHECK(fabs(pl.v[5].note - 60) < 0.01f && fabs(pl.v[2].note - 64) < 0.01f);
    CHECK(player_at(&pl, 600, &sq, 67, 255, 9600, 4) == 0);
    run(buf, 512);
    CHECK(pl.v[5].at_tag == 4 && fabs(pl.v[5].note - 67) < 0.01f && fabs(pl.v[2].note - 64) < 0.01f);
    /* cancel tag 3: its waiting notes forgotten, its sounding one released */
    CHECK(player_at(&pl, 5000, &sq, 72, 255, 480, 3) == 0 && player_at(&pl, 5000, &sq, 74, 255, 480, 4) == 0);
    player_at_cancel(&pl, 3);
    CHECK(player_at_waiting(&pl, 3) == 0 && player_at_waiting(&pl, 4) == 1);
    CHECK(!(pregs[2 * SYNTH_VOICE_BYTES + SYNTH_CONTROL] & SYNTH_GATE));
    CHECK(pregs[5 * SYNTH_VOICE_BYTES + SYNTH_CONTROL] & SYNTH_GATE);
    player_at_cancel(&pl, 0);
    CHECK(player_at_waiting(&pl, 0) == 0 && !(pregs[5 * SYNTH_VOICE_BYTES + SYNTH_CONTROL] & SYNTH_GATE));

    /* in time order whatever the order of queueing; the queue's end */
    fresh(NULL);
    player_at_voices(&pl, 1u << 0);
    CHECK(player_at(&pl, 300, &sq, 72, 255, 64, 1) == 0);
    CHECK(player_at(&pl, 100, &sq, 60, 255, 64, 1) == 0);
    run(buf, 200);
    CHECK(fabs(pl.v[0].note - 60) < 0.01f);
    run(buf, 200);
    CHECK(fabs(pl.v[0].note - 72) < 0.01f);
    int queued = 0;
    for (int i = 0; i < AU_AT_MAX + 5; i++)
        queued += player_at(&pl, 100000 + (uint64_t)i, &sq, 60, 255, 64, 1) == 0;
    CHECK(queued == AU_AT_MAX);
    player_stop_all(&pl);
    CHECK(player_at_waiting(&pl, 0) == 0 && pl.at_next == UINT64_MAX);
}

/* ---------------------------------------------------------------- samples */

/* a sample of n frames (mono or stereo) at rate, made 16-bit with its guards */
static int16_t smp_pcm[8][200000];
static synth_sample_t smp_tab[8];
static void make_sample(int k, const float *l, const float *r, uint32_t n, uint32_t rate, int root, int loop,
                        uint32_t ls, uint32_t le)
{
    synth_sample_t *x = &smp_tab[k];
    memset(x, 0, sizeof *x);
    int c = r ? 2 : 1;
    int16_t *pcm = smp_pcm[k] + c;
    for (uint32_t i = 0; i < n; i++) {
        pcm[c * i] = (int16_t)lrintf(l[i] * 32767.0f);
        if (r)
            pcm[c * i + 1] = (int16_t)lrintf(r[i] * 32767.0f);
    }
    x->len = n;
    x->rate = rate;
    x->channels = (uint8_t)c;
    x->root = (uint8_t)root;
    x->loop = (uint8_t)loop;
    x->loop_start = ls;
    x->loop_end = le;
    synth_sample_ready(x, pcm);
}

/* a voice playing sample k (MOD1) at hz (0: its own speed) */
static void sample_voice(int ch, int k, int hz, int vol, int gate)
{
    clean_voice(ch, hz, SYNTH_SAMPLE, vol, 0, 0, 255, 2, gate);
    tone(ch, SYNTH_FREQ_FRAC, 0);
    tone(ch, SYNTH_MOD1, k);
}

static float smix[2 * 2 * RATE];
static void mix_floats(synth_t *s, int n)
{
    for (int k = 0; k < n; k += 64)
        synth_mix(s, regs, smix + 2 * k, (unsigned)(n - k < 64 ? n - k : 64));
}

/* signal to noise of a sine of hz in the floats (left, every other), from a fit */
static double fit_snr(const float *x, int stride, int from, int n, double hz)
{
    double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0;
    for (int i = from; i < from + n; i++) {
        double si = sin(2 * M_PI * hz * i / RATE), co = cos(2 * M_PI * hz * i / RATE);
        ss += si * si; sc += si * co; cc += co * co;
        xs += x[stride * i] * si; xc += x[stride * i] * co;
    }
    double det = ss * cc - sc * sc, a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    double sig = 0, err = 0;
    for (int i = from; i < from + n; i++) {
        double y = a * sin(2 * M_PI * hz * i / RATE) + b * cos(2 * M_PI * hz * i / RATE);
        sig += y * y;
        err += (x[stride * i] - y) * (x[stride * i] - y);
    }
    return 10 * log10(sig / (err + 1e-30));
}

static int crossings_f(const float *x, int stride, int from, int n)
{
    int e = 0;
    for (int i = from + 1; i < from + n; i++)
        e += x[stride * (i - 1)] < 0 && x[stride * i] >= 0;
    return e;
}

static float fpeak(const float *x, int stride, int from, int n)
{
    float p = 0;
    for (int i = from; i < from + n; i++)
        if (fabsf(x[stride * i]) > p) p = fabsf(x[stride * i]);
    return p;
}

/* a bank of version 3 under construction */
static uint8_t big[1 << 16];
static size_t big_len;
static void bput(int v) { big[big_len++] = (uint8_t)v; }
static void bput32(uint32_t v) { bput(v & 255); bput(v >> 8 & 255); bput(v >> 16 & 255); bput(v >> 24); }
static void bput_sample(const char *name, uint32_t frames, uint32_t rate, int format, int ch, int root, int loop,
                        uint32_t ls, uint32_t le)
{
    for (int i = 0; i < 8; i++) bput(*name ? *name++ : 0);
    bput32(frames); bput32(rate); bput(format); bput(ch); bput(root); bput(0); bput(loop);
    bput(0); bput(0); bput(0); bput32(ls); bput32(le);
}

static void test_samples(void)
{
    static synth_t s;
    char err[64];

    /* ---- the bank: every format into 16 bits, rounded and clamped */
    big_len = 0;
    bput('B'); bput('M'); bput('A'); bput('U'); bput(3);
    bput(1); bput(0); bput(0); bput(0); bput(5);
    while (big_len < 16) bput(0);
    for (int i = 0; i < 8; i++) bput("SMP\0\0\0\0\0"[i]);
    bput(SYNTH_SAMPLE); bput(128); bput(255); bput(0); bput(0); bput(255); bput(2);
    for (int i = 0; i < 9; i++) bput(0);
    for (int i = 0; i < AU_TONE + 3; i++) bput(i == SYNTH_MOD1 - SYNTH_CUTOFF ? 3 : 0);
    bput_sample("U8", 4, 22050, AU_PCM_U8, 1, 0, SYNTH_LOOP_OFF, 0, 0);
    bput(0); bput(255); bput(128); bput(129);
    bput_sample("S24", 4, 44100, AU_PCM_S24, 1, 69, SYNTH_LOOP_FWD, 1, 0);
    bput32(0x7FFFFF); big_len--; bput32(0x800000); big_len--; bput32(0x000080); big_len--; bput32(0xFFFF7F); big_len--;
    bput_sample("S32", 2, 48000, AU_PCM_S32, 2, 60, SYNTH_LOOP_PINGPONG, 0, 0);
    bput32(0x7FFFFFFF); bput32(0x80000000); bput32(0x00008000); bput32(0xFFFF7FFF);
    bput_sample("F32", 4, 96000, AU_PCM_F32, 1, 60, 0, 0, 0);
    union { float f; uint32_t u; } fu;
    float fv[4] = { 1.5f, -1.0f, 0.25f, NAN };
    for (int i = 0; i < 4; i++) { fu.f = fv[i]; bput32(fu.u); }
    bput_sample("S16", 3, 8000, AU_PCM_S16, 1, 60, 0, 0, 0);
    bput(0x34); bput(0x12); bput(0xFF); bput(0xFF); bput(0); bput(0x80);
    static au_bank_t b;
    CHECK(au_parse(big, big_len, &b, err, sizeof err) == 0 && b.nsamples == 5);
    CHECK(b.sample[0].pcm == NULL && b.pcm_need == (4 + 4) + (4 + 4) + (2 + 4) * 2 + (4 + 4) + (3 + 4));
    CHECK(au_sample_find(&b, "s24") == 1 && au_sample_find(&b, "nope") == -1);
    static int16_t pool[64];
    CHECK(au_parse_pcm(big, big_len, &b, pool, b.pcm_need - 1, err, sizeof err) == 0 && b.sample[2].pcm == NULL);
    CHECK(au_parse_pcm(big, big_len, &b, pool, sizeof pool / 2, err, sizeof err) == 0);
    const int16_t *p = b.sample[0].pcm;
    CHECK(p && p[0] == -32768 && p[1] == 32512 && p[2] == 0 && p[3] == 256 && p[-1] == 0 && p[4] == 0);
    p = b.sample[1].pcm;
    CHECK(p && p[0] == 32767 && p[1] == -32768 && p[2] == 1 && p[3] == -1);
    CHECK(b.sample[1].root == 69 && b.sample[1].loop == SYNTH_LOOP_FWD && b.sample[1].loop_end == 4 &&
          p[4] == p[1] && p[5] == p[2]);                  /* the loop's start after its end */
    p = b.sample[2].pcm;
    CHECK(p && b.sample[2].channels == 2 && p[0] == 32767 && p[1] == -32768 && p[2] == 1 && p[3] == -1);
    CHECK(b.sample[2].loop == SYNTH_LOOP_PINGPONG && b.sample[2].loop_end == 2);  /* end 0: the last frame */
    p = b.sample[3].pcm;
    CHECK(p && p[0] == 32767 && p[1] == -32768 && p[2] == 8192 && p[3] == 0 && b.sample[3].rate == 96000);
    p = b.sample[4].pcm;
    CHECK(p && p[0] == 0x1234 && p[1] == -1 && p[2] == -32768 && b.sample[4].root == 60);
    CHECK(b.sound[0].wave == SYNTH_SAMPLE && b.sound[0].tone[SYNTH_MOD1 - SYNTH_CUTOFF] == 3);
    /* broken banks */
    CHECK(au_parse(big, big_len - 1, &b, err, sizeof err) == -1);
    big[9] = AU_SAMPLES + 1;
    CHECK(au_parse(big, big_len, &b, err, sizeof err) == -1);
    big[4] = 2;                                          /* version 2: byte 9 is not read */
    CHECK(au_parse(big, big_len, &b, err, sizeof err) == 0 && b.nsamples == 0);
    big[4] = 4;
    CHECK(au_parse(big, big_len, &b, err, sizeof err) == -1);

    /* ---- Hermite: a 1 kHz sine recorded at 22.05 kHz, played at its own speed */
    static float src[44100], src2[44100];
    for (int i = 0; i < 44100; i++) {
        src[i] = 0.5f * sinf(2 * (float)M_PI * 1000.0f * i / 22050.0f);
        src2[i] = 0.5f * sinf(2 * (float)M_PI * 1500.0f * i / 44100.0f);
    }
    make_sample(0, src, NULL, 22050, 22050, 60, SYNTH_LOOP_OFF, 0, 0);
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    memset(regs, 0, sizeof regs);
    sample_voice(0, 0, 0, 128, 1);
    mix_floats(&s, RATE + 4800);
    double snr = fit_snr(smix, 2, 4800, 32768, 1000.0);
    CHECK(snr > 60);
    CHECK(abs(crossings_f(smix, 2, 4800, 38400) - 800) <= 1);
    CHECK(fpeak(smix, 2, 49000, 4000) < 1e-3f && synth_active(&s) == 0);  /* one second, then over */
    /* the chip's (nearest frame, 8 bits), for comparison */
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 0, 0, 128, 1);
    tone(0, SYNTH_FLAGS, SYNTH_FLAG_RAW);
    mix_floats(&s, 9600);
    double snr_raw = fit_snr(smix, 2, 4800, 4800, 1000.0);
    CHECK(snr_raw < snr - 15);
    printf("  a 1 kHz sine at 22.05 kHz played at 48 kHz: %.1f dB SNR (Hermite), %.1f dB (the chip's)\n", snr,
           snr_raw);
    tone(0, SYNTH_FLAGS, 0);

    /* the note against the root: an octave up from C4 is twice as fast */
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 0, 523, 128, 1);
    tone(0, SYNTH_FREQ_FRAC, (int)(0.2511 * 256));
    mix_floats(&s, RATE);
    CHECK(abs(crossings_f(smix, 2, 2400, 19200) - 800) <= 2);
    CHECK(fpeak(smix, 2, 26000, 4000) < 1e-3f);          /* half a second */

    /* where it starts (MOD2 128: the second half, at 2 kHz after 500 Hz) and backwards */
    for (int i = 0; i < 22050; i++)
        src[i] = 0.4f * sinf(2 * (float)M_PI * (i < 11025 ? 500.0f : 2000.0f) * i / 22050.0f);
    make_sample(1, src, NULL, 22050, 22050, 60, SYNTH_LOOP_OFF, 0, 0);
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 1, 0, 255, 1);
    tone(0, SYNTH_MOD2, 128);
    mix_floats(&s, RATE);
    CHECK(abs(crossings_f(smix, 2, 480, 19200) - 800) <= 2 && fpeak(smix, 2, 26000, 4000) < 1e-3f);
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 1, 0, 255, 1);
    tone(0, SYNTH_MOD2, 0);
    tone(0, SYNTH_FLAGS, SYNTH_FLAG_REVERSE);
    mix_floats(&s, RATE + 4800);
    CHECK(abs(crossings_f(smix, 2, 480, 19200) - 800) <= 2 && abs(crossings_f(smix, 2, 26400, 19200) - 200) <= 2);
    CHECK(synth_active(&s) == 0);
    tone(0, SYNTH_FLAGS, 0);

    /* loops: one cycle of 1 kHz at 48 kHz (48 frames) goes on seamlessly */
    for (int i = 0; i < 48; i++)
        src[i] = 0.5f * sinf(2 * (float)M_PI * i / 48.0f);
    make_sample(2, src, NULL, 48, 48000, 60, SYNTH_LOOP_FWD, 0, 0);
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 2, 0, 128, 1);
    mix_floats(&s, RATE);
    double snr_loop = fit_snr(smix, 2, 4800, 32768, 1000.0);
    CHECK(snr_loop > 80 && synth_active(&s) == 1);
    /* ping-pong over half a cycle (frames 12..36: down from the top to the bottom), down and up
     * again: 1 kHz, never stuck */
    make_sample(3, src, NULL, 48, 48000, 60, SYNTH_LOOP_PINGPONG, 12, 37);
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 3, 0, 128, 1);
    mix_floats(&s, RATE);
    int pp = crossings_f(smix, 2, 4800, 38400);
    CHECK(pp >= 38400 / 48 - 2 && pp <= 38400 / 48 + 2 && fpeak(smix, 2, 40000, 4800) > 0.3f);
    printf("  loops: a cycle of 48 frames again and again %.1f dB SNR; ping-pong over half of it %d Hz\n",
           snr_loop, pp * 48000 / 38400);
    /* a pitch far up a short loop wraps many times a sample: no hang, no reading outside */
    sample_voice(0, 3, 12000, 128, 1);
    mix_floats(&s, 4800);
    CHECK(synth_active(&s) == 1);

    /* stereo: its two channels where they are, the place as a balance */
    for (int i = 0; i < 44100; i++)
        src[i] = 0.4f * sinf(2 * (float)M_PI * 1000.0f * i / 44100.0f);
    make_sample(4, src, src2, 44100, 44100, 60, SYNTH_LOOP_OFF, 0, 0);
    synth_init(&s, RATE);
    synth_samples(&s, smp_tab, 8);
    sample_voice(0, 4, 0, 255, 1);
    mix_floats(&s, 9600);
    CHECK(abs(crossings_f(smix, 2, 2400, 4800) - 100) <= 1 && abs(crossings_f(smix + 1, 2, 2400, 4800) - 150) <= 1);
    tone(0, SYNTH_PAN, (uint8_t)-127);
    mix_floats(&s, 9600);
    CHECK(fpeak(smix, 2, 2400, 4800) > 0.2f && fpeak(smix + 1, 2, 2400, 4800) < 0.01f);
    tone(0, SYNTH_PAN, 0);
    tone(0, SYNTH_CUTOFF, 120);                         /* both channels through the filter */
    mix_floats(&s, 9600);
    CHECK(fpeak(smix, 2, 2400, 4800) < 0.2f && fpeak(smix + 1, 2, 2400, 4800) < 0.15f &&
          fpeak(smix + 1, 2, 2400, 4800) > 0.01f);

    /* no sample (a bank without it, a kit number past the kit): silent, and the voice frees itself */
    synth_init(&s, RATE);
    sample_voice(0, 7, 0, 255, 1);
    mix_floats(&s, 640);
    CHECK(fpeak(smix, 2, 0, 640) == 0 && synth_active(&s) == 0);
    sample_voice(0, SYNTH_KIT + SYNTH_KIT_SIZE, 0, 255, 1);
    synth_retrigger(&s, 0);
    mix_floats(&s, 640);
    CHECK(fpeak(smix, 2, 0, 640) == 0 && synth_active(&s) == 0);

    /* ---- the kit: eight drums under -1 dB, each where its sound is */
    CHECK(synth_kit_find("bd") == SYNTH_KIT && synth_kit_find("CB") == SYNTH_KIT + 7 && synth_kit_find("x") == -1);
    static const int lo_hz[SYNTH_KIT_SIZE] = { 30, 150, 6000, 6000, 600, 400, 60, 400 };
    static const int hi_hz[SYNTH_KIT_SIZE] = { 200, 6000, 16000, 16000, 3000, 3000, 250, 2000 };
    for (unsigned k = 0; k < SYNTH_KIT_SIZE; k++) {
        const synth_sample_t *x = synth_kit(k);
        int pk = 0;
        double num = 0, den = 0;
        for (uint32_t i = 0; i < x->len; i++)
            if (abs(x->pcm[i]) > pk) pk = abs(x->pcm[i]);
        /* the spectral centroid of its first 8192 frames */
        int n = x->len < FFT_N ? (int)x->len : FFT_N;
        for (int i = 0; i < FFT_N; i++) { fre[i] = i < n ? x->pcm[i] : 0; fim[i] = 0; }
        fft(fre, fim, FFT_N);
        for (int i = 1; i < FFT_N / 2; i++) {
            double m = fre[i] * fre[i] + fim[i] * fim[i];
            num += m * i * 48000.0 / FFT_N;
            den += m;
        }
        double c = num / den;
        CHECK(x->len > 3000 && pk < 29205 && pk > 23000 && x->root == 60 && x->channels == 1);
        CHECK(c > lo_hz[k] && c < hi_hz[k]);
        if (c <= lo_hz[k] || c >= hi_hz[k])
            printf("  kit %s: centroid %.0f Hz\n", synth_kit_name(k), c);
        CHECK(x->pcm[x->len - 1] < 200 && x->pcm[x->len - 1] > -200);   /* faded out */
    }
    /* played from the kit (MOD1 128..), it is over when its frames are */
    synth_init(&s, RATE);
    sample_voice(0, SYNTH_KIT + 2, 0, 255, 1);
    mix_floats(&s, 9600);
    CHECK(fpeak(smix, 2, 0, 2400) > 0.1f && synth_active(&s) == 0);

    /* ---- the player: a bank's samples to the synthesizer, and away with it */
    static au_bank_t b2;
    memset(pregs, 0, sizeof pregs);
    synth_init(&syn, RATE);
    player_init(&pl, RATE, pregs, &syn);
    CHECK(au_parse_pcm(big, big_len - 0, &b2, pool, sizeof pool / 2, err, sizeof err) == -1);
    big[4] = 3;
    big[9] = 5;
    CHECK(au_parse_pcm(big, big_len, &b2, pool, sizeof pool / 2, err, sizeof err) == 0);
    player_set_bank(&pl, &b2);
    CHECK(syn.smp == b2.sample && syn.nsmp == 5);
    player_set_bank(&pl, NULL);
    CHECK(syn.smp == NULL && syn.nsmp == 0);
}

/* ---------------------------------------------------------------- the effects */

/* the power at hz in the left floats (Hann window) */
static double fpower_at(const float *x, int from, int n, double hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        re += x[2 * (from + i)] * w * cos(2 * M_PI * hz * i / RATE);
        im -= x[2 * (from + i)] * w * sin(2 * M_PI * hz * i / RATE);
    }
    return re * re + im * im;
}

/* the left floats' power in [lo, hi) Hz, from FFT_N samples */
static double fband(const float *x, int from, double lo, double hi)
{
    for (int i = 0; i < FFT_N; i++) {
        fre[i] = x[2 * (from + i)] * (0.5 - 0.5 * cos(2 * M_PI * i / (FFT_N - 1)));
        fim[i] = 0;
    }
    fft(fre, fim, FFT_N);
    double p = 0;
    for (int i = (int)(lo * FFT_N / RATE); i < (int)(hi * FFT_N / RATE); i++)
        p += fre[i] * fre[i] + fim[i] * fim[i];
    return p;
}

static double frms(const float *x, int stride, int from, int n)
{
    double p = 0;
    for (int i = from; i < from + n; i++)
        p += (double)x[stride * i] * x[stride * i];
    return sqrt(p / n);
}

static float smix2[2 * 2 * RATE];

static void test_effects(void)
{
    static synth_t s, s2;

    /* ---- crush: the difference from the clean sound is the steps' noise,
     * 2^(1-b)/sqrt(12) (x 1.25: the compressor's make-up, the signal quiet) */
    for (int bits = 3; bits <= 8; bits += 5) {
        synth_init(&s, RATE);
        synth_init(&s2, RATE);
        memset(regs, 0, sizeof regs);
        clean_voice(0, 441, SYNTH_SINE, 100, 0, 0, 255, 0, 1);
        mix_floats(&s2, 9600);
        memcpy(smix2, smix, sizeof(float) * 2 * 9600);
        tone(0, SYNTH_CRUSH, bits);
        mix_floats(&s, 9600);
        double d = 0;
        for (int i = 2400; i < 9600; i++)
            d += (double)(smix[2 * i] - smix2[2 * i]) * (smix[2 * i] - smix2[2 * i]);
        d = sqrt(d / 7200);
        double want = 1.25 * pow(2, 1 - bits) / sqrt(12.0);
        CHECK(d > 0.7 * want && d < 1.3 * want);
        if (!(d > 0.7 * want && d < 1.3 * want))
            printf("  crush %d: %.5f rms, %.5f expected\n", bits, d, want);
    }
    /* coarse 4: three samples of every four held (after the DC blocker: each a
     * little less than the one before, by its 0.9995) */
    synth_init(&s, RATE);
    clean_voice(0, 441, SYNTH_SINE, 100, 0, 0, 255, 0, 1);
    tone(0, SYNTH_CRUSH, 3 << 4);
    mix_floats(&s, 9600);
    int held = 0;
    for (int i = 2401; i < 9600; i++)
        held += fabsf(smix[2 * i] - 0.9995f * smix[2 * (i - 1)]) < 1e-5f;
    CHECK(held > 7199 * 74 / 100 && held < 7199 * 76 / 100);

    /* ---- the drive's curves: odd harmonics for the symmetric ones, the
     * second for the asymmetric one; each its own sound */
    double h2[SYNTH_CURVES], h3[SYNTH_CURVES];
    static float curves[SYNTH_CURVES][2 * 8192];
    for (int c = 0; c < SYNTH_CURVES; c++) {
        synth_init(&s, RATE);
        clean_voice(0, 375, SYNTH_SINE, 120, 0, 0, 255, 0, 1);
        tone(0, SYNTH_DRIVE, 200);
        tone(0, SYNTH_FLAGS, c << SYNTH_CURVE_SHIFT);
        mix_floats(&s, 4800 + 8192);
        memcpy(curves[c], smix + 2 * 4800, sizeof curves[c]);
        double h1 = fpower_at(smix, 4800, 8192, 375);
        h2[c] = 10 * log10(fpower_at(smix, 4800, 8192, 750) / h1);
        h3[c] = 10 * log10(fpower_at(smix, 4800, 8192, 1125) / h1);
        CHECK(c == SYNTH_CURVE_ASYM ? h2[c] > -30 : h2[c] < -60);
        CHECK(h3[c] > -40);
    }
    int alike = 0;
    for (int c = 0; c < SYNTH_CURVES; c++)
        for (int d = c + 1; d < SYNTH_CURVES; d++)
            alike += !memcmp(curves[c], curves[d], sizeof curves[c]);
    CHECK(alike == 0);
    printf("  drive 200, its 3rd harmonic: soft %.0f, hard %.0f, fold %.0f, sine %.0f, asym %.0f (2nd %.0f), "
           "cubic %.0f dBc\n", h3[0], h3[1], h3[2], h3[3], h3[4], h2[4], h3[5]);

    /* ---- the noises: white rises 12 dB from 250-500 Hz to 4-8 kHz (four
     * octaves, sixteen times the band), pink stays level, brown falls 12 */
    static const int noise_wave[3] = { SYNTH_NOISE, SYNTH_PINK, SYNTH_BROWN };
    double tilt[3];
    for (int k = 0; k < 3; k++) {
        synth_init(&s, RATE);
        clean_voice(0, k ? 440 : RATE, noise_wave[k], 100, 0, 0, 255, 0, 1);
        mix_floats(&s, 4800 + FFT_N);
        tilt[k] = 10 * log10(fband(smix, 4800, 4000, 8000) / fband(smix, 4800, 250, 500));
    }
    CHECK(fabs(tilt[0] - 12) < 2 && fabs(tilt[1]) < 2 && fabs(tilt[2] + 12) < 2.5);
    printf("  noise from 250-500 Hz to 4-8 kHz: white %+.1f dB, pink %+.1f, brown %+.1f\n", tilt[0], tilt[1], tilt[2]);
    /* pink as the noise mix: the same tilt; the noises as loud as each other */
    synth_init(&s, RATE);
    clean_voice(0, 20, SYNTH_SINE, 100, 0, 0, 255, 0, 1);     /* 20 Hz: out of both bands */
    tone(0, SYNTH_NOISEMIX, 255);
    tone(0, SYNTH_FLAGS, SYNTH_COLOR_PINK << SYNTH_COLOR_SHIFT);
    mix_floats(&s, 4800 + FFT_N);
    double mixtilt = 10 * log10(fband(smix, 4800, 4000, 8000) / fband(smix, 4800, 250, 500));
    CHECK(fabs(mixtilt) < 3);
    double lw = 0, lp = 0, lb = 0;
    for (int k = 0; k < 3; k++) {
        synth_init(&s, RATE);
        clean_voice(0, k ? 440 : RATE, noise_wave[k], 100, 0, 0, 255, 0, 1);
        mix_floats(&s, 9600);
        double r = frms(smix, 2, 2400, 7200);
        if (k == 0) lw = r; else if (k == 1) lp = r; else lb = r;
    }
    CHECK(lp > 0.4 * lw && lp < 1.2 * lw && lb > 0.4 * lw && lb < 1.6 * lw);
    printf("  the noises' rms against the noise wave's: pink %.2f, brown %.2f\n", lp / lw, lb / lw);
    /* crackle: 100 clicks a second (MOD1 0), 400 (MOD1 50); each a step up and back */
    for (int dens = 0; dens <= 50; dens += 50) {
        synth_init(&s, RATE);
        clean_voice(0, 440, SYNTH_CRACKLE, 60, 0, 0, 255, 0, 1);  /* quiet: the compressor rests */
        tone(0, SYNTH_MOD1, dens);
        mix_floats(&s, RATE + 2400);
        int steps = 0;
        for (int i = 2401; i < RATE + 2400; i++)
            steps += fabsf(smix[2 * i] - 0.9995f * smix[2 * (i - 1)]) > 1e-4f;
        int want = dens ? 2 * 400 : 2 * 100;
        CHECK(steps > want * 7 / 10 && steps < want * 13 / 10);
        if (!(steps > want * 7 / 10 && steps < want * 13 / 10))
            printf("  crackle %d: %d steps, about %d wanted\n", dens, steps, want);
    }

    /* ---- tremolo: the LFO (5 Hz) on the volume, all the way down at 15; no LFO, no tremolo */
    for (int depth = 0; depth <= 15; depth += 15) {
        synth_init(&s, RATE);
        clean_voice(0, 500, SYNTH_SINE, 120, 0, 0, 255, 0, 1);  /* five cycles in 10 ms */
        tone(0, SYNTH_LFO_RATE, 1 + (int)(log2(5.0 / 0.1) * 254 / 7.64 + 0.5));
        tone(0, SYNTH_TREMOLO, depth);
        mix_floats(&s, RATE);
        double lo = 1e9, hi = 0;
        for (int b = 4800; b + 480 <= RATE; b += 480) {
            double r = frms(smix, 2, b, 480);
            if (r < lo) lo = r;
            if (r > hi) hi = r;
        }
        CHECK(depth ? hi > 10 * lo : hi < 1.02 * lo);
    }
    synth_init(&s, RATE);
    synth_init(&s2, RATE);
    clean_voice(0, 441, SYNTH_SINE, 120, 0, 0, 255, 0, 1);
    mix_floats(&s2, 4800);
    memcpy(smix2, smix, sizeof(float) * 2 * 4800);
    tone(0, SYNTH_TREMOLO, 15);
    mix_floats(&s, 4800);
    CHECK(!memcmp(smix, smix2, sizeof(float) * 2 * 4800));

    /* ---- the chorus: the two sides differ, the pitch wavers a little;
     * when the voice stops, the chorus empties and the mix is silent again */
    synth_init(&s, RATE);
    clean_voice(0, 440, SYNTH_SINE, 120, 0, 0, 255, 0, 1);
    tone(0, SYNTH_CHORUS, 255);
    mix_floats(&s, RATE);
    double side = 0;
    for (int i = 4800; i < RATE; i++)
        side += fabsf(smix[2 * i] - smix[2 * i + 1]);
    CHECK(side / (RATE - 4800) > 0.02);
    tone(0, SYNTH_CHORUS, 0);
    synth_init(&s, RATE);
    mix_floats(&s, 4800);
    side = 0;
    for (int i = 0; i < 4800; i++)
        side += fabsf(smix[2 * i] - smix[2 * i + 1]);
    CHECK(side == 0);
    synth_init(&s, RATE);
    clean_voice(0, 440, SYNTH_SINE, 120, 0, 0, 255, 2, 1);
    tone(0, SYNTH_CHORUS, 255);
    tone(0, SYNTH_REVERB, 0);
    mix_floats(&s, 9600);
    tone(0, SYNTH_CONTROL, 0);
    int silent_at = -1;
    for (int b = 0; b < 400 && silent_at < 0; b++) {
        float o[128];
        if (synth_mix(&s, regs, o, 64) == SYNTH_MIX_SILENT)
            silent_at = b;
    }
    CHECK(silent_at > 0 && s.chorus_quiet > RATE / 10);

    /* ---- ducking: a 220 Hz note, and a source (duck 15) for 200 ms at 1
     * kHz: the note falls by more than 12 dB while it sounds, and is back
     * within half a second; the source itself is not ducked */
    synth_init(&s, RATE);
    memset(regs, 0, sizeof regs);
    clean_voice(0, 220, SYNTH_SINE, 120, 0, 0, 255, 0, 1);
    clean_voice(1, 1000, SYNTH_SINE, 120, 0, 0, 255, 2, 0);
    tone(1, SYNTH_TREMOLO, 15 << 4);
    mix_floats(&s, 9600);
    double before = fpower_at(smix, 4800, 4800, 220);
    CHECK(s.duck == 0);
    tone(1, SYNTH_CONTROL, SYNTH_GATE);
    mix_floats(&s, 9600);
    double during = fpower_at(smix, 4800, 4800, 220), src = fpower_at(smix, 4800, 4800, 1000);
    tone(1, SYNTH_CONTROL, 0);
    mix_floats(&s, RATE);
    double after = fpower_at(smix, 24000, 4800, 220);
    CHECK(10 * log10(during / before) < -12);
    CHECK(fabs(10 * log10(after / before)) < 1);
    CHECK(src > before * 0.5);
    printf("  ducking: the music %.1f dB while the source sounds, %+.2f dB half a second after\n",
           10 * log10(during / before), 10 * log10(after / before));

    /* ---- the vowels: a saw at 110 Hz has its energy near 600 Hz with "a",
     * near 250 with "i"; about as loud as without */
    double ratio[SYNTH_VOWELS], dry = 0;
    for (int vw = 0; vw < SYNTH_VOWELS; vw++) {
        synth_init(&s, RATE);
        clean_voice(0, 110, SYNTH_SAW, 120, 0, 0, 255, 0, 1);
        tone(0, SYNTH_FILTER, vw << SYNTH_VOWEL_SHIFT);
        mix_floats(&s, 4800 + FFT_N);
        ratio[vw] = 10 * log10(fband(smix, 4800, 500, 700) / fband(smix, 4800, 200, 300));
        double r = frms(smix, 2, 4800, 9600);
        if (!vw)
            dry = r;
        else
            CHECK(r > dry * 0.35 && r < dry * 1.4);
    }
    CHECK(ratio[SYNTH_VOWEL_A] > ratio[SYNTH_VOWEL_I] + 15 && ratio[SYNTH_VOWEL_A] > ratio[0] + 5);
    printf("  vowels, 500-700 Hz against 200-300 Hz: none %+.0f, a %+.0f, e %+.0f, i %+.0f, o %+.0f, u %+.0f dB\n",
           ratio[0], ratio[1], ratio[2], ratio[3], ratio[4], ratio[5]);

    /* ---- the chip: every new register ignored, sample for sample */
    for (int w = 0; w < SYNTH_WAVES; w++) {
        if (w == SYNTH_SAMPLE)
            continue;
        init_retro(&s);
        init_retro(&s2);
        memset(regs, 0, sizeof regs);
        voice(0, w == SYNTH_NOISE || w >= SYNTH_PINK ? 4000 : 440, w, 128, 200, 0, 30, 100, 10, 1);
        static int16_t a[4800], b[4800];
        int mapped = w >= SYNTH_PINK ? SYNTH_NOISE : w;
        regs[SYNTH_WAVEFORM] = (uint8_t)mapped;
        mono(&s2, regs, b, 4800);
        regs[SYNTH_WAVEFORM] = (uint8_t)w;
        tone(0, SYNTH_CRUSH, 0x53);
        tone(0, SYNTH_TREMOLO, 0xFF);
        tone(0, SYNTH_CHORUS, 255);
        tone(0, SYNTH_FILTER, SYNTH_VOWEL_A << SYNTH_VOWEL_SHIFT);
        tone(0, SYNTH_FLAGS, SYNTH_COLOR_BROWN << SYNTH_COLOR_SHIFT | SYNTH_CURVE_FOLD << SYNTH_CURVE_SHIFT);
        tone(0, SYNTH_LFO_RATE, 100);
        mono(&s, regs, a, 4800);
        CHECK(!memcmp(a, b, sizeof a));
    }
}

/* ---------------------------------------------------------------- presets */

static void test_presets(void)
{
    /* every preset sounds, under the limiter's bend, no NaN; the kit's free
     * their voice when their sample ends */
    static float o[2 * 64];
    for (int p = 0; p < au_preset_count; p++) {
        const au_sound_t *snd = &au_presets[p].s;
        memset(pregs, 0, sizeof pregs);
        synth_init(&syn, RATE);
        player_init(&pl, RATE, pregs, &syn);
        int drum = !strcmp(au_presets[p].kind, "drum");
        player_play_sound(&pl, 0, snd, drum ? 60 : 48, 220, 600);
        float pk = 0;
        int bad = 0;
        for (int b = 0; b < RATE * 3 / 2 / 64; b++) {
            player_advance(&pl, 64);
            synth_mix(&syn, pregs, o, 64);
            for (int i = 0; i < 128; i++) {
                bad += !(o[i] == o[i]);
                if (fabsf(o[i]) > pk) pk = fabsf(o[i]);
            }
        }
        int fresh_one = p >= 42;                /* the new ones: heard, under the limiter's bend */
        CHECK(!bad && pk < 1.0f && (!fresh_one || (pk > 0.05f && pk < 0.9f)));
        if (bad || (fresh_one && (pk <= 0.05f || pk >= 0.9f)))
            printf("  preset %s: peak %.3f, %d NaN\n", snd->name, pk, bad);
        if (snd->wave == SYNTH_SAMPLE)
            CHECK(synth_active(&syn) == 0);
    }
    CHECK(au_preset_count == 50 && au_preset_find("kit") >= 42 && au_preset_find("vinyl") == 49);
    CHECK(au_wave_find("sample") == SYNTH_SAMPLE && au_wave_find("crackle") == SYNTH_CRACKLE &&
          au_wave_find("13") == -1);

    /* the tone's keys in plain units */
    uint8_t r[SYNTH_VOICE_BYTES] = { 0 };
    r[SYNTH_FILTER] = SYNTH_BANDPASS | SYNTH_KEYTRACK;
    CHECK(au_tone_str(r, "vowel", "o") == 0 && r[SYNTH_FILTER] == (SYNTH_BANDPASS | SYNTH_KEYTRACK | 4 << 3));
    CHECK(au_tone_str(r, "filter", "hp") == 0 && au_tone_num(r, "keytrack", 0) == 0 &&
          r[SYNTH_FILTER] == (SYNTH_HIGHPASS | 4 << 3));        /* the vowel stays */
    CHECK(au_tone_num(r, "crush", 4) == 0 && au_tone_num(r, "coarse", 8) == 0 && r[SYNTH_CRUSH] == (4 | 7 << 4));
    CHECK(au_tone_num(r, "crush", 16) == 0 && au_tone_num(r, "coarse", 1) == 0 && r[SYNTH_CRUSH] == 0);
    CHECK(au_tone_num(r, "trem", 0.4) == 0 && au_tone_num(r, "duck", 1) == 0 && r[SYNTH_TREMOLO] == (6 | 15 << 4));
    CHECK(au_tone_num(r, "chorus", 0.5) == 0 && r[SYNTH_CHORUS] == 128);
    CHECK(au_tone_str(r, "curve", "fold") == 0 && au_tone_str(r, "color", "brown") == 0 &&
          au_tone_num(r, "raw", 1) == 0 && au_tone_num(r, "reverse", 1) == 0 &&
          r[SYNTH_FLAGS] == (SYNTH_FLAG_RAW | 2 << 1 | 2 << 3 | SYNTH_FLAG_REVERSE));
    CHECK(au_tone_str(r, "curve", "nope") == -1 && au_tone_str(r, "vowel", "none") == 0 &&
          (r[SYNTH_FILTER] & SYNTH_VOWEL) == 0);
    CHECK(au_tone_str(r, "sample", "hh") == 0 && r[SYNTH_MOD1] == SYNTH_KIT + 2 &&
          au_tone_str(r, "sample", "kit:9") == 0 && r[SYNTH_MOD1] == SYNTH_KIT + 1 &&
          au_tone_str(r, "sample", "nothing") == -1);
    CHECK(au_tone_num(r, "begin", 0.5) == 0 && r[SYNTH_MOD2] == 128 && au_tone_num(r, "sample", 3) == 0 &&
          r[SYNTH_MOD1] == 3);
}

int main(void)
{
    test_synth();
    test_clean();
    test_player();
    test_at();
    test_bank2();
    test_iec958();
    test_depth();
    test_ramps();
    test_samples();
    test_effects();
    test_presets();
    printf("audio: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
