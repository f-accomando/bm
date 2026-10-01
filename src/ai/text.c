/*
 * Words and hashed features of a question (see text.h). Any change here
 * must be made in scripts/assistlib.py too (tests/ai/test_ai.c compares).
 */
#include "text.h"

#include <string.h>

#define FNV_BASIS 2166136261u

/* code page 437 0x80..0xA5 -> plain letter (0: not a letter) */
static const char cp437_fold[0x26] = {
    'c', 'u', 'e', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'i', 'i', 'i', 'a', 'a',     /* 80 */
    'e', 0, 0, 'o', 'o', 'o', 'u', 'u', 'y', 'o', 'u', 0, 0, 0, 0, 0,                   /* 90 */
    'a', 'i', 'o', 'u', 'n', 'n',                                                       /* A0 */
};

/* UTF-8 0xC3 then 0x80..0xBF (Latin-1 letters) -> plain letter */
static char utf8_c3_fold(uint8_t b)
{
    if (b >= 0x80 && b <= 0x85) return 'a';
    if (b == 0x87) return 'c';
    if (b >= 0x88 && b <= 0x8B) return 'e';
    if (b >= 0x8C && b <= 0x8F) return 'i';
    if (b == 0x91) return 'n';
    if (b >= 0x92 && b <= 0x96) return 'o';
    if (b >= 0x99 && b <= 0x9C) return 'u';
    if (b >= 0xA0 && b <= 0xA5) return 'a';
    if (b == 0xA7) return 'c';
    if (b >= 0xA8 && b <= 0xAB) return 'e';
    if (b >= 0xAC && b <= 0xAF) return 'i';
    if (b == 0xB1) return 'n';
    if (b >= 0xB2 && b <= 0xB6) return 'o';
    if (b >= 0xB9 && b <= 0xBC) return 'u';
    return 0;
}

uint32_t ai_fnv1a(const void *data, int len, uint32_t h)
{
    const uint8_t *p = data;
    for (int i = 0; i < len; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

void ai_words(const char *text, int max_tokens, ai_words_t *out)
{
    const uint8_t *s = (const uint8_t *)text;
    char cur[AI_TOKEN_LEN + 1];
    int len = 0, have = 0;
    out->n = 0;
    if (max_tokens > AI_MAX_TOKENS) max_tokens = AI_MAX_TOKENS;
    for (;; s++) {
        uint8_t b = *s;
        char c = 0;
        if (b == 0xC3 && utf8_c3_fold(s[1])) {
            c = utf8_c3_fold(s[1]);
            s++;
        } else if (b >= 0x80 && b < 0x80 + sizeof cp437_fold) {
            c = cp437_fold[b - 0x80];
        } else if (b >= 'A' && b <= 'Z') {
            c = (char)(b + 32);
        } else if ((b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '_') {
            c = (char)b;
        }
        if (c) {
            if (len < AI_TOKEN_LEN) cur[len++] = c;
            have = 1;
            continue;
        }
        if (have) {
            cur[len] = 0;
            memcpy(out->w[out->n++], cur, (size_t)len + 1);
            len = have = 0;
            if (out->n == max_tokens)
                return;
        }
        if (!b)
            return;
    }
}

static int is_stop(const ai_text_t *t, const char *w)
{
    const char *p = t->stop;
    for (int i = 0; i < t->nstop; i++) {
        if (!strcmp(p, w))
            return 1;
        p += strlen(p) + 1;
    }
    return 0;
}

void ai_content_words(const ai_text_t *t, const char *text, ai_words_t *out)
{
    ai_words(text, (int)t->max_tokens, out);
    int k = 0;
    for (int i = 0; i < out->n; i++)
        if (out->w[i][1] && !is_stop(t, out->w[i])) {
            if (k != i)
                memcpy(out->w[k], out->w[i], sizeof out->w[i]);
            k++;
        }
    out->n = k;
}

typedef struct {
    const ai_text_t *t;
    uint16_t *f;
    int n;
    uint8_t seen[4096 / 8];
} bag_t;

static void add(bag_t *b, char kind, const char *s1, const char *s2, int len2)
{
    if (b->n >= (int)b->t->max_feats)
        return;
    uint32_t h = ai_fnv1a(&kind, 1, FNV_BASIS);
    h = ai_fnv1a(s1, (int)strlen(s1), h);
    if (s2) {
        h = ai_fnv1a(" ", 1, h);
        h = ai_fnv1a(s2, len2, h);
    }
    uint32_t f = h & (b->t->nbuckets - 1);
    if (b->seen[f >> 3] & (1u << (f & 7)))
        return;
    b->seen[f >> 3] |= (uint8_t)(1u << (f & 7));
    b->f[b->n++] = (uint16_t)f;
}

int ai_features(const ai_text_t *t, const char *text, uint16_t *feats)
{
    static ai_words_t ws;
    static bag_t b;
    if (t->nbuckets > 4096 || (t->nbuckets & (t->nbuckets - 1)))
        return 0;
    ai_content_words(t, text, &ws);
    memset(&b, 0, sizeof b);
    b.t = t;
    b.f = feats;
    for (int i = 0; i < ws.n; i++)
        add(&b, 'w', ws.w[i], NULL, 0);
    for (int i = 0; i + 1 < ws.n; i++)
        add(&b, 'b', ws.w[i], ws.w[i + 1], (int)strlen(ws.w[i + 1]));
    for (int i = 0; i < ws.n; i++) {
        int len = (int)strlen(ws.w[i]);
        if (len < 3)
            continue;
        char tw[AI_TOKEN_LEN + 3];
        tw[0] = '<';
        memcpy(tw + 1, ws.w[i], (size_t)len);
        tw[len + 1] = '>';
        tw[len + 2] = 0;
        for (int k = 0; k + 3 <= len + 2; k++) {
            char tri[4] = { tw[k], tw[k + 1], tw[k + 2], 0 };
            add(&b, 't', tri, NULL, 0);
        }
    }
    /* insertion sort: at most 255 */
    for (int i = 1; i < b.n; i++) {
        uint16_t v = feats[i];
        int j = i - 1;
        while (j >= 0 && feats[j] > v) {
            feats[j + 1] = feats[j];
            j--;
        }
        feats[j + 1] = v;
    }
    return b.n;
}
