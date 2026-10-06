/* Host tests for the synthesizer (the chip voice of the first versions,
 * sample for sample, and the clean one), the player of the sound banks and
 * the IEC 958 subframes sent to the HDMI audio FIFO. */
#include "audio/synth.h"
#include "audio/player.h"
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

int main(void)
{
    test_synth();
    test_clean();
    test_player();
    test_at();
    test_bank2();
    test_iec958();
    printf("audio: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
