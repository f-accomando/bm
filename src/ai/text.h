/*
 * The words of a question and its hashed features (M30), as
 * scripts/assistlib.py computes them: lower-case ASCII words (accented
 * letters of code page 437 or UTF-8 folded to plain ones), function words
 * dropped, then words, pairs of words and letter triples, each hashed
 * (FNV-1a) into one of `nbuckets` features.
 */
#ifndef AI_TEXT_H
#define AI_TEXT_H

#include <stdint.h>

#define AI_MAX_FEATS    255
#define AI_MAX_TOKENS   64
#define AI_TOKEN_LEN    24

typedef struct {
    char w[AI_MAX_TOKENS][AI_TOKEN_LEN + 1];
    int n;
} ai_words_t;

typedef struct {
    uint32_t nbuckets;          /* a power of two */
    uint32_t max_feats;         /* <= AI_MAX_FEATS */
    uint32_t max_tokens;        /* <= AI_MAX_TOKENS */
    const char *stop;           /* function words, each NUL-terminated */
    int nstop;
} ai_text_t;

/* every word of the text, in order */
void ai_words(const char *text, int max_tokens, ai_words_t *out);

/* the words that count: no function words, no single letters */
void ai_content_words(const ai_text_t *t, const char *text, ai_words_t *out);

/* the sorted distinct features of the text; returns how many */
int ai_features(const ai_text_t *t, const char *text, uint16_t *feats);

uint32_t ai_fnv1a(const void *data, int len, uint32_t h);

#endif
