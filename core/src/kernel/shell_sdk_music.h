/*
 * Shell SDK: the music block of a NES project, the data of the Sounds tab. The block is in the source, between
 * ;@music and ;@end (assembly) or //@music and //@end (bm C), and it is the truth: the tab reads it and writes it,
 * the builder reads it and puts the sound driver and its data in its place. Pure model: the tests run on the PC.
 *
 * The block is a list of named lists of values: .byte lines under a label (assembly) or const byte name[] = { }
 * (bm C). The name says what a list is: pattern00 to pattern31, sfx_NAME, song_NAME; any other name is an
 * instrument.
 *
 *   instrument   channel (PULSE, TRIANGLE, NOISE), wave (DUTY12..DUTY75; noise: 0..15, +SHORT), vibrato 0..15,
 *                arpeggio ($47: +4 and +7 semitones), volumes 0..15 one a frame, LOOP, RELEASE, END
 *   patternNN    a row a step: note, instrument for pulse 1, pulse 2 and triangle, then the noise instrument; END
 *   sfx_NAME     frames a step, channel (PU1, PU2, TRI, NOI), wave, then note, volume, effect a step; END
 *   song_NAME    frames a step, the place to loop to (END: no loop), pattern numbers, END
 *
 * Notes: C1..B7 (CS4 is C sharp), on the noise of an effect N0..N15; __ nothing, OFF the release.
 *
 * A SNES project has 8 voices, waves of its own and instruments of its own:
 *   wave_NAME    32 points 0..15 (8 the middle) of one period, END
 *   instrument   wave (SQUARE12, SQUARE25, SQUARE50, TRIANGLE, SAW, SINE, NOISE or NAME), noise rate 0..31, attack 0..15,
 *                decay 0..7, sustain 0..7, release 0..31, volume 0..127, pan 0..16 (8 the middle), vibrato 0..15,
 *                arpeggio, END
 *   patternNN    a row a step: note, instrument for the voices 1 to 8; END
 *   sfx_NAME     frames a step, voice (V1..V8), wave, then note, volume, effect a step; END
 *   song_NAME    as on the NES
 */
#ifndef SHELL_SDK_MUSIC_H
#define SHELL_SDK_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#define SDK_MUS_INS 16
#define SDK_MUS_PATTERNS 32
#define SDK_MUS_STEPS 64
#define SDK_MUS_SFX 16
#define SDK_MUS_SFX_STEPS 32
#define SDK_MUS_SONGS 8
#define SDK_MUS_ORDER 64
#define SDK_MUS_ENV 64
#define SDK_MUS_TRACKS 4                /* NES */
#define SDK_MUS_VOICES 8                /* SNES */
#define SDK_MUS_WAVES 6                 /* the waves of the driver; a wave of this number is the noise */
#define SDK_MUS_DRAWN 8                 /* waves drawn in a SNES project: wave SDK_MUS_WAVES + 1 + k */
#define SDK_MUS_POINTS 32               /* of a wave: one period, 0..15 */
#define SDK_MUS_NAME 12                 /* with the NUL */

enum { SDK_MUS_PULSE, SDK_MUS_TRIANGLE, SDK_MUS_NOISE };        /* the channel of an instrument */
enum { SDK_MUS_NONE = 0, SDK_MUS_OFF = 1, SDK_MUS_LOW = 24, SDK_MUS_HIGH = 107 };     /* notes: C1..B7 */
enum { SDK_FX_UP = 0x10, SDK_FX_DOWN = 0x20, SDK_FX_VIB = 0x30 };
#define SDK_MUS_NO 255                  /* no loop, no release */
#define SDK_MUS_SHORT 0x80              /* the short noise */

typedef struct {
    char name[SDK_MUS_NAME];
    uint8_t channel, wave, vibrato, arp;
    uint8_t len, loop, release;         /* of the volumes; loop and release: a place, or SDK_MUS_NO */
    uint8_t volume[SDK_MUS_ENV];
    uint8_t noise, attack, decay, sustain, rate, level, pan;     /* SNES (wave: see SDK_MUS_DRAWN) */
    int line;                           /* of the source, from 1 (0: not read from it) */
} sdk_mus_ins_t;

/* A step of a track: tracks 0..2 a note and an instrument (its number + 1; 0: as before); track 3, the noise,
 * only ins: 0 nothing, or the instrument + 1, or note SDK_MUS_OFF. */
typedef struct { uint8_t note, ins; } sdk_mus_cell_t;

typedef struct {
    uint8_t used, steps;
    sdk_mus_cell_t cell[SDK_MUS_STEPS][SDK_MUS_VOICES];
    int line;
    uint16_t row_line[SDK_MUS_STEPS];
} sdk_mus_pattern_t;

typedef struct { uint8_t note, volume, fx; } sdk_mus_step_t;

typedef struct {
    char name[SDK_MUS_NAME];            /* without sfx_ */
    uint8_t speed, channel, wave, steps;
    sdk_mus_step_t step[SDK_MUS_SFX_STEPS];
    int line;
} sdk_mus_sfx_t;

typedef struct {
    char name[SDK_MUS_NAME];            /* without song_ */
    uint8_t speed, loop, len;
    uint8_t order[SDK_MUS_ORDER];
    int line;
} sdk_mus_song_t;

typedef struct {
    char name[SDK_MUS_NAME];            /* without wave_ */
    uint8_t point[SDK_MUS_POINTS];
    int line;
} sdk_mus_wave_t;

typedef struct {
    int snes;                           /* the music of a SNES project */
    int instruments, sfxs, songs, waves;
    sdk_mus_ins_t ins[SDK_MUS_INS];
    sdk_mus_pattern_t pat[SDK_MUS_PATTERNS];
    sdk_mus_sfx_t sfx[SDK_MUS_SFX];
    sdk_mus_song_t song[SDK_MUS_SONGS];
    sdk_mus_wave_t drawn[SDK_MUS_DRAWN];
} sdk_music_t;

void sdk_music_clear(sdk_music_t *m);
int sdk_music_empty(const sdk_music_t *m);

/* The block of a source: [*from, *to) with its marker lines. 1, 0 when there is none, -1 when it has no end. */
int sdk_music_find(const char *src, size_t len, int c, size_t *from, size_t *to);
/* Reads the block (no block: an empty music). Returns 0, or -1 with "line N: ..." in err. */
int sdk_music_parse(const char *src, size_t len, int c, sdk_music_t *m, char *err, size_t en);
/* The same for a SNES project. */
int sdk_music_parse_snes(const char *src, size_t len, int c, sdk_music_t *m, char *err, size_t en);
/* The block as text, with its marker lines. Returns its length, or -1 when it does not fit in max. */
long sdk_music_text(const sdk_music_t *m, int c, char *out, size_t max);
/* Writes the block into the text of the Code tab (sdk_edit_replace; run as there). Returns 0, or -1 with the
 * reason in err. */
int sdk_music_commit(const sdk_music_t *m, int c, int run, char *err, size_t en);

/* 1 when the source calls the driver: snd_init, snd_frame, play_song, play_sfx or stop_music. */
int sdk_music_called(const char *src, size_t len, int c);
/* The assembly of the driver and of the data, from malloc: the numbers song_NAME and sfx_NAME, the tables, and
 * the routines snd_init, snd_frame, play_song, play_sfx and stop_music. NULL: no memory. */
char *sdk_music_asm(const sdk_music_t *m);

/* The source of an assembly project with the sound driver and its data in the place of the music block. */
typedef struct {
    char *text;             /* from malloc; NULL: the program has no sound, the source is as it is */
    size_t len;
    int first, lines;       /* the driver: its first line and how many */
    int block;              /* the lines of the music block that it took the place of */
} sdk_music_src_t;

/* Returns 0, or -1 with "main.s:12: music: ..." in err. The driver goes in when the block has music or the
 * program calls it. */
int sdk_music_expand(const char *src, size_t len, sdk_music_src_t *s, char *err, size_t en);
/* The same for a SNES source: the orders and the image of the sound memory in the place of the block. */
int sdk_music_expand_snes(const char *src, size_t len, sdk_music_src_t *s, char *err, size_t en);
/* The assembler's message for the expanded source ("main.s:L: ..."): L becomes the line of the source. */
void sdk_music_error_line(const sdk_music_src_t *s, char *err, size_t en);

/* The names of the notes. note: SDK_MUS_NONE, OFF or LOW..HIGH; out takes 4 bytes ("C4", "CS4", "__", "OFF"). */
void sdk_music_note_name(int note, char *out);
/* A name that an instrument, an effect or a song can have: letters, digits and _, a letter first, not a word of
 * the format. */
int sdk_music_name_ok(const char *name);
/* The instrument has this channel. -1 when none: the next one after `from` (from -1: the first), going `step`. */
int sdk_music_next_ins(const sdk_music_t *m, int from, int step, int channel);
/* Removes an instrument; the cells that use it lose it. */
void sdk_music_remove_ins(sdk_music_t *m, int i);
/* A new instrument of a channel with the usual sound, a free name from the base. -1: no room. */
int sdk_music_add_ins(sdk_music_t *m, int channel);

/* A name that a drawn wave can have: as the others, and not a wave of the driver. */
int sdk_music_wave_name_ok(const char *name);
/* The point i (0..31) of a wave (not the noise): 0..15. */
int sdk_music_wave_point(const sdk_music_t *m, int wave, int i);
/* A new drawn wave: a copy of the drawn wave `from` (-1: a sine), with a free name. Its number k, or -1: no room. */
int sdk_music_add_wave(sdk_music_t *m, int from);
/* Removes the drawn wave k; what used it takes the square of 50%. */
void sdk_music_remove_wave(sdk_music_t *m, int k);
/* How many instruments and effects use the wave. */
int sdk_music_wave_users(const sdk_music_t *m, int wave);

/* The driver (shell_sdk_snd.c): the routines in assembly, without the tables. */
const char *sdk_snd_driver(void);

/* SNES (shell_sdk_spc.c): the image of the sound memory, from $0200: the driver, the tables, the samples. Its
 * length, or -1 when it is larger than max. */
#define SDK_SPC_START 0x0200
int sdk_spc_image(const sdk_music_t *m, uint8_t *out, int max);
/* The assembly of a SNES program with sound, from malloc: the numbers song_NAME and sfx_NAME, snd_init, play_song,
 * play_sfx, stop_music and the image. last: a byte of the direct page for the driver. NULL: no memory, or the
 * music is too large. */
char *sdk_music_snes_asm(const sdk_music_t *m, int last);

#endif /* SHELL_SDK_MUSIC_H */
