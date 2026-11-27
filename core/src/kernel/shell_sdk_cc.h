/*
 * Shell SDK: bm C, the C of the NES and SNES projects. A small compiler of its own (original code) that turns C
 * into the assembly of core/src/script/snes_asm.lua (6502 for the NES; for the SNES the same instructions on the
 * 65C816 in native mode with registers of 8 bits), with the runtime of the machine in it (reset, the NMI, the
 * pad, the sprites, the palettes). The language and its limits: docs/api/bm-c.md. Pure model: the tests run on the PC.
 *
 * It reads: byte, sbyte, word, int (and char, unsigned, signed), pointers, arrays, const data in ROM, functions
 * (no recursion: the locals have fixed places), if, while, do, for, break, continue, return, the operators of C,
 * #define of a number, asm("...") and tiles { ... } (the CHR ROM); on the NES the music block (shell_sdk_music.h)
 * with play_song, play_sfx and stop_music. The assembly says in its comments which line
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
    int ram_size;               /* of the RAM of the machine that bm C uses: 2048 (NES), 8192 (SNES) */
} sdk_cc_out_t;

/* Compiles a source of len bytes for the NES (snes 0) or the SNES. Returns 0 with the assembly in *out (free it with sdk_cc_free), or -1 with the
 * first mistake in err, as "main.c:12: 'y' is not declared. Did you mean 'x'?". */
int sdk_cc_compile(const char *src, size_t len, int snes, sdk_cc_out_t *out, char *err, size_t en);
void sdk_cc_free(sdk_cc_out_t *out);

#endif /* SHELL_SDK_CC_H */
