/*
 * The music assistant (bm Sound): recipes that write music for the sound
 * bank of a cartridge. Rhythms (beat.*), backing tracks (base.*: drums,
 * bass and chords), bass lines (bass.*), arpeggios (arp.*), melodies
 * (melody.*) and classic sound effects (sfx.*). The words of a request
 * pick the key, the mode, the tempo, the length and the instrument ("in la
 * minore", "120 bpm", "8 battute", "con il piano"); the seed gives the
 * variants. The melodies come from a small INT8 network (music_net.c,
 * trained by scripts/trainmusic.py on src/ai/melodies.txt) that chooses
 * the next note from the last ones, the chord and the place in the bar,
 * inside a form that repeats and answers like a song. The rest are the
 * rules of the styles. Portable C: the PC's tests build it as it is.
 */
#ifndef AI_MUSIC_H
#define AI_MUSIC_H

#include <stdint.h>

#define MUS_STEPS       64              /* a pattern: four bars of 16ths */
#define MUS_TRACKS      8
#define MUS_PATS        4
#define MUS_INST        12
#define MUS_SFX_STEPS   32
#define MUS_NFEAT       116             /* the network's inputs and outputs */
#define MUS_NOUT        24

/* a step as the bank keeps it (player.h): note 0 nothing, 128 off; inst is
 * an index into mus_out_t.inst; fx type << 4 | amount */
typedef struct { uint8_t note, inst, vol, fx; } mus_step_t;

typedef struct {
    char gen[24], name[48], kind[8];    /* kind: beat base bass arp melody sfx */
    int bpm, swing, key, minor, meter, bars;
    int echo, room;                     /* the song's echo (steps) and room (0..255) */
    int ninst;
    char inst[MUS_INST][9];             /* the instruments (presets.c) the steps use */
    int npat;
    struct {
        int len;
        uint8_t used;                   /* bit t: track t has notes */
        mus_step_t step[MUS_TRACKS][MUS_STEPS];
    } pat[MUS_PATS];
    char chords[16][6];                 /* the chord of each bar ("Am", "F"...) */
    int nchords;
    /* a sound effect */
    int sfx_ms, sfx_len, sfx_loop0, sfx_loop1;
    mus_step_t sfx[MUS_SFX_STEPS];
} mus_out_t;

typedef struct {
    const char *gen;                    /* the recipe */
    uint32_t seed;
    int key, minor;                     /* -1: the recipe's (or the context's) */
    int bpm, bars;                      /* 0: the recipe's */
    char inst[9];                       /* the main part's instrument, "" for the recipe's */
    const uint8_t *ctx;                 /* notes already there (MIDI), for the key ... */
    const uint8_t *ctx_bar;             /* ... and the bar of each (NULL: the key only), for the chords */
    int nctx;
} mus_req_t;

void mus_req_init(mus_req_t *r, const char *gen);

/* key ("in la minore", "C major"), mode words, "120 bpm", "veloce", "8
 * battute", instruments ("piano", "chitarra", "8 bit"...) */
void mus_parse(const char *text, mus_req_t *r);

/* 0, or -1 for an unknown recipe */
int mus_make(const mus_req_t *r, mus_out_t *out);

/* the recipe the words point to without the assistant's network (the
 * RGB30, or when it is not sure): "ritmo rock" beat.rock, "effetto
 * moneta" sfx.coin, "melodia triste" melody.sad; never NULL */
const char *mus_guess(const char *text);

int mus_recipes(void);
const char *mus_recipe_id(int i);
const char *mus_recipe_name(int i);
int mus_find(const char *gen);

/* the network: the features of a moment of a melody (as trainmusic.py)
 * and the last layer's sums for them (tests) */
typedef struct { int deg, dur; } mus_ev_t;      /* deg: steps on the scale, MUS_REST for a rest */
#define MUS_REST (-100)
void mus_features(const mus_ev_t *hist, int n, int pos, int meter, int root, int quality, int bar,
                  int last_bar, int minor, uint8_t *f);
int mus_net_run(const uint8_t *f, int32_t *out);
float mus_net_scale(void);
extern const uint8_t mus_net[];
extern const unsigned mus_net_len;

#endif
