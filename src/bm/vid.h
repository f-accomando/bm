/*
 * .bmv: a short video clip for a cartridge (docs/BM-VIDEO.md). A 32-byte
 * header, the frames (each a baseline JPEG), a frame index and the audio
 * as raw PCM. All numbers are little-endian. This reader works on a clip
 * that is already in memory and never copies it; plain C, kernel and PC.
 */
#ifndef BM_VID_H
#define BM_VID_H

#include <stddef.h>
#include <stdint.h>

#define BMV_HEADER      32
#define BMV_CODEC_MJPEG 1
#define BMV_VERSION     1

typedef struct {
    const uint8_t *data;
    size_t len;
    int codec;                  /* BMV_CODEC_* */
    int width, height;
    int fps;                    /* frames a second, 1..240 */
    uint32_t frames;
    int audio_channels;         /* 0: no audio, else 1 or 2 */
    int audio_bits;             /* 8 (unsigned) or 16 (signed) */
    uint32_t audio_rate;        /* Hz */
    const uint8_t *index;       /* frames * 8 bytes: offset, size */
    const uint8_t *audio;
    size_t audio_len;           /* bytes, a whole number of sample frames */
} bmv_t;

/* Checks the header and every index entry (the frames lie inside the file,
 * after the header); 0, or -1 with a word in err (NULL allowed). */
int bmv_open(bmv_t *v, const uint8_t *data, size_t len, char *err, size_t errlen);
/* The bytes of frame i. */
int bmv_frame(const bmv_t *v, uint32_t i, const uint8_t **p, size_t *n);
/* The frame to show at ms milliseconds from the start (the last one past the end). */
uint32_t bmv_frame_at(const bmv_t *v, uint32_t ms);
/* Frame i decoded to RGB565 into out (width x height values, rows width apart). */
int bmv_decode(const bmv_t *v, uint32_t i, uint16_t *out, char *err, size_t errlen);
/* Length of the clip in milliseconds. */
uint32_t bmv_duration_ms(const bmv_t *v);
/* The audio that goes with the time span [from_ms, to_ms): pointer and bytes
 * (cut at a sample frame; 0 bytes when there is no audio or the span is empty). */
size_t bmv_audio_span(const bmv_t *v, uint32_t from_ms, uint32_t to_ms, const uint8_t **p);

#endif
