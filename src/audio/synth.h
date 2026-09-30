/*
 * 8-voice synthesizer with the semantics of the APU of lua32's s32 machine
 * (its apu.lua): square / triangle / saw / noise oscillators, a linear
 * ADSR envelope, direct sum of the voices clamped to 16 bits. Portable C,
 * no hardware: the host tests build it as it is.
 */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#define SYNTH_VOICES        8
#define SYNTH_VOICE_BYTES   16
#define SYNTH_REG_BYTES     (SYNTH_VOICES * SYNTH_VOICE_BYTES)

/* Register offsets inside a voice (the same as lua32's s32 APU). */
#define SYNTH_FREQ_LO   0
#define SYNTH_FREQ_HI   1
#define SYNTH_WAVEFORM  2
#define SYNTH_DUTY      3
#define SYNTH_VOLUME    4
#define SYNTH_ATTACK    5
#define SYNTH_DECAY     6
#define SYNTH_SUSTAIN   7
#define SYNTH_RELEASE   8
#define SYNTH_CONTROL   9
#define SYNTH_GATE      0x01

#define SYNTH_SQUARE    0
#define SYNTH_TRIANGLE  1
#define SYNTH_SAW       2
#define SYNTH_NOISE     3

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
    synth_voice_t v[SYNTH_VOICES];
} synth_t;

void synth_init(synth_t *s, uint32_t rate);

/* Level increment per sample for an ADSR rate register: 0 is immediate,
 * 255 crosses the whole 0..255 ramp in 2 s (apu.lua rate_increment). */
float synth_rate_increment(uint8_t rate, uint32_t sample_rate);

/* Renders n mono samples from the 128 register bytes. The registers are
 * read once per call, like apu.lua: a gate change is seen at the start of
 * the next call. */
void synth_render(synth_t *s, const volatile uint8_t *regs, int16_t *out, unsigned n);

/* The next render sees a rising gate edge on voice ch even if the gate
 * bit stayed 1 (a note played again before its release). */
void synth_retrigger(synth_t *s, unsigned ch);

/* Voices whose envelope is not idle. */
unsigned synth_active(const synth_t *s);

#endif
