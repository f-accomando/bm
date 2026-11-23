/*
 * Shell SDK: what the Code tab shows of a ROM. Pure model: the tests run on the PC.
 * It reads the header of a NES ROM (iNES) and of a SNES ROM (.sfc, .smc with or without the copier header).
 */
#ifndef SHELL_SDK_ROM_H
#define SHELL_SDK_ROM_H

#include <stddef.h>
#include <stdint.h>

enum { SDK_ROM_UNKNOWN, SDK_ROM_NES, SDK_ROM_SNES };

typedef struct {
    int kind;                           /* SDK_ROM_* */
    char title[24];                     /* the name in the header ("" for a NES ROM) */
    char line[5][56];                   /* the facts, one per row */
    int lines;
} sdk_rom_t;

/* Reads the header of a ROM. Returns 0, or -1 when it is no NES or SNES ROM (kind SDK_ROM_UNKNOWN, one line
 * that says what is wrong). */
int sdk_rom_probe(const uint8_t *data, size_t size, sdk_rom_t *r);

/* Bytes in a row of the hex dump for a width in cells (4 to 16). */
int sdk_rom_hex_bytes(int width);

/* One row of the hex dump: "000040 4C 61 62 ..  La.." in at most width cells. Returns the bytes shown. */
int sdk_rom_hex_row(const uint8_t *data, size_t size, size_t offset, int width, char *out, size_t n);

#endif /* SHELL_SDK_ROM_H */
