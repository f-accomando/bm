/*
 * The development assistant (M30): a knowledge base of bm (API, how-to with
 * code, Lua errors, sprite recipes) and a tiny INT8 network that maps a
 * question, in Italian or English, to its entries. Everything comes from
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
#define AI_KIND_ALL     63

typedef struct {
    const char *id, *kind, *title, *name, *text, *code, *gen, *see, *keys;
    unsigned kmask;             /* AI_KIND_*, 0 for "none" (off-topic examples) */
} ai_entry_t;

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
