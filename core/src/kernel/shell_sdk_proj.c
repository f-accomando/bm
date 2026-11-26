/*
 * Shell SDK, Project tab: the model (see shell_sdk_proj.h). Original code.
 */
#include "shell_sdk_proj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N(a) ((int)(sizeof (a) / sizeof (a)[0]))

/* ---- NES: the boards of real cartridges ---- */

typedef struct {
    const char *name;
    int mapper;             /* the iNES mapper number */
    int prg_max, chr_max;   /* KB */
    int chr_rom, chr_ram;   /* the board takes CHR ROM, CHR RAM, or both */
    int wram;               /* the board takes 8 KB of work RAM at $6000 */
    int one_screen;         /* mirroring is not wired: one screen */
    const char *note;
} nes_board_t;

static const nes_board_t boards[] = {
    { "NROM",    0,  32,   8, 1, 1, 0, 0, "No mapper chip: 32 KB of PRG and 8 KB of CHR at most." },
    { "MMC1",    1, 256, 128, 1, 1, 1, 0, "A mapper chip: switches 16 KB or 32 KB of PRG and 4 KB or 8 KB of CHR." },
    { "UxROM",   2, 256,   0, 0, 1, 0, 0, "One 74-series chip: switches 16 KB of PRG; CHR is RAM." },
    { "CNROM",   3,  32,  32, 1, 0, 0, 0, "One 74-series chip: switches 8 KB of CHR; PRG is 32 KB at most." },
    { "MMC3",    4, 512, 256, 1, 1, 1, 0, "A mapper chip: 8 KB PRG banks, 1 KB CHR banks and a scanline IRQ." },
    { "AxROM",   7, 256,   0, 0, 1, 0, 1, "One 74-series chip: switches 32 KB of PRG; CHR is RAM; one screen." },
    { "MMC2",    9, 128, 128, 1, 0, 0, 0, "Nintendo: 8 KB PRG banks and two 4 KB CHR banks that the picture fetches switch (Punch-Out!!)." },
    { "FME-7",  69, 256, 256, 1, 0, 1, 0, "Sunsoft: 1 KB CHR banks, PRG RAM or ROM at $6000 and a CPU-cycle IRQ." },
};

static const int nes_prg_list[] = { 16, 32, 64, 128, 256, 512 };
static const int nes_chr_list[] = { 0, 8, 16, 32, 64, 128, 256 };

/* ---- SNES: the coprocessors (core/lua/snescode.lua, SC.CART_CHIPS) ---- */

typedef struct {
    const char *name, *shortname;
    int lorom;              /* the header of this chip is LoROM */
    int emu;                /* the SNES core of bm runs this chip */
    int code;               /* the chipset byte of $FFD6 for this chip (no RAM), as SC.CART_CHIPS */
    int mapmode;            /* the map byte of $FFD5 of this chip's real cartridges (0: the plain one) */
    const char *note;
} snes_chip_t;

static const snes_chip_t chips[] = {
    { "None (plain ROM)",                  "",        0, 1, 0x00, 0,    "A plain cartridge: ROM and, if you want, save RAM." },
    { "DSP-1 (maths: Super Mario Kart)",   "DSP-1",   0, 1, 0x03, 0x20, "Needs the DSP firmware file in /snes to run." },
    { "Super FX / GSU (3D: Star Fox)",     "GSU",     1, 1, 0x13, 0x20, "The GSU CPU runs from the ROM. The header says LoROM; the chip adds its own memory map." },
    { "SA-1 (second CPU: Super Mario RPG)", "SA-1",   1, 1, 0x32, 0x23, "A 10 MHz second 65816 with its own RAM. The core runs it." },
    { "OBC-1 (sprite helper)",             "OBC-1",   0, 0, 0x25, 0x20, "Header only: the core has no OBC-1 yet." },
    { "S-DD1 (graphics decompression)",    "S-DD1",   0, 0, 0x43, 0x22, "Header only: the core has no S-DD1 yet." },
    { "S-RTC (real-time clock)",           "S-RTC",   0, 0, 0x55, 0x20, "Header only: the core has no S-RTC yet." },
    { "Cx4 (wireframe maths)",             "Cx4",     0, 0, 0xF3, 0x20, "Header only: the core has no Cx4 yet." },
    { "SPC7110 (decompression, data ROM)", "SPC7110", 0, 0, 0xF5, 0x3A, "Header only: the core has no SPC7110 yet." },
};
enum { CHIP_NONE, CHIP_DSP1, CHIP_GSU, CHIP_SA1 };

static const int snes_rom_list[] = { 32, 64, 128, 256, 512, 1024, 2048, 4096 };
static const int snes_sram_list[] = { 0, 2, 8, 32, 128 };

static const char *const langs[] = { ".b", "Lua", "C" };
static const char *const kernels[] = { "Pi Zero W and RGB30", "Pi Zero W", "RGB30" };

static const char *const target_name[SDK_TARGETS] = { "NES", "SNES", "bm" };
static const char *const target_cpu[SDK_TARGETS] = { "6502", "65C816", "ARM" };

const char *sdk_target_name(int t) { return t >= 0 && t < SDK_TARGETS ? target_name[t] : ""; }
const char *sdk_target_cpu(int t) { return t >= 0 && t < SDK_TARGETS ? target_cpu[t] : ""; }

/* ---- helpers ---- */

static void size_text(char *out, size_t n, int kb)
{
    if (kb >= 1024)
        snprintf(out, n, "%d MB", kb / 1024);
    else
        snprintf(out, n, "%d KB", kb);
}

/* The next valid value of a list, one step at a time. valid(v) tells whether a value fits now. */
static int list_step(const int *list, int n, int cur, int step, int (*valid)(const sdk_proj_t *, int), const sdk_proj_t *p)
{
    int at = 0;
    for (int i = 0; i < n; i++)
        if (list[i] == cur)
            at = i;
    for (int k = 0; k < n; k++) {
        at = (at + step + n) % n;
        if (valid(p, list[at]))
            return list[at];
    }
    return cur;
}

static int prg_ok(const sdk_proj_t *p, int v) { return v <= boards[p->nes_board].prg_max; }

static int chr_ok(const sdk_proj_t *p, int v)
{
    const nes_board_t *b = &boards[p->nes_board];
    return v == 0 ? b->chr_ram : b->chr_rom && v <= b->chr_max;
}

static int rom_ok(const sdk_proj_t *p, int v)
{
    if (p->snes_hirom && v < 64)
        return 0;
    return !(p->snes_chip == CHIP_SA1 && v < 64);
}

static int sram_ok(const sdk_proj_t *p, int v)
{
    return !(v == 0 && (p->snes_chip == CHIP_GSU || p->snes_chip == CHIP_SA1));
}

static void nes_fix(sdk_proj_t *p)
{
    const nes_board_t *b = &boards[p->nes_board];
    if (!prg_ok(p, p->nes_prg_kb))
        p->nes_prg_kb = b->prg_max;
    if (!chr_ok(p, p->nes_chr_kb))
        p->nes_chr_kb = b->chr_rom ? (b->chr_max < 8 ? b->chr_max : 8) : 0;
    if (!b->wram)
        p->nes_wram_kb = 0;
    if (p->nes_wram_kb == 0)
        p->nes_battery = 0;
    if (b->one_screen)
        p->nes_vertical = 0;
}

static void snes_fix(sdk_proj_t *p)
{
    if (chips[p->snes_chip].lorom)
        p->snes_hirom = 0;                      /* the GSU and SA-1 headers are LoROM */
    if (p->snes_chip == CHIP_GSU || p->snes_chip == CHIP_SA1) {
        if (p->snes_sram_kb == 0)
            p->snes_sram_kb = 32;               /* both chips come with RAM */
        if (p->snes_chip == CHIP_SA1 && p->snes_rom_kb < 64)
            p->snes_rom_kb = 64;
    }
    if (p->snes_hirom && p->snes_rom_kb < 64)
        p->snes_rom_kb = 64;                    /* a HiROM bank is 64 KB */
}

void sdk_proj_default(sdk_proj_t *p, int target)
{
    memset(p, 0, sizeof *p);
    p->target = target;
    p->nes_board = 0;
    p->nes_prg_kb = 32;
    p->nes_chr_kb = 8;
    p->snes_rom_kb = 32;
    p->snes_battery = 1;
    nes_fix(p);
    snes_fix(p);
}

/* ---- the rows ---- */

int sdk_proj_rows(const sdk_proj_t *p, sdk_row_t *rows, int max)
{
    int n = 0;
    char sz[24], sz2[24];
#define ROW(i, l, ...) do { if (n < max) { rows[n].id = (i); rows[n].label = (l); \
                            snprintf(rows[n].value, sizeof rows[n].value, __VA_ARGS__); n++; } } while (0)
    ROW(SDK_ROW_PLATFORM, "Platform", "%s (%s)", p->target == SDK_BM ? "bm project, SD card" :
        p->target == SDK_NES ? "NES cartridge" : "SNES cartridge", target_cpu[p->target]);
    if (p->target == SDK_NES) {
        const nes_board_t *b = &boards[p->nes_board];
        ROW(SDK_ROW_BOARD, "Board", "%s (mapper %d)", b->name, b->mapper);
        size_text(sz, sizeof sz, p->nes_prg_kb);
        ROW(SDK_ROW_PRG, "PRG ROM", "%s", sz);
        if (p->nes_chr_kb == 0) {
            ROW(SDK_ROW_CHR, "CHR", "RAM 8 KB (the program loads it)");
        } else {
            size_text(sz, sizeof sz, p->nes_chr_kb);
            ROW(SDK_ROW_CHR, "CHR", "ROM %s", sz);
        }
        if (p->nes_wram_kb == 0) {
            ROW(SDK_ROW_WRAM, "Work RAM", "none");
        } else {
            ROW(SDK_ROW_WRAM, "Work RAM", "%d KB at $6000", p->nes_wram_kb);
        }
        ROW(SDK_ROW_NES_BATTERY, "Battery", "%s", p->nes_battery ? "yes (the save stays)" : "no");
        ROW(SDK_ROW_MIRROR, "Mirroring", "%s", b->one_screen ? "one screen" : p->nes_vertical ? "vertical" : "horizontal");
    } else if (p->target == SDK_SNES) {
        size_text(sz, sizeof sz, p->snes_rom_kb);
        ROW(SDK_ROW_MAP, "Map", "%s", p->snes_hirom ? "HiROM (64 KB banks)" : "LoROM (32 KB banks at $8000)");
        ROW(SDK_ROW_SPEED, "ROM speed", "%s", p->snes_fast ? "FastROM (120 ns, 3.58 MHz)" : "SlowROM (200 ns, 2.68 MHz)");
        ROW(SDK_ROW_ROM, "ROM size", "%s", sz);
        if (p->snes_sram_kb == 0) {
            ROW(SDK_ROW_SRAM, "Save RAM", "none");
        } else {
            size_text(sz2, sizeof sz2, p->snes_sram_kb);
            ROW(SDK_ROW_SRAM, "Save RAM", "%s", sz2);
        }
        ROW(SDK_ROW_SNES_BATTERY, "Battery", "%s", p->snes_battery ? "yes (the save stays)" : "no");
        ROW(SDK_ROW_CHIP, "Coprocessor", "%s", chips[p->snes_chip].name);
    } else {
        ROW(SDK_ROW_LANG, "Language", "%s", langs[p->bm_lang]);
        ROW(SDK_ROW_KERNELS, "Kernels", "%s", kernels[p->bm_kernels]);
    }
#undef ROW
    return n;
}

void sdk_proj_step(sdk_proj_t *p, int row, int step)
{
    if (step != 1 && step != -1)
        return;
    switch (row) {
    case SDK_ROW_PLATFORM: p->target = (p->target + step + SDK_TARGETS) % SDK_TARGETS; break;
    case SDK_ROW_BOARD:
        p->nes_board = (p->nes_board + step + N(boards)) % N(boards);
        break;
    case SDK_ROW_PRG: p->nes_prg_kb = list_step(nes_prg_list, N(nes_prg_list), p->nes_prg_kb, step, prg_ok, p); break;
    case SDK_ROW_CHR: p->nes_chr_kb = list_step(nes_chr_list, N(nes_chr_list), p->nes_chr_kb, step, chr_ok, p); break;
    case SDK_ROW_WRAM:
        if (boards[p->nes_board].wram)
            p->nes_wram_kb = p->nes_wram_kb ? 0 : 8;
        break;
    case SDK_ROW_NES_BATTERY:
        if (p->nes_wram_kb)
            p->nes_battery = !p->nes_battery;
        break;
    case SDK_ROW_MIRROR:
        if (!boards[p->nes_board].one_screen)
            p->nes_vertical = !p->nes_vertical;
        break;
    case SDK_ROW_MAP: p->snes_hirom = !p->snes_hirom; break;
    case SDK_ROW_SPEED: p->snes_fast = !p->snes_fast; break;
    case SDK_ROW_ROM: p->snes_rom_kb = list_step(snes_rom_list, N(snes_rom_list), p->snes_rom_kb, step, rom_ok, p); break;
    case SDK_ROW_SRAM: p->snes_sram_kb = list_step(snes_sram_list, N(snes_sram_list), p->snes_sram_kb, step, sram_ok, p); break;
    case SDK_ROW_SNES_BATTERY: p->snes_battery = !p->snes_battery; break;
    case SDK_ROW_CHIP: p->snes_chip = (p->snes_chip + step + N(chips)) % N(chips); break;
    case SDK_ROW_LANG: p->bm_lang = (p->bm_lang + step + N(langs)) % N(langs); break;
    case SDK_ROW_KERNELS: p->bm_kernels = (p->bm_kernels + step + N(kernels)) % N(kernels); break;
    default: break;
    }
    nes_fix(p);
    snes_fix(p);
}

const char *sdk_proj_note(const sdk_proj_t *p, int row)
{
    switch (row) {
    case SDK_ROW_PLATFORM: return "Platform: NES cartridge (6502), SNES cartridge (65C816) or a bm project on the SD card (ARM).";
    case SDK_ROW_BOARD: return boards[p->nes_board].note;
    case SDK_ROW_PRG: return "PRG ROM: the program, in 16 KB banks.";
    case SDK_ROW_CHR: return "CHR: the tiles of the sprites and the background; ROM on the board, or 8 KB of RAM.";
    case SDK_ROW_WRAM: return "Work RAM: 8 KB at $6000 on boards that wire it (a save needs it and a battery).";
    case SDK_ROW_NES_BATTERY: return "Battery: keeps the work RAM when the console is off.";
    case SDK_ROW_MIRROR: return "Mirroring: how the two name tables repeat. Vertical: side scrolling. Horizontal: vertical scrolling.";
    case SDK_ROW_MAP: return "Map: how the ROM shows in the address space of the 65C816.";
    case SDK_ROW_SPEED: return "ROM speed: FastROM needs the fast ROM chips and the program to ask for it.";
    case SDK_ROW_ROM: return "ROM size: real ROM chips come in these sizes.";
    case SDK_ROW_SRAM: return "Save RAM: the RAM of the cartridge for a save.";
    case SDK_ROW_SNES_BATTERY: return "Battery: keeps the save RAM when the console is off.";
    case SDK_ROW_CHIP: return chips[p->snes_chip].note;
    case SDK_ROW_LANG: return "Language: the program is .b, Lua or C; the output is a .bm.";
    case SDK_ROW_KERNELS: return "Kernels: the targets in bm.info of the .bm.";
    default: return "";
    }
}

int sdk_proj_info(const sdk_proj_t *p, char lines[][72], int max)
{
    int n = 0;
#define LINE(...) do { if (n < max) { snprintf(lines[n], 72, __VA_ARGS__); n++; } } while (0)
    if (p->target == SDK_NES) {
        const int chr = p->nes_chr_kb ? p->nes_chr_kb : 8;
        const int tiles = chr * 64;
        LINE("RAM: 2 KB of CPU RAM, 2 KB of video RAM, 256 bytes of OAM");
        LINE("PRG ROM %d KB: %d banks of 16 KB%s", p->nes_prg_kb, p->nes_prg_kb / 16,
             p->nes_wram_kb ? ", 8 KB work RAM" : "");
        LINE("CHR %s %d KB: %d tiles", p->nes_chr_kb ? "ROM" : "RAM", chr, tiles);
        LINE("Sprite sheet: 128 px wide (16 tiles a row), %d px high", tiles / 16 * 8);
        LINE("Sprites: 64, 8 per line, 8x8 or 8x16; 4 sprite palettes of 3 colours");
    } else if (p->target == SDK_SNES) {
        const int banks = p->snes_rom_kb / (p->snes_hirom ? 64 : 32);
        LINE("RAM: 128 KB of work RAM, 64 KB of video RAM, 544 bytes of OAM");
        LINE("ROM %d KB: %d bank%s of %d KB", p->snes_rom_kb, banks, banks == 1 ? "" : "s", p->snes_hirom ? 64 : 32);
        if (p->snes_sram_kb)
            LINE("Save RAM %d KB%s", p->snes_sram_kb, p->snes_battery ? " with a battery" : ", no battery: the save is lost");
        else
            LINE("Save RAM: none");
        LINE("Sprite sheet: 128 px wide (16 tiles a row), 4 bits a pixel, 16 colours");
        LINE("Sprites: 128, 32 per line, two sizes from 8x8 up to 64x64");
    } else {
        LINE("No cartridge: the project becomes a .bm in bm/ on the SD card");
        LINE("Language %s, kernels: %s", langs[p->bm_lang], kernels[p->bm_kernels]);
        LINE("Sprite sheet: set in the Sprite tab (no cartridge limit)");
    }
#undef LINE
    return n;
}

void sdk_proj_sheet(const sdk_proj_t *p, char lines[2][48])
{
    if (p->target == SDK_NES) {
        const int kb = p->nes_chr_kb ? p->nes_chr_kb : 8, tiles = kb * 64;
        snprintf(lines[0], 48, "128x256 px  512 tiles  2bpp 8 KB");
        if (!p->nes_chr_kb)
            snprintf(lines[1], 48, "CHR RAM: the build needs CHR ROM");
        else if (tiles > 512)
            snprintf(lines[1], 48, "CHR %d KB: 128x%d, first 512", kb, tiles / 16 * 8);
        else
            snprintf(lines[1], 48, "CHR ROM 8 KB: the whole sheet");
    } else if (p->target == SDK_SNES) {
        snprintf(lines[0], 48, "128x256 px  512 tiles  4bpp 16 KB");
        snprintf(lines[1], 48, "ROM %d KB; sprites address 512", p->snes_rom_kb);
    } else {
        snprintf(lines[0], 48, "128x256 px  512 tiles");
        snprintf(lines[1], 48, "bm: no limit; kept while open");
    }
}

void sdk_proj_pcb(const sdk_proj_t *p, sdk_pcb_t *pcb)
{
    char sz[24];
    memset(pcb, 0, sizeof *pcb);
    if (p->target == SDK_NES) {
        const nes_board_t *b = &boards[p->nes_board];
        pcb->has_mapper = b->mapper != 0;       /* a mapper chip, or one 74-series chip */
        if (pcb->has_mapper)
            snprintf(pcb->mapper, sizeof pcb->mapper, "%s",
                     b->mapper == 2 || b->mapper == 3 || b->mapper == 7 ? "74xx" : b->name);
        pcb->chr_ram = p->nes_chr_kb == 0;
        pcb->has_ram = p->nes_wram_kb != 0;
        pcb->has_battery = p->nes_battery;
        size_text(sz, sizeof sz, p->nes_prg_kb);
        snprintf(pcb->rom, sizeof pcb->rom, "PRG %s", sz);
        if (pcb->chr_ram) {
            snprintf(pcb->rom2, sizeof pcb->rom2, "CHR RAM");
        } else {
            size_text(sz, sizeof sz, p->nes_chr_kb);
            snprintf(pcb->rom2, sizeof pcb->rom2, "CHR %s", sz);
        }
    } else if (p->target == SDK_SNES) {
        pcb->has_mapper = p->snes_chip != CHIP_NONE;
        if (pcb->has_mapper)
            snprintf(pcb->mapper, sizeof pcb->mapper, "%s", chips[p->snes_chip].shortname);
        pcb->has_ram = p->snes_sram_kb != 0;
        pcb->has_battery = pcb->has_ram && p->snes_battery;
        size_text(sz, sizeof sz, p->snes_rom_kb);
        snprintf(pcb->rom, sizeof pcb->rom, "ROM %s", sz);
    }
}

/* ---- the header for the builder ---- */

/* The chipset byte of the header, with the save RAM and the battery (core/lua/snescode.lua, SC.cart_chipset). */
static int snes_chipset(const sdk_proj_t *p)
{
    const int ram = p->snes_sram_kb > 0, bat = p->snes_battery;
    switch (p->snes_chip) {
    case CHIP_NONE: return ram ? (bat ? 0x02 : 0x01) : 0x00;
    case CHIP_DSP1: return ram ? (bat ? 0x05 : 0x04) : 0x03;
    case CHIP_GSU:  return ram ? (bat ? 0x15 : 0x14) : 0x13;
    case CHIP_SA1:  return ram ? (bat ? 0x35 : 0x34) : 0x32;
    case 5:         return ram ? 0x45 : 0x43;          /* S-DD1 */
    case 4:         return ram ? 0x25 : 0x23;          /* OBC-1 */
    default:        return chips[p->snes_chip].code;
    }
}

int sdk_proj_header(const sdk_proj_t *p, const char *title, char *out, size_t n)
{
    int len;
    if (p->target == SDK_NES) {
        const nes_board_t *b = &boards[p->nes_board];
        len = snprintf(out, n,
            "{platform=\"nes\",mapper=%d,prg_kb=%d,chr_kb=%d,wram_kb=%d,battery=%s,vertical=%s}",
            b->mapper, p->nes_prg_kb, p->nes_chr_kb, p->nes_wram_kb, p->nes_battery ? "true" : "false",
            p->nes_vertical && !b->one_screen ? "true" : "false");
    } else if (p->target == SDK_SNES) {
        char t[24];
        size_t k = 0;
        for (const char *s = title; s && *s && k < 21; s++) {         /* the title of the header: 21 capitals */
            const int c = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
            t[k++] = (c >= ' ' && c < 127 && c != '"' && c != '\\') ? (char)c : '?';
        }
        t[k] = 0;
        len = snprintf(out, n,
            "{platform=\"snes\",title=\"%s\",map=\"%s\",fast=%s,min_kb=%d,sram_kb=%d,battery=%s,chipset=%d%s%.0d}",
            k ? t : "BM SNES PROGRAM", p->snes_hirom ? "hirom" : "lorom", p->snes_fast ? "true" : "false",
            p->snes_rom_kb, p->snes_sram_kb, p->snes_battery ? "true" : "false", snes_chipset(p),
            chips[p->snes_chip].mapmode && p->snes_chip != CHIP_NONE ? ",mapmode=" : "",
            p->snes_chip != CHIP_NONE ? chips[p->snes_chip].mapmode : 0);
    } else {
        len = snprintf(out, n, "{platform=\"bm\"}");
    }
    return len < 0 || (size_t)len >= n ? -1 : len;
}

/* ---- the project as text ---- */

int sdk_proj_text(const sdk_proj_t *p, char *out, size_t n)
{
    const int len = snprintf(out, n,
        "-- bm shell sdk project\n"
        "--sdk v=1 t=%d b=%d prg=%d chr=%d wram=%d nbat=%d vert=%d hirom=%d fast=%d rom=%d sram=%d sbat=%d chip=%d lang=%d kern=%d\n",
        p->target, p->nes_board, p->nes_prg_kb, p->nes_chr_kb, p->nes_wram_kb, p->nes_battery, p->nes_vertical,
        p->snes_hirom, p->snes_fast, p->snes_rom_kb, p->snes_sram_kb, p->snes_battery, p->snes_chip, p->bm_lang,
        p->bm_kernels);
    return len < 0 || (size_t)len >= n ? -1 : len;
}

size_t sdk_proj_source_offset(const char *text, size_t len)
{
    size_t i = 0;
    while (i + 6 <= len && !((i == 0 || text[i - 1] == '\n') && !strncmp(text + i, "--sdk ", 6)))
        i++;
    if (i + 6 > len)
        return len;
    while (i < len && text[i] != '\n')
        i++;
    return i < len ? i + 1 : len;
}

const char *sdk_proj_rom_dir(const sdk_proj_t *p)
{
    return p->target == SDK_NES ? "/nes" : p->target == SDK_SNES ? "/snes" : NULL;
}

int sdk_proj_rom_path(const sdk_proj_t *p, const char *stem, char *out, size_t n)
{
    const char *dir = sdk_proj_rom_dir(p);
    if (!dir)
        return -1;
    snprintf(out, n, "%s/%s.%s", dir, stem, p->target == SDK_NES ? "nes" : "sfc");
    return 0;
}

int sdk_proj_error_line(const char *msg)
{
    const char *c = strchr(msg, ':');
    if (!c || c[1] < '0' || c[1] > '9')
        return 0;
    return atoi(c + 1);
}

static int in_list(const int *list, int n, int v)
{
    for (int i = 0; i < n; i++)
        if (list[i] == v)
            return 1;
    return 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int sdk_proj_parse(const char *text, size_t len, sdk_proj_t *p)
{
    sdk_proj_default(p, SDK_NES);
    const char *line = NULL;
    for (size_t i = 0; i + 6 <= len; i++)
        if ((i == 0 || text[i - 1] == '\n') && !strncmp(text + i, "--sdk ", 6)) {
            line = text + i + 6;
            break;
        }
    if (!line)
        return -1;
    const char *end = line;
    while ((size_t)(end - text) < len && *end != '\n')
        end++;
    static const struct { const char *key; int off; } keys[] = {
        { "t", 0 }, { "b", 1 }, { "prg", 2 }, { "chr", 3 }, { "wram", 4 }, { "nbat", 5 }, { "vert", 6 },
        { "hirom", 7 }, { "fast", 8 }, { "rom", 9 }, { "sram", 10 }, { "sbat", 11 }, { "chip", 12 },
        { "lang", 13 }, { "kern", 14 },
    };
    int *field[15] = { &p->target, &p->nes_board, &p->nes_prg_kb, &p->nes_chr_kb, &p->nes_wram_kb, &p->nes_battery,
                       &p->nes_vertical, &p->snes_hirom, &p->snes_fast, &p->snes_rom_kb, &p->snes_sram_kb,
                       &p->snes_battery, &p->snes_chip, &p->bm_lang, &p->bm_kernels };
    while (line < end) {
        while (line < end && *line == ' ')
            line++;
        const char *eq = line;
        while (eq < end && *eq != '=' && *eq != ' ')
            eq++;
        if (eq < end && *eq == '=') {
            char word[16], num[16];
            const char *v = eq + 1, *ve = v;
            while (ve < end && *ve != ' ')
                ve++;
            snprintf(word, sizeof word, "%.*s", (int)(eq - line), line);
            snprintf(num, sizeof num, "%.*s", (int)(ve - v), v);
            for (int k = 0; k < (int)(sizeof keys / sizeof keys[0]); k++)
                if (!strcmp(word, keys[k].key))
                    *field[keys[k].off] = (int)strtol(num, NULL, 10);
            line = ve;
        } else {
            while (line < end && *line != ' ')
                line++;
        }
    }
    p->target = clampi(p->target, 0, SDK_TARGETS - 1);
    p->nes_board = clampi(p->nes_board, 0, N(boards) - 1);
    p->snes_chip = clampi(p->snes_chip, 0, N(chips) - 1);
    p->bm_lang = clampi(p->bm_lang, 0, N(langs) - 1);
    p->bm_kernels = clampi(p->bm_kernels, 0, N(kernels) - 1);
    if (!in_list(nes_prg_list, N(nes_prg_list), p->nes_prg_kb))
        p->nes_prg_kb = 32;
    if (!in_list(nes_chr_list, N(nes_chr_list), p->nes_chr_kb))
        p->nes_chr_kb = 8;
    p->nes_wram_kb = p->nes_wram_kb ? 8 : 0;
    p->nes_battery = !!p->nes_battery;
    p->nes_vertical = !!p->nes_vertical;
    p->snes_hirom = !!p->snes_hirom;
    p->snes_fast = !!p->snes_fast;
    p->snes_battery = !!p->snes_battery;
    if (!in_list(snes_rom_list, N(snes_rom_list), p->snes_rom_kb))
        p->snes_rom_kb = 32;
    if (!in_list(snes_sram_list, N(snes_sram_list), p->snes_sram_kb))
        p->snes_sram_kb = 0;
    nes_fix(p);
    snes_fix(p);
    return 0;
}
