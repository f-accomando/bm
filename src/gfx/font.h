#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* Monospace bitmap font, up to 8 pixels wide: one byte per glyph row, MSB =
 * left. 256 glyphs in code page 437 order. */
typedef struct {
    uint8_t width;
    uint8_t height;
    const uint8_t *glyphs;
} font_t;

extern const font_t font_console_8x16;
extern const font_t font_console_8x14;     /* .bm print(): font("8x14") */
extern const font_t font_console_6x12;     /* font("6x12"): 106 x 30 at 640x360 */

#endif
