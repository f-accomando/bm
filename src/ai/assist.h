/*
 * The development assistant (M30): a knowledge base of bm (API, how-to with
 * code, Lua errors, sprite recipes) and a tiny INT8 network that maps a
 * question, in Italian or English, to its entries; the 3D recipes
 * (mesh.c) are entries too. Everything comes from
 * one BMAI file built by scripts/mkassist.py and embedded in the kernel; it
 * does nothing until asked, then answers in well under a millisecond.
 */
#ifndef AI_ASSIST_H
#define AI_ASSIST_H

#include <stdint.h>

#define AI_KIND_API     1
#define AI_KIND_HOWTO   2
#define AI_KIND_ERROR   4
#define AI_KIND_SPRITE  8
#define AI_KIND_TIP     16
#define AI_KIND_ACTION  32      /* something to do on the code (#entry: lines) */
#define AI_KIND_MESH    64      /* a 3D recipe (bm Studio, bm Animator) */
#define AI_KIND_GUIDE   128     /* how to make a game with the SDK, step by step */
#define AI_KIND_MUSIC   256     /* a music recipe (bm Sound: music.c) */
#define AI_KIND_ALL     511

typedef struct {
    const char *id, *kind, *title, *name, *text, *code, *gen, *see, *keys;
    const char *title_en, *text_en;     /* the English (R18), "" if none */
    unsigned kmask;             /* AI_KIND_*, 0 for "none" (off-topic examples) */
} ai_entry_t;

/* The language of the answers (R18): Italian or English; following, the
 * panel's questions change it (ai_lang_of). ai_title and ai_text give the
 * entry's in that language (the Italian when there is no English). */
#define AI_LANG_IT      0
#define AI_LANG_EN      1
void ai_set_lang(int lang, int follow);
int  ai_lang(void);
int  ai_lang_follows(void);
int  ai_lang_of(const char *q);         /* AI_LANG_*, or -1 if it cannot say */
const char *ai_title(const ai_entry_t *e);
const char *ai_text(const ai_entry_t *e);

typedef struct {
    int entry;
    float score;                /* the network's probability plus word matches */
} ai_hit_t;

/* parses a BMAI file (kept in place: it must stay); 0 or a negative error */
int ai_open(const void *blob, uint32_t len);
int ai_is_open(void);
const char *ai_error(int err);

int ai_count(void);
int ai_get(int i, ai_entry_t *e);       /* 0, or -1 out of range */
int ai_find(const char *id);            /* the entry's index, or -1 */
int ai_kind_mask(const char *kinds);    /* "api,howto" -> AI_KIND_API | AI_KIND_HOWTO */

/* the best entries of the kinds asked for, best first; ctx is an optional
 * word (the one under the cursor). Returns how many. */
int ai_ask(const char *q, const char *ctx, unsigned kinds, ai_hit_t *hits, int max);

/* the network's raw output for a question, one per entry (tests) */
int ai_logits(const char *q, int32_t *out, int max);
int ai_feats(const char *q, uint16_t *out);

/* the API name closest to `word` (a typo in an error message), or NULL;
 * *dist is the number of letters to change */
const char *ai_near(const char *word, int *dist);

#endif
