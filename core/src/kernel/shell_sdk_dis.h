/*
 * Shell SDK: the ASM listing of a ROM. It runs core/src/script/sdk_dis.lua in the Lua of the shell, with the
 * opcode table of the assembler (snes_asm.lua) and, for a SNES ROM, the disassembler core/lua/snesrom.lua.
 */
#ifndef SHELL_SDK_DIS_H
#define SHELL_SDK_DIS_H

#include <stddef.h>
#include <stdint.h>

/* What a listing shows. A NES ROM with a mapper that switches its PRG is read one bank at a time. */
typedef struct {
    int bank, banks;                    /* 0 up; banks is 0 when the listing is not one bank */
    int addr;                           /* where the bank is seen */
    int kb;                             /* its size */
    int guess;                          /* addr was worked out from the calls of the bank */
} sdk_dis_info_t;

/* The listing of a .nes or .sfc, .smc file: lines separated by '\n', in a block from malloc (the caller frees
 * it) that ends with a 0. bank: the PRG bank of a NES ROM that has banks, -1 for the last one. Returns 0, or
 * -1 with the reason in err. */
int sdk_dis(const uint8_t *rom, size_t len, int bank, char **text, size_t *text_len, sdk_dis_info_t *info,
            char *err, size_t en);

#endif /* SHELL_SDK_DIS_H */
