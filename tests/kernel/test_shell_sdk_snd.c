/*
 * Host test for the sound of the SNES projects of the Shell SDK (shell_sdk_spc.c, the driver shell_sdk_spc.asm and
 * the SNES music block of shell_sdk_music.c): the block read and written, the programs in bm C and in assembly
 * assembled by core/src/script/snes_asm.lua and run in the SNES core, where the test reads the registers of the sound
 * chip and the samples it makes.
 *   make test-sdk-snd      (run from the top of the repository)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell_sdk_cc.h"
#include "kernel/shell_sdk_music.h"
#include "kernel/shell_sdk_play.h"
#include "kernel/shell_sdk_proj.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "snes_int.h"

uint8_t *spc_dsp_ptr(void);

/* What the listening of the tab (shell_sdk_play.c) takes from the kernel. A SNES music needs no NES program. */
static uint32_t ticks;
static int (*output)(float *, unsigned, float);
uint32_t timer_ticks(void) { return ticks; }
void audio_set_ext(int (*mix)(float *, unsigned, float)) { output = mix; }
int audio_ready(void) { return 1; }
int audio_init(void) { return 0; }
void sdk_proj_default(sdk_proj_t *p, int target) { memset(p, 0, sizeof *p); (void)target; }
int sdk_build_plain(const sdk_proj_t *p, const char *name, const char *source, size_t len, uint8_t **rom,
                    size_t *rom_len, char *err, size_t en)
{
    (void)p, (void)name, (void)source, (void)len, (void)rom, (void)rom_len, (void)err, (void)en;
    return -1;
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static lua_State *L;
static sdk_music_t music, again;

static const char driver[] =
    "local A = assert(loadfile('core/src/script/snes_asm.lua'))()\n"
    "return function(src)\n"
    "  local res = A.assemble(src, { map = 'lorom', name = 'main.s' })\n"
    "  if not res.ok then return nil, A.format_errors(res) end\n"
    "  local rom, info = A.build_rom(res, { title = 'TEST', map = 'lorom', min_kb = 32, chipset = 0 })\n"
    "  if not rom then return nil, info end\n"
    "  return rom\n"
    "end\n";

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
    if (snes_load(rom, n, &why)) {
        printf("load: %s\n", why ? why : "?");
        fails++;
        return -1;
    }
    return 0;
}

static float loudest;                   /* of the samples since the last quiet() */

static void frames(int n, uint16_t pad)
{
    static float out[2 * 800];
    for (int f = 0; f < n; f++) {
        snes_frame(pad, 0, 1);
        memset(out, 0, sizeof out);
        snes_audio_mix(out, 800, 1.0f);
        for (int i = 0; i < 1600; i++) {
            const float v = out[i] < 0 ? -out[i] : out[i];
            loudest = v > loudest ? v : loudest;
        }
    }
}

/* A register of the voice v of the sound chip. */
static int voice(int v, int reg) { return spc_dsp_ptr()[v * 16 + reg]; }
static int pitch(int v) { return voice(v, 2) | voice(v, 3) << 8; }

static const char *const sample =
    ";@music\n"
    "lead:\n"
    "    .byte SQUARE25, 0\n"
    "    .byte 15, 7, 7, 0\n"
    "    .byte 100, 8, 0, 0, END\n"
    "bass: .byte TRIANGLE,0, 10,5,6,18, 80,2,0,$00, END\n"
    "drum: .byte NOISE,24, 15,7,3,26, 90,12,0,0, END\n"
    "pattern00:\n"
    "    .byte C4,LEAD, __,0, C2,BASS, __,0, __,0, __,0, __,0, __,0\n"
    "    .byte __,0, E4,LEAD, __,0, A4,DRUM, __,0, __,0, __,0, __,0\n"
    "    .byte OFF,0, __,0, __,0, __,0, __,0, __,0, __,0, __,0\n"
    "    .byte A4,0, __,0, __,0, __,0, __,0, __,0, __,0, B7,LEAD\n"
    "    .byte END\n"
    "sfx_beep:\n"
    "    .byte 4, V6, SINE\n"
    "    .byte C5,15,__,  __,10,UP+8,  OFF,0,__\n"
    "    .byte END\n"
    "sfx_hit:\n"
    "    .byte 2, V7, NOISE\n"
    "    .byte N9,15,__,  N3,7,__\n"
    "    .byte END\n"
    "song_one:\n"
    "    .byte 12, 0\n"
    "    .byte 0, END\n"
    ";@end\n";

static void test_block(void)
{
    static char text[32 * 1024], twice[32 * 1024];
    char err[160] = "";
    CHECK(sdk_music_parse_snes(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
    if (err[0])
        printf("%s\n", err);
    CHECK(music.snes && music.instruments == 3 && music.sfxs == 2 && music.songs == 1 && music.pat[0].steps == 4);
    CHECK(music.ins[1].wave == 3 && music.ins[1].attack == 10 && music.ins[1].rate == 18 && music.ins[1].pan == 2);
    CHECK(music.ins[2].wave == SDK_MUS_WAVES && music.ins[2].noise == 24);
    CHECK(music.pat[0].cell[1][3].note == 69 && music.pat[0].cell[1][3].ins == 3 && music.pat[0].cell[3][7].note == 107);
    CHECK(music.sfx[0].channel == 5 && music.sfx[0].wave == 5 && music.sfx[1].wave == SDK_MUS_WAVES &&
          music.sfx[1].step[0].note == SDK_MUS_LOW + 9);
    for (int c = 0; c < 2; c++) {
        CHECK(sdk_music_parse_snes(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
        const long n = sdk_music_text(&music, c, text, sizeof text);
        CHECK(n > 0 && sdk_music_parse_snes(text, (size_t)n, c, &again, err, sizeof err) == 0);
        if (err[0])
            printf("%s\n%s\n", err, text);
        CHECK(sdk_music_text(&again, c, twice, sizeof twice) == n && !strcmp(text, twice));
        for (const char *s = text; *s; s = strchr(s, '\n') + 1)
            CHECK(strchr(s, '\n') - s <= 80);
    }
    static uint8_t image[0x8000];
    const int n = sdk_spc_image(&music, image, sizeof image);
    CHECK(n > 0x900 && n < 0x1400 && image[0x800] == 1 && image[0x801] == 2);
    CHECK(sdk_spc_image(&music, image, 0x900) == -1);
    const char *bad = ";@music\nx: .byte PULSE,0,0,0, END\n;@end\n";
    CHECK(sdk_music_parse_snes(bad, strlen(bad), 0, &music, err, sizeof err) != 0);
    bad = ";@music\nsfx_a: .byte 2, PU1, SINE, END\n;@end\n";
    CHECK(sdk_music_parse_snes(bad, strlen(bad), 0, &music, err, sizeof err) != 0 && strstr(err, "V1 to V8"));
}

/* A program that sends the sound, then gives the order that a button asks for: A song_one, B sfx_beep, X sfx_hit,
 * Y stop_music. */
static const char *const player =
    ".lorom\n.org $8000\n"
    "reset:\n"
    "    sei\n    clc\n    xce\n    rep #$10\n    sep #$20\n    .a8\n    .i16\n"
    "    ldx #$1FFF\n    txs\n    lda #$8F\n    sta $2100\n    stz $0F\n    stz $10\n"
    "    jsr snd_init\n"
    "    lda #$81\n    sta $4200\n"
    "loop:\n"
    "    lda $10\n"
    "@frame: cmp $10\n    beq @frame\n"
    "@busy: lda $4212\n    and #1\n    bne @busy\n"
    "    lda $4219              ; B Y Select Start Up Down Left Right\n"
    "    sta $11\n"
    "    lda $4218              ; A X L R\n"
    "    sta $12\n"
    "    bit #$80\n    beq @not_a\n    lda #song_one\n    jsr play_song\n"
    "@not_a:\n"
    "    lda $11\n    bit #$80\n    beq @not_b\n    lda #sfx_beep\n    jsr play_sfx\n"
    "@not_b:\n"
    "    lda $12\n    bit #$40\n    beq @not_x\n    lda #sfx_hit\n    jsr play_sfx\n"
    "@not_x:\n"
    "    lda $11\n    bit #$40\n    beq loop\n    jsr stop_music\n    jmp loop\n"
    "nmi:\n    sep #$20\n    pha\n    lda $4210\n    inc $10\n    pla\n    rti\n"
    "irq:\n    rti\n";

static void test_driver(void)
{
    static char src[64 * 1024];
    const int n = snprintf(src, sizeof src, "%s%s", player, sample);
    sdk_music_src_t s;
    char err[160] = "";
    CHECK(sdk_music_expand_snes(src, (size_t)n, &s, err, sizeof err) == 0 && s.text);
    if (!s.text || load(s.text, s.len) != 0) {
        printf("%s\n", err);
        return;
    }
    free(s.text);
    loudest = 0;
    frames(40, 0);                              /* the upload */
    snes_debug_t d;
    snes_debug(&d);
    CHECK(d.spc_running && !d.spc_halted);
    CHECK(spc_dsp_ptr()[0x0C] == 127 && spc_dsp_ptr()[0x5D] != 0);
    CHECK(loudest < 0.001f);
    /* the effect beep on the voice 6 (number 5): its sample, its pitch, the slide, the release */
    frames(1, SNES_PAD_B);
    frames(3, 0);
    CHECK(voice(5, 4) == 5 && voice(5, 0) == 120 && pitch(5) > 2000 && pitch(5) < 2300);    /* C5: 523 Hz */
    CHECK(loudest > 0.02f);
    frames(4, 0);
    CHECK(pitch(5) > 2300 && voice(5, 0) == 80);
    frames(8, 0);
    CHECK(spc_dsp_ptr()[0x5C] & 0x20);
    /* the noise effect on the voice 7 */
    frames(1, SNES_PAD_X);
    frames(1, 0);
    frames(1, 0);
    if (!((spc_dsp_ptr()[0x3D] & 0x40) && ((spc_dsp_ptr()[0x6C] & 0x1F) == 19 || (spc_dsp_ptr()[0x6C] & 0x1F) == 7)))
        printf("noise: NON %02X FLG %02X\n", spc_dsp_ptr()[0x3D], spc_dsp_ptr()[0x6C]), fails++;
    frames(20, 0);
    /* the song: the voices of the first row, then of the others */
    loudest = 0;
    frames(1, SNES_PAD_A);
    frames(5, 0);
    CHECK(voice(0, 4) == 1 && voice(0, 5) == 0xFF && voice(0, 0) == 100 && voice(0, 1) == 100);
    CHECK(pitch(0) >= 1070 && pitch(0) <= 1074);                        /* C4: 261.6 Hz */
    CHECK(voice(2, 4) == 3 && voice(2, 5) == 0xDA && voice(2, 6) == 0xD2 && voice(2, 0) == 80 && voice(2, 1) == 20);
    CHECK(loudest > 0.02f);
    frames(12, 0);                              /* row 2: E4 and a drum */
    CHECK(pitch(1) > 1340 && pitch(1) < 1360 && (spc_dsp_ptr()[0x3D] & 0x08));
    frames(12, 0);                              /* row 3: voice 1 released */
    CHECK(spc_dsp_ptr()[0x5C] & 1);
    frames(12, 0);                              /* row 4: A4 again on voice 1, B7 on voice 8 */
    CHECK(!(spc_dsp_ptr()[0x5C] & 1) && pitch(0) >= 1798 && pitch(0) <= 1806 && pitch(7) == 16183);
    frames(24, 0);                              /* round again */
    CHECK(pitch(0) >= 1070 && pitch(0) <= 1074);
    loudest = 0;
    frames(1, SNES_PAD_Y);
    frames(20, 0);
    loudest = 0;
    frames(20, 0);
    CHECK((spc_dsp_ptr()[0x5C] & 0x07) == 0x07 && loudest < 0.01f);
}

/* n frames of listening: the ticks of the tab, then what the output would take. */
static void listen(int n)
{
    static float out[2 * 800];
    for (int f = 0; f < n; f++) {
        ticks += 16639;
        sdk_play_tick();
        memset(out, 0, sizeof out);
        if (output)
            output(out, 800, 1.0f);
        for (int i = 0; i < 1600; i++) {
            const float v = out[i] < 0 ? -out[i] : out[i];
            loudest = v > loudest ? v : loudest;
        }
    }
}

static void test_listening(void)
{
    char err[160] = "";
    int order, row;
    CHECK(sdk_music_parse_snes(sample, strlen(sample), 0, &music, err, sizeof err) == 0);
    loudest = 0;
    CHECK(sdk_play_start(&music, SDK_PLAY_SONG, 0, 0, 0, err, sizeof err) == 0);
    if (err[0])
        printf("%s\n", err);
    CHECK(sdk_play_what() == SDK_PLAY_SONG && output);
    listen(6);
    sdk_play_position(&order, &row);
    CHECK(order == 0 && row == 0 && voice(0, 4) == 1 && voice(0, 0) == 100 && loudest > 0.02f);
    CHECK(pitch(0) >= 1070 && pitch(0) <= 1074);
    listen(12);
    sdk_play_position(&order, &row);
    CHECK(row == 1 && pitch(1) > 1340 && pitch(1) < 1360);
    listen(36);                                 /* round again */
    sdk_play_position(&order, &row);
    CHECK(row == 0 && sdk_play_what() == SDK_PLAY_SONG);
    sdk_play_stop();
    CHECK(sdk_play_what() == SDK_PLAY_NOTHING && !output);
    /* a pattern, at the speed asked */
    CHECK(sdk_play_start(&music, SDK_PLAY_PATTERN, 0, 0, 4, err, sizeof err) == 0);
    listen(7);
    sdk_play_position(&order, &row);
    CHECK(row == 1);
    sdk_play_stop();
    /* an effect ends by itself */
    loudest = 0;
    CHECK(sdk_play_start(&music, SDK_PLAY_SFX, 0, 0, 0, err, sizeof err) == 0);
    listen(4);
    CHECK(voice(5, 4) == 5 && pitch(5) > 2000 && pitch(5) < 2300 && loudest > 0.02f);
    listen(60);
    CHECK(sdk_play_what() == SDK_PLAY_NOTHING);
    /* a note of an instrument: it sounds, is released, ends; the next one needs no new start */
    loudest = 0;
    CHECK(sdk_play_start(&music, SDK_PLAY_NOTE, 1, 69, 0, err, sizeof err) == 0);
    listen(4);
    CHECK(voice(0, 4) == 3 && pitch(0) >= 1798 && pitch(0) <= 1806 && loudest > 0.02f);
    listen(30);
    CHECK(spc_dsp_ptr()[0x5C] & 1);
    listen(80);
    CHECK(sdk_play_what() == SDK_PLAY_NOTHING);
    loudest = 0;
    CHECK(sdk_play_start(&music, SDK_PLAY_NOTE, 0, 60, 0, err, sizeof err) == 0);
    listen(4);
    CHECK(voice(0, 4) == 1 && pitch(0) >= 1070 && pitch(0) <= 1074 && loudest > 0.02f);
    sdk_play_stop();
    /* a new instrument of the tab has what a SNES one needs */
    const int added = sdk_music_add_ins(&music, 2);
    CHECK(added == 3 && !strcmp(music.ins[added].name, "drum2") && music.ins[added].wave == SDK_MUS_WAVES &&
          music.ins[added].channel == 0 && music.ins[added].pan == 8);
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

/* The start files: A plays the effect. */
static void test_templates(void)
{
    char err[200] = "";
    for (int c = 0; c < 2; c++) {
        size_t n;
        const char *src = slurp(c ? "core/src/script/sdk_tpl_snes_c.txt" : "core/src/script/sdk_tpl_snes.s", &n);
        CHECK(sdk_music_parse_snes(src, n, c, &music, err, sizeof err) == 0 && music.sfxs == 1);
        sdk_music_src_t s = { 0 };
        sdk_cc_out_t out = { 0 };
        if (c)
            CHECK(sdk_cc_compile(src, n, 1, &out, err, sizeof err) == 0);
        else
            CHECK(sdk_music_expand_snes(src, n, &s, err, sizeof err) == 0 && s.text);
        if ((c ? out.text : s.text) && load(c ? out.text : s.text, c ? out.len : s.len) == 0) {
            frames(40, 0);
            loudest = 0;
            frames(10, 0);
            CHECK(loudest < 0.001f);
            frames(1, SNES_PAD_A);
            frames(8, 0);
            CHECK(voice(7, 4) == 2 && pitch(7) > 1000 && loudest > 0.02f);
        } else
            printf("%s: %s\n", c ? "bm C" : "assembly", err);
        free(s.text);
        sdk_cc_free(&out);
    }
    /* a program with no sound has none of it */
    sdk_cc_out_t out = { 0 };
    const char *quiet = "void update(void) { }\n";
    CHECK(sdk_cc_compile(quiet, strlen(quiet), 1, &out, err, sizeof err) == 0 && !strstr(out.text, "snd_init"));
    sdk_cc_free(&out);
}

int main(void)
{
    L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dostring(L, driver) != LUA_OK) {
        printf("%s\n", lua_tostring(L, -1));
        return 1;
    }
    test_block();
    test_driver();
    test_templates();
    test_listening();
    printf("test_shell_sdk_snd: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
