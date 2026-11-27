/*
 * Shell SDK: the builder. It runs the assembler of the kernel (core/src/script/snes_asm.lua, in the Lua
 * of the shell) on the source of the Code tab and builds a .nes (6502, iNES header) or a .sfc (65C816, SNES
 * header) from the header that the Project tab makes (sdk_proj_header).
 */
#ifndef SHELL_SDK_BUILD_H
#define SHELL_SDK_BUILD_H

#include <stddef.h>
#include <stdint.h>

#include "shell_sdk_proj.h"

/* What a build took. */
typedef struct {
    unsigned rom;           /* bytes of program and data in the ROM */
    unsigned rom_size;      /* of the ROM of the board that hold the program (NES: the PRG) */
    int ram;                /* bm C: bytes of RAM of the variables; -1 for assembly */
    int ram_size;           /* of the RAM that bm C uses on the machine */
} sdk_build_info_t;

/* 1 when the source of the project is bm C (main.c). */
int sdk_build_is_c(const sdk_proj_t *p);

/* Assembles and builds; a bm C source is compiled first. info may be NULL. name is the project stem (the SNES title, and the .chr file of a NES project:
 * /bmr/NAME.chr is the CHR ROM when the board has CHR ROM). Returns 0 with the ROM in a block from malloc
 * (the caller frees it), or -1 with the assembler's message in err ("main.s:12: unknown instruction ..."). */
int sdk_build(const sdk_proj_t *p, const char *name, const char *source, size_t len, uint8_t **rom, size_t *rom_len,
              sdk_build_info_t *info, char *err, size_t en);

/* Writes the ROM to /nes or /snes (the folder is made; a file of the same name is replaced).
 * Returns 0, or -1 with the reason in err. */
int sdk_build_write(const sdk_proj_t *p, const char *name, const uint8_t *rom, size_t rom_len, char *err, size_t en);

#endif /* SHELL_SDK_BUILD_H */
