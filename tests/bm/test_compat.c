/* Host test of the compatibility policy (bm_compat_facts, bm_compat_classify in src/bm/format.c).
 *
 *   test_compat          the cases: every rule of docs/spec/compat-policy.toml once, the old files, the new header
 *   test_compat --grid   prints the verdict for a grid of facts, read from real headers;
 *                        tests/tools/test_compat_policy.py compares it with scripts/bm_weight.py classify */
#include <stdio.h>
#include <string.h>

#include "bm/bm.h"

static int fails;

#define CHECK(c)                                                                   \
    do {                                                                           \
        if (!(c)) {                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                    \
            fails++;                                                               \
        }                                                                          \
    } while (0)

enum { P = BM_TARGET_PI, R = BM_TARGET_RGB30 };
enum { ED_BM = 0, ED_B16 = 2, ED_PROJECT = 1 };         /* the flag bits of the header */

/* a header with a table: `secs` sections of the types in `types` and the sizes in `sizes` */
static size_t make(uint8_t *buf, unsigned flags, unsigned w, unsigned h, unsigned compat_targets, int n,
                   const unsigned *types, const unsigned *sizes)
{
    memset(buf, 0, 128 + 16 * 8);
    memcpy(buf, "BMCART", 6);
    buf[8] = 1;
    buf[10] = 128;
    buf[12] = (uint8_t)w; buf[13] = (uint8_t)(w >> 8);
    buf[14] = (uint8_t)h; buf[15] = (uint8_t)(h >> 8);
    buf[16] = 1;
    buf[17] = (uint8_t)n;
    if (compat_targets) {
        flags |= BM_FLAG_COMPAT;
        buf[120] = (uint8_t)compat_targets;
    }
    buf[18] = (uint8_t)flags;
    buf[19] = (uint8_t)(flags >> 8);
    for (int i = 0; i < n; i++) {
        uint8_t *e = buf + 128 + 16 * i;
        e[0] = (uint8_t)types[i];
        e[8] = (uint8_t)sizes[i]; e[9] = (uint8_t)(sizes[i] >> 8); e[10] = (uint8_t)(sizes[i] >> 16);
        e[11] = (uint8_t)(sizes[i] >> 24);
    }
    return 128 + 16u * (unsigned)n;
}

/* the verdict of a file: action; the rule number and the reason in *rule and *why */
static int verdict(unsigned flags, unsigned w, unsigned h, unsigned targets, int n, const unsigned *types,
                   const unsigned *sizes, uint32_t file_size, unsigned kernel, int market, int *rule,
                   const char **why)
{
    uint8_t buf[128 + 16 * 8];
    size_t len = make(buf, flags, w, h, targets, n, types, sizes);
    bm_compat_facts_t f;
    bm_compat_facts(buf, len, file_size, NULL, &f);
    return bm_compat_classify(&f, kernel, market, why, rule);
}

static int grid(void)
{
    static const unsigned kernels[] = { P, R };
    static const unsigned editions[] = { ED_BM, ED_B16, ED_PROJECT };
    static const unsigned declared[] = { 0, P, R, P | R };
    static const unsigned res[][2] = { { 640, 360 }, { 720, 720 } };
    static const unsigned audio[] = { 0, 16777216, 16777217 };
    static const unsigned sizes[] = { 1000, 100u << 20, (100u << 20) + 1 };
    for (int k = 0; k < 2; k++)
    for (int m = 0; m < 2; m++)
    for (int e = 0; e < 3; e++)
    for (int d = 0; d < 4; d++)
    for (int r = 0; r < 2; r++)
    for (int t = 0; t < 2; t++)
    for (int a = 0; a < 3; a++)
    for (int s = 0; s < 3; s++) {
        unsigned types[2] = { BM_SEC_MESH, BM_SEC_AUDIO }, secs[2] = { 100, audio[a] };
        int n = 0;
        unsigned ty[2], sz[2];
        if (t) { ty[n] = types[0]; sz[n++] = secs[0]; }
        if (audio[a]) { ty[n] = types[1]; sz[n++] = secs[1]; }
        int rule;
        const char *why;
        int act = verdict(editions[e], res[r][0], res[r][1], declared[d], n, ty, sz, sizes[s], kernels[k], m,
                          &rule, &why);
        printf("%u %d %u %u %u %u %d %u %u -> %d %d %s\n", kernels[k], m, editions[e], declared[d], res[r][0],
               res[r][1], t, audio[a], sizes[s], act, rule, why);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--grid"))
        return grid();

    int rule;
    const char *why;
    unsigned none[1] = { 0 };

    /* the table has the rules of the toml: a new rule needs a case below */
    CHECK(bm_compat_rule_count() == 12);

    /* rule 1 and 2: a project is hidden */
    CHECK(verdict(ED_PROJECT, 640, 360, 0, 0, none, none, 1000, P, 0, &rule, &why) == BM_ACT_HIDE && rule == 1);
    CHECK(verdict(ED_PROJECT, 640, 360, 0, 0, none, none, 1000, R, 0, &rule, &why) == BM_ACT_HIDE && rule == 2);

    /* rule 3 and 4: a header that does not list the kernel greys the cartridge, with the reason */
    CHECK(verdict(ED_BM, 640, 360, R, 0, none, none, 1000, P, 0, &rule, &why) == BM_ACT_GREY && rule == 3);
    CHECK(!strcmp(why, "This edition is for the RGB30"));
    CHECK(verdict(ED_BM, 640, 360, P, 0, none, none, 1000, R, 0, &rule, &why) == BM_ACT_GREY && rule == 4);
    CHECK(!strcmp(why, "This edition is for the Pi"));
    CHECK(verdict(ED_B16, 360, 360, P, 0, none, none, 1000, R, 1, &rule, &why) == BM_ACT_GREY && rule == 4);

    /* a header that lists the kernel shows it; the old-.bm rule (5) does not apply to a declared file */
    CHECK(verdict(ED_BM, 640, 360, P | R, 0, none, none, 1000, R, 0, &rule, &why) == BM_ACT_SHOW && rule == 0);
    CHECK(verdict(ED_BM, 640, 360, P | R, 0, none, none, 1000, P, 0, &rule, &why) == BM_ACT_SHOW && rule == 12);

    /* rule 5: an old .bm (no block) is for the Pi only: grey on the RGB30, in the menu and in the Market */
    CHECK(verdict(ED_BM, 640, 360, 0, 0, none, none, 1000, R, 0, &rule, &why) == BM_ACT_GREY && rule == 5);
    CHECK(verdict(ED_BM, 640, 360, 0, 0, none, none, 1000, R, 1, &rule, &why) == BM_ACT_GREY && rule == 5);
    CHECK(!strcmp(why, "This edition is for the Pi"));

    /* rule 6 and 7: a sound bank over 16 MB (8,388,608 values) is grey */
    {
        unsigned ty[1] = { BM_SEC_AUDIO }, sz[1] = { 16777216 };
        CHECK(verdict(ED_B16, 360, 360, 0, 1, ty, sz, 1000, P, 0, &rule, &why) == BM_ACT_SHOW && rule == 10);
        sz[0] = 16777217;
        CHECK(verdict(ED_B16, 360, 360, 0, 1, ty, sz, 1000, P, 0, &rule, &why) == BM_ACT_GREY && rule == 6);
        CHECK(verdict(ED_B16, 360, 360, 0, 1, ty, sz, 1000, R, 0, &rule, &why) == BM_ACT_GREY && rule == 7);
        CHECK(!strcmp(why, "sound bank over the limit"));
        unsigned two_ty[2] = { BM_SEC_AUDIO, BM_SEC_AUDIO }, two_sz[2] = { 10000000, 10000000 };      /* the sizes add up */
        CHECK(verdict(ED_B16, 360, 360, 0, 2, two_ty, two_sz, 1000, R, 0, &rule, &why) == BM_ACT_GREY && rule == 7);
    }

    /* rule 8 and 9: over 100 MB the menu shows, the Market hides */
    CHECK(verdict(ED_B16, 360, 360, 0, 0, none, none, 100u << 20, P, 1, &rule, &why) == BM_ACT_SHOW && rule == 10);
    CHECK(verdict(ED_B16, 360, 360, 0, 0, none, none, (100u << 20) + 1, P, 0, &rule, &why) == BM_ACT_SHOW &&
          rule == 8);
    CHECK(verdict(ED_B16, 360, 360, 0, 0, none, none, (100u << 20) + 1, P, 1, &rule, &why) == BM_ACT_HIDE &&
          rule == 8);
    CHECK(verdict(ED_B16, 360, 360, 0, 0, none, none, (100u << 20) + 1, R, 1, &rule, &why) == BM_ACT_HIDE &&
          rule == 9);

    /* rule 10 and 11: an old .b16 runs on both */
    CHECK(verdict(ED_B16, 360, 360, 0, 0, none, none, 1000, P, 0, &rule, &why) == BM_ACT_SHOW && rule == 10);
    CHECK(verdict(ED_B16, 360, 360, 0, 0, none, none, 1000, R, 0, &rule, &why) == BM_ACT_SHOW && rule == 11);

    /* rule 12: an old .bm shows on the Pi */
    CHECK(verdict(ED_BM, 640, 360, 0, 0, none, none, 1000, P, 0, &rule, &why) == BM_ACT_SHOW && rule == 12);

    /* an unknown kernel shows; a rule number 0 is the kernel's default row */
    CHECK(verdict(ED_BM, 640, 360, 0, 0, none, none, 1000, 4, 0, &rule, &why) == BM_ACT_SHOW && rule == 0);

    /* every reason fits the small screen */
    {
        bm_compat_facts_t f = { 0 };
        for (unsigned k = 1; k <= 2; k++)
            for (unsigned e = 0; e < 3; e++)
                for (unsigned d = 0; d < 4; d++) {
                    f.edition = (uint8_t)(e == 0 ? BM_EDITION_BM : e == 1 ? BM_EDITION_B16 : BM_EDITION_PROJECT);
                    f.declared = d != 0;
                    f.targets = (uint8_t)(d ? d : 1);
                    bm_compat_classify(&f, k, 0, &why, &rule);
                    CHECK(strlen(why) <= BM_REASON_MAX);
                }
    }

    /* the facts: the header read, the table read as far as the bytes go */
    {
        uint8_t buf[128 + 16 * 8];
        unsigned ty[3] = { BM_SEC_LUA, BM_SEC_MESH, BM_SEC_AUDIO }, sz[3] = { 5, 7, 4096 };
        size_t len = make(buf, ED_B16, 360, 360, P | R, 3, ty, sz);
        bm_compat_facts_t f;
        bm_compat_facts(buf, len, 5000, NULL, &f);
        CHECK(f.edition == BM_EDITION_B16 && f.declared == 1 && f.targets == (P | R));
        CHECK(f.w == 360 && f.h == 360 && f.uses_3d == 1 && f.audio_bytes == 4096 && f.size_bytes == 5000);
        bm_compat_facts(buf, 128 + 16 * 2, 5000, NULL, &f);           /* the read ended after two table rows */
        CHECK(f.uses_3d == 1 && f.audio_bytes == 0);
        bm_compat_facts(buf, 100, 5000, NULL, &f);                    /* less than a header: nothing */
        CHECK(f.edition == BM_EDITION_BM && f.size_bytes == 5000 && f.w == 0 && !f.declared);
        /* bit 4 with targets 0 is invalid: it reads as an old file (bm_compat_read) */
        make(buf, ED_BM, 640, 360, 0, 0, none, none);
        buf[18] |= BM_FLAG_COMPAT;
        bm_compat_facts(buf, 128, 1000, NULL, &f);
        CHECK(!f.declared && f.targets == P);
        /* the extension tells the edition: a .b16 built before the flag bit existed is still a .b16 */
        bm_compat_facts(buf, 128, 1000, "OLD.B16", &f);
        CHECK(f.edition == BM_EDITION_B16 && f.targets == BM_TARGET_ALL);
        bm_compat_facts(buf, 128, 1000, "game.bme", &f);
        CHECK(f.edition == BM_EDITION_PROJECT);
        bm_compat_facts(buf, 128, 1000, "game.bm", &f);
        CHECK(f.edition == BM_EDITION_BM && f.targets == BM_TARGET_PI);
        bm_compat_facts(buf, 128, 1000, "readme.txt", &f);      /* no known extension: the flags */
        CHECK(f.edition == BM_EDITION_BM);
        /* a Market entry: the name tells the edition */
        bm_compat_facts_named(0, 1000, &f);
        CHECK(bm_compat_classify(&f, R, 1, &why, &rule) == BM_ACT_GREY && rule == 5);
        bm_compat_facts_named(1, 1000, &f);
        CHECK(bm_compat_classify(&f, R, 1, &why, &rule) == BM_ACT_SHOW && rule == 11);
    }

    if (fails) {
        printf("test_compat: %d failed\n", fails);
        return 1;
    }
    printf("test_compat: ok\n");
    return 0;
}
