/*
 * Host unit test for the Shell SDK Code tab models: the text buffer (shell_sdk_edit.c) and the ROM header
 * reader (shell_sdk_rom.c).
 */
#include <stdio.h>
#include <string.h>

#include "kernel/shell_sdk_edit.h"
#include "kernel/shell_sdk_rom.h"

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

int main(void)
{
    test_edit();
    test_keys();
    test_nes();
    test_snes();
    test_hex();
    if (fails) {
        printf("test_shell_sdk_code: %d FAILED\n", fails);
        return 1;
    }
    printf("test_shell_sdk_code: OK\n");
    return 0;
}
