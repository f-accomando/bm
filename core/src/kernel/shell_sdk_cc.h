/*
 * Shell SDK: bm C, the C of the NES projects. A small compiler of its own (original code) that turns C into the
 * 6502 assembly of core/src/script/snes_asm.lua, with the runtime of the machine in it (reset, the NMI, the pad,
 * the sprites, the palettes). The language and its limits: docs/api/bm-c.md. Pure model: the tests run on the PC.
 *
 * It reads: byte, sbyte, word, int (and char, unsigned, signed), pointers, arrays, const data in ROM, functions
 * (no recursion: the locals have fixed places), if, while, do, for, break, continue, return, the operators of C,
 * #define of a number, asm("...") and tiles { ... } (the CHR ROM). The assembly says in its comments which line
 * of C each part comes from. The messages say what is wrong and how to put it right.
 */
#ifndef SHELL_SDK_CC_H
#define SHELL_SDK_CC_H

#include <stddef.h>

typedef struct {
    char *text;                 /* the assembly, NUL-terminated */
    size_t len;
    int *line_of;               /* the line of C of each line of the assembly (0: none), lines entries */
    int lines;
    int ram;                    /* bytes of RAM that the variables of the program take */
} sdk_cc_out_t;

/* Compiles a source of len bytes. Returns 0 with the assembly in *out (free it with sdk_cc_free), or -1 with the
 * first mistake in err, as "main.c:12: 'y' is not declared. Did you mean 'x'?". */
int sdk_cc_compile(const char *src, size_t len, sdk_cc_out_t *out, char *err, size_t en);
void sdk_cc_free(sdk_cc_out_t *out);

#endif /* SHELL_SDK_CC_H */
