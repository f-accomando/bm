/*
 * Shell SDK, Project tab: the model. It holds what a real cartridge has (the board of a NES
 * cartridge, the map, ROM, save RAM, battery and coprocessor of a SNES cartridge) or the settings
 * of a bm project (no cartridge: a .bm on the SD card). It has no hardware: the host test
 * (make test-shell-sdk) runs it. The SNES part follows core/lua/snescode.lua (SC.cart_*).
 */
#ifndef SHELL_SDK_PROJ_H
#define SHELL_SDK_PROJ_H

#include <stddef.h>

enum { SDK_NES, SDK_SNES, SDK_BM, SDK_TARGETS };

/* The rows of the menu. The first row is always SDK_ROW_PLATFORM. */
enum {
    SDK_ROW_PLATFORM,
    SDK_ROW_BOARD, SDK_ROW_PRG, SDK_ROW_CHR, SDK_ROW_WRAM, SDK_ROW_NES_BATTERY, SDK_ROW_MIRROR,      /* NES */
    SDK_ROW_MAP, SDK_ROW_SPEED, SDK_ROW_ROM, SDK_ROW_SRAM, SDK_ROW_SNES_BATTERY, SDK_ROW_CHIP,     /* SNES */
    SDK_ROW_LANG, SDK_ROW_KERNELS                                                                      /* bm */
};

typedef struct {
    int target;
    /* NES */
    int nes_board;          /* index in the board table */
    int nes_prg_kb;         /* 16 to 512 */
    int nes_chr_kb;         /* 0 = 8 KB of CHR RAM on the board, else the KB of CHR ROM */
    int nes_wram_kb;        /* 0 or 8 */
    int nes_battery;
    int nes_vertical;       /* mirroring: 0 horizontal, 1 vertical (AxROM: one screen, fixed) */
    /* SNES */
    int snes_hirom;
    int snes_fast;
    int snes_rom_kb;
    int snes_sram_kb;
    int snes_battery;
    int snes_chip;          /* index in the coprocessor table */
    /* bm */
    int bm_lang;            /* 0 .b, 1 Lua, 2 C */
    int bm_kernels;         /* 0 Pi Zero W and RGB30, 1 Pi Zero W, 2 RGB30 */
} sdk_proj_t;

typedef struct {
    int id;                 /* SDK_ROW_* */
    const char *label;
    char value[56];
} sdk_row_t;

/* What the PCB shows (shell_sdk_pcb.c draws it). */
typedef struct {
    int has_mapper;         /* NES: a mapper chip; SNES: a coprocessor */
    char mapper[12];        /* its short name: "MMC3", "DSP-1" */
    int chr_ram;            /* NES: a RAM chip in place of the CHR ROM */
    int has_ram;            /* a save RAM chip */
    int has_battery;        /* a coin cell */
    char rom[32];           /* the label of the ROM chip: "PRG 32K", "ROM 1M" */
    char rom2[32];          /* NES: the CHR chip; SNES: "" */
} sdk_pcb_t;

void sdk_proj_default(sdk_proj_t *p, int target);

/* The rows of the menu for the current target (at most max). Returns the number of rows. */
int sdk_proj_rows(const sdk_proj_t *p, sdk_row_t *rows, int max);

/* Changes the value of a row by one step (+1 or -1); the settings that depend on each other stay right. */
void sdk_proj_step(sdk_proj_t *p, int row_id, int step);

/* One help line for a row (what it is, what it costs). */
const char *sdk_proj_note(const sdk_proj_t *p, int row_id);

/* Memory and sprite sheet facts, one per line. Returns the number of lines (at most max). */
int sdk_proj_info(const sdk_proj_t *p, char lines[][72], int max);

void sdk_proj_pcb(const sdk_proj_t *p, sdk_pcb_t *pcb);

/* The project as text, for the LUA section of a .bme (the same idea as the "--cart" line of the SNES SDK):
 * a comment line and one "--sdk key=value ..." line. Returns the length, or -1 when n is too small. */
int sdk_proj_text(const sdk_proj_t *p, char *out, size_t n);

/* The settings of such a text (a bad or missing value takes the default, and the settings that depend on
 * each other are put right). Returns 0, or -1 when the text has no "--sdk" line (p is then the default). */
int sdk_proj_parse(const char *text, size_t len, sdk_proj_t *p);

/* The name of a target, its cartridge and its CPU ("NES", "Cartridge", "6502"). */
const char *sdk_target_name(int target);
const char *sdk_target_cpu(int target);

#endif /* SHELL_SDK_PROJ_H */
