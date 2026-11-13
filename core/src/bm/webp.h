/*
 * A WebP decoder to RGB565 (the pictures on the SD card, the web page
 * images): VP8 lossy key frames and VP8L lossless. It follows the public
 * RFC 6386 and the WebP container and lossless specifications. The
 * alpha channel, animation and the extended color profiles are ignored
 * or refused. Plain C, kernel and PC.
 *
 * Memory: one malloc per call, freed before the call returns. A lossy
 * picture needs about 1.5 bytes a pixel (rounded up to whole 16x16
 * blocks) plus 4 bytes per block. A lossless picture needs 4 bytes a
 * pixel plus the code tables. A picture over WEBP_MAX_PIXELS is refused.
 */
#ifndef WEBP_H
#define WEBP_H

#include <stddef.h>
#include <stdint.h>

#ifndef WEBP_MAX_PIXELS
#define WEBP_MAX_PIXELS (8u * 1024u * 1024u)
#endif

/* 1 if the bytes start like a WebP file (RIFF, size, WEBP). */
int webp_is(const uint8_t *buf, size_t len);
/* The size of the picture: 0, or -1 if the file is not a readable WebP
 * (w and h are untouched then). */
int webp_info(const uint8_t *buf, size_t len, int *w, int *h);
/* Decodes the picture into out (RGB565, stride in pixels). The picture is
 * cut at the top left corner to maxw x maxh. 0, or -1 if the file is
 * broken, truncated, animated, or too big. */
int webp_decode565(const uint8_t *buf, size_t len, uint16_t *out, int stride, int maxw, int maxh);

#endif
