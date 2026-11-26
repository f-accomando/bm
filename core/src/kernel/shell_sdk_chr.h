/*
 * Shell SDK: the sheet of the Sprite tab as assembly in the source of the project. The tiles are a block in the
 * ;@sprites section, from a line ";@tiles" to a line ";@tiles-end": for a NES project a .chr block (2 bits a
 * pixel, 16 bytes a tile; the build puts it in the CHR ROM), for a SNES project bytes after the label
 * sprites_chr (4 bits a pixel, 32 bytes a tile). The tile at row r, column c of the sheet is tile r * 16 + c.
 * No screen, no keyboard: the tests run on the PC.
 */
#ifndef SHELL_SDK_CHR_H
#define SHELL_SDK_CHR_H

#include <stddef.h>

/* The format the tiles of a project are written with (SDK_TILE_NES or SDK_TILE_SNES4). nes: a NES project. */
int sdk_chr_format(int nes);

/* The block for the sheet, from ";@tiles" to ";@tiles-end" with its '\n', in out (at most max bytes with the
 * end byte). Returns its length, 0 when the sheet is empty (no block), or -1 when it does not fit. */
long sdk_chr_text(int nes, char *out, size_t max);

/* Reads the block of a source into the sheet; no block: an empty sheet. It reads .db and .byte with numbers
 * ($ hex, % binary or decimal), .res and .ds, labels, .chr, .endchr and comments. Returns 0, or -1 with the
 * line in err; the sheet stays as it was then. */
int sdk_chr_read(int nes, const char *src, size_t len, char *err, size_t en);

/* Writes the sheet into the text of the Code tab (shell_sdk_edit.h), as one step of its undo: the old block is
 * replaced, a new one goes before the ;@end of the ;@sprites section (the section is made at the end of the
 * text when there is none), an empty sheet removes the block. run as in sdk_edit_replace (the pixels painted on
 * one tile are one step). Returns 0, or -1 with the reason in err. */
int sdk_chr_commit(int nes, int run, char *err, size_t en);

#endif /* SHELL_SDK_CHR_H */
