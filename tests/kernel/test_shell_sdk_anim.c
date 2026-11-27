/*
 * Host test for the animations of the Shell SDK (core/src/kernel/shell_sdk_anim.c): the block read and written,
 * the plan of the tiles with their mirrors, their place in the sheet, and the recall from a program: animate() in
 * bm C and anim_draw in NES assembly, assembled by core/src/script/snes_asm.lua and run in the NES and SNES cores,
 * where the test reads the sprites.
 *   make test-sdk-anim      (run from the top of the repository)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell_sdk_anim.h"
#include "kernel/shell_sdk_cc.h"
#include "kernel/shell_sdk_edit.h"
#include "kernel/shell_sdk_sheet.h"
#include "kernel/shell_sdk_tiles.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "nes.h"
#include "snes_int.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static lua_State *L;
static sdk_anims_t anims, again;
static sdk_anim_pixels_t pix, back;
static sdk_anim_plan_t plan;
static int snes;

static const char driver[] =
    "local A = assert(loadfile('core/src/script/snes_asm.lua'))()\n"
    "return function(src, snes)\n"
    "  local res = A.assemble(src, { cpu = not snes and '6502' or nil, map = snes and 'lorom' or nil, name = 'main.s' })\n"
    "  if not res.ok then return nil, A.format_errors(res) end\n"
    "  local rom, info\n"
    "  if snes then rom, info = A.build_rom(res, { title = 'TEST', map = 'lorom', min_kb = 32, chipset = 0 })\n"
    "  else rom, info = A.build_nes(res, { prg_kb = 32, chr_kb = 8, mapper = 0 }) end\n"
    "  if not rom then return nil, info end\n"
    "  return rom\n"
    "end\n";

static int load(const char *src, size_t len)
{
    lua_settop(L, 1);
    lua_pushvalue(L, 1);
    lua_pushlstring(L, src, len);
    lua_pushboolean(L, snes);
    if (lua_pcall(L, 2, 2, 0) != LUA_OK || !lua_isstring(L, 2)) {
        const char *why = lua_tostring(L, -1);
        printf("assemble: %.800s\n", why ? why : "?");
        fails++;
        return -1;
    }
    size_t n;
    const uint8_t *rom = (const uint8_t *)lua_tolstring(L, 2, &n);
    const char *why = NULL;
    if (snes ? snes_load(rom, n, &why) : nes_load(rom, n, &why)) {
        printf("load: %s\n", why ? why : "?");
        fails++;
        return -1;
    }
    if (!snes)
        nes_reset();
    return 0;
}

static void frames(int n)
{
    for (int i = 0; i < n; i++)
        if (snes)
            snes_frame(0, 0, 1);
        else
            nes_frame(0, 0, 0);
}

/* A coin of 16x16: one quarter drawn, the others its mirrors; frame 2 is a different quarter, mirrored too. */
static void coin(sdk_anim_t *a)
{
    sdk_anim_new(a, "coin");
    a->frames = 2;
    memset(pix, 0, sizeof pix);
    for (int f = 0; f < 2; f++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t v = (uint8_t)(x + y > 6 + 3 * f ? 1 + f : x == 7 && y == 2 ? 3 : 0);
                pix[f][y][x] = pix[f][y][15 - x] = pix[f][15 - y][x] = pix[f][15 - y][15 - x] = v;
            }
}

static void test_plan(void)
{
    sdk_sheet_reset();
    sdk_anim_t *a = &anims.anim[0];
    anims.count = 1;
    coin(a);
    sdk_anim_plan(a, pix, 0, &plan);
    CHECK(plan.count == 8);                     /* no reuse: a tile a cell */
    sdk_anim_plan(a, pix, 1, &plan);
    CHECK(plan.count == 2);                     /* the mirrors: one tile a frame */
    CHECK(plan.use[0][0] == 0 && plan.flip[0][0] == 0);
    CHECK(plan.use[0][1] == 0 && plan.flip[0][1] == SDK_ANIM_FLIP_X);
    CHECK(plan.use[0][2] == 0 && plan.flip[0][2] == SDK_ANIM_FLIP_Y);
    CHECK(plan.use[0][3] == 0 && plan.flip[0][3] == (SDK_ANIM_FLIP_X | SDK_ANIM_FLIP_Y));
    CHECK(plan.use[1][3] == 1);

    /* the places: from tile 3, round a tile that is taken */
    sdk_tile_t other = { .used = 1, .fmt = SDK_TILE_NES };
    other.pix[0] = 2;
    sdk_sheet_set(0, 4, &other);
    int where[SDK_ANIM_UNIQUE];
    CHECK(sdk_anim_spots(&plan, 4, 256, NULL, where) == 2 && where[0] == 5 && where[1] == 6);
    CHECK(sdk_anim_spots(&plan, 255, 256, NULL, where) == 1);
    CHECK(sdk_anim_spots(&plan, 3, 256, NULL, where) == 2 && where[0] == 3 && where[1] == 5);
    sdk_anim_place(a, &plan, where, SDK_TILE_NES, 1);
    CHECK(sdk_sheet_tile(0, 3)->used && sdk_sheet_tile(0, 5)->used && sdk_sheet_tile(0, 4)->pix[0] == 2);
    CHECK(a->cell[0][0][0] == 3 && a->cell[0][0][1] == 1 && a->cell[0][3][0] == 3 && a->cell[0][3][1] == 0xC1);
    CHECK(a->cell[1][0][0] == 5);

    /* back from the sheet: the same pixels */
    sdk_anim_load(a, back);
    CHECK(!memcmp(back[0], pix[0], sizeof pix[0]) && !memcmp(back[1], pix[1], sizeof pix[1]));

    /* its own tiles: written again they take the same places */
    uint8_t own[SDK_ANIM_TILES];
    sdk_anim_own(&anims, 0, own);
    CHECK(own[3] && own[5] && !own[4]);
    CHECK(sdk_anim_spots(&plan, 0, 256, own, where) == 2 && where[0] == 0 && where[1] == 1);
    CHECK(sdk_anim_spots(&plan, 3, 256, own, where) == 2 && where[0] == 3 && where[1] == 5);
    anims.anim[1] = *a;                         /* another one shows the same tiles: they are not its own */
    anims.count = 2;
    sdk_anim_own(&anims, 0, own);
    CHECK(!own[3] && !own[5]);
    anims.count = 1;

    /* an empty cell takes no tile */
    memset(pix[1], 0, sizeof pix[1]);
    sdk_anim_plan(a, pix, 1, &plan);
    CHECK(plan.count == 1 && plan.use[1][0] == -1);
    sdk_anim_place(a, &plan, where, SDK_TILE_NES, 0);
    CHECK(a->cell[1][2][1] == SDK_ANIM_EMPTY);

    /* the frames from the sheet: two frames of 16x16 side by side from tile $20 */
    sdk_tile_t t = { .used = 1, .fmt = SDK_TILE_NES };
    t.pix[9] = 3;
    sdk_sheet_set(3, 2, &t);                    /* tile $32: frame 2, the cell down left */
    CHECK(sdk_anim_import(a, 0x20, back) == 0 && back[1][8 + 1][1] == 3 && !back[0][9][1]);
    CHECK(sdk_anim_import(a, 0x0D, back) == -1);

    /* mirror and flip of a rectangle */
    memset(back, 0, sizeof back);
    back[0][0][0] = 1;
    sdk_anim_flip(back, 0, 0, 0, 15, 15, 1);
    CHECK(back[0][0][15] == 1 && !back[0][0][0]);
    sdk_anim_flip(back, 0, 8, 0, 15, 7, 0);
    CHECK(back[0][7][15] == 1 && !back[0][0][15]);
}

static void test_block(void)
{
    static char text[8192], twice[8192];
    char err[160] = "";
    sdk_sheet_reset();
    coin(&anims.anim[0]);
    anims.count = 2;
    sdk_anim_plan(&anims.anim[0], pix, 1, &plan);
    int where[SDK_ANIM_UNIQUE];
    sdk_anim_spots(&plan, 1, 256, NULL, where);
    sdk_anim_place(&anims.anim[0], &plan, where, SDK_TILE_NES, 0);
    sdk_anim_new(&anims.anim[1], "walk");
    anims.anim[1].wide = 4;
    anims.anim[1].high = 3;
    anims.anim[1].frames = 8;
    for (int c = 0; c < 2; c++) {
        const long n = sdk_anim_text(&anims, c, text, sizeof text);
        CHECK(n > 0);
        CHECK(sdk_anim_parse(text, (size_t)n, c, &again, err, sizeof err) == 0);
        if (err[0])
            printf("%s\n%s\n", err, text);
        CHECK(again.count == 2 && !strcmp(again.anim[0].name, "coin") && again.anim[1].frames == 8);
        CHECK(!memcmp(again.anim[0].cell[0], anims.anim[0].cell[0], 8));
        CHECK(sdk_anim_text(&again, c, twice, sizeof twice) == n && !strcmp(text, twice));
        for (const char *s = text; *s; s = strchr(s, '\n') + 1)
            CHECK(strchr(s, '\n') - s <= 80);
    }
    const char *bad = ";@anim\nanim_x: .byte 2,2,1,8, 0,0\n;@end\n";
    CHECK(sdk_anim_parse(bad, strlen(bad), 0, &again, err, sizeof err) != 0 && strstr(err, "are 12"));
    bad = ";@anim\ncoin: .byte 1,1,1,8, 0,0\n;@end\n";
    CHECK(sdk_anim_parse(bad, strlen(bad), 0, &again, err, sizeof err) != 0 && strstr(err, "anim_NAME"));
    CHECK(sdk_anim_parse("x", 1, 0, &again, err, sizeof err) == 0 && again.count == 0);
    sdk_edit_set("code\n;@music\n;@end\n", 18);
    CHECK(sdk_anim_commit(&anims, 0, 0, err, sizeof err) == 0);
    CHECK(strstr(sdk_edit_text(), ";@anim") && strstr(sdk_edit_text(), ";@anim") < strstr(sdk_edit_text(), ";@music"));
    CHECK(sdk_anim_commit(&anims, 0, 0, err, sizeof err) == 0);
    CHECK(sdk_anim_parse(sdk_edit_text(), sdk_edit_length(), 0, &again, err, sizeof err) == 0 && again.count == 2);
}

/* A 2x1 animation of 2 frames, 4 screen frames each: tiles 1,2 then 3 mirrored and an empty cell. */
static const char *const c_program =
    "//@anim\n"
    "const byte anim_two[] = { 2, 1, 2, 4,\n"
    "    1,0x01, 2,0x00,\n"
    "    3,0x40, 0,0x10 };\n"
    "//@end\n"
    "void update(void) { animate(4, 50, 60, anim_two); }\n";

static const char *const asm_program =
    ".org $8000\n"
    "reset:\n"
    "    sei\n    cld\n    ldx #$FF\n    txs\n    inx\n    stx $2000\n    stx $2001\n"
    "    bit $2002\n"
    "@a: bit $2002\n    bpl @a\n"
    "@b: bit $2002\n    bpl @b\n"
    "    lda #50\n    sta anim_x\n    lda #60\n    sta anim_y\n    lda #4\n    sta anim_n\n"
    "    lda #$80\n    sta $2000\n"
    "loop:\n"
    "    lda $10\n"
    "@wait: cmp $10\n    beq @wait\n"
    "    jsr anim_tick\n"
    "    lda #<anim_two\n    ldy #>anim_two\n    jsr anim_draw\n"
    "    jmp loop\n"
    "nmi:\n    inc $10\n    rti\n"
    "irq:\n    rti\n"
    ";@anim\n"
    "anim_two: .byte 2, 1, 2, 4,  1,$01, 2,$00,  3,$40, 0,$10\n"
    ";@end\n"
    "after = 1\n";

static const char *const c_big =
    "//@anim\n"
    "const byte anim_big[] = { 0x82, 2, 2, 4,\n"
    "    4,0x05,\n"
    "    0x2C,0x48 };\n"
    "//@end\n"
    "void update(void) { animate(4, 300, 60, anim_big); }\n";

static const char *const snes_program =
    ".lorom\n.org $8000\n"
    "reset:\n"
    "    sei\n    clc\n    xce\n    rep #$10\n    sep #$20\n    .a8\n    .i16\n"
    "    ldx #$1FFF\n    txs\n    lda #$8F\n    sta $2100\n    stz $10\n"
    "    ldx #0\n@zero: stz $0200,x\n    inx\n    cpx #$600\n    bne @zero\n"
    "    lda #<300\n    sta anim_x\n    lda #>300\n    sta anim_x+1\n"
    "    lda #60\n    sta anim_y\n    lda #4\n    sta anim_n\n"
    "    lda #$81\n    sta $4200\n"
    "loop:\n"
    "    lda $10\n"
    "@wait: cmp $10\n    beq @wait\n"
    "    jsr anim_tick\n"
    "    ldx #anim_big\n    jsr anim_draw\n"
    "    jmp loop\n"
    "nmi:\n    sep #$20\n    pha\n    lda $4210\n    inc $10\n    pla\n    rti\n"
    "irq:\n    rti\n"
    ";@anim\n"
    "anim_big: .byte $82, 2, 2, 4,  4,$05,  $2C,$48\n"
    ";@end\n";

/* One 16x16 sprite (the sprite 4) over time, at x 300: frame 1 the tile 4 in the palette 5, frame 2 the tile 300
 * mirrored. */
static void watch_big(const char *what)
{
    int seen[2] = { 0, 0 };
    frames(12);
    for (int f = 0; f < 32; f++) {
        frames(1);
        const uint8_t *o = &S.wram[0x200 + 16];
        const int high = S.wram[0x200 + 512 + 1] & 3;
        if (o[0] != (300 & 255) || o[1] != 59 || high != 3)
            continue;
        if (o[2] == 4 && o[3] == 0x3A)
            seen[0]++;
        else if (o[2] == 0x2C && o[3] == 0x71)
            seen[1]++;
    }
    if (!(seen[0] >= 12 && seen[1] >= 12)) {
        const uint8_t *o = &S.wram[0x200 + 16];
        printf("FAIL %s: frames seen %d %d; sprite %d %d %d %02X high %02X\n", what, seen[0], seen[1], o[0], o[1],
               o[2], o[3], S.wram[0x200 + 513]);
        fails++;
    }
}

static void test_big(void)
{
    char err[200] = "";
    /* the model: a block of 16x16 takes the tiles t, t+1, t+16, t+17 and turns as a whole */
    sdk_sheet_reset();
    sdk_anim_t *a = &anims.anim[0];
    anims.count = 1;
    sdk_anim_new(a, "big");
    a->big = 1;
    a->frames = 2;
    a->wide = 4;
    memset(pix, 0, sizeof pix);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x <= y; x++) {
            pix[0][y][x] = 5;
            pix[0][y][31 - x] = 5;          /* the second block: the first mirrored */
            pix[1][y][x] = 9;
        }
    sdk_anim_plan(a, pix, 1, &plan);
    CHECK(plan.side == 16 && plan.count == 2 && plan.use[0][1] == 0 && plan.flip[0][1] == SDK_ANIM_FLIP_X &&
          plan.use[1][0] == 1 && plan.use[1][1] == -1);
    int where[SDK_ANIM_UNIQUE];
    sdk_tile_t other = { .used = 1, .fmt = SDK_TILE_SNES4 };
    sdk_sheet_set(18, 1, &other);           /* the tile $121 is taken */
    CHECK(sdk_anim_spots(&plan, 271, 512, NULL, where) == 2 && where[0] == 274 && where[1] == 276);
    CHECK(sdk_anim_spots(&plan, 495, 512, NULL, where) == 0);
    CHECK(sdk_anim_spots(&plan, 300, 512, NULL, where) == 2 && where[0] == 300 && where[1] == 302);
    sdk_anim_place(a, &plan, where, SDK_TILE_SNES4, 6);
    CHECK(a->cell[0][0][0] == (300 & 255) && a->cell[0][0][1] == (SDK_ANIM_HIGH | 6) &&
          a->cell[0][1][1] == (SDK_ANIM_HIGH | SDK_ANIM_FLIP_X | 6) && sdk_anim_tile(a->cell[1][0]) == 302);
    CHECK(sdk_sheet_tile(19, 13)->used && sdk_sheet_tile(19, 12)->pix[0] == 5);     /* the tile t+17, t+16 */
    sdk_anim_load(a, back);
    CHECK(!memcmp(back[0], pix[0], sizeof pix[0]) && !memcmp(back[1], pix[1], sizeof pix[1]));
    uint8_t own[SDK_ANIM_TILES];
    sdk_anim_own(&anims, 0, own);
    CHECK(own[300] && own[301] && own[316] && own[317] && own[302] && !own[304]);
    static char text[4096];
    for (int c = 0; c < 2; c++) {
        const long n = sdk_anim_text(&anims, c, text, sizeof text);
        CHECK(n > 0 && sdk_anim_parse(text, (size_t)n, c, &again, err, sizeof err) == 0);
        CHECK(again.anim[0].big && again.anim[0].wide == 4 && !memcmp(again.anim[0].cell, a->cell, sizeof a->cell));
    }
    const char *bad = ";@anim\nanim_x: .byte $81,2,1,8, 0,0\n;@end\n";
    CHECK(sdk_anim_parse(bad, strlen(bad), 0, &again, err, sizeof err) != 0 && strstr(err, "16x16"));

    snes = 1;
    sdk_cc_out_t c = { 0 };
    CHECK(sdk_cc_compile(c_big, strlen(c_big), 1, &c, err, sizeof err) == 0);
    if (c.text && load(c.text, c.len) == 0)
        watch_big("animate() with 16x16 sprites");
    else
        printf("%s\n", err);
    sdk_cc_free(&c);
    sdk_anim_src_t s;
    CHECK(sdk_anim_expand(snes_program, strlen(snes_program), 1, &s, err, sizeof err) == 0 && s.text);
    if (s.text && load(s.text, s.len) == 0)
        watch_big("anim_draw of the SNES");
    free(s.text);
    snes = 0;
}

/* The sprites 4 and 5 over time: both frames show, each for 4 screen frames. */
static void watch(const char *what)
{
    int seen[2] = { 0, 0 }, changes = 0, last = -1;
    frames(12);
    for (int f = 0; f < 32; f++) {
        frames(1);
        int shown = -1;
        if (!snes) {
            const uint8_t *o = (const uint8_t *)nes_ram() + 0x200 + 16;
            if (o[1] == 1 && o[2] == 0x01 && o[3] == 50 && o[0] == 59 && o[5] == 2 && o[7] == 58)
                shown = 0;
            else if (o[1] == 3 && o[2] == 0x40 && o[3] == 50 && o[4] == 0xFF)
                shown = 1;
        } else {
            const uint8_t *o = &S.wram[0x200 + 16];
            if (o[0] == 50 && o[1] == 59 && o[2] == 1 && o[4] == 58 && o[6] == 2)
                shown = 0;
            else if (o[0] == 50 && o[2] == 3 && (o[3] & 0x40))
                shown = 1;
        }
        if (shown >= 0) {
            seen[shown]++;
            changes += last >= 0 && shown != last;
            last = shown;
        }
    }
    if (!(seen[0] >= 12 && seen[1] >= 12 && changes >= 6 && changes <= 9)) {
        printf("FAIL %s: frames seen %d %d, changes %d\n", what, seen[0], seen[1], changes);
        fails++;
    }
}

static void test_recall(void)
{
    char err[200] = "";
    for (snes = 0; snes < 2; snes++) {
        sdk_cc_out_t c = { 0 };
        CHECK(sdk_cc_compile(c_program, strlen(c_program), snes, &c, err, sizeof err) == 0);
        if (!c.text) {
            printf("%s\n", err);
            continue;
        }
        if (load(c.text, c.len) == 0)
            watch(snes ? "animate() on the SNES" : "animate() on the NES");
        sdk_cc_free(&c);
    }
    snes = 0;
    sdk_anim_src_t s;
    CHECK(sdk_anim_expand(asm_program, strlen(asm_program), 0, &s, err, sizeof err) == 0 && s.text);
    if (s.text && load(s.text, s.len) == 0)
        watch("anim_draw in assembly");
    /* an error after the routines keeps the line of the source */
    char msg[160];
    snprintf(msg, sizeof msg, "main.s:%d: boom", s.first + s.lines + 1);
    sdk_anim_error_line(&s, msg, sizeof msg);
    char want[32];
    snprintf(want, sizeof want, "main.s:%d: boom", s.first + 1);
    CHECK(!strcmp(msg, want));
    free(s.text);
    CHECK(sdk_anim_expand("rts\n", 4, 0, &s, err, sizeof err) == 0 && !s.text);
    CHECK(sdk_anim_called("a(); animate (1,2,3,t);", 23, 1) && !sdk_anim_called("// animate(1)", 13, 1));
    CHECK(!sdk_anim_called("x(){animate(1);} void animate(byte a){}", 39, 1));
}

int main(void)
{
    L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dostring(L, driver) != LUA_OK) {
        printf("%s\n", lua_tostring(L, -1));
        return 1;
    }
    test_plan();
    test_block();
    test_recall();
    test_big();
    printf("test_shell_sdk_anim: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
