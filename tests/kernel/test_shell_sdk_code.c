/*
 * Host unit test for the Shell SDK Code tab models: the text buffer (shell_sdk_edit.c) and the ROM header
 * reader (shell_sdk_rom.c).
 */
#include <stdio.h>
#include <string.h>

#include "kernel/shell_sdk_edit.h"
#include "kernel/shell_sdk_rom.h"
#include "kernel/shell_sdk_tiles.h"
#include "kernel/shell_sdk_view.h"

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void type(const char *s)
{
    for (; *s; s++)
        sdk_edit_type((unsigned char)*s);
}

static int same(const char *text)
{
    return sdk_edit_length() == strlen(text) && !memcmp(sdk_edit_text(), text, strlen(text));
}

static void test_edit(void)
{
    int line, col;
    const char *s;

    sdk_edit_reset();
    CHECK(sdk_edit_length() == 0 && sdk_edit_line_count() == 1);
    type("lda #1\nsta $2000\n");
    CHECK(same("lda #1\nsta $2000\n") && sdk_edit_line_count() == 3);
    sdk_edit_pos(&line, &col);
    CHECK(line == 2 && col == 0);
    CHECK(sdk_edit_line(1, &s) == 9 && !memcmp(s, "sta $2000", 9));
    CHECK(sdk_edit_line(5, &s) == 0 && s == NULL);

    /* Up and Down keep the column through a short line */
    sdk_edit_reset();
    type("abcdef\nab\nabcdef");
    sdk_edit_pos(&line, &col);
    CHECK(line == 2 && col == 6);
    sdk_edit_up();
    sdk_edit_pos(&line, &col);
    CHECK(line == 1 && col == 2);
    sdk_edit_up();
    sdk_edit_pos(&line, &col);
    CHECK(line == 0 && col == 6);
    sdk_edit_down();
    sdk_edit_down();
    sdk_edit_pos(&line, &col);
    CHECK(line == 2 && col == 6);

    /* Backspace, Delete, Home, End */
    sdk_edit_home();
    sdk_edit_pos(&line, &col);
    CHECK(col == 0);
    CHECK(sdk_edit_backspace() == 1);                      /* joins the two lines */
    CHECK(same("abcdef\nababcdef") && sdk_edit_line_count() == 2);
    CHECK(sdk_edit_delete() == 1 && same("abcdef\nabbcdef"));
    sdk_edit_end();
    sdk_edit_pos(&line, &col);
    CHECK(line == 1 && col == 7);
    CHECK(sdk_edit_delete() == 0);

    /* Tab gives spaces up to the next multiple of 4, other control codes do nothing */
    sdk_edit_reset();
    type("ab\t");
    CHECK(same("ab  "));
    type("c\t");
    CHECK(same("ab  c   "));
    CHECK(sdk_edit_type(7) == 0 && sdk_edit_type(0x1B) == 0 && sdk_edit_type(200) == 0);
    sdk_edit_reset();
    CHECK(sdk_edit_backspace() == 0);
    sdk_edit_left();
    sdk_edit_up();
    sdk_edit_down();
    sdk_edit_pos(&line, &col);
    CHECK(line == 0 && col == 0);

    /* the buffer has a limit */
    sdk_edit_reset();
    for (int i = 0; i < SDK_EDIT_MAX + 10; i++)
        sdk_edit_type('x');
    CHECK(sdk_edit_length() == SDK_EDIT_MAX);
}

static void test_keys(void)
{
    int line, col;
    size_t a, b;

    /* word moves: letters, digits and '_' make a word */
    sdk_edit_reset();
    type("lda #$2000 ; set_x\nrts");
    sdk_edit_doc_start();
    sdk_edit_word_right();
    sdk_edit_pos(&line, &col);
    CHECK(line == 0 && col == 6);                           /* "lda" and the gap " #$" skipped */
    sdk_edit_word_right();
    sdk_edit_pos(&line, &col);
    CHECK(col == 13);                                        /* "2000" and the gap " ; " skipped */
    sdk_edit_end();
    sdk_edit_word_left();
    sdk_edit_pos(&line, &col);
    CHECK(col == 13);                                       /* the start of "set_x" */
    sdk_edit_end();
    sdk_edit_word_right();                                  /* at a line end: the next line */
    sdk_edit_pos(&line, &col);
    CHECK(line == 1 && col == 0);
    sdk_edit_word_left();
    sdk_edit_pos(&line, &col);
    CHECK(line == 0 && col == 18);                          /* the end of the line above */
    sdk_edit_doc_end();
    sdk_edit_pos(&line, &col);
    CHECK(line == 1 && col == 3);

    /* selection: grows with the cursor, ends with anchor(0) */
    sdk_edit_reset();
    type("one two\nthree");
    sdk_edit_doc_start();
    CHECK(!sdk_edit_selection(&a, &b));
    sdk_edit_anchor(1);
    sdk_edit_word_right();
    sdk_edit_anchor(1);                                     /* a second call keeps the first anchor */
    sdk_edit_word_right();
    CHECK(sdk_edit_selection(&a, &b) && a == 0 && b == 7);
    sdk_edit_anchor(1);
    sdk_edit_left();
    CHECK(sdk_edit_selection(&a, &b) && a == 0 && b == 6);
    sdk_edit_anchor(0);
    CHECK(!sdk_edit_selection(&a, &b));
    CHECK(sdk_edit_line_offset(0) == 0 && sdk_edit_line_offset(1) == 8 && sdk_edit_line_offset(9) == 13);

    /* copy, paste, cut of a selection */
    sdk_edit_select_all();
    CHECK(sdk_edit_selection(&a, &b) && a == 0 && b == 13);
    CHECK(sdk_edit_copy(0) == 13 && sdk_edit_clip_length() == 13 && same("one two\nthree"));
    sdk_edit_anchor(0);
    sdk_edit_doc_end();
    CHECK(sdk_edit_paste() == 13 && same("one two\nthreeone two\nthree"));
    sdk_edit_doc_start();
    sdk_edit_anchor(1);
    sdk_edit_word_right();
    CHECK(sdk_edit_copy(1) == 4 && same("two\nthreeone two\nthree"));       /* cut "one " */
    CHECK(!sdk_edit_selection(&a, &b));

    /* typing, Enter, Backspace and Delete act on the selection */
    sdk_edit_reset();
    type("abcdef");
    sdk_edit_home();
    sdk_edit_anchor(1);
    sdk_edit_right();
    sdk_edit_right();
    CHECK(sdk_edit_type('X') == 1 && same("Xcdef"));
    sdk_edit_home();
    sdk_edit_anchor(1);
    sdk_edit_end();
    CHECK(sdk_edit_backspace() == 1 && sdk_edit_length() == 0);
    type("ab");
    sdk_edit_home();
    sdk_edit_anchor(1);
    sdk_edit_right();
    CHECK(sdk_edit_delete() == 1 && same("b"));             /* removes the selection, not one more byte */
    sdk_edit_select_all();
    CHECK(sdk_edit_type('\n') == 1 && same("\n"));

    /* no selection: copy and cut take the line */
    sdk_edit_reset();
    type("aa\nbb\ncc");
    sdk_edit_up();
    CHECK(sdk_edit_copy(0) == 3 && sdk_edit_clip_length() == 3 && same("aa\nbb\ncc"));
    CHECK(sdk_edit_copy(1) == 3 && same("aa\ncc"));
    sdk_edit_pos(&line, &col);
    CHECK(line == 1 && col == 0);
    CHECK(sdk_edit_paste() == 3 && same("aa\nbb\ncc"));
    sdk_edit_doc_end();
    CHECK(sdk_edit_copy(1) == 3 && same("aa\nbb"));         /* the last line: no empty line is left */
    sdk_edit_pos(&line, &col);
    CHECK(line == 1);

    /* duplicate line, pages */
    sdk_edit_reset();
    type("x1\nx2");
    CHECK(sdk_edit_duplicate_line() == 1 && same("x1\nx2\nx2"));
    sdk_edit_pos(&line, &col);
    CHECK(line == 2 && col == 2);
    sdk_edit_page_up(10);
    sdk_edit_pos(&line, &col);
    CHECK(line == 0);
    sdk_edit_page_down(2);
    sdk_edit_pos(&line, &col);
    CHECK(line == 2);
    sdk_edit_page_down(5);
    sdk_edit_pos(&line, &col);
    CHECK(line == 2);
}

static void test_undo(void)
{
    int line, col;
    size_t a, b;

    /* typing in a row is one step; undo and redo bring the text and the cursor back */
    sdk_edit_reset();
    CHECK(sdk_edit_undo() == 0 && sdk_edit_redo() == 0);
    type("hello");
    CHECK(sdk_edit_undo() == 1 && sdk_edit_length() == 0);
    CHECK(sdk_edit_undo() == 0);
    CHECK(sdk_edit_redo() == 1 && same("hello"));
    sdk_edit_pos(&line, &col);
    CHECK(col == 5);
    CHECK(sdk_edit_redo() == 0);

    /* a move ends the step; Enter is a step of its own */
    type(" wor");
    sdk_edit_left();
    type("X");                                              /* the cursor moved: a new step */
    CHECK(same("hello woXr"));
    CHECK(sdk_edit_undo() == 1 && same("hello wor"));
    sdk_edit_reset();
    type("ab");
    type("\n");
    type("cd");
    CHECK(sdk_edit_undo() == 1 && same("ab\n"));
    CHECK(sdk_edit_undo() == 1 && same("ab"));
    CHECK(sdk_edit_undo() == 1 && same(""));

    /* a new change ends the redo history */
    sdk_edit_reset();
    type("one");
    sdk_edit_undo();
    type("two");
    CHECK(sdk_edit_redo() == 0 && same("two"));

    /* Backspaces in a row are one step */
    sdk_edit_reset();
    type("abc");
    sdk_edit_left();
    sdk_edit_left();                                        /* a|bc: not merged with the typing */
    sdk_edit_end();
    sdk_edit_backspace();
    sdk_edit_backspace();
    CHECK(same("a"));
    CHECK(sdk_edit_undo() == 1 && same("abc"));
    CHECK(sdk_edit_undo() == 1 && same(""));

    /* cut, paste, selection, duplicate: each one a step */
    sdk_edit_reset();
    type("aa\nbb\ncc");
    sdk_edit_up();
    CHECK(sdk_edit_copy(1) == 3 && same("aa\ncc"));
    CHECK(sdk_edit_undo() == 1 && same("aa\nbb\ncc"));
    sdk_edit_pos(&line, &col);
    CHECK(line == 1 || line == 2);
    CHECK(sdk_edit_redo() == 1 && same("aa\ncc"));
    CHECK(sdk_edit_paste() == 3 && same("aa\nbb\ncc"));
    CHECK(sdk_edit_undo() == 1 && same("aa\ncc"));
    sdk_edit_select_all();
    sdk_edit_type('Z');
    CHECK(same("Z"));
    CHECK(sdk_edit_undo() == 1 && same("aa\ncc"));
    CHECK(!sdk_edit_selection(&a, &b));
    sdk_edit_duplicate_line();
    CHECK(sdk_edit_length() == 8 && sdk_edit_undo() == 1 && same("aa\ncc"));

    /* a copy changes nothing: no step */
    sdk_edit_reset();
    type("xy");
    sdk_edit_copy(0);
    CHECK(sdk_edit_undo() == 1 && same(""));

    /* a new text (a project opens) has no history; the revision counts an undo as a change */
    sdk_edit_reset();
    type("abc");
    sdk_edit_set("new", 3);
    CHECK(sdk_edit_undo() == 0 && same("new"));
    type("!");
    {
        const unsigned r = sdk_edit_revision();
        sdk_edit_undo();
        CHECK(sdk_edit_revision() != r);
    }

    /* the history is 100 steps: older ones go */
    sdk_edit_reset();
    for (int i = 0; i < 150; i++)
        sdk_edit_type('\n');
    int n = 0;
    while (sdk_edit_undo())
        n++;
    CHECK(n == 100 && sdk_edit_length() == 50);
}

static void test_nes(void)
{
    uint8_t rom[16 + 32768 + 8192];
    sdk_rom_t r;
    memset(rom, 0, sizeof rom);
    memcpy(rom, "NES\x1a", 4);
    rom[4] = 2;
    rom[5] = 1;
    rom[6] = 0x91;                                          /* mapper low 9, vertical */
    CHECK(sdk_rom_probe(rom, sizeof rom, &r) == 0 && r.kind == SDK_ROM_NES);
    CHECK(strstr(r.line[0], "PRG 32 KB") && strstr(r.line[0], "CHR 8 KB"));
    CHECK(strstr(r.line[1], "Mapper 9 MMC2"));
    CHECK(strstr(r.line[2], "Vertical"));
    CHECK(sdk_rom_probe(rom, 100, &r) == 0 && r.lines == 4);          /* shorter than its header says */
    CHECK(strstr(r.line[3], "shorter"));
}

/* A SNES ROM with its header at base. */
static void make_snes(uint8_t *rom, size_t size, size_t base, uint8_t mode)
{
    memset(rom, 0, size);
    uint8_t *h = rom + base;
    memcpy(h, "MY GAME             ", 21);
    h[0x15] = mode;
    h[0x17] = 9;                                            /* 512 KB */
    h[0x18] = 3;                                            /* 8 KB of SRAM */
    h[0x1C] = 0x34;
    h[0x1D] = 0x12;
    h[0x1E] = 0xCB;
    h[0x1F] = 0xED;
}

static void test_snes(void)
{
    static uint8_t rom[0x10000 + 512];
    sdk_rom_t r;

    make_snes(rom, 0x10000, 0x7FC0, 0x20);
    CHECK(sdk_rom_probe(rom, 0x10000, &r) == 0 && r.kind == SDK_ROM_SNES);
    CHECK(!strcmp(r.title, "MY GAME") && strstr(r.line[0], "LoROM") && strstr(r.line[0], "SlowROM"));
    CHECK(strstr(r.line[1], "ROM 512 KB") && strstr(r.line[1], "SRAM 8 KB"));

    make_snes(rom, 0x10000, 0xFFC0, 0x31);
    CHECK(sdk_rom_probe(rom, 0x10000, &r) == 0 && strstr(r.line[0], "HiROM") && strstr(r.line[0], "FastROM"));

    /* .smc: 512 bytes of copier header in front */
    make_snes(rom + 512, 0x10000, 0x7FC0, 0x20);
    memset(rom, 0, 512);
    CHECK(sdk_rom_probe(rom, 0x10000 + 512, &r) == 0 && strstr(r.line[0], ".smc header"));

    /* not a ROM */
    memset(rom, 0xAA, sizeof rom);
    CHECK(sdk_rom_probe(rom, 0x10000, &r) == -1 && r.kind == SDK_ROM_UNKNOWN && r.lines == 1);
    CHECK(sdk_rom_probe(rom, 8, &r) == -1);
}

static void test_hex(void)
{
    uint8_t d[20];
    char row[96];
    for (int i = 0; i < 20; i++)
        d[i] = (uint8_t)('A' + i);
    d[3] = 0;
    CHECK(sdk_rom_hex_bytes(80) == 16 && sdk_rom_hex_bytes(40) == 8 && sdk_rom_hex_bytes(10) == 4);
    CHECK(sdk_rom_hex_row(d, sizeof d, 0, 40, row, sizeof row) == 8);
    CHECK(!strncmp(row, "000000 41 42 43 00 45 46 47 48 ", 31) && !strcmp(row + 31, "ABC.EFGH"));
    CHECK(sdk_rom_hex_row(d, sizeof d, 16, 40, row, sizeof row) == 4);           /* the file ends: blanks */
    CHECK(!strncmp(row, "000010 51 52 53 54 ", 19) && strlen(row) == 6 + 1 + 8 * 3 + 8);
}

/* the read-only view of the dump and the listing */
static const char *const view_text[] = { "8000  lda #$00", "", "8002  rts" };

static int view_line(void *ctx, int line, char *out, size_t n)
{
    (void)ctx;
    return snprintf(out, n, "%s", view_text[line]);
}

static void test_view(void)
{
    sdk_view_t v;
    char out[64];
    int cut, l1, c1, l2, c2;
    sdk_view_reset(&v, 3);

    /* nothing selected: the line with its end */
    size_t n = sdk_view_copy(&v, view_line, NULL, out, sizeof out, &cut);
    CHECK(n == 15 && !cut && !memcmp(out, "8000  lda #$00\n", n));

    /* words and a selection with Shift */
    sdk_view_move(&v, SDK_VIEW_WORD_RIGHT, 0, 10, view_line, NULL);
    CHECK(v.line == 0 && v.col == 6);
    sdk_view_move(&v, SDK_VIEW_WORD_RIGHT, 1, 10, view_line, NULL);
    CHECK(sdk_view_selection(&v, &l1, &c1, &l2, &c2) && l1 == 0 && c1 == 6 && l2 == 0 && c2 == 12);
    n = sdk_view_copy(&v, view_line, NULL, out, sizeof out, &cut);
    CHECK(n == 6 && !memcmp(out, "lda #$", n));
    sdk_view_move(&v, SDK_VIEW_DOWN, 1, 10, view_line, NULL);       /* an empty line: the column is kept */
    CHECK(v.line == 1 && v.col == 0 && v.want == 12);
    sdk_view_move(&v, SDK_VIEW_DOWN, 1, 10, view_line, NULL);
    CHECK(v.line == 2 && v.col == 9);                               /* "8002  rts" is shorter */
    n = sdk_view_copy(&v, view_line, NULL, out, sizeof out, &cut);
    CHECK(n == 19 && !memcmp(out, "lda #$00\n\n8002  rts", n));

    /* backwards, and a move with no Shift ends the selection */
    sdk_view_move(&v, SDK_VIEW_HOME, 0, 10, view_line, NULL);
    CHECK(!sdk_view_selection(&v, &l1, &c1, &l2, &c2) && v.col == 0);
    sdk_view_move(&v, SDK_VIEW_LEFT, 1, 10, view_line, NULL);       /* to the end of the line above */
    CHECK(v.line == 1 && v.col == 0);
    CHECK(sdk_view_selection(&v, &l1, &c1, &l2, &c2) && l1 == 1 && l2 == 2 && c2 == 0);
    n = sdk_view_copy(&v, view_line, NULL, out, sizeof out, &cut);
    CHECK(n == 1 && out[0] == '\n');

    /* all, and a buffer too small */
    sdk_view_select_all(&v, view_line, NULL);
    n = sdk_view_copy(&v, view_line, NULL, out, sizeof out, &cut);
    CHECK(n == 25 && !cut && !memcmp(out, "8000  lda #$00\n\n8002  rts", n));
    n = sdk_view_copy(&v, view_line, NULL, out, 10, &cut);
    CHECK(n == 10 && cut);

    /* pages, the ends and the scroll */
    sdk_view_move(&v, SDK_VIEW_DOC_START, 0, 10, view_line, NULL);
    CHECK(v.line == 0 && v.col == 0);
    sdk_view_move(&v, SDK_VIEW_PGDN, 0, 2, view_line, NULL);
    sdk_view_scroll(&v, 2, 80);
    CHECK(v.line == 2 && v.top == 1);
    sdk_view_move(&v, SDK_VIEW_PGUP, 0, 2, view_line, NULL);
    sdk_view_scroll(&v, 2, 80);
    CHECK(v.line == 0 && v.top == 0);
    sdk_view_move(&v, SDK_VIEW_END, 0, 2, view_line, NULL);
    sdk_view_scroll(&v, 2, 8);
    CHECK(v.col == 14 && v.left == 7);
    sdk_view_move(&v, SDK_VIEW_DOC_END, 0, 2, view_line, NULL);
    CHECK(v.line == 2 && v.col == 9);

    /* the copy goes to the clipboard of the editor */
    sdk_edit_reset();
    sdk_edit_clip_set("sei", 3);
    CHECK(sdk_edit_paste() == 3 && !memcmp(sdk_edit_text(), "sei", 3));
}

static void test_tiles(void)
{
    uint8_t d[128], pix[64];
    int ok;

    /* NES: bit 0 of the 8 lines, then bit 1 */
    memset(d, 0, sizeof d);
    d[0] = 0x80;                    /* line 0, bit 0: the left pixel */
    d[8] = 0x81;                    /* line 0, bit 1: the left and the right pixel */
    d[7] = 0x01;                    /* line 7, bit 0: the right pixel */
    sdk_tile_decode(d, sizeof d, 0, SDK_TILE_NES, pix);
    CHECK(pix[0] == 3 && pix[7] == 2 && pix[1] == 0 && pix[63] == 1 && pix[56] == 0);

    /* SNES 2 bpp: two bytes per line */
    memset(d, 0, sizeof d);
    d[0] = 0x80;
    d[1] = 0xC0;
    d[15] = 0x01;
    sdk_tile_decode(d, sizeof d, 0, SDK_TILE_SNES2, pix);
    CHECK(pix[0] == 3 && pix[1] == 2 && pix[2] == 0 && pix[63] == 2);

    /* SNES 4 bpp: bits 2 and 3 from byte 16; 8 bpp: bits 6 and 7 from byte 48 */
    d[16] = 0x80;
    d[17] = 0x80;
    sdk_tile_decode(d, sizeof d, 0, SDK_TILE_SNES4, pix);
    CHECK(pix[0] == 15 && pix[1] == 2 && pix[63] == 2);
    d[49] = 0x80;
    sdk_tile_decode(d, sizeof d, 0, SDK_TILE_SNES8, pix);
    CHECK(pix[0] == 15 + 128);

    /* past the end of the file: colour 0 */
    memset(d, 0xFF, sizeof d);
    sdk_tile_decode(d, 4, 0, SDK_TILE_NES, pix);
    ok = pix[0] == 1 && pix[24] == 1 && pix[32] == 0;
    CHECK(ok);
    CHECK(sdk_tile_bytes(SDK_TILE_NES) == 16 && sdk_tile_bytes(SDK_TILE_SNES4) == 32 &&
          sdk_tile_bytes(SDK_TILE_SNES8) == 64 && sdk_tile_colors(SDK_TILE_SNES4) == 16);
    CHECK(sdk_tile_next(SDK_TILE_SNES4) == SDK_TILE_SNES2 && sdk_tile_next(SDK_TILE_SNES2) == SDK_TILE_SNES8 &&
          sdk_tile_next(SDK_TILE_SNES8) == SDK_TILE_SNES4 && sdk_tile_next(SDK_TILE_NES) == SDK_TILE_NES);
}

static void test_tiles_start(void)
{
    static uint8_t rom[16 + 512 + 16384 + 8192];
    sdk_rom_t r;
    int fmt, chr;

    memcpy(rom, "NES", 4);
    rom[4] = 1;
    rom[5] = 1;
    CHECK(sdk_rom_probe(rom, sizeof rom - 512, &r) == 0);
    CHECK(sdk_tiles_start(rom, sizeof rom - 512, &r, &fmt, &chr) == 0x4010 && chr == 1 && fmt == SDK_TILE_NES);
    rom[6] = 4;                     /* a trainer */
    CHECK(sdk_tiles_start(rom, sizeof rom, &r, &fmt, &chr) == 0x4210 && chr == 1);
    rom[5] = 0;                     /* CHR RAM: the tiles are somewhere in the program */
    CHECK(sdk_tiles_start(rom, sizeof rom, &r, &fmt, &chr) == 0x210 && chr == 0);

    r.kind = SDK_ROM_SNES;
    CHECK(sdk_tiles_start(rom, 0x8000, &r, &fmt, &chr) == 0 && fmt == SDK_TILE_SNES4 && chr == 0);
    CHECK(sdk_tiles_start(rom, 0x4000 + 512, &r, &fmt, &chr) == 512);
}

static void test_tiles_move(void)
{
    const size_t size = 0x6010;     /* a NES file: 16 KB of program, 8 KB of CHR */
    const int f = SDK_TILE_NES;     /* a row is 0x100 bytes; a page of 10 rows is 0xA00 */

    CHECK(sdk_tiles_move(0x4010, size, f, 16, 10, SDK_TILES_DOWN) == 0x4110);
    CHECK(sdk_tiles_move(0x4010, size, f, 16, 10, SDK_TILES_PGDN) == 0x4A10);
    CHECK(sdk_tiles_move(0x4010, size, f, 16, 10, SDK_TILES_END) == 0x5610);
    CHECK(sdk_tiles_move(0x5610, size, f, 16, 10, SDK_TILES_DOWN) == 0x5610);       /* the last page stays */
    CHECK(sdk_tiles_move(0x5510, size, f, 16, 10, SDK_TILES_PGDN) == 0x5610);
    CHECK(sdk_tiles_move(0x4010, size, f, 16, 10, SDK_TILES_UP) == 0x3F10);
    CHECK(sdk_tiles_move(0x0110, size, f, 16, 10, SDK_TILES_PGUP) == 0);            /* a whole tile */
    CHECK(sdk_tiles_move(0x0115, size, f, 16, 10, SDK_TILES_PGUP) == 5);
    CHECK(sdk_tiles_move(0x4010, size, f, 16, 10, SDK_TILES_RIGHT) == 0x4011);
    CHECK(sdk_tiles_move(0, size, f, 16, 10, SDK_TILES_LEFT) == 0);
    CHECK(sdk_tiles_move(size - 1, size, f, 16, 10, SDK_TILES_RIGHT) == size - 1);
    CHECK(sdk_tiles_move(size - 1, size, f, 16, 10, SDK_TILES_DOWN) == size - 1);   /* past the last page: stays */
    CHECK(sdk_tiles_move(0, 64, f, 16, 10, SDK_TILES_PGDN) == 0);                   /* a file shorter than a page */
    /* a tile at a time, and other widths of a row */
    CHECK(sdk_tiles_move(0x4010, size, f, 16, 10, SDK_TILES_NEXT) == 0x4020);
    CHECK(sdk_tiles_move(0x4020, size, f, 16, 10, SDK_TILES_BACK) == 0x4010);
    CHECK(sdk_tiles_move(5, size, f, 16, 10, SDK_TILES_BACK) == 5);
    CHECK(sdk_tiles_move(size - 1, size, f, 16, 10, SDK_TILES_NEXT) == size - 1);
    CHECK(sdk_tiles_move(0x4010, size, f, 2, 10, SDK_TILES_DOWN) == 0x4030);
    CHECK(sdk_tiles_move(0x4010, size, f, 3, 10, SDK_TILES_PGDN) == 0x41F0);
    /* the cells: in rows, or as 8x16 sprites (0 above 1, 2 above 3) */
    CHECK(sdk_tiles_cell(0, 0, 16, 0) == 0 && sdk_tiles_cell(1, 2, 16, 0) == 18);
    CHECK(sdk_tiles_cell(1, 0, 2, 0) == 2 && sdk_tiles_cell(2, 1, 3, 0) == 7);
    CHECK(sdk_tiles_cell(0, 0, 4, 1) == 0 && sdk_tiles_cell(1, 0, 4, 1) == 1);
    CHECK(sdk_tiles_cell(0, 1, 4, 1) == 2 && sdk_tiles_cell(1, 1, 4, 1) == 3);
    CHECK(sdk_tiles_cell(2, 0, 4, 1) == 8 && sdk_tiles_cell(3, 3, 4, 1) == 15);
}

int main(void)
{
    test_edit();
    test_keys();
    test_undo();
    test_nes();
    test_snes();
    test_hex();
    test_view();
    test_tiles();
    test_tiles_start();
    test_tiles_move();
    if (fails) {
        printf("test_shell_sdk_code: %d FAILED\n", fails);
        return 1;
    }
    printf("test_shell_sdk_code: OK\n");
    return 0;
}
