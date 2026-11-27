/*
 * Host test for bm C, the C of the NES and SNES projects of the Shell SDK (core/src/kernel/shell_sdk_cc*.c):
 * each program is compiled, assembled by core/src/script/snes_asm.lua and run in the NES or the SNES core; the
 * checks read its variables in the RAM, the sprites and the colours. Also the messages of the mistakes.
 *   make test-sdk-cc      (run from the top of the repository)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell_sdk_cc.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "nes.h"
#include "snes_int.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static lua_State *L;
static sdk_cc_out_t out;
static int snes;                        /* the machine of the test */

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

/* Compiles, assembles and runs `frames` frames with no button. 0 when all went well. */
static int run(const char *src, int frames)
{
    char err[256];
    sdk_cc_free(&out);
    if (sdk_cc_compile(src, strlen(src), snes, &out, err, sizeof err) != 0) {
        printf("compile: %s\n%s\n", err, src);
        fails++;
        return -1;
    }
    lua_settop(L, 1);
    lua_pushvalue(L, 1);
    lua_pushlstring(L, out.text, out.len);
    lua_pushboolean(L, snes);
    if (lua_pcall(L, 2, 2, 0) != LUA_OK || !lua_isstring(L, 2)) {
        const char *why = lua_tostring(L, -1);
        printf("assemble: %.600s\n%s\n", why ? why : "?", src);
        const int line = why ? atoi(strchr(why, ':') ? strchr(why, ':') + 1 : "0") : 0;
        const char *p = out.text;
        for (int n = 1; *p && n <= line + 2; n++, p = strchr(p, '\n') + 1)
            if (n >= line - 4)
                printf("%5d %.*s\n", n, (int)(strchr(p, '\n') - p), p);
        fails++;
        return -1;
    }
    size_t len;
    const uint8_t *rom = (const uint8_t *)lua_tolstring(L, 2, &len);
    const char *why = NULL;
    if (snes ? snes_load(rom, len, &why) : nes_load(rom, len, &why)) {
        printf("load: %s\n", why ? why : "?");
        fails++;
        return -1;
    }
    if (!snes)
        nes_reset();
    for (int i = 0; i < frames; i++)
        if (snes)
            snes_frame(0, 0, 1);
        else
            nes_frame(0, 0, 1);
    return 0;
}

static void frames(int n, uint16_t pad)
{
    for (int i = 0; i < n; i++)
        if (snes)
            snes_frame(pad, 0, 1);
        else
            nes_frame((uint8_t)pad, 0, 1);
}

/* The address the assembly gives to a label ("v_x"). */
static int address(const char *label)
{
    char key[128];
    snprintf(key, sizeof key, "\n%s ", label);
    const char *p = out.text ? strstr(out.text, key) : NULL;
    p = p ? strchr(p, '$') : NULL;
    return p ? (int)strtol(p + 1, NULL, 16) : -1;
}

static int byte(const char *label, int i)
{
    const int a = address(label);
    return a < 0 ? -999999 : snes ? S.wram[(a + i) & 0x1FFF] : nes_ram()[(a + i) & 0x7FF];
}

static int word(const char *label, int i)
{
    return byte(label, 2 * i) | byte(label, 2 * i + 1) << 8;
}

/* The numbers a program leaves in r[]: compared with what C would give. */
static void check_words(int line, const char *name, const int *want, int n)
{
    for (int i = 0; i < n; i++)
        if (word(name, i) != (want[i] & 0xFFFF)) {
            printf("FAIL line %d: %s[%d] is %d, not %d\n", line, name, i, word(name, i), want[i] & 0xFFFF);
            fails++;
        }
}
#define WORDS(name, ...) do { const int w_[] = { __VA_ARGS__ }; \
    check_words(__LINE__, name, w_, (int)(sizeof w_ / sizeof w_[0])); } while (0)

static void test_arithmetic(void)
{
    if (run("word r[24];\n"
            "byte a = 200; byte b = 100; word w = 1000; word v = 7;\n"
            "sbyte s = -5; int n = -1234;\n"
            "void main(void) {\n"
            "  r[0] = a + b;\n"                      /* the destination is a word: 16 bits */
            "  byte c = a + b; r[1] = c;\n"          /* a byte: it wraps */
            "  r[2] = w + a; r[3] = w - 2000; r[4] = a - b;\n"
            "  r[5] = w * v; r[6] = w / v; r[7] = w % v;\n"
            "  r[8] = n / v; r[9] = n % v; r[10] = s * 3;\n"
            "  r[11] = a & 0x0F; r[12] = a | b; r[13] = a ^ 0xFF;\n"
            "  r[14] = w << 3; r[15] = w >> 3; r[16] = n >> 2;\n"
            "  byte k = 5; r[17] = w << k; r[18] = w >> k; r[19] = s >> k;\n"
            "  r[20] = -w; r[21] = ~a; r[22] = (a + b) / 2; r[23] = a * 16 + w / 8 + w % 16;\n"
            "}\n", 8) == 0)
        WORDS("v_r", 300, 44, 1200, -1000, 100, 7000, 142, 6, -176, -2, -15, 8, 236, 55, 8000, 125, -309, 32000,
              31, -1, -1000, -201, 150, 3333);
}

static void test_compare(void)
{
    if (run("word r[20];\n"
            "byte a = 200; byte b = 100; sbyte s = -5; sbyte t = 3; word w = 40000; word v = 300;\n"
            "int m = -2; int n = 2;\n"
            "void main(void) {\n"
            "  r[0] = a > b; r[1] = a < b; r[2] = a >= 200; r[3] = a <= 199; r[4] = a == 200; r[5] = a != 200;\n"
            "  r[6] = s < t; r[7] = s > t; r[8] = s < 0; r[9] = s < a; r[10] = w > v; r[11] = v >= w;\n"
            "  r[12] = m < n; r[13] = n <= m; r[14] = m == -2; r[15] = w == 40000;\n"
            "  r[16] = a > b && s < 0; r[17] = a < b || t == 3; r[18] = !(a == 200); r[19] = a > b ? 11 : 22;\n"
            "}\n", 8) == 0)
        WORDS("v_r", 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 11);
}

static void test_flow(void)
{
    if (run("#define LAST 10\n"
            "const byte SIZE = 4;\n"
            "word r[12];\n"
            "byte list[SIZE] = { 3, 1, 4, 1 };\n"
            "const word table[] = { 100, 2000, 30000 };\n"
            "word sum(byte n) { word t = 0; for (byte i = 1; i <= n; i++) t += i; return t; }\n"
            "word twice(word x) { return x * 2; }\n"
            "byte add3(byte x, byte y, byte z) { return x + y + z; }\n"
            "void main(void) {\n"
            "  r[0] = sum(LAST);\n"
            "  r[1] = twice(twice(21));\n"
            "  r[2] = add3(1, add3(2, 3, 4), add3(5, 6, 7));\n"
            "  byte i = 0; while (1) { i++; if (i == 3) continue; if (i > 6) break; r[3] += i; }\n"
            "  i = 0; do { i += 2; } while (i < 9); r[4] = i;\n"
            "  for (i = 0; i < SIZE; i++) r[5] += list[i];\n"
            "  r[6] = table[2] + table[0]; i = 1; r[7] = table[i];\n"
            "  i = 2; list[i] = 9; list[i] += 5; list[i]++; r[8] = list[2];\n"
            "  i = 5; r[9] = i++; r[9] += ++i;\n"
            "  word big = 65535; big++; r[10] = big; big--; r[11] = big;\n"
            "}\n", 20) == 0)
        WORDS("v_r", 55, 84, 28, 18, 10, 9, 30100, 2000, 15, 12, 0, 65535);
}

static void test_pointers(void)
{
    if (run("word r[10];\n"
            "byte bytes[5] = { 10, 20, 30, 40, 50 };\n"
            "word words[300];\n"
            "const byte rom[] = { 7, 8, 9 };\n"
            "void bump(byte *p, byte n) { while (n--) { *p += 1; p++; } }\n"
            "void main(void) {\n"
            "  byte *p = bytes; r[0] = *p; p += 2; r[1] = *p; r[2] = p[1]; p++; r[3] = *p;\n"
            "  bump(bytes, 3); r[4] = bytes[0] + bytes[1] + bytes[2] + bytes[3];\n"
            "  word i = 299; words[i] = 1234; words[3] = 5; i = 3; words[i] += 7; r[5] = words[299]; r[6] = words[3];\n"
            "  word *q = &words[i]; *q = 4000; r[7] = words[3];\n"
            "  byte x = 5; byte *px = &x; *px = 77; r[8] = x;\n"
            "  const byte *c = rom; r[9] = c[2] + *c;\n"
            "}\n", 12) == 0)
        WORDS("v_r", 10, 30, 40, 40, 103, 1234, 12, 4000, 77, 16);
}

static void test_machine(void)
{
    if (run("byte x = 100; byte frames; byte press; byte held_a; byte seen[4]; byte code = 3;\n"
            "void start(void) {\n"
            "  background(BLUE);\n"
            "  sprite_palette(1, WHITE, RED, BLACK);\n"
            "  background_palette(2, SKY, NAVY, PINK);\n"
            "  sprite(0, x, 80, 5); sprite_style(0, 1 | FLIP_X);\n"
            "  sprite(3, 10, 20, 2); hide_sprite(3);\n"
            "  poke(0x7F0, 42); seen[0] = peek(0x7F0);\n"
            "  seen[1] = random(); seen[2] = random();\n"
            "  asm(\"lda {code}\", \"asl a\\n sta {seen}+3\");\n"
            "}\n"
            "void update(void) {\n"
            "  frames++;\n"
            "  if (held(RIGHT)) x++;\n"
            "  if (pressed(A)) press++;\n"
            "  held_a = held(A) != 0;\n"
            "  sprite(0, x, 80, 5);\n"
            "}\n", 20) != 0)
        return;
    CHECK(nes_palette()[0] == 0x12 && nes_palette()[17 + 4] == 0x30 && nes_palette()[18 + 4] == 0x16);
    CHECK(nes_palette()[9] == 0x21 && nes_palette()[11] == 0x25);
    CHECK(nes_oam()[0] == 79 && nes_oam()[1] == 5 && nes_oam()[2] == 0x41 && nes_oam()[3] == 100);
    CHECK(nes_oam()[12] == 0xFF);
    CHECK(byte("v_frames", 0) >= 14 && byte("v_frames", 0) <= 20);
    CHECK(byte("v_seen", 0) == 42 && byte("v_seen", 1) != byte("v_seen", 2) && byte("v_seen", 3) == 6);
    frames(10, NES_PAD_RIGHT);
    CHECK(byte("v_x", 0) >= 108 && byte("v_x", 0) <= 111);
    CHECK(byte("v_x", 0) - nes_oam()[3] <= 1);          /* the sprites go to the screen at the next frame */
    frames(5, NES_PAD_A);
    CHECK(byte("v_press", 0) == 1 && byte("v_held_a", 0) == 1);
    frames(3, 0);
    frames(3, NES_PAD_A);
    CHECK(byte("v_press", 0) == 2);
    /* the line of C beside its instructions, and the tiles in the CHR ROM */
    CHECK(strstr(out.text, "; main.c:3  background(BLUE);") && strstr(out.text, "RAM of the program"));
    CHECK(out.ram >= 8 && out.lines > 100 && out.line_of);
}

/* A source with a mistake: the message has the line and says what to do. */
static void mistake(int line, const char *src, const char *want)
{
    char err[256] = "";
    sdk_cc_out_t o;
    if (sdk_cc_compile(src, strlen(src), snes, &o, err, sizeof err) == 0) {
        printf("FAIL line %d: compiled\n", line);
        fails++;
        sdk_cc_free(&o);
    } else if (!strstr(err, want)) {
        printf("FAIL line %d: \"%s\" has no \"%s\"\n", line, err, want);
        fails++;
    }
}
#define MISTAKE(src, want) mistake(__LINE__, src, want)

/* The same names on the SNES: colours of 16 bits, 128 sprites, a pad of 12 buttons. */
static void test_machine_snes(void)
{
    if (run("#define PINKISH rgb(31, 16, 20)\n"
            "word x = 300; byte beats; byte press; byte held_a; byte seen[4]; byte code = 3; word buttons;\n"
            "void start(void) {\n"
            "  background(rgb(0, 0, 31));\n"
            "  sprite_palette(1, WHITE, RED, BLACK);\n"
            "  palette(200, PINKISH); palette(5, 0x1234);\n"
            "  sprite(0, x, 80, 0x105); sprite_style(0, 3 | FLIP_X);\n"
            "  sprite(70, 10, 20, 2); sprite_big(70, 1);\n"
            "  sprite(3, 10, 20, 2); hide_sprite(3);\n"
            "  sprite(127, 5, 6, 7); sprite_style(127, BEHIND | 2);\n"
            "  poke(0x1000, 42); seen[0] = peek(0x1000);\n"
            "  seen[1] = random(); seen[2] = random();\n"
            "  asm(\"lda {code}\", \"asl a\\n sta {seen}+3\");\n"
            "}\n"
            "void update(void) {\n"
            "  beats++;\n"
            "  if (held(RIGHT)) x++;\n"
            "  held_a = held(A | START);\n"
            "  if (pressed(A)) press++;\n"
            "  buttons = pad;\n"
            "  sprite(0, x, 80, 0x105);\n"
            "}\n", 20) != 0)
        return;
    const uint8_t *c = S.cgram, *o = S.oam;
    CHECK(c[0] == 0x00 && c[1] == 0x7C && c[290] == 0xFF && c[291] == 0x7F && c[292] == 0x9F && c[293] == 0x10);
    CHECK(c[400] == 0x1F && c[401] == 0x52 && c[10] == 0x34 && c[11] == 0x12);
    CHECK(o[0] == 44 && o[1] == 79 && o[2] == 0x05 && o[3] == 0x77 && (o[512] & 3) == 1);
    CHECK(o[280] == 10 && o[281] == 19 && o[282] == 2 && (o[512 + 17] & 0x30) == 0x20);
    CHECK(o[13] == 0xF0 && o[5] == 0xF0 && o[508] == 5 && o[509] == 5 && o[511] == 0x04);
    CHECK(byte("v_beats", 0) >= 14 && byte("v_beats", 0) <= 20);
    CHECK(byte("v_seen", 0) == 42 && byte("v_seen", 1) != byte("v_seen", 2) && byte("v_seen", 3) == 6);
    CHECK(byte("v_held_a", 0) == 0);
    frames(10, SNES_PAD_RIGHT);
    CHECK(word("v_x", 0) >= 308 && word("v_x", 0) <= 311 && (word("v_x", 0) & 0xFF) - o[0] <= 1);
    frames(5, SNES_PAD_A);
    CHECK(byte("v_press", 0) == 1 && byte("v_held_a", 0) != 0 && word("v_buttons", 0) == SNES_PAD_A);
    frames(3, SNES_PAD_START);
    CHECK(byte("v_held_a", 0) != 0 && byte("v_press", 0) == 1);
    frames(3, SNES_PAD_A);
    CHECK(byte("v_press", 0) == 2);
    CHECK(out.ram_size == 8192 && strstr(out.text, "__upload_tiles:") && strstr(out.text, "; main.c:4  background"));

    if (run("void update(void) { }\n"
            "tiles {\n  blank(1),\n  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 1,2,3,4,5,6,7,8,\n"
            "  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,9,\n  blank(2),\n}\n", 4) == 0)
        CHECK(S.vram[31] == 0 && S.vram[32] == 0xFF && S.vram[40] == 1 && S.vram[63] == 9 && S.vram[64] == 0);
    mistake(__LINE__, "void update(void) { }\ntiles { 1, 2, 3 }\n", "a tile is 32");
    MISTAKE("void update(void) { background_palette(0, 1, 2, 3); }", "use palette(index, colour)");
    MISTAKE("void update(void) { background(rgb(40, 0, 0)); }", "0 to 31");
    MISTAKE("void update(void) { background(rgb(pad, 0, 0)); }", "known when the program is built");
    MISTAKE("byte big[7000];\nvoid update(void) { }", "1 to 2048");
}

static void test_tiles(void)
{
    char err[200];
    const char *src = "void update(void) { }\n"
                      "tiles {\n  blank(1),\n  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0,0,0,0,0,0,0,0,\n"
                      "  blank(2),\n}\n";
    CHECK(run(src, 2) == 0 && strstr(out.text, ".chr") && strstr(out.text, ".res 32,0"));
    const char *bad = "void update(void) { }\ntiles { 1, 2, 3 }\n";
    CHECK(sdk_cc_compile(bad, strlen(bad), 0, &out, err, sizeof err) == -1 && strstr(err, "a tile is 16"));
}

static void test_mistakes(void)
{
    MISTAKE("byte x;\nvoid update(void) {\n  y = 1;\n}", "main.c:3: 'y' is not declared. Did you mean 'x'?");
    MISTAKE("byte x;\nvoid update(void) {\n  x = 1\n}", "main.c:3: add ';'");
    MISTAKE("void update(void) { byte x = 300; }", "does not fit in 'x'");
    MISTAKE("void update(void) { sprit(0,0,0,0); }", "Did you mean 'sprite'?");
    MISTAKE("void update(void) { sprite(0, 1, 2); }", "sprite(n, x, y, tile) takes 4 arguments, not 3");
    MISTAKE("void f(void) { update(); }\nvoid update(void) { f(); }", "cannot call itself");
    MISTAKE("float x;\nvoid update(void) { }", "the NES has no fractions");
    MISTAKE("struct s { };\nvoid update(void) { }", "one array for each field");
    MISTAKE("byte x;", "add void update(void)");
    MISTAKE("void update(void) { }\nvoid main(void) { }", "not both");
    MISTAKE("const byte t[] = {1};\nvoid update(void) { t[0] = 2; }", "is const");
    MISTAKE("byte a[4];\nvoid update(void) { a[4] = 0; }", "index 4 is outside 'a' (0 to 3)");
    MISTAKE("void update(void) { if (pad = 1) { } }", "write '=='");
    MISTAKE("void update(void) { break; }", "only inside");
    MISTAKE("void update(void) {", "never closed");
    MISTAKE("byte A;\nvoid update(void) { }", "is a name of bm C");
    MISTAKE("void update(void) { frame = 0; }", "cannot change");
    MISTAKE("byte *p;\nvoid update(void) { byte x = p; }", "declare a pointer");
    MISTAKE("int f(void) { return; }\nvoid update(void) { }", "write return value;");
    MISTAKE("#include <nes.h>\nvoid update(void) { }", "needs no #include");
    MISTAKE("void update(void) { switch (pad) { } }", "write if, else if");
    MISTAKE("byte x = 1.5;\nvoid update(void) { }", "not whole");
    MISTAKE("byte big[2000];\nvoid update(void) { }", "no room for 'big'");
    MISTAKE("void update(void) { asm(\"lda {nope}\"); }", "'nope' is not declared");
}

/* The start file of a bm C project: a blue screen, a ball of the sheet that the pad moves. */
static void test_template(void)
{
    static char src[16384];
    FILE *f = fopen(snes ? "core/src/script/sdk_tpl_snes_c.txt" : "core/src/script/sdk_tpl_nes_c.txt", "rb");
    const size_t n = f ? fread(src, 1, sizeof src - 1, f) : 0;
    if (f)
        fclose(f);
    src[n] = 0;
    CHECK(n > 500 && strstr(src, "//@sprites") && strstr(src, "//@music") && strstr(src, "//@map"));
    if (run(src, 10) != 0)
        return;
    if (snes) {
        CHECK(S.cgram[0] == 0x44 && S.cgram[1] == 0x71 && S.cgram[258] == 0xFF && S.cgram[259] == 0x7F);
        CHECK(S.oam[0] == 124 && S.oam[1] == 107 && S.oam[2] == 1);
        frames(10, SNES_PAD_RIGHT);
        CHECK(S.oam[0] >= 140);
        int lines = 0;
        const uint16_t *now = snes_picture(&lines);
        CHECK(now && lines > 200 && now[256 * 111 + S.oam[0] + 4] != now[256 * 30 + 30] &&
              (now[256 * 30 + 30] & 0x1F) > (now[256 * 30 + 30] >> 11));
        return;
    }
    CHECK(nes_palette()[0] == 0x12 && nes_palette()[17] == 0x30);
    CHECK(nes_oam()[0] == 111 && nes_oam()[1] == 1 && nes_oam()[3] == 124);
    frames(10, NES_PAD_RIGHT);
    CHECK(nes_oam()[3] >= 132);
    const uint16_t *pic = nes_picture();
    const unsigned back = pic[256 * 30 + 30], ball = pic[256 * 115 + nes_oam()[3] + 4];
    CHECK((back & 0x1F) > (back >> 11) && ball != back);
}

int main(void)
{
    L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dostring(L, driver) != LUA_OK) {
        printf("driver: %s\n", lua_tostring(L, -1));
        return 1;
    }
    for (snes = 0; snes < 2; snes++) {          /* the same programs on the 6502 and on the 65C816 */
        test_arithmetic();
        test_compare();
        test_flow();
        test_pointers();
        test_template();
    }
    snes = 0;
    test_machine();
    test_tiles();
    test_mistakes();
    MISTAKE("void update(void) { sprite_big(0, 1); }", "for the SNES only");
    MISTAKE("void update(void) { background(rgb(1, 2, 3)); }", "for the SNES only");
    snes = 1;
    test_machine_snes();
    sdk_cc_free(&out);
    lua_close(L);
    if (fails) {
        printf("test_shell_sdk_cc: %d FAILED\n", fails);
        return 1;
    }
    puts("test_shell_sdk_cc: OK");
    return 0;
}
