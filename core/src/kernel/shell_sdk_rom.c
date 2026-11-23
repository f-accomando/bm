/*
 * Shell SDK: what the Code tab shows of a ROM (see shell_sdk_rom.h). Original code.
 */
#include "shell_sdk_rom.h"

#include <stdio.h>
#include <string.h>

static const char *mapper_name(int m)
{
    switch (m) {
    case 0: return "NROM";
    case 1: return "MMC1";
    case 2: return "UxROM";
    case 3: return "CNROM";
    case 4: return "MMC3";
    case 7: return "AxROM";
    case 9: return "MMC2";
    case 69: return "FME-7";
    default: return "";
    }
}

static void addn(sdk_rom_t *r, const char *s)
{
    if (r->lines < 5)
        snprintf(r->line[r->lines++], sizeof r->line[0], "%s", s);
}

static void probe_nes(const uint8_t *d, size_t n, sdk_rom_t *r)
{
    r->kind = SDK_ROM_NES;
    const int prg = d[4] * 16, chr = d[5] * 8;
    const int mapper = (d[6] >> 4) | (d[7] & 0xF0);
    char b[56];
    snprintf(b, sizeof b, "NES  PRG %d KB  CHR %d KB", prg, chr);
    addn(r, b);
    const char *name = mapper_name(mapper);
    snprintf(b, sizeof b, "Mapper %d%s%s", mapper, *name ? " " : "", name);
    addn(r, b);
    snprintf(b, sizeof b, "%s mirroring%s%s", d[6] & 8 ? "Four-screen" : d[6] & 1 ? "Vertical" : "Horizontal",
             d[6] & 2 ? ", battery" : "", d[6] & 4 ? ", trainer" : "");
    addn(r, b);
    if (16 + (size_t)prg * 1024 + (size_t)chr * 1024 > n)
        addn(r, "The file is shorter than its header says");
}

/* The header of a SNES ROM at base (0x7FC0 or 0xFFC0 of the data): a score, 0 when it does not fit. */
static int snes_score(const uint8_t *d, size_t n, size_t base)
{
    if (base + 0x40 > n)
        return 0;
    const uint8_t *h = d + base;
    int score = 0;
    const unsigned sum = (unsigned)(h[0x1E] | (h[0x1F] << 8)), inv = (unsigned)(h[0x1C] | (h[0x1D] << 8));
    if ((sum ^ inv) == 0xFFFF)
        score += 4;
    if ((h[0x15] & 1) == (base >= 0xFFC0 ? 1u : 0u))           /* the map mode agrees with where the header is */
        score += 2;
    if (h[0x15] & 0x20)
        score += 1;
    if (h[0x17] >= 7 && h[0x17] <= 13)
        score += 1;
    return score;
}

static int probe_snes(const uint8_t *data, size_t size, sdk_rom_t *r)
{
    const size_t skip = size % 1024 == 512 ? 512 : 0;          /* the copier header of .smc */
    const uint8_t *d = data + skip;
    const size_t n = size - skip;
    const int lo = snes_score(d, n, 0x7FC0), hi = snes_score(d, n, 0xFFC0);
    if (lo < 6 && hi < 6) {
        addn(r, "Not a NES or SNES ROM (no header found)");
        return -1;
    }
    const int hirom = hi > lo;
    const uint8_t *h = d + (hirom ? 0xFFC0 : 0x7FC0);
    r->kind = SDK_ROM_SNES;
    for (int i = 0; i < 21; i++)
        r->title[i] = h[i] >= 32 && h[i] < 127 ? (char)h[i] : ' ';
    r->title[21] = 0;
    for (int i = 20; i >= 0 && r->title[i] == ' '; i--)
        r->title[i] = 0;
    char b[56];
    snprintf(b, sizeof b, "SNES  %s  %s%s", hirom ? "HiROM" : "LoROM", h[0x15] & 0x10 ? "FastROM" : "SlowROM",
             skip ? "  (.smc header)" : "");
    addn(r, b);
    const int rom_kb = h[0x17] < 16 ? 1 << h[0x17] : 0, sram_kb = h[0x18] && h[0x18] < 8 ? 1 << h[0x18] : 0;
    snprintf(b, sizeof b, "ROM %d KB  SRAM %d KB", rom_kb, sram_kb);
    addn(r, b);
    snprintf(b, sizeof b, "Chip type %02X  region %02X", h[0x16], h[0x19]);
    addn(r, b);
    return 0;
}

int sdk_rom_probe(const uint8_t *data, size_t size, sdk_rom_t *r)
{
    memset(r, 0, sizeof *r);
    if (size >= 16 && !memcmp(data, "NES\x1a", 4)) {
        probe_nes(data, size, r);
        return 0;
    }
    return probe_snes(data, size, r);
}

int sdk_rom_hex_bytes(int width)
{
    int b = (width - 8) / 4;                    /* 6 for the address, 1 gap, then 4 cells per byte */
    if (b > 16)
        b = 16;
    if (b < 4)
        b = 4;
    return b;
}

int sdk_rom_hex_row(const uint8_t *data, size_t size, size_t offset, int width, char *out, size_t n)
{
    const int bpr = sdk_rom_hex_bytes(width);
    size_t k = (size_t)snprintf(out, n, "%06X ", (unsigned)offset);
    int shown = 0;
    for (int i = 0; i < bpr && k + 4 < n; i++) {
        if (offset + (size_t)i < size) {
            k += (size_t)snprintf(out + k, n - k, "%02X ", data[offset + (size_t)i]);
            shown++;
        } else
            k += (size_t)snprintf(out + k, n - k, "   ");
    }
    for (int i = 0; i < bpr && k + 2 < n; i++) {
        const unsigned c = offset + (size_t)i < size ? data[offset + (size_t)i] : ' ';
        out[k++] = c >= 32 && c < 127 ? (char)c : '.';
    }
    out[k] = 0;
    return shown;
}
