/*
 * The assistant's knowledge base and question answering (see assist.h).
 * BMAI file (scripts/assistlib.py, build_bmai): "BMAI", version, sections
 * TEXT STOP POOL EMBD RELU DENS ENTR STRS, then the CRC-32 of all of it.
 */
#include "assist.h"
#include "nn.h"
#include "text.h"
#include "lib/crc32.h"

#include <math.h>
#include <string.h>

#define AI_VERSION      1
#define MAX_ENTRIES     1024
#define MAX_HID         256
#define NFIELDS         9

static struct {
    int open;
    ai_text_t text;
    const int32_t *pool;
    int hid;
    const int8_t *w1;
    const int32_t *b1;
    int32_t mult, shift;
    const int8_t *w2;
    const int32_t *b2;
    float scale;
    int nent;
    const uint32_t *ent;        /* NFIELDS string offsets per entry */
    const char *strs;
    uint32_t nstrs;
    uint8_t kmask[MAX_ENTRIES];
} ai;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const char *ai_error(int err)
{
    switch (err) {
    case -1: return "not a BMAI file";
    case -2: return "unknown BMAI version";
    case -3: return "BMAI file damaged (CRC)";
    case -4: return "BMAI section missing or too short";
    case -5: return "BMAI network too large";
    default: return "BMAI error";
    }
}

static const uint8_t *section(const uint8_t *b, uint32_t len, const char *tag, uint32_t need, uint32_t *size)
{
    int n = b[6] | b[7] << 8;
    for (int i = 0; i < n; i++) {
        const uint8_t *s = b + 8 + 12 * i;
        if (memcmp(s, tag, 4))
            continue;
        uint32_t off = rd32(s + 4), sz = rd32(s + 8);
        if (off > len || sz > len - off || sz < need || (off & 3))
            return NULL;
        if (size) *size = sz;
        return b + off;
    }
    return NULL;
}

static int kind_bit(const char *k)
{
    static const char *const names[] = { "api", "howto", "error", "sprite", "tip", "action", "mesh" };
    for (int i = 0; i < 7; i++)
        if (!strcmp(k, names[i]))
            return 1 << i;
    return 0;
}

int ai_open(const void *blob, uint32_t len)
{
    const uint8_t *b = blob;
    ai.open = 0;
    if (len < 16 || memcmp(b, "BMAI", 4) || ((uintptr_t)b & 3))
        return -1;
    if ((b[4] | b[5] << 8) != AI_VERSION)
        return -2;
    if (crc32(b, len - 4) != rd32(b + len - 4))
        return -3;
    len -= 4;
    uint32_t sz;
    const uint8_t *p;
    if (!(p = section(b, len, "TEXT", 12, NULL))) return -4;
    ai.text.nbuckets = rd32(p);
    ai.text.max_feats = rd32(p + 4);
    ai.text.max_tokens = rd32(p + 8);
    if (ai.text.nbuckets > 4096 || ai.text.max_feats > AI_MAX_FEATS || ai.text.max_tokens > AI_MAX_TOKENS)
        return -5;
    if (!(p = section(b, len, "STOP", 4, &sz))) return -4;
    ai.text.nstop = (int)rd32(p);
    ai.text.stop = (const char *)p + 4;
    if (!(p = section(b, len, "POOL", 4 * (AI_MAX_FEATS + 1), NULL))) return -4;
    ai.pool = (const int32_t *)(const void *)p;
    if (!(p = section(b, len, "EMBD", 8, &sz))) return -4;
    if (rd32(p) != ai.text.nbuckets) return -4;
    ai.hid = (int)rd32(p + 4);
    if (ai.hid <= 0 || ai.hid > MAX_HID || (ai.hid & 3)) return -5;
    if (sz < 8 + ai.text.nbuckets * (uint32_t)ai.hid + 4u * (uint32_t)ai.hid) return -4;
    ai.w1 = (const int8_t *)p + 8;
    ai.b1 = (const int32_t *)(const void *)(p + 8 + ai.text.nbuckets * (uint32_t)ai.hid);
    if (!(p = section(b, len, "RELU", 8, NULL))) return -4;
    ai.mult = (int32_t)rd32(p);
    ai.shift = (int32_t)rd32(p + 4);
    if (ai.shift < 1 || ai.shift > 30) return -5;
    if (!(p = section(b, len, "DENS", 8, &sz))) return -4;
    if ((int)rd32(p) != ai.hid) return -4;
    ai.nent = (int)rd32(p + 4);
    if (ai.nent > MAX_ENTRIES) return -5;
    if (sz < 8 + (uint32_t)ai.nent * (uint32_t)ai.hid + 4u * (uint32_t)ai.nent + 4) return -4;
    ai.w2 = (const int8_t *)p + 8;
    ai.b2 = (const int32_t *)(const void *)(p + 8 + (uint32_t)ai.nent * (uint32_t)ai.hid);
    memcpy(&ai.scale, p + 8 + (uint32_t)ai.nent * (uint32_t)ai.hid + 4u * (uint32_t)ai.nent, 4);
    if (!(p = section(b, len, "STRS", 1, &sz))) return -4;
    ai.strs = (const char *)p;
    ai.nstrs = sz;
    if (ai.strs[sz - 1]) return -4;
    if (!(p = section(b, len, "ENTR", 4, &sz))) return -4;
    if ((int)rd32(p) != ai.nent || sz < 4 + 4u * NFIELDS * (uint32_t)ai.nent) return -4;
    ai.ent = (const uint32_t *)(const void *)(p + 4);
    for (int i = 0; i < ai.nent * NFIELDS; i++)
        if (ai.ent[i] >= ai.nstrs) return -4;
    for (int i = 0; i < ai.nent; i++)
        ai.kmask[i] = (uint8_t)kind_bit(ai.strs + ai.ent[i * NFIELDS + 1]);
    ai.open = 1;
    return 0;
}

int ai_is_open(void) { return ai.open; }
int ai_count(void) { return ai.open ? ai.nent : 0; }

int ai_get(int i, ai_entry_t *e)
{
    if (!ai.open || i < 0 || i >= ai.nent)
        return -1;
    const uint32_t *f = ai.ent + i * NFIELDS;
    e->id = ai.strs + f[0];
    e->kind = ai.strs + f[1];
    e->title = ai.strs + f[2];
    e->name = ai.strs + f[3];
    e->text = ai.strs + f[4];
    e->code = ai.strs + f[5];
    e->gen = ai.strs + f[6];
    e->see = ai.strs + f[7];
    e->keys = ai.strs + f[8];
    e->kmask = ai.kmask[i];
    return 0;
}

int ai_find(const char *id)
{
    for (int i = 0; i < ai_count(); i++)
        if (!strcmp(ai.strs + ai.ent[i * NFIELDS], id))
            return i;
    return -1;
}

int ai_kind_mask(const char *kinds)
{
    unsigned m = 0;
    char k[16];
    while (kinds && *kinds) {
        int n = 0;
        while (*kinds == ',' || *kinds == ' ') kinds++;
        while (*kinds && *kinds != ',' && *kinds != ' ' && n < 15) k[n++] = *kinds++;
        while (*kinds && *kinds != ',' && *kinds != ' ') kinds++;
        k[n] = 0;
        if (n) m |= (unsigned)kind_bit(k);
    }
    return (int)m;
}

/* ---------------------------------------------------------------- network */

static uint16_t feats[AI_MAX_FEATS];
static int32_t acc[MAX_HID];
static int8_t hid8[MAX_HID] __attribute__((aligned(4)));
static int32_t out[MAX_ENTRIES];

int ai_feats(const char *q, uint16_t *f)
{
    return ai.open ? ai_features(&ai.text, q, f) : 0;
}

static int run(const char *q)
{
    int n = ai_features(&ai.text, q, feats);
    nn_embed_bag(ai.w1, ai.hid, feats, n, ai.pool[n], ai.b1, acc);
    nn_relu_q(acc, ai.hid, ai.mult, ai.shift, hid8);
    nn_dense(ai.w2, ai.b2, hid8, ai.hid, ai.nent, out);
    return n;
}

int ai_logits(const char *q, int32_t *dst, int max)
{
    if (!ai.open)
        return 0;
    run(q);
    int n = ai.nent < max ? ai.nent : max;
    memcpy(dst, out, (size_t)n * sizeof *dst);
    return n;
}

/* ---------------------------------------------------------------- answers */

/* is `w` one of the comma-separated words of `list`? */
static int in_list(const char *list, const char *w)
{
    size_t n = strlen(w);
    while (*list) {
        const char *e = strchr(list, ',');
        size_t len = e ? (size_t)(e - list) : strlen(list);
        if (len == n && !memcmp(list, w, n))
            return 1;
        if (!e)
            break;
        list = e + 1;
    }
    return 0;
}

/* API names that are also everyday words of the questions ("tempo": time):
 * they count only under the cursor or written as a call, "tempo(" */
static const char *const plain_names = "tempo";

int ai_ask(const char *q, const char *ctx, unsigned kinds, ai_hit_t *hits, int max)
{
    static float score[MAX_ENTRIES];
    static ai_words_t all, words;
    if (!ai.open || max <= 0)
        return 0;
    if (!kinds)
        kinds = AI_KIND_ALL;
    int nf = run(q);
    /* probabilities over the kinds asked for and the off-topic examples:
     * an unknown question spreads them thin */
    float top = -1e30f;
    for (int i = 0; i < ai.nent; i++)
        if ((ai.kmask[i] & kinds) || !ai.kmask[i]) {
            float v = (float)out[i] * ai.scale;
            if (v > top) top = v;
        }
    float sum = 0;
    for (int i = 0; i < ai.nent; i++) {
        score[i] = 0;
        if ((ai.kmask[i] & kinds) || !ai.kmask[i]) {
            score[i] = nf ? expf((float)out[i] * ai.scale - top) : 0;
            sum += score[i];
        }
    }
    if (sum > 0)
        for (int i = 0; i < ai.nent; i++)
            score[i] /= sum;
    /* words: an API name written in the question or under the cursor, the
     * entry's key words */
    ai_words(q, AI_MAX_TOKENS, &all);
    ai_content_words(&ai.text, q, &words);
    for (int i = 0; i < ai.nent; i++) {
        if (!(ai.kmask[i] & kinds))
            continue;
        const char *name = ai.strs + ai.ent[i * NFIELDS + 3];
        const char *keys = ai.strs + ai.ent[i * NFIELDS + 8];
        if (*name) {
            if (ctx && !strcmp(ctx, name))
                score[i] += 2.0f;
            if (in_list(plain_names, name)) {
                const char *at = strstr(q, name);
                if (at && at[strlen(name)] == '(')
                    score[i] += 0.6f;
            } else {
                for (int k = 0; k < all.n; k++)
                    if (!strcmp(all.w[k], name)) {
                        score[i] += 0.6f;
                        break;
                    }
            }
        }
        if (*keys) {
            float kb = 0;
            for (int k = 0; k < words.n; k++)
                if (in_list(keys, words.w[k]))
                    kb += 0.15f;
            score[i] += kb > 0.45f ? 0.45f : kb;
        }
    }
    /* the best `max`, best first */
    int n = 0;
    for (int i = 0; i < ai.nent; i++) {
        if (!(ai.kmask[i] & kinds) || score[i] < 0.002f)
            continue;
        int j = n < max ? n++ : max;
        if (j == max && score[i] <= hits[max - 1].score)
            continue;
        if (j == max)
            j = max - 1;
        while (j > 0 && hits[j - 1].score < score[i]) {
            hits[j] = hits[j - 1];
            j--;
        }
        hits[j].entry = i;
        hits[j].score = score[i];
    }
    return n;
}

/* ---------------------------------------------------------------- typos */

static int edit_distance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 31 || lb > 31)
        return 99;
    int row[32];
    for (int j = 0; j <= lb; j++)
        row[j] = j;
    for (int i = 1; i <= la; i++) {
        int diag = row[0];
        row[0] = i;
        for (int j = 1; j <= lb; j++) {
            int up = row[j];
            int v = diag + (a[i - 1] != b[j - 1]);
            if (row[j] + 1 < v) v = row[j] + 1;
            if (row[j - 1] + 1 < v) v = row[j - 1] + 1;
            row[j] = v;
            diag = up;
        }
    }
    return row[lb];
}

const char *ai_near(const char *word, int *dist)
{
    const char *best = NULL;
    int bd = 99;
    for (int i = 0; i < ai_count(); i++) {
        const char *name = ai.strs + ai.ent[i * NFIELDS + 3];
        if (!*name || !(ai.kmask[i] & AI_KIND_API))
            continue;
        int d = edit_distance(word, name);
        if (d < bd) {
            bd = d;
            best = name;
        }
    }
    /* close enough to be a typo: 1 letter in short names, 2 in longer ones */
    int len = (int)strlen(word);
    if (!best || bd > (len <= 4 ? 1 : 2)) {
        if (dist) *dist = bd;
        return NULL;
    }
    if (dist) *dist = bd;
    return best;
}
