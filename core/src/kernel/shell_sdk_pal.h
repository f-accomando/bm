/*
 * Shell SDK, Sprite tab: the colours the colour numbers of the tiles show with. Greys, the palettes written in
 * the code of the project (NES: the label "palette", 4 colours each; bm C: the calls sprite_palette and
 * background_palette; SNES: the label "sprites_pal", 16 colours of BGR555 each, or the calls sprite_palette
 * and palette of bm C) and, for tiles of more than 4 colours, a set of 16. Pure model: the tests run on the PC.
 */
#ifndef SHELL_SDK_PAL_H
#define SHELL_SDK_PAL_H

#include <stddef.h>
#include <stdint.h>

#define SDK_PAL_CODE_MAX 8              /* palettes of the code */

/* Greys, nothing read. */
void sdk_pal_reset(void);
/* The palettes of a source for a project: kind SDK_SPRITE_* (shell_sdk_sprite.h). The choice stays when the
 * source still has it, else greys. */
void sdk_pal_load(int kind, const char *src, size_t len);
/* The next palette, round. */
void sdk_pal_next(void);
/* "greys", "SPR0 of the code", "16 colours"... */
const char *sdk_pal_name(void);
/* The palettes that can be chosen, greys included. */
int sdk_pal_count(void);
/* The colour 0xRRGGBB of a colour number (1 or more) of a tile with `colours` colour numbers. */
uint32_t sdk_pal_rgb(int number, int colours);

#endif /* SHELL_SDK_PAL_H */
