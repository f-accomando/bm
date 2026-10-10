/*
 * 8-voice synthesizer. Each voice: a wave (square with duty, triangle,
 * saw, sine, two noises, two-operator FM, a plucked string, a supersaw,
 * an organ of four harmonics, a sample: a recorded sound of the bank or of
 * the console's own drum kit, pink, brown noise, crackle), noise mixed in,
 * a drive of six curves, a resonant filter (low, band, high pass, notch)
 * with its envelope and an LFO, the formants of a vowel, an ADSR envelope,
 * tremolo, a bit crusher and a sample-rate reducer, a place left to right
 * and three sends: the room (a reverb), the echo and the chorus. A voice
 * can duck the others (a sidechain). The voices are summed in stereo in
 * 32-bit floats, the room, the echo and the chorus added, softly limited,
 * and only at the very end rounded to the output's depth: 16 or 24 bits
 * with TPDF dither, or 32.
 *
 * The waves are band-limited (PolyBLEP: no aliasing whistles on the high
 * notes), the envelopes exponential like an analog synth's, volume and
 * pitch move smoothly within a block. A voice with SYNTH_RAW in its
 * control register (or every voice, with synth_t.retro) is the chip of
 * the first versions: naive waves, held noise, straight envelopes, and its
 * 16 bits truncated as they always were (no dither), whatever the depth.
 * The tone moves smoothly within a block too: the filter's coefficients
 * and the square's width ramp sample by sample from the last block's.
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
#define SYNTH_FILTER    13      /* bits 0-1 SYNTH_LOWPASS..SYNTH_NOTCH, bit 2 SYNTH_KEYTRACK,
                                   bits 3-5 a vowel's formants after it (SYNTH_VOWEL_*) */
#define SYNTH_FENV      14      /* s8: the filter envelope opens it 1/16 octave per unit */
#define SYNTH_FDECAY    15      /* the filter envelope's decay (as ADSR times) */
#define SYNTH_PAN       16      /* s8: -127 left .. 0 middle .. 127 right */
#define SYNTH_MOD1      17      /* FM ratio x/16, PLUCK brightness, SUPERSAW spread, ORGAN bars 1-2,
                                   SAMPLE which one (0..127 the bank's, SYNTH_KIT.. the kit's) */
#define SYNTH_MOD2      18      /* FM depth x/32 rad, PLUCK sustain, ORGAN bars 3-4,
                                   SAMPLE where it starts, x/256 of its length */
#define SYNTH_MODDECAY  19      /* FM: the depth fades in this time (0: it stays) */
#define SYNTH_NOISEMIX  20      /* noise added to the wave (its colour: FLAGS), 0..255 */
#define SYNTH_DRIVE     21      /* saturation before the filter (its curve: FLAGS), 0..255 */
#define SYNTH_REVERB    22      /* send to the room, 0..255          */
#define SYNTH_ECHO      23      /* send to the echo, 0..255          */
#define SYNTH_LFO_RATE  24      /* 0 off; 1..255 = 0.1 .. 20 Hz      */
#define SYNTH_LFO_CUT   25      /* the LFO moves the cutoff, 0..255 = 0..4 octaves */
#define SYNTH_LFO_PWM   26      /* the LFO moves the square's duty, 0..255 */
#define SYNTH_FMFB      27      /* FM: the modulator's feedback, 0..255 */
#define SYNTH_FLAGS     28      /* SYNTH_FLAG_*: the chip voice, the noise's colour, the drive's
                                   curve, a sample backwards */
#define SYNTH_CRUSH     29      /* low nibble: bits 1..15 kept of every sample (0: all); high
                                   nibble n: every value held n + 1 samples (coarse; 0: none) */
#define SYNTH_TREMOLO   30      /* low nibble: the LFO moves the volume n/15 (tremolo); high
                                   nibble: the voice ducks the others n/15 (sidechain) */
#define SYNTH_CHORUS    31      /* send to the chorus, 0..255 */

#define SYNTH_GATE      0x01    /* control: key down                 */
#define SYNTH_RAW       0x02    /* control: the 8-bit chip voice     */
#define SYNTH_FLAG_RAW  0x01    /* flags: the same, kept with the sound's tone */
#define SYNTH_FLAG_COLOR 0x06   /* flags bits 1-2: the noise mix's colour, SYNTH_COLOR_* */
#define SYNTH_FLAG_CURVE 0x38   /* flags bits 3-5: the drive's curve, SYNTH_CURVE_* */
#define SYNTH_FLAG_REVERSE 0x40 /* flags: a sample plays backwards, from its end */
#define SYNTH_COLOR_SHIFT 1
#define SYNTH_CURVE_SHIFT 3
enum { SYNTH_COLOR_WHITE, SYNTH_COLOR_PINK, SYNTH_COLOR_BROWN, SYNTH_COLOR_CRACKLE };
enum {
    SYNTH_CURVE_SOFT,       /* a rational tanh (the drive of the first clean voice) */
    SYNTH_CURVE_HARD,       /* clipped flat at +-1 */
    SYNTH_CURVE_FOLD,       /* folded back at +-1 (a triangle wavefolder) */
    SYNTH_CURVE_SINE,       /* a sine of the signal: soft, then folding */
    SYNTH_CURVE_ASYM,       /* bent more on one side (a diode, a tube): even harmonics */
    SYNTH_CURVE_CUBIC,      /* 1.5x - 0.5x^3: the gentlest */
    SYNTH_CURVES
};

#define SYNTH_LOWPASS   0
#define SYNTH_BANDPASS  1
#define SYNTH_HIGHPASS  2
#define SYNTH_NOTCH     3
#define SYNTH_KEYTRACK  0x04    /* the cutoff follows the note (C4 = as set) */
#define SYNTH_VOWEL     0x38    /* bits 3-5: 0 none, SYNTH_VOWEL_A .. SYNTH_VOWEL_U */
#define SYNTH_VOWEL_SHIFT 3
enum { SYNTH_VOWEL_NONE, SYNTH_VOWEL_A, SYNTH_VOWEL_E, SYNTH_VOWEL_I, SYNTH_VOWEL_O, SYNTH_VOWEL_U, SYNTH_VOWELS };

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
#define SYNTH_SAMPLE    10      /* a sample (MOD1, MOD2): the note against its root note sets its
                                   speed (frequency 0: its own speed); a one-shot ends the note */
#define SYNTH_PINK      11      /* pink noise, -3 dB an octave (the note does not matter): rain, surf */
#define SYNTH_BROWN     12      /* brown noise, -6 dB an octave: rumble, wind, waves */
#define SYNTH_CRACKLE   13      /* random clicks, MOD1 how many (0: 100 a second): vinyl, fire */
#define SYNTH_WAVES     14

/* A sample: 16-bit frames (interleaved if stereo) at its own rate. The
 * buffer holds one guard frame before frame 0 and three after the last
 * (synth_sample_ready writes them), so the interpolation never reads
 * outside it. */
#define SYNTH_LOOP_OFF      0       /* plays once: the voice ends with it */
#define SYNTH_LOOP_FWD      1       /* [loop_start, loop_end) again and again */
#define SYNTH_LOOP_PINGPONG 2       /* forwards and backwards in it */
#define SYNTH_SAMPLE_GUARD  4       /* frames around a sample's own: one before, three after */
#define SYNTH_KIT           128     /* MOD1 from here: the console's drum kit */
typedef struct {
    const int16_t *pcm;     /* frame 0; NULL: silent */
    uint32_t len;           /* frames (a looped sample: up to its loop's end) */
    uint32_t loop_start, loop_end;
    uint32_t rate;          /* Hz */
    uint8_t channels;       /* 1, 2 */
    uint8_t loop;           /* SYNTH_LOOP_* */
    uint8_t root;           /* the MIDI note at which it plays at its own speed (60 = C4) */
    int8_t fine;            /* ... and cents */
    float rk;               /* rate / the root's Hz (synth_sample_ready) */
} synth_sample_t;

/* the reverb's delay lines and the echo's, in samples */
#define SYNTH_ROOM_LINES    8
#define SYNTH_ROOM_LEN      1700        /* at half the rate */
#define SYNTH_ECHO_LEN      32768       /* 0.68 s at 48 kHz */
#define SYNTH_PLUCK_LEN     2048        /* the lowest string: 23 Hz at 48 kHz */
#define SYNTH_CHORUS_LEN    2048        /* 42 ms at 48 kHz */
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
    float fg, fk;           /* its g and k at the end of the last block (fg 0: none, no ramp) */
    float pw;               /* the square's width at the end of the last block (< 0: none) */
    float fm_fb;            /* the modulator's last value */
    uint32_t lfo;           /* the LFO's phase */
    uint16_t ks_w;          /* pluck: where the string is written */
    float ks_lp, ks_ax, ks_ay;  /* pluck: the damping and allpass filters' state */
    uint8_t env_r[4];       /* the ADSR registers the coefficients are for */
    float env_c[4];         /* attack, decay, release and filter decay per sample */
    int64_t spos;           /* sample: where it plays, frames in 32.32 fixed point */
    uint8_t smp;            /* sample: which (MOD1 when the note started) */
    uint8_t sdone;          /* sample: it ended (the voice too, at the block's end) */
    int8_t sdir;            /* sample: +1 forwards, -1 backwards (ping-pong) */
    float f1b, f2b;         /* the filter's state for a stereo sample's right */
    float pink[3], brown;   /* the coloured noise of the wave ... */
    float mpink[3], mbrown; /* ... and of the noise mix */
    float vow[2][6];        /* the vowel's three formant filters, left and right */
    float held[2];          /* coarse: the value held, left and right ... */
    uint8_t hold;           /* ... and for how many more samples */
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

    /* the chorus: one delay line, two taps moved by an LFO a quarter turn
     * apart, left and right (synth_chorus) */
    float chorus_delay, chorus_depth, chorus_wet;   /* samples, samples, 0..1 */
    uint32_t chorus_lfo, chorus_inc, chorus_i, chorus_quiet;
    float chorus[SYNTH_CHORUS_LEN];

    float duck;                             /* how far the others are ducked, 0..1 */
    float vowel[SYNTH_VOWELS][3][4];        /* each vowel's formants: a1, a2, a3 and gain */

    float dc_x[2], dc_y[2];                 /* the output's DC blocker */
    float comp_env, comp_gain;              /* the output's compressor */
    uint32_t dither;                        /* the dither's random numbers (xorshift, never 0) */

    /* the bank's samples (MOD1 0..127 of a SYNTH_SAMPLE voice) */
    const synth_sample_t *smp;
    unsigned nsmp;

    /* one block: a voice's samples (vbuf2: a stereo sample's right), then
     * the mix and the sends */
    float vbuf[SYNTH_BLOCK], vbuf2[SYNTH_BLOCK];
    float mix[2][SYNTH_BLOCK], mix_c[SYNTH_BLOCK], send_room[SYNTH_BLOCK], send_echo[SYNTH_BLOCK];
    float send_chorus[SYNTH_BLOCK];
    uint8_t center_used;                    /* a voice in the middle went into mix_c */
    float out[2 * SYNTH_BLOCK];             /* synth_render's block, before the rounding */
    float pluck[SYNTH_VOICES][SYNTH_PLUCK_LEN];
} synth_t;

/* The first call also makes the console's drum kit (a few ms, once). */
void synth_init(synth_t *s, uint32_t rate);

/* The bank's samples (NULL: none): MOD1 n < n plays tab[n]. synth_init
 * forgets them: set them again after it. */
void synth_samples(synth_t *s, const synth_sample_t *tab, unsigned n);

/* A sample's frames written at pcm (frame 0; the buffer holds
 * SYNTH_SAMPLE_GUARD more frames around them: pcm - channels is its
 * start): its loop checked (a looped sample ends with its loop: what
 * follows is never heard), the guard frames written, its tuning set. */
void synth_sample_ready(synth_sample_t *x, int16_t *pcm);

/* The console's drum kit, made by the synthesizer (no recording, nothing
 * to license): MOD1 SYNTH_KIT + k plays sample k. Its names: "bd" kick,
 * "sd" snare, "hh" closed hi-hat, "oh" open hi-hat, "cp" clap, "rim",
 * "tom", "cb" cowbell; all mono, 48 kHz, root C4 (60). */
#define SYNTH_KIT_SIZE  8
const synth_sample_t *synth_kit(unsigned k);
const char *synth_kit_name(unsigned k);
int synth_kit_find(const char *name);       /* SYNTH_KIT + k, or -1 */

/* The room: size 0..1 (a small room .. a hall), damping 0..1 (bright ..
 * dull), wet 0..1 (how much of the sends is heard). The echo: the time
 * between repeats (at most 680 ms), feedback 0..0.95, wet 0..1. */
void synth_room(synth_t *s, float size, float damp, float wet);
void synth_echo(synth_t *s, float ms, float feedback, float wet);
/* The chorus: its LFO's rate (0.05..5 Hz, 0.8 after synth_init), how far
 * the delay swings (0..8 ms around 14 ms, 2.5) and how much of it is heard
 * (0..1, 1). */
void synth_chorus(synth_t *s, float rate_hz, float depth_ms, float wet);

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

/* The same at a depth of 16, 24 or 32 bits, in words aligned to the left
 * (bit 31 the sign; at 16 bits the low half 0, at 24 the low byte). */
void synth_render32(synth_t *s, const volatile uint8_t *regs, int32_t *out, unsigned n, unsigned bits);

/* The two halves of a render, for a caller that adds its own sound in
 * between (audio.c: nano8). synth_mix: n frames as floats (2n, left
 * first), 1.0 = full scale, after the room, the echo, the chorus, the DC
 * blocker, the compressor, the limiter and the master volume; it says
 * what they are: */
#define SYNTH_MIX_SILENT    0       /* all 0: no voice, the room, echo and chorus quiet (no dither) */
#define SYNTH_MIX_SOUND     1       /* the clean sound: rounded with dither */
#define SYNTH_MIX_RETRO     2       /* the chip (retro): truncated to 16 bits, as always */
int  synth_mix(synth_t *s, const volatile uint8_t *regs, float *out, unsigned n);

/* The floats (2n) rounded to `bits` (16, 24; 32 is the float as it is):
 * TPDF dither, two uniform random numbers of one step each, so the error
 * is +-1 LSB, triangular, never following the sound (the quiet tails of
 * the room fade into noise instead of breaking into steps). A sample past
 * +-1.0 is clipped. `kind` is synth_mix's (SILENT: zeros, no noise). */
void synth_quantize(synth_t *s, const float *in, int32_t *out, unsigned n, unsigned bits, int kind);
void synth_quantize16(synth_t *s, const float *in, int16_t *out, unsigned n, int kind);

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
