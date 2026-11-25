/*
 * Shell SDK: the ASM listing of a ROM. It runs core/src/script/sdk_dis.lua in the Lua of the shell, with the
 * opcode table of the assembler (snes_asm.lua) and, for a SNES ROM, the disassembler core/lua/snesrom.lua.
 */
#ifndef SHELL_SDK_DIS_H
#define SHELL_SDK_DIS_H

#include <stddef.h>
#include <stdint.h>

/* The listing of a .nes (mapper 0) or .sfc, .smc file: lines separated by '\n', in a block from malloc (the
 * caller frees it) that ends with a 0. Returns 0, or -1 with the reason in err. */
int sdk_dis(const uint8_t *rom, size_t len, char **text, size_t *text_len, char *err, size_t en);

#endif /* SHELL_SDK_DIS_H */
