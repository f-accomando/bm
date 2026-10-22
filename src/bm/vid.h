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
#define BMV_CODEC_RAW565 2      /* a frame is width*height RGB565 values (the recorder) */
#define BMV_CODEC_RGB332 3      /* a frame is width*height bytes, rrrgggbb */
#define BMV_CODEC_RLE565 4      /* a frame is RLE tokens over width*height RGB565 values (the recorder), see BMV_RLE565 below */
#define BMV_VERSION     1

/* RLE565 frame: tokens, no header, until width*height pixels are made. A token byte holds a type (top 2 bits) and
 * a count - 1 (low 6 bits, so 1 to 64 pixels): 0 literal (count RGB565 values follow, 2 bytes each, little-endian),
 * 1 repeat (one value follows, repeated count times), 2 copy above (the count pixels of the row above, none in
 * row 0), 3 reserved. Lossless; every frame is a key frame. */
#define BMV_RLE565_MAX(w, h) ((size_t)(h) * ((size_t)(w) * 2u + ((size_t)(w) + 63u) / 64u))   /* the largest frame */

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
/* Frame i decoded to RGB565 (a raw codec is copied or expanded) into out (width x height values, rows width apart). */
int bmv_decode(const bmv_t *v, uint32_t i, uint16_t *out, char *err, size_t errlen);
/* RLE565 encoder: one row of w pixels to out (BMV_RLE565_MAX(w, 1) bytes at most). prev is the row above, NULL
 * for row 0. Returns the bytes written. Runs never cross a row. */
size_t bmv_rle565_row(uint8_t *out, const uint16_t *cur, const uint16_t *prev, int w);
/* A whole picture (w x h values, rows w apart) to out (BMV_RLE565_MAX(w, h) bytes at most); returns the bytes. */
size_t bmv_rle565_encode(uint8_t *out, const uint16_t *px, int w, int h);
/* RLE565 decoder: n bytes at p to w x h values (rows w apart). 0, or -1 if the tokens do not make exactly w x h
 * pixels, use the reserved type, copy above in row 0, or run past the end of the frame. */
int bmv_rle565_decode(const uint8_t *p, size_t n, uint16_t *out, int w, int h);
/* Length of the clip in milliseconds. */
uint32_t bmv_duration_ms(const bmv_t *v);
/* The audio that goes with the time span [from_ms, to_ms): pointer and bytes
 * (cut at a sample frame; 0 bytes when there is no audio or the span is empty). */
size_t bmv_audio_span(const bmv_t *v, uint32_t from_ms, uint32_t to_ms, const uint8_t **p);

#endif
