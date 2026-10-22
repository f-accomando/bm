/*
 * Screen recorder for games (VID-REC, off by default: bm/config.txt "rec=on").
 *
 * At the game's present point rec_frame() copies the frame, scaled to the
 * recording size, into a chunk of a ring in RAM. Finished chunks are written
 * to /bm/rec as .bmv files (docs/BM-VIDEO.md, raw codecs) by a fiber that
 * runs in the time the game would wait for the next frame (rec_pump), so the
 * game does not stall on the card. When the ring is full the frame is
 * dropped and counted: the recorder never blocks the game. Plain C, kernel only.
 */
#ifndef BM_REC_H
#define BM_REC_H

#include <stdint.h>

typedef struct {
    int w, h;                   /* the recording size */
    int fps;                    /* 1..30 */
    int format;                 /* BMV_CODEC_RAW565 or BMV_CODEC_RGB332 */
    uint32_t ring_bytes;        /* the ring in RAM */
} rec_cfg_t;

typedef struct {
    uint32_t captured;          /* frames in the ring or on the card */
    uint32_t dropped_full;      /* due, but every chunk of the ring was in use */
    uint32_t chunks;            /* files written */
    uint32_t chunk_fail;        /* files that could not be written */
    uint64_t bytes;             /* written */
    uint32_t write_us;          /* time in fat_write_file, all chunks */
    uint32_t write_worst_us;    /* the longest chunk */
    uint32_t pump_worst_us;     /* the longest rec_pump (the most the game waited) */
    uint32_t slots, slots_high; /* chunks of the ring, and the most in use at once */
    uint32_t run;               /* the number in the file names */
} rec_stats_t;

/* The recording configured by bm/config.txt: rec (on/off), rec_fps, rec_size (WxH), rec_format (rgb565, rgb332),
 * rec_ring_mb. Fills cfg with defaults and returns 1 if rec is on. */
int rec_config(rec_cfg_t *cfg);

/* Starts a recording (allocates the ring: smaller if memory is short). 0, or -1 with a message printed. */
int rec_start(const rec_cfg_t *cfg);
/* Ends it: the rest is written (the game is over), the report printed. */
void rec_stop(void);
int rec_active(void);
/* 1 if a frame is due now (rec_frame would take it). */
int rec_due(void);

/* The frame on show: copies it if one is due. px/stride in pixels, w x h of the game's screen. */
void rec_frame(const uint16_t *px, uint32_t stride, int w, int h);
/* Writes finished chunks until the timer reaches `until` (timer_ticks); at least 1 ms when there is work. */
void rec_pump(uint32_t until);
/* The game is writing to the card (a save, a screenshot): no new chunk is started for a while. */
void rec_note_game_io(void);

const rec_stats_t *rec_stats(void);
/* "REC 123 d0": for the corner of the screen. */
void rec_overlay_text(char *buf, int n);

#endif
