/*
 * The sound of bm on every console: the 8 voices of the synthesizer
 * (synth.h), the player of the cartridges' banks (player.h), nano8's
 * channels (n8snd.h), the notes and the volume. The samples go out through
 * the console's output (audio_out.h: HDMI on the Pi, I2S and the RK817's
 * codec on the RGB30), which calls audio_render32() from its interrupt:
 * everything is mixed in floats and rounded once, at the end, to the
 * depth the output carries (sound_depth: 16, 24 or 32 bits).
 */
#include "audio.h"
#include "audio_out.h"
#include "n8snd.h"
#include "synth.h"
#include "player.h"
#include "presets.h"
#include "drivers/timer.h"
#include "kernel/irq.h"
#include "lib/printf.h"

#include <string.h>

static synth_t synth;
static player_t player;
static au_bank_t banks[2];          /* the one playing, and the one a new bank is parsed into */
static int bank_now = -1;           /* -1: no bank */
static volatile uint8_t own_regs[SYNTH_REG_BYTES];
static int volume = AUDIO_VOLUME_MAX;
static int retro;                   /* AUDIO_RETRO_* */
static uint32_t idle_t;             /* audio_idle(): the last time it ran */

static int ready;
static const char *status = "not started";
static volatile uint32_t max_us, max_n;

/* ---- the render -------------------------------------------------------- */

/* The player moves every BLOCK samples: steps and effects keep a 1.3 ms
 * grid, whatever the output asks for. */
#define BLOCK 64

static unsigned depth = AUDIO_DEPTH_DEFAULT;    /* asked (sound_depth) */
static volatile unsigned depth_out;             /* what the output last rendered at */
static float fmix[2 * BLOCK];                   /* a block of the mix, before the rounding */

/* A block of the sound in floats: the player, the voices, nano8's
 * channels; what it is (SYNTH_MIX_*: silent blocks get no dither) */
static int mix_block(unsigned m)
{
    player_advance(&player, m);
    int kind = synth_mix(&synth, own_regs, fmix, m);
    if (n8snd_mix_float(fmix, m, synth.gain) && kind == SYNTH_MIX_SILENT)
        kind = SYNTH_MIX_SOUND;
    return kind;
}

static void timed(uint32_t t0, unsigned n)
{
    uint32_t us = timer_ticks() - t0;
    if (us > max_us) {
        max_us = us;
        max_n = n;
    }
}

void audio_render32(int32_t *out, unsigned n, unsigned bits)
{
    uint32_t t0 = timer_ticks();
    depth_out = bits;
    for (unsigned k = 0; k < n; k += BLOCK) {
        unsigned m = n - k < BLOCK ? n - k : BLOCK;
        synth_quantize(&synth, fmix, out + 2 * k, m, bits, mix_block(m));
    }
    timed(t0, n);
}

void audio_render(int16_t *out, unsigned n)
{
    uint32_t t0 = timer_ticks();
    depth_out = 16;
    for (unsigned k = 0; k < n; k += BLOCK) {
        unsigned m = n - k < BLOCK ? n - k : BLOCK;
        synth_quantize16(&synth, fmix, out + 2 * k, m, mix_block(m));
    }
    timed(t0, n);
}

void audio_set_depth(unsigned bits)
{
    depth = bits == 16 || bits == 24 || bits == 32 ? bits : AUDIO_DEPTH_DEFAULT;
}

unsigned audio_depth(void)
{
    return depth;
}

unsigned audio_depth_out(void)
{
    return ready ? depth_out : 0;
}

/* ---- start ------------------------------------------------------------ */

int audio_init(void)
{
    if (ready)
        return 0;
    synth_init(&synth, AUDIO_RATE);
    player_init(&player, AUDIO_RATE, own_regs, &synth);
    audio_reset();
    audio_set_volume(volume);
    if (audio_out_start(&status) != 0)
        return -1;
    ready = 1;
    return 0;
}

int audio_ready(void)
{
    return ready;
}

const char *audio_status(void)
{
    return status;
}

void audio_print(void)
{
    if (!ready) {
        kprintf("audio: off - %s\n", status);
        return;
    }
    kprintf("audio: %s\n", status);
    audio_out_print();
    kprintf("       %u-bit samples (sound_depth=%u), %s\n", depth_out, depth,
            depth_out == 32 ? "the float as it is" : "TPDF dither");
    kprintf("       synth %lu us per %lu samples (max; %lu us of sound), %u voices on\n",
            max_us, max_n, max_n * 1000000u / AUDIO_RATE, synth_active(&synth));
}

/* ---- registers and notes ---------------------------------------------- */

volatile uint8_t *audio_regs(void)
{
    return own_regs;
}

static volatile uint8_t *voice(unsigned ch)
{
    return own_regs + (ch % SYNTH_VOICES) * SYNTH_VOICE_BYTES;
}

void audio_reset(void)
{
    uint32_t s = irq_save();
    player_stop_all(&player);
    player_at_voices(&player, 0);
    for (unsigned ch = 0; ch < SYNTH_VOICES; ch++)
        au_voice_default(voice(ch));    /* no filter, in the middle, a little room */
    float gain = synth.gain;
    synth_init(&synth, AUDIO_RATE);
    synth.gain = gain;
    retro &= ~AUDIO_RETRO_GAME;
    synth.retro = retro != 0;
    irq_restore(s);
}

static uint8_t clamp8(int x)
{
    return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

void audio_note(unsigned ch, float freq, uint32_t ms, int wave, int vol)
{
    ch %= SYNTH_VOICES;
    uint32_t s = irq_save();
    volatile uint8_t *v = voice(ch);
    if (wave >= 0)
        v[SYNTH_WAVEFORM] = (uint8_t)(wave < SYNTH_WAVES ? wave : 0);
    if (vol >= 0)
        v[SYNTH_VOLUME] = clamp8(vol);
    player_lua_note(&player, (int)ch, freq, ms);
    irq_restore(s);
}

void audio_note_off(unsigned ch)
{
    uint32_t s = irq_save();
    player_lua_off(&player, (int)(ch % SYNTH_VOICES));
    irq_restore(s);
}

void audio_freq(unsigned ch, float freq)
{
    uint32_t s = irq_save();
    player_lua_freq(&player, (int)(ch % SYNTH_VOICES), freq);
    irq_restore(s);
}

void audio_envelope(unsigned ch, int a, int d, int su, int r)
{
    uint32_t s = irq_save();
    volatile uint8_t *v = voice(ch);
    v[SYNTH_ATTACK] = clamp8(a);
    v[SYNTH_DECAY] = clamp8(d);
    v[SYNTH_SUSTAIN] = clamp8(su);
    v[SYNTH_RELEASE] = clamp8(r);
    irq_restore(s);
}

void audio_duty(unsigned ch, int duty)
{
    voice(ch)[SYNTH_DUTY] = clamp8(duty);
}

int audio_busy(unsigned ch)
{
    return player_busy(&player, (int)(ch % SYNTH_VOICES));
}

void audio_slide(unsigned ch, float hz, uint32_t ms)
{
    uint32_t s = irq_save();
    player_slide(&player, (int)(ch % SYNTH_VOICES), hz, ms);
    irq_restore(s);
}

void audio_vibrato(unsigned ch, float semitones, float rate_hz)
{
    uint32_t s = irq_save();
    player_vibrato(&player, (int)(ch % SYNTH_VOICES), semitones, rate_hz);
    irq_restore(s);
}

void audio_arp(unsigned ch, const int8_t *semis, int n, uint32_t ms)
{
    uint32_t s = irq_save();
    player_arp(&player, (int)(ch % SYNTH_VOICES), semis, n, ms);
    irq_restore(s);
}

/* ---- the bank: sound effects and music --------------------------------- */

int audio_bank(const uint8_t *data, size_t len, char *err, size_t errlen)
{
    if (!data || !len) {
        uint32_t s = irq_save();
        player_set_bank(&player, NULL);
        bank_now = -1;
        irq_restore(s);
        return 0;
    }
    int next = bank_now == 0 ? 1 : 0;   /* the interrupt never reads this one */
    if (au_parse(data, len, &banks[next], err, errlen) != 0)
        return -1;
    uint32_t s = irq_save();
    player_set_bank(&player, &banks[next]);
    bank_now = next;
    irq_restore(s);
    return 0;
}

int audio_sfx(int n, int ch, int transpose, float vol)
{
    uint32_t s = irq_save();
    int v = player_sfx(&player, n, ch, transpose, vol);
    irq_restore(s);
    return v;
}

void audio_sfx_stop(int ch)
{
    uint32_t s = irq_save();
    player_sfx_stop(&player, ch);
    irq_restore(s);
}

int audio_sfx_pos(int ch, int *step)
{
    uint32_t s = irq_save();
    int n = player_sfx_pos(&player, ch, step);
    irq_restore(s);
    return n;
}

void audio_music(int song, int order, int fade_ms)
{
    uint32_t s = irq_save();
    player_music(&player, song, order, fade_ms);
    irq_restore(s);
}

void audio_music_pattern(int pat, int bpm, int swing, int step)
{
    uint32_t s = irq_save();
    player_music_pattern(&player, pat, bpm, swing, step);
    irq_restore(s);
}

void audio_music_stop(int fade_ms)
{
    uint32_t s = irq_save();
    player_music_stop(&player, fade_ms);
    irq_restore(s);
}

int audio_music_pos(int *song, int *order, int *step, int *pat)
{
    uint32_t s = irq_save();
    int on = player_music_pos(&player, song, order, step, pat);
    irq_restore(s);
    return on;
}

void audio_tempo(float scale)
{
    uint32_t s = irq_save();
    player_tempo(&player, scale);
    irq_restore(s);
}

void audio_mute(int track, int on)
{
    uint32_t s = irq_save();
    player_mute(&player, track, on);
    irq_restore(s);
}

void audio_play(int ch, int sound, int note, int vol, int fx, uint32_t ms)
{
    uint32_t s = irq_save();
    player_play(&player, ch, sound, note, vol, fx, ms);
    irq_restore(s);
}

int audio_play_sound(int ch, const au_sound_t *snd, int note, int vol, uint32_t ms)
{
    uint32_t s = irq_save();
    int v = player_play_sound(&player, ch, snd, note, vol, ms);
    irq_restore(s);
    return v;
}

uint64_t audio_clock(void)
{
    uint32_t s = irq_save();
    uint64_t t = player_clock(&player);
    irq_restore(s);
    return t;
}

int audio_at(uint64_t when, const au_sound_t *snd, int note, int vol, uint32_t len, int tag)
{
    uint32_t s = irq_save();
    int r = player_at(&player, when, snd, note, vol, len, tag);
    irq_restore(s);
    return r;
}

void audio_at_cancel(int tag)
{
    uint32_t s = irq_save();
    player_at_cancel(&player, tag);
    irq_restore(s);
}

void audio_at_voices(uint8_t mask)
{
    uint32_t s = irq_save();
    player_at_voices(&player, mask);
    irq_restore(s);
}

int audio_at_waiting(int tag)
{
    return player_at_waiting(&player, tag);
}

const au_bank_t *audio_bank_now(void)
{
    return bank_now >= 0 ? &banks[bank_now] : NULL;
}

void audio_tone(unsigned ch, const uint8_t *regs)
{
    ch %= SYNTH_VOICES;
    uint32_t s = irq_save();
    volatile uint8_t *v = voice(ch);
    for (unsigned i = SYNTH_WAVEFORM; i <= SYNTH_RELEASE; i++)
        v[i] = regs[i];
    for (unsigned i = SYNTH_CUTOFF; i < SYNTH_VOICE_BYTES; i++)
        v[i] = regs[i];
    player.v[ch].bank_tone = 0;         /* the game's own: a note keeps it */
    irq_restore(s);
}

void audio_tone_get(unsigned ch, uint8_t *regs)
{
    volatile uint8_t *v = voice(ch % SYNTH_VOICES);
    for (unsigned i = 0; i < SYNTH_VOICE_BYTES; i++)
        regs[i] = v[i];
}

void audio_room(float size, float damp, float wet)
{
    uint32_t s = irq_save();
    synth_room(&synth, size, damp, wet);
    irq_restore(s);
}

void audio_echo(float ms, float feedback, float wet)
{
    uint32_t s = irq_save();
    synth_echo(&synth, ms, feedback, wet);
    irq_restore(s);
}

void audio_fx_get(float room[3], float echo[3])
{
    room[0] = synth.room_size;
    room[1] = synth.room_damp;
    room[2] = synth.room_wet;
    echo[0] = (float)synth.echo_len * 1000.0f / AUDIO_RATE;
    echo[1] = synth.echo_fb;
    echo[2] = synth.echo_wet;
}

void audio_retro(int who, int on)
{
    uint32_t s = irq_save();
    retro = on ? retro | who : retro & ~who;
    synth.retro = retro != 0;
    irq_restore(s);
}

int audio_retro_on(void)
{
    return retro != 0;
}

void audio_pause(int on)
{
    n8snd_pause(on);
    uint32_t s = irq_save();
    player_music_pause(&player, on);
    if (on) {
        player_at_cancel(&player, 0);
        player_sfx_stop(&player, -1);
        for (unsigned ch = 0; ch < SYNTH_VOICES; ch++) {
            player_lua_off(&player, (int)ch);
            player_vibrato(&player, (int)ch, 0, 0);
            player_arp(&player, (int)ch, NULL, 0, 0);
        }
    }
    irq_restore(s);
}

/* 0..10; the gain is the square of the level (10 = as the voices are) */
void audio_set_volume(int level)
{
    volume = level < 0 ? 0 : level > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : level;
    float k = (float)volume / AUDIO_VOLUME_MAX;
    synth.gain = k * k;
}

int audio_volume(void)
{
    return volume;
}

/* Without an output (QEMU's raspi0, a Pi on DVI, the PC's bmhost) the
 * player and the envelopes still move, in silence, from the game's frame
 * loop: music positions, playing() and the editor's playhead behave as
 * with the sound. */
void audio_idle(void)
{
    if (ready) {
        audio_out_idle();
        return;
    }
    uint32_t now = timer_ticks();
    if (!idle_t) {
        idle_t = now;
        return;
    }
    uint32_t n = (uint32_t)((uint64_t)(now - idle_t) * AUDIO_RATE / 1000000u);
    if (!n)
        return;
    idle_t += (uint32_t)((uint64_t)n * 1000000u / AUDIO_RATE);
    if (n > AUDIO_RATE / 10)
        n = AUDIO_RATE / 10;            /* a long pause is not caught up */
    while (n) {                         /* the mix only: nobody hears it rounded */
        uint32_t m = n < BLOCK ? n : BLOCK;
        mix_block(m);
        n -= m;
    }
}

/* ---- monitor test ------------------------------------------------------ */

void audio_test(void)
{
    audio_print();
    if (!ready)
        return;
    static const uint16_t tune[] = { 262, 330, 392, 523 };     /* C E G C */
    audio_reset();
    kprintf("  volume %d/10\n", volume);
    for (int w = 0; w < SYNTH_WAVES; w++) {
        kprintf("  %-8s ", au_wave_names[w]);
        for (int i = 0; i < 4; i++) {
            uint32_t f = w == SYNTH_NOISE || w == SYNTH_METAL ? tune[i] * 16u : tune[i];
            audio_note(0, (float)f, 180, w, 160);
            kprintf(".");
            timer_delay_ms(250);
        }
        kprintf("\n");
    }
    kprintf("  chord (3 voices, envelope with decay)\n");
    for (unsigned ch = 0; ch < 3; ch++) {
        audio_envelope(ch, 2, 60, 90, 60);
        audio_note(ch, tune[ch + 1], 700, 1, 110);
    }
    timer_delay_ms(1000);
    audio_reset();
    kprintf("  slide, vibrato, arpeggio\n");
    audio_note(0, 220, 600, SYNTH_SAW, 110);
    audio_slide(0, 880, 500);
    timer_delay_ms(800);
    audio_note(0, 440, 900, SYNTH_SINE, 150);
    audio_vibrato(0, 0.5f, 6);
    timer_delay_ms(1100);
    static const int8_t major[] = { 0, 4, 7, 12 };
    audio_note(0, 262, 900, SYNTH_SQUARE, 100);
    audio_arp(0, major, 4, 60);
    timer_delay_ms(1100);
    audio_reset();
    /* a bar of the instruments: drums, bass, piano and string together */
    kprintf("  instruments: kick, hat, snare, bass, epiano, pluck\n");
    static const char *const kit[] = { "kick", "hat", "snare", "hat" };
    for (int step = 0; step < 8; step++) {
        audio_play_sound(0, &au_presets[au_preset_find(kit[step & 3])].s, 48, 230, 120);
        if (step % 2 == 0)
            audio_play_sound(1, &au_presets[au_preset_find("bass")].s, step & 4 ? 41 : 36, 230, 200);
        if (step == 0 || step == 4) {
            audio_play_sound(2, &au_presets[au_preset_find("epiano")].s, step ? 65 : 64, 200, 500);
            audio_play_sound(3, &au_presets[au_preset_find("epiano")].s, step ? 69 : 67, 200, 500);
        }
        audio_play_sound(4, &au_presets[au_preset_find("pluck")].s, 72 + major[step & 3], 160, 150);
        timer_delay_ms(250);
    }
    timer_delay_ms(800);
    audio_reset();
    audio_print();
}
