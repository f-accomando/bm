/*
 * 8-voice synthesizer: square (with duty), triangle, saw, sine and two
 * noises (a 15-bit LFSR, and a short 93-step one that sounds metallic),
 * a linear ADSR envelope per voice, the voices summed and softly limited
 * to 16 bits. Every voice is driven by 16 bytes of registers (the layout
 * below), so the same sound can be set from C, from Lua (apu()) or by the
 * sequencer of player.c. Portable C, no hardware: the host tests build it
 * as it is.
 */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#define SYNTH_VOICES        8
#define SYNTH_VOICE_BYTES   16
#define SYNTH_REG_BYTES     (SYNTH_VOICES * SYNTH_VOICE_BYTES)

/* Register offsets inside a voice. */
#define SYNTH_FREQ_LO   0       /* frequency in Hz, 16 bits ...      */
#define SYNTH_FREQ_HI   1
#define SYNTH_WAVEFORM  2       /* SYNTH_SQUARE ... SYNTH_METAL      */
#define SYNTH_DUTY      3       /* square: high part, 0..255 = 0..100% */
#define SYNTH_VOLUME    4       /* 0..255                            */
#define SYNTH_ATTACK    5       /* ADSR times 0..255 = 0..2 s ...    */
#define SYNTH_DECAY     6
#define SYNTH_SUSTAIN   7       /* ... sustain is a level, 0..255    */
#define SYNTH_RELEASE   8
#define SYNTH_CONTROL   9       /* bit 0: gate (key down)            */
#define SYNTH_FREQ_FRAC 10      /* ... plus this many 1/256 Hz       */
/* 11..15: reserved, read as 0 */
#define SYNTH_GATE      0x01

#define SYNTH_SQUARE    0
#define SYNTH_TRIANGLE  1
#define SYNTH_SAW       2
#define SYNTH_NOISE     3
#define SYNTH_SINE      4
#define SYNTH_METAL     5       /* short-period noise: hi-hats, bells, metal */
#define SYNTH_WAVES     6

enum { SYNTH_IDLE, SYNTH_ATTACK_ST, SYNTH_DECAY_ST, SYNTH_SUSTAIN_ST, SYNTH_RELEASE_ST };

typedef struct {
    uint32_t phase;         /* fraction of a cycle, 32-bit fixed point */
    float level;            /* envelope, 0..255 */
    uint8_t stage;
    uint8_t gated;          /* gate seen by the previous render */
    uint16_t lfsr;          /* never 0 */
    float noise;
} synth_voice_t;

typedef struct {
    uint32_t rate;          /* samples per second */
    float gain;             /* master volume, 0..1 (1 after synth_init) */
    synth_voice_t v[SYNTH_VOICES];
} synth_t;

void synth_init(synth_t *s, uint32_t rate);

/* Level increment per sample for an ADSR rate register: 0 is immediate,
 * 255 crosses the whole 0..255 ramp in 2 s. */
float synth_rate_increment(uint8_t rate, uint32_t sample_rate);

/* Renders n mono samples from the 128 register bytes. The registers are
 * read once per call: a gate change is seen at the start of the next
 * call. A new note attacks from the level the voice is at (no click when
 * a sounding voice is played again); a voice that was silent starts its
 * waveform from the beginning, so drums sound the same every time. */
void synth_render(synth_t *s, const volatile uint8_t *regs, int16_t *out, unsigned n);

/* The next render sees a rising gate edge on voice ch even if the gate
 * bit stayed 1 (a note played again before its release). */
void synth_retrigger(synth_t *s, unsigned ch);

/* Voices whose envelope is not idle. */
unsigned synth_active(const synth_t *s);

/* The soft limiter of the mix: straight up to 0.85, then bending towards
 * 1.0 (the sum of loud voices saturates smoothly instead of clipping). */
float synth_limit(float x);

#endif
