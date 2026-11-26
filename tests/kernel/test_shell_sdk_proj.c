/*
 * Host unit test for the Shell SDK Project tab model (core/src/kernel/shell_sdk_proj.c).
 */
#include <stdio.h>
#include <string.h>

#include "kernel/shell_sdk_proj.h"

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const char *value(const sdk_proj_t *p, int id)
{
    static sdk_row_t rows[16];
    const int n = sdk_proj_rows(p, rows, 16);
    for (int i = 0; i < n; i++)
        if (rows[i].id == id)
            return rows[i].value;
    return NULL;
}

int main(void)
{
    sdk_proj_t p;
    sdk_row_t rows[16];
    char info[8][72];

    /* the platform row comes first, each target has its rows */
    sdk_proj_default(&p, SDK_SNES);
    CHECK(sdk_proj_rows(&p, rows, 16) == 7 && rows[0].id == SDK_ROW_PLATFORM && rows[6].id == SDK_ROW_CHIP);
    CHECK(!strcmp(value(&p, SDK_ROW_MAP), "LoROM (32 KB banks at $8000)"));
    CHECK(!strcmp(value(&p, SDK_ROW_SPEED), "SlowROM (200 ns, 2.68 MHz)"));
    CHECK(!strcmp(value(&p, SDK_ROW_ROM), "32 KB"));
    CHECK(!strcmp(value(&p, SDK_ROW_SRAM), "none"));
    CHECK(!strcmp(value(&p, SDK_ROW_CHIP), "None (plain ROM)"));
    sdk_proj_default(&p, SDK_NES);
    CHECK(sdk_proj_rows(&p, rows, 16) == 7 && rows[1].id == SDK_ROW_BOARD);
    sdk_proj_default(&p, SDK_BM);
    CHECK(sdk_proj_rows(&p, rows, 16) == 3 && rows[1].id == SDK_ROW_LANG);

    /* the platform steps around */
    sdk_proj_step(&p, SDK_ROW_PLATFORM, 1);
    CHECK(p.target == SDK_NES);
    sdk_proj_step(&p, SDK_ROW_PLATFORM, -1);
    CHECK(p.target == SDK_BM);

    /* SNES: the dependent settings stay right */
    sdk_proj_default(&p, SDK_SNES);
    sdk_proj_step(&p, SDK_ROW_MAP, 1);
    CHECK(p.snes_hirom && p.snes_rom_kb == 64);             /* a HiROM bank is 64 KB */
    sdk_proj_step(&p, SDK_ROW_ROM, -1);
    CHECK(p.snes_rom_kb == 4096);                           /* 32 KB is not valid on HiROM: wraps to the top */
    while (p.snes_chip != 2)
        sdk_proj_step(&p, SDK_ROW_CHIP, 1);                 /* Super FX */
    CHECK(!p.snes_hirom && p.snes_sram_kb == 32);           /* GSU: LoROM, comes with RAM */
    sdk_proj_step(&p, SDK_ROW_SRAM, -1);
    sdk_proj_step(&p, SDK_ROW_SRAM, -1);
    sdk_proj_step(&p, SDK_ROW_SRAM, -1);                    /* 8, 2, then 0 is skipped: the chip needs RAM */
    CHECK(p.snes_sram_kb == 128);
    sdk_proj_step(&p, SDK_ROW_CHIP, 1);                     /* SA-1 */
    sdk_proj_default(&p, SDK_SNES);
    p.snes_chip = 3;
    sdk_proj_step(&p, SDK_ROW_SPEED, 1);
    CHECK(p.snes_rom_kb == 64 && p.snes_fast);

    /* NES: the board limits PRG, CHR and work RAM */
    sdk_proj_default(&p, SDK_NES);
    sdk_proj_step(&p, SDK_ROW_PRG, 1);
    CHECK(p.nes_prg_kb == 16);                              /* NROM: 32 KB at most, wraps */
    sdk_proj_step(&p, SDK_ROW_WRAM, 1);
    CHECK(p.nes_wram_kb == 0);                              /* NROM has no work RAM */
    sdk_proj_step(&p, SDK_ROW_BOARD, 1);                    /* MMC1 */
    sdk_proj_step(&p, SDK_ROW_WRAM, 1);
    sdk_proj_step(&p, SDK_ROW_NES_BATTERY, 1);
    CHECK(p.nes_wram_kb == 8 && p.nes_battery);
    sdk_proj_step(&p, SDK_ROW_BOARD, 1);                    /* UxROM: CHR is RAM, no work RAM */
    CHECK(p.nes_chr_kb == 0 && p.nes_wram_kb == 0 && !p.nes_battery);
    sdk_proj_step(&p, SDK_ROW_CHR, 1);
    CHECK(p.nes_chr_kb == 0);                               /* nothing else fits */
    sdk_proj_step(&p, SDK_ROW_BOARD, 1);                    /* CNROM: CHR ROM only, 32 KB PRG at most */
    CHECK(p.nes_chr_kb == 8 && p.nes_prg_kb <= 32);
    sdk_proj_step(&p, SDK_ROW_BOARD, 1);                    /* MMC3 */
    for (int i = 0; i < 4; i++)
        sdk_proj_step(&p, SDK_ROW_PRG, 1);
    CHECK(p.nes_prg_kb > 32);
    sdk_proj_step(&p, SDK_ROW_BOARD, 1);                    /* AxROM: one screen */
    CHECK(p.nes_chr_kb == 0);
    sdk_proj_step(&p, SDK_ROW_MIRROR, 1);
    CHECK(!p.nes_vertical);

    /* the boards: mapper 9 (MMC2) is there, 128 KB at most */
    sdk_proj_default(&p, SDK_NES);
    for (int i = 0; i < 6; i++)
        sdk_proj_step(&p, SDK_ROW_BOARD, 1);
    CHECK(!strcmp(value(&p, SDK_ROW_BOARD), "MMC2 (mapper 9)"));
    sdk_proj_step(&p, SDK_ROW_PRG, 1);
    sdk_proj_step(&p, SDK_ROW_PRG, 1);
    sdk_proj_step(&p, SDK_ROW_PRG, 1);
    sdk_proj_step(&p, SDK_ROW_PRG, 1);
    CHECK(p.nes_prg_kb <= 128);

    /* info and the PCB */
    sdk_proj_default(&p, SDK_NES);
    CHECK(sdk_proj_info(&p, info, 8) == 5 && strstr(info[3], "128 px wide") && strstr(info[3], "256 px high"));
    sdk_proj_default(&p, SDK_SNES);
    CHECK(sdk_proj_info(&p, info, 8) == 5 && strstr(info[1], "1 bank of 32 KB"));
    sdk_proj_default(&p, SDK_BM);
    CHECK(sdk_proj_info(&p, info, 8) == 3);
    {                                           /* the head of the sprite sheet of the Sprite tab */
        char head[2][48];
        sdk_proj_t q;
        sdk_proj_default(&q, SDK_NES);
        sdk_proj_sheet(&q, head);
        CHECK(strstr(head[0], "128x256 px  512 tiles") && strstr(head[1], "the whole sheet"));
        q.nes_chr_kb = 256;
        sdk_proj_sheet(&q, head);
        CHECK(strstr(head[1], "CHR 256 KB: 128x8192, first 512") && strlen(head[1]) <= 36);
        q.nes_chr_kb = 0;
        sdk_proj_sheet(&q, head);
        CHECK(strstr(head[1], "CHR RAM"));
        sdk_proj_default(&q, SDK_SNES);
        sdk_proj_sheet(&q, head);
        CHECK(strstr(head[0], "4bpp 16 KB") && strstr(head[1], "sprites address 512") && strlen(head[1]) <= 36);
        sdk_proj_default(&q, SDK_BM);
        sdk_proj_sheet(&q, head);
        CHECK(strstr(head[1], "kept while open"));
    }

    sdk_pcb_t pcb;
    sdk_proj_default(&p, SDK_SNES);
    p.snes_chip = 1;
    sdk_proj_step(&p, SDK_ROW_SRAM, 1);
    sdk_proj_pcb(&p, &pcb);
    CHECK(pcb.has_mapper && !strcmp(pcb.mapper, "DSP-1") && pcb.has_ram && pcb.has_battery && !strcmp(pcb.rom, "ROM 32 KB"));
    sdk_proj_default(&p, SDK_NES);
    sdk_proj_pcb(&p, &pcb);
    CHECK(!pcb.has_mapper && !pcb.chr_ram && !strcmp(pcb.rom, "PRG 32 KB") && !strcmp(pcb.rom2, "CHR 8 KB"));

    /* the text of a project and back */
    char text[400];
    sdk_proj_t q;
    sdk_proj_default(&p, SDK_SNES);
    p.snes_chip = 3;                                        /* SA-1 */
    sdk_proj_step(&p, SDK_ROW_MAP, 1);
    sdk_proj_step(&p, SDK_ROW_SPEED, 1);
    const int tl = sdk_proj_text(&p, text, sizeof text);
    CHECK(tl > 0 && strstr(text, "--sdk v=1 t=1 "));
    CHECK(sdk_proj_parse(text, (size_t)tl, &q) == 0 && !memcmp(&p, &q, sizeof p));
    CHECK(sdk_proj_text(&p, text, 20) == -1);
    CHECK(sdk_proj_parse("-- nothing", 10, &q) == -1 && q.target == SDK_NES);
    {
        const char bad[] = "--sdk t=9 b=99 prg=7 chr=3 rom=5 chip=77 lang=-4 kern=x\n";
        CHECK(sdk_proj_parse(bad, sizeof bad - 1, &q) == 0);
        CHECK(q.target == SDK_TARGETS - 1 && q.nes_board < 8 && q.nes_prg_kb == 32 && q.snes_rom_kb == 32);
        CHECK(q.snes_chip < 9 && q.bm_lang == 0 && q.bm_kernels == 0);
    }

    /* the header for the builder */
    {
        char h[256];
        sdk_proj_t q;
        sdk_proj_default(&q, SDK_NES);
        CHECK(sdk_proj_header(&q, "x", h, sizeof h) > 0);
        CHECK(!strcmp(h, "{platform=\"nes\",mapper=0,prg_kb=32,chr_kb=8,wram_kb=0,battery=false,vertical=false}"));
        q.nes_board = 4;                                    /* MMC3 */
        q.nes_prg_kb = 128;
        q.nes_chr_kb = 0;
        q.nes_battery = 1;
        q.nes_wram_kb = 8;
        q.nes_vertical = 1;
        sdk_proj_parse("--sdk v=1 t=0 b=4 prg=128 chr=0 wram=8 nbat=1 vert=1", strlen("--sdk v=1 t=0 b=4 prg=128 chr=0 wram=8 nbat=1 vert=1"), &q);
        CHECK(sdk_proj_header(&q, "x", h, sizeof h) > 0 && strstr(h, "mapper=4,prg_kb=128,chr_kb=0,wram_kb=8,battery=true,vertical=true"));
        sdk_proj_default(&q, SDK_SNES);
        CHECK(sdk_proj_header(&q, "My game", h, sizeof h) > 0);
        CHECK(!strcmp(h, "{platform=\"snes\",title=\"MY GAME\",map=\"lorom\",fast=false,min_kb=32,sram_kb=0,battery=true,chipset=0}"));
        sdk_proj_parse("--sdk v=1 t=1 hirom=1 fast=1 rom=1024 sram=8 sbat=1 chip=2", strlen("--sdk v=1 t=1 hirom=1 fast=1 rom=1024 sram=8 sbat=1 chip=2"), &q);
        CHECK(sdk_proj_header(&q, "", h, sizeof h) > 0);
        CHECK(strstr(h, "title=\"BM SNES PROGRAM\"") && strstr(h, "map=\"lorom\"") && strstr(h, "fast=true"));
        CHECK(strstr(h, "min_kb=1024,sram_kb=8") && strstr(h, "chipset=21,mapmode=32}"));        /* GSU: $15, map $20 */
        sdk_proj_default(&q, SDK_BM);
        CHECK(sdk_proj_header(&q, "x", h, sizeof h) > 0 && !strcmp(h, "{platform=\"bm\"}"));
        CHECK(sdk_proj_header(&q, "x", h, 5) == -1);
    }

    /* the source code follows the --sdk line */
    {
        const char text[] = "-- bm shell sdk project\n--sdk v=1 t=0\nreset: sei\n  rts\n";
        const size_t off = sdk_proj_source_offset(text, sizeof text - 1);
        CHECK(off < sizeof text && !strncmp(text + off, "reset: sei", 10));
        CHECK(sdk_proj_source_offset("no line", 7) == 7);
        CHECK(sdk_proj_source_offset("--sdk t=1", 9) == 9);
    }

    if (fails) {
        printf("test_shell_sdk_proj: %d FAILED\n", fails);
        return 1;
    }
    printf("test_shell_sdk_proj: OK\n");
    return 0;
}
