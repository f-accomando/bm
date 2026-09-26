#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* Monospace bitmap font, 8 pixels wide: one byte per glyph row, MSB = left.
 * 256 glyphs in code page 437 order. */
typedef struct {
    uint8_t width;
    uint8_t height;
    const uint8_t *glyphs;
} font_t;

extern const font_t font_console_8x16;

#endif
