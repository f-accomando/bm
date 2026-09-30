/*
 * Sound output: the 8-voice synthesizer (synth.h) played through HDMI
 * (the BCM2835 MAI audio FIFO fed by DMA, 48 kHz). The synthesizer runs in
 * the DMA interrupt, one chunk of 256 samples (5.3 ms) at a time, so the
 * sound never waits for a game's main loop.
 *
 * Voices are driven by 128 bytes of registers laid out like the s32 APU
 * (spec §8). By default they are the module's own registers (for .bm
 * games, the monitor); the s32 player points the synthesizer at the
 * machine's APU memory instead.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_RATE          48000
#define AUDIO_CHUNK         256         /* samples per DMA buffer */

/* Detects HDMI audio and starts the output (silence). 0 = running;
 * otherwise audio_status() says why not (e.g. no HDMI audio in QEMU). */
int audio_init(void);
int audio_ready(void);
const char *audio_status(void);
void audio_print(void);                 /* status, clocks, DMA counters */

/* Registers the synthesizer reads: NULL = the module's own. */
void audio_use_regs(volatile uint8_t *regs);
volatile uint8_t *audio_regs(void);     /* the module's own registers */

/* Helpers on the module's own registers (IRQ-safe). */
void audio_reset(void);                 /* all voices off, default settings */
/* Starts a note (restarting the envelope). ms > 0: released after ms. */
void audio_note(unsigned ch, uint32_t freq, uint32_t ms, int wave, int vol);
void audio_note_off(unsigned ch);
void audio_freq(unsigned ch, uint32_t freq);
void audio_envelope(unsigned ch, int a, int d, int s, int r);
void audio_duty(unsigned ch, int duty);
int  audio_busy(unsigned ch);           /* envelope not idle */

/* Monitor command: status plus a short tune through every waveform. */
void audio_test(void);

#endif
