/*
 * Ready-made instruments of the synthesizer: drums, basses, keys, pads,
 * plucks, leads and effects, each a sound as the bank stores it
 * (au_sound_t: wave, envelope, pitch envelope, tone). The same names in
 * Lua (tone(), play(), instruments()), in bm Sound (a preset into a
 * sound of the bank) and for the assistant that writes music.
 *
 * And the tone in plain units: a key and a value ("cutoff" in Hz,
 * "attack" in ms, "pan" -1..1...) into a voice's registers, for tone()
 * and the pattern language. Portable C, no hardware.
 */
#ifndef PRESETS_H
#define PRESETS_H

#include "player.h"

typedef struct {
    au_sound_t s;
    const char *kind;           /* "drum", "bass", "keys", "pad", "pluck", "lead", "fx" */
    const char *about;          /* one line, for the lists */
} au_preset_t;

extern const au_preset_t au_presets[];
extern const int au_preset_count;

/* the preset of that name (case does not matter), or -1 */
int au_preset_find(const char *name);

/* the wave of a name ("square" .. "organ", or a number), or -1 */
int au_wave_find(const char *name);
extern const char *const au_wave_names[SYNTH_WAVES];

/* A sound's wave, duty, envelope and tone into a voice's registers (the
 * volume too). Its pitch envelope, vibrato and detune need the player
 * (player_play_sound). */
void au_sound_regs(const au_sound_t *s, volatile uint8_t *voice_regs);

/* One key of the tone in plain units into a voice's registers:
 *   wave (au_wave_find), filter ("lp" "bp" "hp" "notch"): au_tone_str
 *   vol, duty, sustain, res, noise, drive, reverb, echo, pwm, feedback,
 *   bright, ring, spread 0..1;  attack, decay, release, fdecay, mdecay ms;
 *   cutoff Hz (0: no filter);  fenv, wah octaves;  pan -1..1;  lfo Hz;
 *   ratio (FM, 1/16..16);  depth (FM, radians 0..8);  keytrack, raw 0/1;
 *   bar1..bar4 (the organ's drawbars, 0..15);  sample (wave "sample": a
 *   number, 0.. the bank's, 128.. the kit's; au_tone_str: a kit name, "bd"
 *   .. "cb", or "kit:N"; lua_tone.c: the bank's names too);  begin 0..1
 *   (where it starts);  reverse 0/1
 * Returns 0, or -1 for a key it does not know. */
int au_tone_num(volatile uint8_t *voice_regs, const char *key, double value);
int au_tone_str(volatile uint8_t *voice_regs, const char *key, const char *value);

#endif
