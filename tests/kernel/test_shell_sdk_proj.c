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

    sdk_pcb_t pcb;
    sdk_proj_default(&p, SDK_SNES);
    p.snes_chip = 1;
    sdk_proj_step(&p, SDK_ROW_SRAM, 1);
    sdk_proj_pcb(&p, &pcb);
    CHECK(pcb.has_mapper && !strcmp(pcb.mapper, "DSP-1") && pcb.has_ram && pcb.has_battery && !strcmp(pcb.rom, "ROM 32 KB"));
    sdk_proj_default(&p, SDK_NES);
    sdk_proj_pcb(&p, &pcb);
    CHECK(!pcb.has_mapper && !pcb.chr_ram && !strcmp(pcb.rom, "PRG 32 KB") && !strcmp(pcb.rom2, "CHR 8 KB"));

    if (fails) {
        printf("test_shell_sdk_proj: %d FAILED\n", fails);
        return 1;
    }
    printf("test_shell_sdk_proj: OK\n");
    return 0;
}
