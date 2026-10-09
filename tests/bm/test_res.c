/* The resource files that tests/res/test_bmres.py wrote, read by the
 * kernel's parser (bm_parse_any, bm_zone, bm_info_get): the same kinds,
 * models, zones and INFO; bm_parse still takes only cartridges; a kind
 * with a section it does not have, and a broken CRC, are refused.
 *
 *   test_res BUILD/res BUILD/carts/village.bm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/bm.h"
#include "lib/crc32.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *load(const char *dir, const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s%s%s", dir, name ? "/" : "", name ? name : "");
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL cannot open %s\n", path);
        fails++;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*len);
    if (fread(d, 1, *len, f) != *len) *len = 0;
    fclose(f);
    return d;
}

static int open_any(const char *dir, const char *name, bm_cart_t *c, uint8_t **data, size_t *len)
{
    char err[64] = "";
    *data = load(dir, name, len);
    if (!*data)
        return -1;
    int r = bm_parse_any(*data, *len, c, err, sizeof err);
    CHECK(r == 0, "%s: %s", name, err);
    return r;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/res";
    const char *village = argc > 2 ? argv[2] : "build/carts/village.bm";
    bm_cart_t c;
    uint8_t *d;
    size_t len;
    char err[64], v[96];

    if (open_any(dir, "HOUSE.bmm", &c, &d, &len) == 0) {
        CHECK(c.kind == BM_RES_MODEL && c.models == 2 && c.anim && c.sheet8, "HOUSE.bmm: two models, a skeleton, a sheet");
        bm_model_t m;
        CHECK(bm_mesh_model(c.mesh, c.mesh_size, 0, &m) == 0 && !strcmp(m.name, "house"), "HOUSE.bmm: house first");
        bm_rig_t r;
        CHECK(bm_anim_rig(c.anim, c.anim_size, "villager", &r) == 0, "HOUSE.bmm: the villager's skeleton");
        bm_cart_t cc;
        CHECK(bm_parse(d, len, &cc, err, sizeof err) != 0, "bm_parse refuses a resource file");
        /* a models file that says it is a sounds file: MESH is not its */
        d[12] = BM_RES_SOUND;
        CHECK(bm_parse_any(d, len, &cc, err, sizeof err) != 0, "a section the kind does not have");
        d[12] = BM_RES_MODEL;
        d[len - 1] ^= 1;
        CHECK(bm_parse_any(d, len, &cc, err, sizeof err) != 0 && !strcmp(err, "CRC mismatch"), "a broken CRC");
        free(d);
    }
    if (open_any(dir, "FLAG.bmi", &c, &d, &len) == 0) {
        bm_zone_t z;
        CHECK(c.kind == BM_RES_IMAGE && c.zones == 1 && bm_zone(&c, 0, &z) == 0, "FLAG.bmi: one zone");
        CHECK(!strcmp(z.name, "flag") && z.w == 8 && z.h == 8 && z.frames == 2 && z.fps == 6,
              "FLAG.bmi: flag, 8x8, 2 frames at 6 fps (%s %dx%d %d %d)", z.name, z.w, z.h, z.frames, z.fps);
        CHECK(bm_zone(&c, 1, &z) != 0, "no second zone");
        free(d);
    }
    if (open_any(dir, "HERO.bmi", &c, &d, &len) == 0) {
        /* a zone with its hitbox and hurtbox (BOXES) */
        bm_box_t b;
        CHECK(c.kind == BM_RES_IMAGE && c.zones == 1 && c.nboxes == 2, "HERO.bmi: one zone, two boxes");
        CHECK(bm_box(&c, 0, &b) == 0 && !strcmp(b.zone, "hero") && b.frame == 0 && b.kind == BM_BOX_HURT &&
              b.x == 1 && b.y == 1 && b.w == 6 && b.h == 7, "HERO.bmi: the hurtbox of every frame");
        CHECK(bm_box(&c, 1, &b) == 0 && b.frame == 2 && b.kind == BM_BOX_HIT && b.y == -2,
              "HERO.bmi: the hitbox of frame 2 (y below 0)");
        CHECK(bm_box(&c, 2, &b) != 0, "no third box");
        /* a box of a frame the zone does not have: refused in a resource */
        bm_cart_t cc;
        uint32_t n = d[17];
        for (uint32_t i = 0; i < n; i++) {
            uint8_t *e = d + BM_HEADER_SIZE + i * 16;
            if ((e[0] | e[1] << 8) == BM_SEC_BOXES) {
                uint32_t off = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
                d[off + 4 + BM_BOX_SIZE + 16] = 3;          /* box 2: frame 3 of 2 */
                uint32_t crc = crc32(d + BM_HEADER_SIZE, (uint32_t)(len - BM_HEADER_SIZE));
                d[20] = (uint8_t)crc; d[21] = (uint8_t)(crc >> 8); d[22] = (uint8_t)(crc >> 16); d[23] = (uint8_t)(crc >> 24);
            }
        }
        CHECK(bm_parse_any(d, len, &cc, err, sizeof err) != 0, "a box of a frame the zone does not have");
        free(d);
    }
    if (open_any(dir, "JUMP.bms", &c, &d, &len) == 0) {
        CHECK(c.kind == BM_RES_SOUND && c.audio && !c.sheet_w, "JUMP.bms: a sound bank");
        free(d);
    }
    if (open_any(dir, "DEMO.bmt", &c, &d, &len) == 0) {
        CHECK(c.kind == BM_RES_MAP && c.map_w == 160 && c.map_h == 90 && c.sheet_w, "DEMO.bmt: the 160x90 map, its tiles");
        free(d);
    }
    if (open_any(dir, "VILLAGE.bmc", &c, &d, &len) == 0) {
        CHECK(c.kind == BM_RES_PALETTE && c.sheet8 && c.sheet_h == 1, "VILLAGE.bmc: a palette");
        free(d);
    }
    if (open_any(dir, "demo_models.bm", &c, &d, &len) == 0) {
        bm_cart_t cc;
        CHECK(c.kind == BM_RES_CART && c.models == 4 && c.lua, "demo_models.bm: a cartridge with four models");
        CHECK(bm_parse(d, len, &cc, err, sizeof err) == 0, "bm_parse takes it: %s", err);
        CHECK(bm_info_get(&c, "model", "house2", "origin", v, sizeof v) && !strcmp(v, "y"),
              "INFO: the origin of house2 (%s)", v);
        CHECK(!bm_info_get(&c, "model", "nobody", "origin", v, sizeof v), "INFO: no such part");
        free(d);
    }
    if (open_any(dir, "cli.bme", &c, &d, &len) == 0) {     /* the project bmres add made of cli.bm */
        CHECK(bm_info_get(&c, "model", "well", "license", v, sizeof v) && !strcmp(v, "CC0-1.0"),
              "INFO: the licence of the well (%s)", v);
        CHECK(bm_info_get(&c, "model", "well", "author", v, 3) && !strcmp(v, "bm"), "INFO: cut to the buffer");
        free(d);
    }
    if (open_any(village, NULL, &c, &d, &len) == 0) {
        bm_cart_t cc;
        CHECK(c.kind == BM_RES_CART && c.models == 8 && !c.info && !c.zones, "a game of before: no INFO, no zones");
        CHECK(bm_parse(d, len, &cc, err, sizeof err) == 0, "bm_parse still reads it");
        free(d);
    }
    printf("test_res: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
