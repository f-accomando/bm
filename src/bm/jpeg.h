/*
 * A baseline JPEG decoder (the textures of the image-to-3D services, the
 * pictures on the SD card): Huffman, 8 bits, 1 or 3 components, any
 * sampling (chroma copied, not smoothed), restart markers. Progressive
 * and arithmetic files are refused. Plain C, kernel and PC.
 */
#ifndef JPEG_H
#define JPEG_H

#include <stddef.h>
#include <stdint.h>

/* 1 if the bytes start like a JPEG file. */
int jpeg_is(const uint8_t *data, size_t len);
/* The picture as RGB (3 bytes a pixel, malloc'd): 0, or -1 with a word in
 * err (NULL allowed) saying why. */
int jpeg_decode(const uint8_t *data, size_t len, uint8_t **rgb, int *w, int *h, char *err, size_t errlen);

#endif
