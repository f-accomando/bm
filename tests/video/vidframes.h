#ifndef VIDFRAMES_H
#define VIDFRAMES_H

#include <stddef.h>
#include <stdint.h>

/* A baseline JPEG frame, 320x180 (vidframes.c). */
extern const uint8_t vidframe_jpg320[];
extern const unsigned vidframe_jpg320_len;

/* The pixel-art test frame (w x h values). */
void vidframe_synth_frame(uint16_t *px, int w, int h);

#endif
