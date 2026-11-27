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
#define SDK_MUS_TRACKS 4
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
    int line;                           /* of the source, from 1 (0: not read from it) */
} sdk_mus_ins_t;

/* A step of a track: tracks 0..2 a note and an instrument (its number + 1; 0: as before); track 3, the noise,
 * only ins: 0 nothing, or the instrument + 1, or note SDK_MUS_OFF. */
typedef struct { uint8_t note, ins; } sdk_mus_cell_t;

typedef struct {
    uint8_t used, steps;
    sdk_mus_cell_t cell[SDK_MUS_STEPS][SDK_MUS_TRACKS];
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
    int instruments, sfxs, songs;
    sdk_mus_ins_t ins[SDK_MUS_INS];
    sdk_mus_pattern_t pat[SDK_MUS_PATTERNS];
    sdk_mus_sfx_t sfx[SDK_MUS_SFX];
    sdk_mus_song_t song[SDK_MUS_SONGS];
} sdk_music_t;

void sdk_music_clear(sdk_music_t *m);
int sdk_music_empty(const sdk_music_t *m);

/* The block of a source: [*from, *to) with its marker lines. 1, 0 when there is none, -1 when it has no end. */
int sdk_music_find(const char *src, size_t len, int c, size_t *from, size_t *to);
/* Reads the block (no block: an empty music). Returns 0, or -1 with "line N: ..." in err. */
int sdk_music_parse(const char *src, size_t len, int c, sdk_music_t *m, char *err, size_t en);
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

/* The driver (shell_sdk_snd.c): the routines in assembly, without the tables. */
const char *sdk_snd_driver(void);

#endif /* SHELL_SDK_MUSIC_H */
