/*
 * The sound bank of a cartridge and its player: sounds (instruments),
 * sound effects, patterns and songs, played on the 8 voices of synth.h.
 * The player runs in the audio interrupt, before every block of samples,
 * so music and effects keep their time whatever the game's frame rate.
 * Portable C (no hardware): the host tests build it as it is.
 *
 * The bank, as stored in the AUDIO section of a .bm (type 6, see bm.h),
 * little endian:
 *
 *   0   char[4] "BMAU"
 *   4   u8      version (1)
 *   5   u8      sounds (0..32), 6 u8 sfx (0..64), 7 u8 patterns (0..64),
 *   8   u8      songs (0..8), 9..15 reserved (0)
 *   16  sounds, 24 bytes each:
 *         0 char[8] name (ASCII, zero-padded)
 *         8 wave, 9 duty, 10 volume, 11 attack, 12 decay, 13 sustain,
 *        14 release (as the synth registers, synth.h)
 *        15 s8 pitch: the note starts this many semitones away...
 *        16 u8 ...and glides to it in this x 10 ms (0: no pitch envelope)
 *        17 u8 vibrato depth (cents), 18 u8 vibrato rate (x 0.1 Hz)
 *        19 s8 detune (cents), 20..23 reserved
 *   then the sound effects, 16 + 4 x steps bytes each:
 *         0 char[8] name, 8 u16 step length (ms, 1..2000), 10 u8 steps
 *        (1..32), 11 u8 loop start, 12 u8 loop end (end > start: steps
 *        start..end-1 repeat until the effect is stopped), 13..15 reserved,
 *        16 the steps
 *   then the patterns, 4 + 4 x steps x (tracks stored) bytes each:
 *         0 u8 steps (1..64), 1 u8 track mask (bit t: track t is stored,
 *        the others are empty), 2..3 reserved, 4 the steps of each stored
 *        track, track after track
 *   then the songs, 16 + length bytes each:
 *         0 char[8] name, 8 u8 tempo (BPM, a step is a 16th note), 9 u8
 *        swing (0..100), 10 u8 length (1..64), 11 u8 loop (the position
 *        played after the last one; 255: the song stops), 12..15 reserved,
 *        16 the pattern of each position
 *
 *   A step is 4 bytes: note (0 nothing new, the note goes on; 1..127 a
 *   MIDI note, 60 = C4, 69 = A4 440 Hz; 128 off: the note is released),
 *   sound, volume (0..255 of the sound's own), effect (high nibble the
 *   type AU_FX_*, low nibble its amount 0..15). An effect on a step with
 *   no note acts on the note that is playing.
 *
 * Track t of a pattern plays on voice t. A sound effect started with no
 * voice takes a free one, preferring the voices the song leaves silent;
 * while it plays, the music track of its voice is muted.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stddef.h>
#include <stdint.h>

#include "synth.h"

#define AU_MAGIC        "BMAU"
#define AU_VERSION      1
#define AU_SOUNDS       32
#define AU_SFX          64
#define AU_PATTERNS     64
#define AU_SONGS        8
#define AU_TRACKS       SYNTH_VOICES
#define AU_SFX_STEPS    32
#define AU_PAT_STEPS    64
#define AU_SONG_LEN     64
#define AU_NOTE_OFF     128
#define AU_NO_LOOP      255

/* effects: the high nibble of a step's fourth byte; the low nibble is n */
enum {
    AU_FX_NONE,
    AU_FX_GLIDE,        /* slides from the previous note in (n+1)/4 steps */
    AU_FX_BEND_UP,      /* bends up n semitones over the step */
    AU_FX_BEND_DOWN,    /* bends down n semitones over the step */
    AU_FX_VIBRATO,      /* vibrato n/8 semitone deep, 6 Hz */
    AU_FX_TREMOLO,      /* the volume wobbles by n/15, 6 Hz */
    AU_FX_CHORD,        /* chord n played as a fast arpeggio (60 notes/s) */
    AU_FX_ARP,          /* chord n, one note per step while the note holds */
    AU_FX_FADE_OUT,     /* to silence in n+1 steps */
    AU_FX_FADE_IN,      /* from silence in n+1 steps */
    AU_FX_RETRIG,       /* the note is struck n+1 times in the step */
    AU_FX_DELAY,        /* the note starts n/16 of a step late */
    AU_FX_CUT,          /* the note is released after (n+1)/16 of the step */
    AU_FX_COUNT
};

#define AU_CHORDS 16
/* semitones of chord n (AU_FX_CHORD, AU_FX_ARP), first `count` of 4 */
extern const int8_t au_chord[AU_CHORDS][4];
extern const uint8_t au_chord_len[AU_CHORDS];

typedef struct { uint8_t note, sound, vol, fx; } au_step_t;

typedef struct {
    char name[9];
    uint8_t wave, duty, vol, attack, decay, sustain, release;
    int8_t pitch;
    uint8_t pitch_time;
    uint8_t vib_depth, vib_rate;
    int8_t detune;
} au_sound_t;

typedef struct {
    char name[9];
    uint16_t ms;
    uint8_t len, loop_start, loop_end;
    au_step_t step[AU_SFX_STEPS];
} au_sfx_t;

typedef struct {
    uint8_t len;
    au_step_t step[AU_TRACKS][AU_PAT_STEPS];
} au_pattern_t;

typedef struct {
    char name[9];
    uint8_t bpm, swing, len, loop;
    uint8_t order[AU_SONG_LEN];
    uint8_t tracks;             /* tracks with notes in its patterns */
} au_song_t;

typedef struct {
    uint8_t nsounds, nsfx, npatterns, nsongs;
    au_sound_t sound[AU_SOUNDS];
    au_sfx_t sfx[AU_SFX];
    au_pattern_t pat[AU_PATTERNS];
    au_song_t song[AU_SONGS];
} au_bank_t;

/* Parses a bank (the AUDIO section). Returns 0, or -1 with a message. */
int au_parse(const uint8_t *data, size_t len, au_bank_t *b, char *err, size_t errlen);

/* who drives a voice */
enum { AU_OWN_NONE, AU_OWN_LUA, AU_OWN_SFX, AU_OWN_MUSIC };

typedef struct {
    uint8_t owner;
    int8_t sound;               /* bank sound of the note, -1 none (a Lua note) */
    uint8_t fx, fxn;            /* the effect in action */
    uint8_t arp_i;
    float note;                 /* base pitch, semitones (MIDI number) */
    float vol;                  /* 0..1: step volume x the effect's sfx / music level */
    uint32_t t;                 /* samples since the note started */
    uint32_t fx_t;              /* samples since the effect started */
    uint32_t step_len;          /* samples of the step it started on */
    float glide;                /* semitones away at fx_t = 0 (AU_FX_GLIDE) */
    uint32_t glide_len;
    uint32_t cut_at, retrig_every, retrig_t;
    /* a note waiting for AU_FX_DELAY */
    uint32_t delay_left;
    au_step_t pending;
    /* a Lua note: release timer, and slide / vibrato / arpeggio of the
     * Lua helpers */
    uint32_t gate_left;
    uint8_t mods;
    float mod_glide, mod_vib_depth, mod_vib_rate;
    uint32_t mod_glide_len, mod_t;
    int8_t mod_arp[8];
    uint8_t mod_arp_n;
    uint32_t mod_arp_len;
} au_voice_t;

typedef struct {
    int16_t n;                  /* the effect playing, -1 none */
    uint8_t step;
    int8_t transpose;
    float vol;
    float left;                 /* samples to the next step */
    uint32_t serial;            /* order of start (to steal the oldest) */
} au_sfxch_t;

typedef struct {
    au_bank_t *bank;            /* NULL: no bank */
    volatile uint8_t *regs;
    synth_t *synth;
    uint32_t rate;
    au_voice_t v[AU_TRACKS];
    au_sfxch_t sfx[AU_TRACKS];
    uint32_t serial;
    struct {
        int8_t song;            /* -1 none, -2 one pattern looping (the editor) */
        uint8_t pat;            /* the pattern of a -2 loop */
        uint8_t order, step;    /* the next step */
        uint8_t cur_order, cur_step, cur_pat;   /* the step sounding now */
        uint8_t bpm, swing;     /* of a -2 loop */
        uint8_t mute;           /* tracks not played */
        uint8_t paused;
        float left;             /* samples to the next step */
        float tempo;            /* x the song's tempo */
        float level, fade;      /* volume 0..1, change per sample */
    } m;
} player_t;

void player_init(player_t *p, uint32_t rate, volatile uint8_t *regs, synth_t *synth);

/* The bank to play (NULL: none). Music and effects keep their place if
 * they still exist in the new bank (the editor swaps banks while it plays). */
void player_set_bank(player_t *p, au_bank_t *b);

/* Advances everything by n samples: steps that fall due, effects, the
 * registers of the voices it drives. Call it before rendering n samples. */
void player_advance(player_t *p, unsigned n);

/* Stops everything and frees every voice (registers untouched). */
void player_stop_all(player_t *p);

/* Sound effect n on `voice` (-1: a free one), transposed, at volume
 * vol 0..1. Returns the voice, or -1. */
int  player_sfx(player_t *p, int n, int voice, int transpose, float vol);
void player_sfx_stop(player_t *p, int voice);              /* -1: all */
/* The effect and step playing on a voice; -1 if none. */
int  player_sfx_pos(const player_t *p, int voice, int *step);

/* Song n from position `order`, fading in over fade_ms (0: at once). */
void player_music(player_t *p, int song, int order, int fade_ms);
/* One pattern looping at a tempo (the editor). */
void player_music_pattern(player_t *p, int pat, int bpm, int swing, int step);
void player_music_stop(player_t *p, int fade_ms);
/* 1 and the place if music plays (song -2: a pattern loop), else 0. */
int  player_music_pos(const player_t *p, int *song, int *order, int *step, int *pat);
void player_music_pause(player_t *p, int on);
void player_tempo(player_t *p, float scale);
void player_mute(player_t *p, int track, int on);

/* A sound of the bank on a voice, as a step would play it (previews in
 * the editor); released after ms (0: held until player_release). */
void player_play(player_t *p, int voice, int sound, int note, int vol, int fx, uint32_t ms);
void player_release(player_t *p, int voice);

/* The Lua notes: note(), noteoff(), freq() and the helpers. */
void player_lua_note(player_t *p, int voice, float hz, uint32_t ms);
void player_lua_off(player_t *p, int voice);
void player_lua_freq(player_t *p, int voice, float hz);
void player_slide(player_t *p, int voice, float hz, uint32_t ms);
void player_vibrato(player_t *p, int voice, float semitones, float rate_hz);
void player_arp(player_t *p, int voice, const int8_t *semis, int n, uint32_t ms);
/* 1 while the voice sounds or a sequence holds it */
int  player_busy(const player_t *p, int voice);

/* MIDI note <-> Hz */
float au_note_hz(float note);
float au_hz_note(float hz);

#endif
