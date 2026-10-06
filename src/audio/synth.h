/*
 * 8-voice synthesizer. Each voice: a wave (square with duty, triangle,
 * saw, sine, two noises, two-operator FM, a plucked string, a supersaw,
 * an organ of four harmonics), white noise mixed in, a soft drive, a
 * resonant filter (low, band, high pass, notch) with its envelope and an
 * LFO, an ADSR envelope, a place left to right and two sends: the room (a
 * reverb) and the echo. The voices are summed in stereo, the room and the
 * echo added, and softly limited to 16 bits.
 *
 * The waves are band-limited (PolyBLEP: no aliasing whistles on the high
 * notes), the envelopes exponential like an analog synth's, volume and
 * pitch move smoothly within a block. A voice with SYNTH_RAW in its
 * control register (or every voice, with synth_t.retro) is the chip of
 * the first versions: naive waves, held noise, straight envelopes.
 *
 * Every voice is driven by 32 bytes of registers (the layout below; a
 * register left at 0 is the plain sound), so the same sound can be set
 * from C, from Lua (apu(), tone()) or by the sequencer of player.c.
 * Portable C, no hardware: the host tests build it as it is.
 */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#define SYNTH_VOICES        8
#define SYNTH_VOICE_BYTES   32
#define SYNTH_REG_BYTES     (SYNTH_VOICES * SYNTH_VOICE_BYTES)

/* Register offsets inside a voice. */
#define SYNTH_FREQ_LO   0       /* frequency in Hz, 16 bits ...      */
#define SYNTH_FREQ_HI   1
#define SYNTH_WAVEFORM  2       /* SYNTH_SQUARE ... SYNTH_ORGAN      */
#define SYNTH_DUTY      3       /* square: high part, 0..255 = 0..100% */
#define SYNTH_VOLUME    4       /* 0..255                            */
#define SYNTH_ATTACK    5       /* ADSR times 0..255 = 0..2 s ...    */
#define SYNTH_DECAY     6
#define SYNTH_SUSTAIN   7       /* ... sustain is a level, 0..255    */
#define SYNTH_RELEASE   8
#define SYNTH_CONTROL   9       /* SYNTH_GATE, SYNTH_RAW             */
#define SYNTH_FREQ_FRAC 10      /* ... plus this many 1/256 Hz       */
/* the tone: every one 0 is the plain wave in the middle, dry */
#define SYNTH_CUTOFF    11      /* filter: 0 none; 1..255 = 20 Hz .. 20 kHz */
#define SYNTH_RESONANCE 12      /* 0..255: flat .. ringing           */
#define SYNTH_FILTER    13      /* bits 0-1 SYNTH_LOWPASS..SYNTH_NOTCH, bit 2 SYNTH_KEYTRACK */
#define SYNTH_FENV      14      /* s8: the filter envelope opens it 1/16 octave per unit */
#define SYNTH_FDECAY    15      /* the filter envelope's decay (as ADSR times) */
#define SYNTH_PAN       16      /* s8: -127 left .. 0 middle .. 127 right */
#define SYNTH_MOD1      17      /* FM ratio x/16, PLUCK brightness, SUPERSAW spread, ORGAN bars 1-2 */
#define SYNTH_MOD2      18      /* FM depth x/32 rad, PLUCK sustain, ORGAN bars 3-4 */
#define SYNTH_MODDECAY  19      /* FM: the depth fades in this time (0: it stays) */
#define SYNTH_NOISEMIX  20      /* white noise added to the wave, 0..255 */
#define SYNTH_DRIVE     21      /* soft saturation before the filter, 0..255 */
#define SYNTH_REVERB    22      /* send to the room, 0..255          */
#define SYNTH_ECHO      23      /* send to the echo, 0..255          */
#define SYNTH_LFO_RATE  24      /* 0 off; 1..255 = 0.1 .. 20 Hz      */
#define SYNTH_LFO_CUT   25      /* the LFO moves the cutoff, 0..255 = 0..4 octaves */
#define SYNTH_LFO_PWM   26      /* the LFO moves the square's duty, 0..255 */
#define SYNTH_FMFB      27      /* FM: the modulator's feedback, 0..255 */
#define SYNTH_FLAGS     28      /* SYNTH_FLAG_RAW: a sound that is the chip voice */
/* 29..31: reserved, read as 0 */

#define SYNTH_GATE      0x01    /* control: key down                 */
#define SYNTH_RAW       0x02    /* control: the 8-bit chip voice     */
#define SYNTH_FLAG_RAW  0x01    /* flags: the same, kept with the sound's tone */

#define SYNTH_LOWPASS   0
#define SYNTH_BANDPASS  1
#define SYNTH_HIGHPASS  2
#define SYNTH_NOTCH     3
#define SYNTH_KEYTRACK  0x04    /* the cutoff follows the note (C4 = as set) */

#define SYNTH_SQUARE    0
#define SYNTH_TRIANGLE  1
#define SYNTH_SAW       2
#define SYNTH_NOISE     3
#define SYNTH_SINE      4
#define SYNTH_METAL     5       /* short-period noise: hi-hats, bells, metal */
#define SYNTH_FM        6       /* two sines, one bending the other: e-pianos, bells, basses, brass */
#define SYNTH_PLUCK     7       /* a plucked string (Karplus-Strong): guitars, harps, plucks */
#define SYNTH_SUPERSAW  8       /* three saws a little out of tune: pads, big leads */
#define SYNTH_ORGAN     9       /* four harmonics (drawbars): organs, flutes, soft leads */
#define SYNTH_WAVES     10

/* the reverb's delay lines and the echo's, in samples */
#define SYNTH_ROOM_LINES    8
#define SYNTH_ROOM_LEN      1700        /* at half the rate */
#define SYNTH_ECHO_LEN      32768       /* 0.68 s at 48 kHz */
#define SYNTH_PLUCK_LEN     2048        /* the lowest string: 23 Hz at 48 kHz */
#define SYNTH_BLOCK         64          /* samples between updates of the tone */

enum { SYNTH_IDLE, SYNTH_ATTACK_ST, SYNTH_DECAY_ST, SYNTH_SUSTAIN_ST, SYNTH_RELEASE_ST };

typedef struct {
    uint32_t phase;         /* fraction of a cycle, 32-bit fixed point */
    uint32_t phase2, phase3;/* FM's modulator, the supersaw's other saws */
    float level;            /* envelope, 0..255 */
    uint8_t stage;
    uint8_t gated;          /* gate seen by the previous render */
    uint8_t fresh;          /* the note started this block: no ramps */
    uint16_t lfsr;          /* never 0 */
    float noise, noise0;    /* the LFSR's value, and the one before (interpolated) */
    uint32_t inc;           /* the phase step at the end of the last block */
    uint32_t f88, f88_inc;  /* the frequency registers, and their phase step */
    float vol;              /* the volume at the end of the last block */
    float fenv, menv;       /* filter envelope 0..1, FM depth envelope 1..0 */
    float f1, f2;           /* the filter's state */
    float fm_fb;            /* the modulator's last value */
    uint32_t lfo;           /* the LFO's phase */
    uint16_t ks_w;          /* pluck: where the string is written */
    float ks_lp, ks_ax, ks_ay;  /* pluck: the damping and allpass filters' state */
    uint8_t env_r[4];       /* the ADSR registers the coefficients are for */
    float env_c[4];         /* attack, decay, release and filter decay per sample */
} synth_voice_t;

typedef struct {
    uint32_t rate;          /* samples per second */
    float gain;             /* master volume, 0..1 (1 after synth_init) */
    uint8_t retro;          /* every voice raw, no room and no echo */
    uint32_t rng;           /* white noise */
    synth_voice_t v[SYNTH_VOICES];

    /* the room: size, damping and level 0..1 (synth_room) */
    float room_size, room_damp, room_wet;
    float room_g[SYNTH_ROOM_LINES];         /* feedback of each line for the size */
    float room_lp[SYNTH_ROOM_LINES];
    uint16_t room_len[SYNTH_ROOM_LINES], room_i[SYNTH_ROOM_LINES];
    float room_ap[2][256];                  /* input diffusion */
    float room_out[2];                      /* its last output (the line between) */
    uint16_t room_api[2];
    float room[SYNTH_ROOM_LINES][SYNTH_ROOM_LEN];
    uint32_t room_quiet;                    /* samples with nothing in it */

    /* the echo: time, feedback and level (synth_echo), left and right */
    uint32_t echo_len;
    float echo_fb, echo_wet, echo_lp[2];
    uint32_t echo_i;
    float echo[2][SYNTH_ECHO_LEN];
    uint32_t echo_quiet;

    float dc_x[2], dc_y[2];                 /* the output's DC blocker */
    float comp_env, comp_gain;              /* the output's compressor */

    /* one block: a voice's samples, then the mix and the sends */
    float vbuf[SYNTH_BLOCK];
    float mix[2][SYNTH_BLOCK], mix_c[SYNTH_BLOCK], send_room[SYNTH_BLOCK], send_echo[SYNTH_BLOCK];
    uint8_t center_used;                    /* a voice in the middle went into mix_c */
    float pluck[SYNTH_VOICES][SYNTH_PLUCK_LEN];
} synth_t;

void synth_init(synth_t *s, uint32_t rate);

/* The room: size 0..1 (a small room .. a hall), damping 0..1 (bright ..
 * dull), wet 0..1 (how much of the sends is heard). The echo: the time
 * between repeats (at most 680 ms), feedback 0..0.95, wet 0..1. */
void synth_room(synth_t *s, float size, float damp, float wet);
void synth_echo(synth_t *s, float ms, float feedback, float wet);

/* Level increment per sample for an ADSR rate register of a raw voice: 0
 * is immediate, 255 crosses the whole 0..255 ramp in 2 s. */
float synth_rate_increment(uint8_t rate, uint32_t sample_rate);

/* Renders n stereo frames (2n samples, left first) from the register
 * bytes. The registers are read once per block of SYNTH_BLOCK: a gate
 * change is seen at the start of the next call. A new note attacks from
 * the level the voice is at (no click when a sounding voice is played
 * again); a voice that was silent starts its waveform from the beginning,
 * so drums sound the same every time. */
void synth_render(synth_t *s, const volatile uint8_t *regs, int16_t *out, unsigned n);

/* The next render sees a rising gate edge on voice ch even if the gate
 * bit stayed 1 (a note played again before its release). */
void synth_retrigger(synth_t *s, unsigned ch);

/* Voices whose envelope is not idle. */
unsigned synth_active(const synth_t *s);

/* The soft limiter of the mix: straight up to 0.85, then bending towards
 * 1.0 (the sum of loud voices saturates smoothly instead of clipping). */
float synth_limit(float x);

/* The cutoff register's frequency in Hz (1..255; 0 is no filter). */
float synth_cutoff_hz(uint8_t reg);

#endif
