/*
 * Host test for the music of the Shell SDK (core/src/kernel/shell_sdk_music.c and shell_sdk_snd.c): the block read
 * and written in assembly and in bm C, the messages of the mistakes, and the sound driver assembled by
 * core/src/script/snes_asm.lua and run in the NES core, where the test reads what reaches the sound registers.
 *   make test-sdk-music      (run from the top of the repository)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell_sdk_cc.h"
#include "kernel/shell_sdk_edit.h"
#include "kernel/shell_sdk_music.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "nes.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static lua_State *L;
static sdk_music_t music, again;

static const char driver[] =
    "local A = assert(loadfile('core/src/script/snes_asm.lua'))()\n"
    "return function(src)\n"
    "  local res = A.assemble(src, { cpu = '6502', name = 'main.s' })\n"
    "  if not res.ok then return nil, A.format_errors(res) end\n"
    "  local rom, info = A.build_nes(res, { prg_kb = 32, chr_kb = 8, mapper = 0 })\n"
    "  if not rom then return nil, info end\n"
    "  return rom\n"
    "end\n";

/* Assembles and loads a source. 0 when it went well. */
static int load(const char *src, size_t len)
{
    lua_settop(L, 1);
    lua_pushvalue(L, 1);
    lua_pushlstring(L, src, len);
    if (lua_pcall(L, 1, 2, 0) != LUA_OK || !lua_isstring(L, 2)) {
        const char *why = lua_tostring(L, -1);
        printf("assemble: %.800s\n", why ? why : "?");
        fails++;
        return -1;
    }
    size_t n;
    const uint8_t *rom = (const uint8_t *)lua_tolstring(L, 2, &n);
    const char *why = NULL;
    if (nes_load(rom, n, &why)) {
        printf("load: %s\n", why ? why : "?");
        fails++;
        return -1;
    }
    nes_reset();
    return 0;
}

static void frames(int n)
{
    for (int i = 0; i < n; i++)
        nes_frame(0, 0, 0);
}

static const char *const sample =
    "; a program\n"
    ";@music\n"
    "lead:\n"
    "    .byte PULSE, DUTY25, 0, 0\n"
    "    .byte 15, 12, LOOP, 9, 8, RELEASE, 4, 2, END\n"
    "wob:  .byte PULSE, 2, 4, $47\n"
    "    .byte 10, END\n"
    "bass:\n"
    "    .byte TRIANGLE, 0, 0, 0, 15, LOOP, 15, END\n"
    "kick:\n"
    "    .byte NOISE, 12+SHORT, 0, 0, 14, 9, 4, 0, END\n"
    "pattern00:                 ; four steps\n"
    "    .byte C4,LEAD,  __,0,     C2,BASS,  KICK\n"
    "    .byte __,0,     E4,LEAD,  __,0,     __\n"
    "    .byte OFF,0,    __,0,     OFF,0,    OFF\n"
    "    .byte A4,0,     __,0,     __,0,     __\n"
    "    .byte END\n"
    "pattern02:\n"
    "    .byte C5,lead,  __,0,     __,0,     __\n"
    "    .byte END\n"
    "sfx_beep:\n"
    "    .byte 3, PU1, DUTY12\n"
    "    .byte C5,15,__,  __,10,UP+2,  OFF,0,__\n"
    "    .byte END\n"
    "sfx_hit:\n"
    "    .byte 2, NOI, SHORT\n"
    "    .byte N9,15,__,  N3,7,__\n"
    "    .byte END\n"
    "song_one:\n"
    "    .byte 6, 1\n"
    "    .byte 0, 2, 5, END\n"
    "song_two:\n"
    "    .byte 4, END\n"
    "    .byte 2, END\n"
    ";@end\n"
    "after = 1\n";

static void test_read(void)
{
    char err[160] = "";
    CHECK(sdk_music_parse(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
    if (err[0])
        printf("%s\n", err);
    CHECK(music.instruments == 4 && music.sfxs == 2 && music.songs == 2);
    const sdk_mus_ins_t *lead = &music.ins[0];
    CHECK(!strcmp(lead->name, "lead") && lead->channel == SDK_MUS_PULSE && lead->wave == 1);
    CHECK(lead->len == 6 && lead->loop == 2 && lead->release == 4 && lead->volume[5] == 2);
    CHECK(music.ins[1].arp == 0x47 && music.ins[1].vibrato == 4 && music.ins[1].loop == SDK_MUS_NO);
    CHECK(music.ins[3].wave == (12 | SDK_MUS_SHORT));
    const sdk_mus_pattern_t *p = &music.pat[0];
    CHECK(p->used && p->steps == 4 && !music.pat[1].used && music.pat[2].steps == 1);
    CHECK(p->cell[0][0].note == 60 && p->cell[0][0].ins == 1 && p->cell[0][2].note == 36 && p->cell[0][3].ins == 4);
    CHECK(p->cell[2][0].note == SDK_MUS_OFF && p->cell[2][3].note == SDK_MUS_OFF && p->cell[3][0].note == 69);
    CHECK(music.sfx[0].steps == 3 && music.sfx[0].step[1].fx == (SDK_FX_UP | 2) && music.sfx[0].step[1].note == 0);
    CHECK(music.sfx[1].channel == 3 && music.sfx[1].wave == SDK_MUS_SHORT && music.sfx[1].step[0].note == SDK_MUS_LOW + 9);
    CHECK(music.song[0].speed == 6 && music.song[0].loop == 1 && music.song[0].len == 3 && music.song[0].order[2] == 5);
    CHECK(music.song[1].loop == SDK_MUS_NO && music.song[1].len == 1);
    CHECK(p->row_line[1] == p->row_line[0] + 1 && p->line > 0);
}

/* The block that is written reads back the same, in both languages. */
static void test_write(void)
{
    static char text[64 * 1024], twice[64 * 1024];
    for (int c = 0; c < 2; c++) {
        char err[160] = "";
        CHECK(sdk_music_parse(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
        const long n = sdk_music_text(&music, c, text, sizeof text);
        CHECK(n > 0);
        CHECK(sdk_music_parse(text, (size_t)n, c, &again, err, sizeof err) == 0);
        if (err[0])
            printf("%s\n%s\n", err, text);
        CHECK(sdk_music_text(&again, c, twice, sizeof twice) == n && !strcmp(text, twice));
        for (const char *s = text; *s; s = strchr(s, '\n') + 1)
            CHECK(strchr(s, '\n') - s <= 80);       /* a screen of the console */
        CHECK(sdk_music_text(&music, c, twice, 100) == -1);
    }
    CHECK(strstr(text, "const byte pattern00[] = {") != NULL);

    /* into the editor: a new block before the map, then the same block replaced */
    char err[160];
    sdk_edit_reset();
    sdk_edit_set("code\n;@map\n;@end\n", 17);
    sdk_music_clear(&again);
    CHECK(sdk_music_add_ins(&again, SDK_MUS_PULSE) == 0);
    CHECK(sdk_music_commit(&again, 0, 0, err, sizeof err) == 0);
    const char *t = sdk_edit_text();
    CHECK(strstr(t, ";@music") && strstr(t, ";@music") < strstr(t, ";@map") && strstr(t, "lead:"));
    CHECK(sdk_music_add_ins(&again, SDK_MUS_PULSE) == 1 && !strcmp(again.ins[1].name, "lead2"));
    CHECK(sdk_music_commit(&again, 0, 0, err, sizeof err) == 0);
    CHECK(sdk_music_parse(sdk_edit_text(), sdk_edit_length(), 0, &music, err, sizeof err) == 0);
    CHECK(music.instruments == 2 && strstr(sdk_edit_text(), "code\n;@music") && strstr(sdk_edit_text(), ";@map\n"));
}

static void test_edit(void)
{
    char err[160];
    CHECK(sdk_music_parse(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
    CHECK(sdk_music_next_ins(&music, -1, 1, SDK_MUS_PULSE) == 0);
    CHECK(sdk_music_next_ins(&music, 0, 1, SDK_MUS_PULSE) == 1);
    CHECK(sdk_music_next_ins(&music, 1, 1, SDK_MUS_PULSE) == 0);
    CHECK(sdk_music_next_ins(&music, -1, 1, SDK_MUS_NOISE) == 3);
    sdk_music_remove_ins(&music, 0);
    CHECK(music.instruments == 3 && music.pat[0].cell[0][0].ins == 0 && music.pat[0].cell[0][3].ins == 3);
    CHECK(sdk_music_name_ok("kick_2") && !sdk_music_name_ok("C4") && !sdk_music_name_ok("loop") &&
          !sdk_music_name_ok("2a") && !sdk_music_name_ok("sfx_a") && !sdk_music_name_ok("pattern01"));
    char note[4];
    sdk_music_note_name(61, note);
    CHECK(!strcmp(note, "CS4"));
}

static void mistake(int line, const char *block, const char *want)
{
    char src[1024], err[160] = "";
    snprintf(src, sizeof src, ";@music\n%s\n;@end\n", block);
    if (sdk_music_parse(src, strlen(src), 0, &music, err, sizeof err) == 0 || !strstr(err, want)) {
        printf("FAIL line %d: wanted '%s', got '%s'\n", line, want, err);
        fails++;
    }
}
#define MISTAKE(block, want) mistake(__LINE__, block, want)

static void test_mistakes(void)
{
    MISTAKE("lead:\n .byte PULSE, 0, 0, 0, 15", "ends with END");
    MISTAKE("lead:\n .byte SQUARE, 0, 0, 0, END", "PULSE, TRIANGLE or NOISE");
    MISTAKE("lead:\n .byte PULSE, 0, 0, 0, 16, END", "line 3: a volume is 0 to 15, not 16");
    MISTAKE("pattern00:\n .byte C4,PIANO, __,0, __,0, __, END", "there is no instrument 'PIANO'");
    MISTAKE("pattern00:\n .byte H4,0, __,0, __,0, __, END", "'H4' is not a note");
    MISTAKE("pattern00:\n .byte C4,0, __,0, END", "a step is 7");
    MISTAKE("b: .byte TRIANGLE,0,0,0,END\npattern00:\n .byte C4,B, __,0, __,0, __, END", "track 1 wants PULSE");
    MISTAKE("sfx_a:\n .byte 2, PU3, 0, END", "PU1, PU2, TRI or NOI");
    MISTAKE("sfx_a:\n .byte 2, PU1, 0, C4,15,UP, END", "an effect is");
    MISTAKE("song_a:\n .byte 6, 4, 0, END", "the place to loop to");
    MISTAKE("lead:\n lda #1", "labels and .byte lines only");
    char err[160];
    CHECK(sdk_music_parse(";@music\nx", 9, 0, &music, err, sizeof err) != 0 && strstr(err, "no ;@end"));
    CHECK(sdk_music_parse("nothing", 7, 0, &music, err, sizeof err) == 0 && sdk_music_empty(&music));
}

/* A program that starts the driver and then plays what the test writes at $07F0. */
static const char *const player =
    ".org $8000\n"
    "reset:\n"
    "    sei\n    cld\n    ldx #$FF\n    txs\n    inx\n    stx $2000\n    stx $2001\n"
    "    bit $2002\n"
    "@a: bit $2002\n    bpl @a\n"
    "@b: bit $2002\n    bpl @b\n"
    "    lda #0\n    sta $07F0\n"
    "    jsr snd_init\n"
    "    lda #$80\n    sta $2000\n"
    "loop:\n"
    "    lda $07F0\n    beq loop\n"
    "    ldx #0\n    stx $07F0\n"
    "    cmp #1\n    bne @sfx\n    lda $07F1\n    jsr play_song\n    jmp loop\n"
    "@sfx:\n    cmp #2\n    bne @stop\n    lda $07F1\n    jsr play_sfx\n    jmp loop\n"
    "@stop:\n    jsr stop_music\n    jmp loop\n"
    "nmi:\n    jsr snd_frame\n    rti\n"
    "irq:\n    rti\n";

/* What the driver wrote in the registers, seen through a copy it cannot make: the test reads the RAM of the driver
 * (the layout of shell_sdk_snd.c). */
enum { M_ON = 0x700, M_AT_ORDER = 0x77E, M_AT_ROW = 0x77F, C_NOTE = 0x714, C_STATE = 0x720, X_ON = 0x730 };

static int ram(int a) { return nes_ram()[a]; }

static void test_driver(void)
{
    static char src[96 * 1024];
    const int n = snprintf(src, sizeof src, "%s%s", player, sample);
    sdk_music_src_t s;
    char err[160] = "";
    CHECK(sdk_music_expand(src, (size_t)n, &s, err, sizeof err) == 0 && s.text);
    if (!s.text || load(s.text, s.len) != 0)
        return;
    /* RAM the test writes: through the pad is not possible, so the commands go by a second program */
    frames(6);
    CHECK(ram(M_ON) == 0);
    free(s.text);

    /* each command is a program of its own: play at the start, then watch */
    static const char *const start[] = { "lda #song_one\n    jsr play_song", "lda #sfx_beep\n    jsr play_sfx",
                                         "lda #sfx_hit\n    jsr play_sfx" };
    for (int k = 0; k < 3; k++) {
        char head[8192];
        const char *at = strstr(player, "    lda #$80\n");
        snprintf(head, sizeof head, "%.*s    %s\n%s", (int)(at - player), player, start[k], at);
        snprintf(src, sizeof src, "%s%s", head, sample);
        CHECK(sdk_music_expand(src, strlen(src), &s, err, sizeof err) == 0);
        if (!s.text || load(s.text, s.len) != 0)
            return;
        free(s.text);
        int f = 0;
        while (f < 12 && ram(k ? (k == 1 ? X_ON : X_ON + 3) : M_ON) == 0)
            frames(1), f++;
        CHECK(f < 12);
        nes_debug_t d;
        if (k == 0) {                           /* the song: row 0 sounds, speed 6 */
            CHECK(ram(M_AT_ROW) == 0 && ram(C_NOTE) == 60 && ram(C_NOTE + 2) == 36 && ram(C_STATE) == 1);
            CHECK(ram(C_STATE + 3) == 1);
            frames(6);
            CHECK(ram(M_AT_ROW) == 1 && ram(C_NOTE + 1) == 64 && ram(C_STATE + 1) == 1);
            frames(6);
            CHECK(ram(M_AT_ROW) == 2 && ram(C_STATE) == 2 && ram(C_STATE + 3) == 0);   /* OFF: released */
            frames(6);
            CHECK(ram(M_AT_ROW) == 3 && ram(C_NOTE) == 69 && ram(C_STATE) == 1);
            frames(6);
            CHECK(ram(M_AT_ORDER) == 1 && ram(M_AT_ROW) == 0 && ram(C_NOTE) == 72);    /* pattern 2: one step */
            frames(6);
            CHECK(ram(M_AT_ORDER) == 2 && ram(M_ON) == 1);                    /* pattern 5: empty, 16 steps */
            frames(16 * 6);
            CHECK(ram(M_AT_ORDER) == 1 && ram(M_ON) == 1);                    /* the loop, to place 1 */
        } else {
            const int x = k == 1 ? X_ON : X_ON + 3;
            const int length = k == 1 ? 9 : 4;
            frames(length - 2);
            CHECK(ram(x) == 1);
            frames(4);
            CHECK(ram(x) == 0);
        }
        nes_debug(&d);
        CHECK(!d.jammed);
    }
}

/* The sound is heard: the samples of the core change. */
static void test_sound(void)
{
    static char src[96 * 1024];
    const char *at = strstr(player, "    lda #$80\n");
    snprintf(src, sizeof src, "%.*s    lda #song_one\n    jsr play_song\n%s%s", (int)(at - player), player, at,
             sample);
    sdk_music_src_t s;
    char err[160];
    CHECK(sdk_music_expand(src, strlen(src), &s, err, sizeof err) == 0);
    if (!s.text || load(s.text, s.len) != 0)
        return;
    free(s.text);
    static float out[2 * 800];
    float low = 0, high = 0;
    for (int f = 0; f < 30; f++) {
        frames(1);
        memset(out, 0, sizeof out);
        nes_audio_mix(out, 800, 1.0f);
        for (int i = 0; i < 1600; i++) {
            low = out[i] < low ? out[i] : low;
            high = out[i] > high ? out[i] : high;
        }
    }
    printf("sound: samples from %.3f to %.3f\n", (double)low, (double)high);
    CHECK(high - low > 0.05f);
}

static char *slurp(const char *path, size_t *n)
{
    static char b[64 * 1024];
    FILE *f = fopen(path, "rb");
    *n = f ? fread(b, 1, sizeof b - 1, f) : 0;
    if (f)
        fclose(f);
    b[*n] = 0;
    return b;
}

/* The start files: their block reads; in bm C the program plays the effect when A goes down. */
static void test_templates(void)
{
    size_t n;
    char err[200] = "";
    char *src = slurp("core/src/script/sdk_tpl_nes.s", &n);
    CHECK(sdk_music_parse(src, n, 0, &music, err, sizeof err) == 0 && music.sfxs == 1);
    sdk_music_src_t s;
    CHECK(sdk_music_expand(src, n, &s, err, sizeof err) == 0 && s.text);
    if (s.text && load(s.text, s.len) == 0) {
        frames(10);
        CHECK(ram(X_ON) == 0);
        for (int i = 0; i < 3; i++)
            nes_frame(NES_PAD_A, 0, 0);
        CHECK(ram(X_ON) == 1);
    }
    free(s.text);
    src = slurp("core/src/script/sdk_tpl_nes_c.txt", &n);
    sdk_cc_out_t c;
    CHECK(sdk_cc_compile(src, n, 0, &c, err, sizeof err) == 0);
    if (!c.text) {
        printf("%s\n", err);
        return;
    }
    if (load(c.text, c.len) == 0) {
        frames(10);
        CHECK(ram(X_ON) == 0);
        for (int i = 0; i < 4; i++)
            nes_frame(NES_PAD_A, 0, 0);
        CHECK(ram(X_ON) == 1);
        frames(30);
        CHECK(ram(X_ON) == 0);
    }
    sdk_cc_free(&c);
    /* the whole sample in bm C, written by the tab, with songs */
    static char text[64 * 1024];
    CHECK(sdk_music_parse(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
    long k = sdk_music_text(&music, 1, text, sizeof text - 200);
    snprintf(text + k, sizeof text - (size_t)k, "byte v[300];\nvoid start(void) { play_song(song_one); }\n"
             "void update(void) { if (pressed(A)) play_sfx(sfx_hit); if (pressed(B)) stop_music(); }\n");
    CHECK(sdk_cc_compile(text, strlen(text), 0, &c, err, sizeof err) == 0);
    if (c.text && load(c.text, c.len) == 0) {
        frames(14);
        CHECK(ram(M_ON) == 1);
        for (int i = 0; i < 3; i++)
            nes_frame(NES_PAD_B, 0, 0);
        frames(2);
        CHECK(ram(M_ON) == 0);
    } else
        printf("%s\n", err);
    sdk_cc_free(&c);
    const char *bad = "//@music\nconst byte lead[] = { PULSE, 0, 0, 0, 99, END };\n//@end\nvoid main(void) {}\n";
    CHECK(sdk_cc_compile(bad, strlen(bad), 0, &c, err, sizeof err) != 0 &&
          strstr(err, "main.c:2: music: a volume is 0 to 15"));
    bad = "void main(void) { play_sfx(sfx_none); }\n";
    CHECK(sdk_cc_compile(bad, strlen(bad), 0, &c, err, sizeof err) != 0 && strstr(err, "sfx_none"));
    CHECK(sdk_cc_compile(bad, strlen(bad), 1, &c, err, sizeof err) != 0);
}

int main(void)
{
    L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dostring(L, driver) != LUA_OK) {
        printf("%s\n", lua_tostring(L, -1));
        return 1;
    }
    test_read();
    test_write();
    test_edit();
    test_mistakes();
    test_driver();
    test_sound();
    test_templates();
    printf("test_shell_sdk_music: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
