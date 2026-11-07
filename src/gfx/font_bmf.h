/*
 * font_bmf.h: Dynamic BareMetal Font (.bmf) loading and management.
 */
#ifndef FONT_BMF_H
#define FONT_BMF_H

#include "gfx/font.h"

#define BMF_MAGIC       "BMFONT"
#define BMF_VERSION     1
#define BMF_HEADER_SIZE 32
#define BMF_DIR         "/bmf"

/* Scan /bmf and built-ins for available font names. Returns count. */
int font_bmf_scan(char list[][16], int max_fonts);

/* Find or load a font by name (built-in or from /bmf/<name>.bmf) */
const font_t *font_find(const char *name);

#endif
