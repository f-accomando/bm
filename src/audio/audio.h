/*
 * Sound output: the 8-voice synthesizer (synth.h) and the player of the
 * cartridges' sound banks (player.h), played through HDMI (the BCM2835
 * MAI audio FIFO fed by DMA, 48 kHz). Both run in the DMA interrupt, one
 * chunk of 256 samples (5.3 ms) at a time, in blocks of 64 samples, so the
 * sound never waits for a game's main loop.
 *
 * Each voice is driven by 16 bytes of registers (synth.h): .bm games can
 * read and write them with apu(), the player and the note helpers write
 * them for the voices they drive. Everything here is IRQ-safe.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_RATE          48000
#define AUDIO_CHUNK         256         /* samples per DMA buffer */
#define AUDIO_VOLUME_MAX    10

/* Detects HDMI audio and starts the output (silence). 0 = running;
 * otherwise audio_status() says why not (e.g. no HDMI audio in QEMU). */
int audio_init(void);
int audio_ready(void);
const char *audio_status(void);
void audio_print(void);                 /* status, clocks, DMA counters */

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
/* A game left (suspended): music paused and every voice released; 0
 * goes on from there. */
void audio_pause(int on);

/* Master volume 0..AUDIO_VOLUME_MAX (10 = full). */
void audio_set_volume(int level);
int  audio_volume(void);

/* Call once per frame: without HDMI audio the player still moves, silent. */
void audio_idle(void);

/* Monitor command: status plus a short tune through every waveform and
 * the effects. */
void audio_test(void);

#endif
