/*
 * nano8 cartridges: the .p8 text format and the .p8.png image format.
 *
 * .p8.png: a 160x205 PNG; each pixel hides one byte in the two low bits of
 * its channels (A R G B, high to low): 0x0000-0x42ff the cart's ROM
 * (sprites, map, flags, music, sound effects), 0x4300-0x7fff the code,
 * plain or compressed (":c:" or "\0pxa"), 0x8000 the format version. The
 * picture shows the label at (16, 24).
 * .p8: text in sections (__lua__, __gfx__, __gff__, __label__, __map__,
 * __sfx__, __music__), the symbols written as Unicode.
 * The code comes out in P8SCII, one byte per character.
 */
#ifndef N8CART_H
#define N8CART_H

#include <stddef.h>
#include <stdint.h>

#include "n8.h"

#define N8_LOAD_LABEL   1           /* only the label, title and author */

typedef struct {
    uint8_t rom[N8_ROM_SIZE];
    char *code;                     /* malloc'd, P8SCII, code_len bytes + 0 */
    size_t code_len;
    uint16_t *label;                /* 128x128 RGB565, malloc'd, or NULL */
    int version;
    char title[48], author[48];     /* from the first two comment lines */
} n8_cart_t;

/* Reads a cartridge (either format, told apart by its first bytes).
 * Returns 0, or -1 with a message. Free with n8_cart_free. */
int  n8_cart_load(const uint8_t *data, size_t len, int flags, n8_cart_t *c, char *err, size_t errlen);
void n8_cart_free(n8_cart_t *c);

/* Inflates a zlib stream into out (at most cap bytes). Returns the
 * length, or -1. */
long n8_inflate(const uint8_t *src, size_t len, uint8_t *out, size_t cap);
/* A PNG as RGBA8888 (malloc'd). Returns 0, or -1. */
int  n8_png_rgba(const uint8_t *png, size_t len, uint8_t **rgba, int *w, int *h);
/* Decompresses the code area (0x4300-0x7fff of a .p8.png). Returns the
 * malloc'd code, or NULL. */
char *n8_code_unpack(const uint8_t *area, size_t len, size_t *out_len);
/* UTF-8 text of a .p8 to P8SCII; returns the new length (in place). */
size_t n8_utf8_to_p8(char *s, size_t len);

#endif
