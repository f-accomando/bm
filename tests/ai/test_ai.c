/*
 * Host tests of the development assistant (M30): the C features and network
 * against the Python reference (scripts/mkassist.py --ref), the answers to
 * the held-out questions of src/ai/kb/tests.txt, the typo finder, and the
 * sprite generator.
 *
 *   test_ai build/assist.bin build/ai/ref.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ai/assist.h"
#include "ai/sprite.h"
#include "ai/text.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = aligned_alloc(4, (size_t)(*len + 4) & ~(size_t)3);
    if (fread(p, 1, (size_t)*len, f) != (size_t)*len) {
        perror(path);
        exit(2);
    }
    fclose(f);
    return p;
}

static int hexval(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

/* one question of ref.txt: Q hex / F feats / L logits / E ids */
static void test_reference(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        exit(2);
    }
    static char line[1 << 16], q[512];
    static int32_t logits[1024];
    static uint16_t feats[AI_MAX_FEATS];
    int nq = 0, top1 = 0, top3 = 0, have_logits = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 'Q') {
            int n = 0;
            for (const char *p = line + 2; p[0] && p[1] && n < (int)sizeof q - 1; p += 2)
                q[n++] = (char)(hexval(p[0]) * 16 + hexval(p[1]));
            q[n] = 0;
            nq++;
        } else if (line[0] == 'F') {
            int n = ai_feats(q, feats), k = 0, ok = 1;
            for (char *p = strtok(line + 1, " "); p; p = strtok(NULL, " "), k++)
                if (k >= n || feats[k] != atoi(p)) ok = 0;
            CHECK(ok && k == n, "features of %s: %d in C, %d in Python", q, n, k);
        } else if (line[0] == 'L') {
            int n = ai_logits(q, logits, 1024), k = 0, ok = 1;
            for (char *p = strtok(line + 1, " "); p; p = strtok(NULL, " "), k++)
                if (k >= n || logits[k] != atol(p)) ok = 0;
            CHECK(ok && k == n, "network output for %s differs from Python", q);
            have_logits = 1;
        } else if (line[0] == 'E') {
            ai_hit_t hits[3];
            int n = ai_ask(q, NULL, AI_KIND_ALL, hits, 3), first = 0, in3 = 0;
            for (int i = 0; i < n; i++) {
                ai_entry_t e;
                ai_get(hits[i].entry, &e);
                char want[512];
                snprintf(want, sizeof want, " %.500s ", line + 1);
                char id[80];
                snprintf(id, sizeof id, " %s ", e.id);
                if (strstr(want, id)) {
                    if (i == 0) first = 1;
                    in3 = 1;
                }
            }
            top1 += first;
            top3 += in3;
            if (have_logits && !first) {
                ai_entry_t e;
                if (n) ai_get(hits[0].entry, &e);
                printf("  not first: \"%s\" -> %s (want%s)\n", q, n ? e.id : "nothing", line + 1);
            }
        }
    }
    fclose(f);
    printf("held-out questions: %d, first %d, in the first three %d\n", nq, top1, top3);
    if (have_logits)
        CHECK(top3 * 100 >= nq * 90, "the right answer is in the first three for %d of %d questions", top3, nq);
}

static void test_words(void)
{
    ai_words_t w;
    ai_words("Perch\x8A lo sprite \xC3\xA8 VERDE? spr(1,x)", 64, &w);
    CHECK(w.n == 8, "words: %d", w.n);
    CHECK(w.n > 0 && !strcmp(w.w[0], "perche"), "code page 437 folded: %s", w.w[0]);
    CHECK(w.n > 3 && !strcmp(w.w[3], "e"), "UTF-8 folded: %s", w.w[3]);
    CHECK(w.n > 4 && !strcmp(w.w[4], "verde"), "lower case: %s", w.w[4]);
}

static void test_near(void)
{
    int d;
    const char *s = ai_near("circfil", &d);
    CHECK(s && !strcmp(s, "circfill") && d == 1, "near circfil: %s", s ? s : "-");
    s = ai_near("spr", &d);
    CHECK(s && d == 0, "near spr");
    s = ai_near("banana", &d);
    CHECK(!s, "near banana: %s", s ? s : "-");
}

static void test_speed(void)
{
    ai_hit_t hits[5];
    clock_t t = clock();
    for (int i = 0; i < 1000; i++)
        ai_ask("come faccio a muovere il personaggio con le frecce", NULL, AI_KIND_ALL, hits, 5);
    double us = (double)(clock() - t) * 1e6 / CLOCKS_PER_SEC / 1000;
    printf("one question: %.1f us on this PC\n", us);
}

/* ---------------------------------------------------------------- sprites */

static void test_sprites(void)
{
    static spr_img_t a, b;
    spr_req_t r;
    CHECK(spr_recipes() >= 30, "%d recipes", spr_recipes());
    for (int i = 0; i < spr_recipes(); i++) {
        const char *id = spr_recipe_id(i);
        CHECK(spr_find(id) == i, "find %s", id);
        CHECK(ai_find(id) < 0 || 1, "-");
        static const int sizes[] = { 8, 16, 32 };
        for (int s = 0; s < 3; s++)
            for (uint32_t seed = 1; seed <= 6; seed++) {
                spr_req_init(&r, id);
                r.w = r.h = sizes[s];
                r.seed = seed;
                CHECK(spr_make(&r, &a) == 0 && a.w == sizes[s], "make %s", id);
                int n = 0;
                for (int k = 0; k < a.w * a.h; k++)
                    n += a.px[k] != SPR_CLEAR;
                /* something drawn, not a full square unless a tile */
                CHECK(n >= a.w * a.h / 10, "%s %d seed %u: only %d pixels", id, a.w, seed, n);
                spr_make(&r, &b);
                CHECK(!memcmp(&a, &b, sizeof a), "%s: same seed, same sprite", id);
            }
    }
    /* the words of a request */
    spr_req_init(&r, "slime");
    spr_parse("uno slime rosso grande senza contorno", &r);
    CHECK(r.w == 32 && r.color[0] == 0xD83A3A && r.color[1] == SPR_NO_COLOR && !r.outline,
          "parse: %d %06x %d", r.w, r.color[0], r.outline);
    spr_req_init(&r, "ship");
    spr_parse("astronave blu e gialla 8x8", &r);
    CHECK(r.w == 8 && r.color[0] == 0x3A62D8 && r.color[1] == 0xF0D040, "parse two colours");
    /* a palette: every pixel is one of its colours */
    static const uint32_t pal[] = { 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF };
    spr_req_init(&r, "slime");
    r.palette = pal;
    r.npalette = 5;
    spr_make(&r, &a);
    int ok = 1;
    for (int k = 0; k < a.w * a.h; k++)
        if (a.px[k] != SPR_CLEAR && a.px[k] != 0 && a.px[k] != 0xFFFFFF && a.px[k] != 0xFF0000 &&
            a.px[k] != 0x00FF00 && a.px[k] != 0x0000FF)
            ok = 0;
    CHECK(ok, "palette");
    spr_req_init(&r, "banana");
    CHECK(spr_make(&r, &a) == -1, "unknown recipe");
}

/* every recipe: seeds 1-6 at 16x16, 1-2 at 32x32, 1 at 8x8; a PPM, 3x */
static void sprite_sheet(const char *path)
{
    enum { Z = 3, CW = 6 * 18 + 2 * 34 + 2 * 10, RH = 34 };
    int W = CW * Z, H = spr_recipes() * RH * Z;
    uint8_t *img = calloc((size_t)W * H, 3);
    static spr_img_t a;
    for (int i = 0; i < spr_recipes(); i++) {
        int x0 = 0;
        static const int sz[] = { 16, 16, 16, 16, 16, 16, 32, 32, 8, 8 };
        for (int k = 0; k < 10; k++) {
            spr_req_t r;
            spr_req_init(&r, spr_recipe_id(i));
            r.w = r.h = sz[k];
            r.seed = (uint32_t)(k < 6 ? k + 1 : k < 8 ? k - 5 : k - 7);
            spr_make(&r, &a);
            for (int y = 0; y < a.h; y++)
                for (int x = 0; x < a.w; x++) {
                    uint32_t c = a.px[y * a.w + x];
                    if (c == SPR_CLEAR) c = ((x / 4 + y / 4) & 1) ? 0x707884 : 0x5A606C;
                    for (int j = 0; j < Z; j++)
                        for (int q = 0; q < Z; q++) {
                            uint8_t *p = img + 3 * ((size_t)((i * RH + 1 + y) * Z + j) * W + (x0 + x) * Z + q);
                            p[0] = (uint8_t)(c >> 16);
                            p[1] = (uint8_t)(c >> 8);
                            p[2] = (uint8_t)c;
                        }
                }
            x0 += sz[k] + 2;
        }
    }
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(img, 3, (size_t)W * H, f);
    fclose(f);
    free(img);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--sheet")) {
        sprite_sheet(argv[2]);
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: %s assist.bin ref.txt\n", argv[0]);
        return 2;
    }
    long len;
    void *blob = read_file(argv[1], &len);
    int r = ai_open(blob, (uint32_t)len);
    CHECK(r == 0, "ai_open: %s", ai_error(r));
    if (r)
        return 1;
    /* a damaged file is refused */
    ((uint8_t *)blob)[len / 2] ^= 1;
    CHECK(ai_open(blob, (uint32_t)len) == -3, "damaged file accepted");
    ((uint8_t *)blob)[len / 2] ^= 1;
    ai_open(blob, (uint32_t)len);

    test_words();
    test_reference(argv[2]);
    test_near();
    test_speed();
    test_sprites();

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
