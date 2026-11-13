/*
 * Decodes a picture a cartridge got from the web (or from anywhere) into RGB565 pixels
 * with an opacity flag, for image_load: baseline JPEG (jpeg.c), PNG (png.c) and WebP
 * (webp.c). The size is read from the file header first, so a huge picture is refused
 * before it is decoded. Plain C, kernel and PC.
 */
#ifndef WEBIMG_H
#define WEBIMG_H

#include <stddef.h>
#include <stdint.h>

#define WEBIMG_MAX_PIXELS (4u * 1024u * 1024u)  /* the largest picture decoded */

typedef struct {
    int w, h;
    uint16_t *px;               /* w x h, RGB565 */
    uint8_t *alpha;             /* w x h, 1 = opaque (PNG alpha >= 128; JPEG and WebP: all 1) */
} webimg_t;

/* "jpeg", "png", "webp", or NULL if the bytes are none of them. */
const char *webimg_kind(const uint8_t *data, size_t len);
/* The picture cut at the top left corner to maxw x maxh (0 or less: no cut) into out
 * (malloc'd; webimg_free). 0, or -1 with the reason in err. */
int webimg_decode(const uint8_t *data, size_t len, int maxw, int maxh, webimg_t *out, char *err, size_t en);
void webimg_free(webimg_t *im);

#endif
