/*
 * Sound output: the 8-voice synthesizer (synth.h) and the player of the
 * cartridges' sound banks (player.h), played through the console's output
 * at 48 kHz (audio_out.h: HDMI on the Pi, the MAI audio FIFO fed by DMA;
 * on the RGB30 the I2S and the RK817's codec, its FIFO fed by an
 * interrupt). Both run in the output's interrupt, a chunk of samples (256,
 * 5.3 ms) at a time, in blocks of 64 samples, so the sound never waits
 * for a game's main loop.
 *
 * Each voice is driven by 16 bytes of registers (synth.h): .bm games can
 * read and write them with apu(), the player and the note helpers write
 * them for the voices they drive. Everything here is IRQ-safe.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "player.h"

#define AUDIO_RATE          48000
#define AUDIO_CHUNK         256         /* samples per buffer of the output */
#define AUDIO_VOLUME_MAX    10

/* Starts the output (silence). 0 = running; otherwise audio_status() says
 * why not (e.g. no HDMI audio in QEMU's raspi0). */
int audio_init(void);
int audio_ready(void);
const char *audio_status(void);
void audio_print(void);                 /* status, the output's clocks and counters */

volatile uint8_t *audio_regs(void);     /* the 128 register bytes */

/* All voices off, sequences stopped, default settings (the bank stays). */
void audio_reset(void);

/* Notes on one voice. ms > 0: released after ms. wave and vol < 0: as
 * the voice had them. freq in Hz, with a fraction. */
void audio_note(unsigned ch, float freq, uint32_t ms, int wave, int vol);
void audio_note_off(unsigned ch);
void audio_freq(unsigned ch, float freq);
void audio_envelope(unsigned ch, int a, int d, int s, int r);
void audio_duty(unsigned ch, int duty);
int  audio_busy(unsigned ch);           /* sounding, or held by a sequence */
/* The note of a voice glides to hz in ms; wobbles semitones deep at
 * rate_hz (0: off); runs through semitone offsets, ms each (n = 0: off). */
void audio_slide(unsigned ch, float hz, uint32_t ms);
void audio_vibrato(unsigned ch, float semitones, float rate_hz);
void audio_arp(unsigned ch, const int8_t *semis, int n, uint32_t ms);

/* The sound bank (the AUDIO section of a .bm, player.h): NULL = none.
 * Returns 0, or -1 with a message (the old bank stays). Music and effects
 * that are playing go on with the new bank. */
int  audio_bank(const uint8_t *data, size_t len, char *err, size_t errlen);
/* Sound effect n on voice ch (-1: a free one): returns the voice or -1. */
int  audio_sfx(int n, int ch, int transpose, float vol);
void audio_sfx_stop(int ch);            /* -1: every effect */
int  audio_sfx_pos(int ch, int *step);  /* the effect playing on ch, or -1 */
void audio_music(int song, int order, int fade_ms);
void audio_music_pattern(int pat, int bpm, int swing, int step);
void audio_music_stop(int fade_ms);
int  audio_music_pos(int *song, int *order, int *step, int *pat);
void audio_tempo(float scale);
void audio_mute(int track, int on);
/* A sound of the bank as a step plays it (the editor's previews). */
void audio_play(int ch, int sound, int note, int vol, int fx, uint32_t ms);
/* Any sound (a preset of presets.h) on a voice (-1: a free one), pitch
 * envelope and vibrato included: the voice, or -1. */
int  audio_play_sound(int ch, const au_sound_t *s, int note, int vol, uint32_t ms);

/* Notes at a time (the pattern language): the clock in samples (48 kHz)
 * since the sound started, a note queued for a time on it (player_at:
 * 0, or -1 when the queue is full), the notes of a tag forgotten and
 * released (0: all), the voices they may use (bit v; 0: all), how many
 * wait. */
uint64_t audio_clock(void);
int  audio_at(uint64_t when, const au_sound_t *s, int note, int vol, uint32_t len, int tag);
void audio_at_cancel(int tag);
void audio_at_voices(uint8_t mask);
int  audio_at_waiting(int tag);
/* the cartridge's bank (NULL: none), for the names of its sounds */
const au_bank_t *audio_bank_now(void);

/* The sound of a voice for note(): registers 2..8 (wave, duty, volume,
 * envelope) and 11..31 (the tone) of `regs`, a whole voice's 32 bytes;
 * audio_tone_get reads them. */
void audio_tone(unsigned ch, const uint8_t *regs);
void audio_tone_get(unsigned ch, uint8_t *regs);

/* The room (size, damping, level 0..1) and the echo (ms, feedback,
 * level); audio_reset() puts back the usual ones. */
void audio_room(float size, float damp, float wet);
void audio_echo(float ms, float feedback, float wet);
void audio_fx_get(float room[3], float echo[3]);

/* Every voice the 8-bit chip of the first versions, no room nor echo:
 * asked by the game (until audio_reset) or by the player (Settings). */
#define AUDIO_RETRO_GAME    1
#define AUDIO_RETRO_USER    2
void audio_retro(int who, int on);
int  audio_retro_on(void);
/* A game left (suspended): music paused and every voice released; 0
 * goes on from there. */
void audio_pause(int on);

/* Master volume 0..AUDIO_VOLUME_MAX (10 = full). */
void audio_set_volume(int level);
int  audio_volume(void);

/* The output's depth (sound_depth in bm/config.txt): 16, 24 or 32 bits a
 * sample, rounded from the float mix with TPDF dither (32: none). Any
 * other value is the default. HDMI carries 24 at most (32 goes out as 24);
 * the RGB30's I2S sends 32-bit words, its codec (RK817) converts 24.
 * audio_depth_out(): what the output asks for now (0 without one). */
#define AUDIO_DEPTH_DEFAULT 24
void     audio_set_depth(unsigned bits);
unsigned audio_depth(void);
unsigned audio_depth_out(void);

/* Call once per frame: without HDMI audio the player still moves, silent. */
void audio_idle(void);

/* Monitor command: status plus a short tune through every waveform and
 * the effects. */
void audio_test(void);

#endif
