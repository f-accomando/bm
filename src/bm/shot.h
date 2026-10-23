/*
 * Screenshots (docs/SCREENSHOTS.md): a frame of RGB565 pixels as a PNG, to be
 * written as bm/shots/<GAME><NNNN>.png. No library: a zlib stream of one
 * fixed-Huffman block, whose matches are the pixel to the left and the pixel
 * above (a flat colour costs a few bits), then the PNG chunks.
 */
#ifndef SHOT_H
#define SHOT_H

#include <stddef.h>
#include <stdint.h>

/* The PNG of w x h RGB565 pixels (`stride` pixels a row), malloc'd (the
 * caller frees it); its size in *len. NULL if there is no memory or the
 * size is not 1..4096 a side. */
uint8_t *shot_png(const uint16_t *px, int stride, int w, int h, size_t *len);

/* The file name a screenshot gets: the first letters of `title` (up to 4,
 * lower case letters and digits; "shot" if none) or of `prefix` if it is
 * not NULL or empty, then the number `n` (0..9999): "pong0007.png".
 * Returns 0, or -1 for a bad number. */
int shot_name(char out[13], const char *title, const char *prefix, int n);

/* The number in `name` if it is one of this prefix's ("pong0007.png" for
 * "pong": 7), else -1: for the next number, one more than the highest. */
int shot_number(const char *name, const char prefix[5]);

/* The prefix (up to 4 characters) that shot_name makes from title/prefix. */
void shot_prefix(char out[5], const char *title, const char *prefix);

#endif
