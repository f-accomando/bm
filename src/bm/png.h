#ifndef PNG_H
#define PNG_H

#include <stddef.h>
#include <stdint.h>

/* Inflates a zlib stream (2-byte header, deflate blocks) into out: the
 * bytes written, or -1 if broken or over cap. */
long png_inflate(const uint8_t *src, size_t len, uint8_t *out, size_t cap);
/* A PNG as RGBA (malloc'd): 0, or -1 if it cannot be read. */
int  png_rgba(const uint8_t *png, size_t len, uint8_t **rgba, int *w, int *h);

#endif
