#ifndef BM_VIDBENCH_H
#define BM_VIDBENCH_H

#include <stddef.h>
#include <stdint.h>

/* Two baseline JPEG frames for the benchmark (vidbench_data.c). */
extern const uint8_t vidbench_jpg320[], vidbench_jpg480[];
extern const unsigned vidbench_jpg320_len, vidbench_jpg480_len;

/*
 * Video bench (VID-S1, VID-S2): what a clip player and a recorder cost on
 * this machine, with the console print as the report.
 *   1. SD write: 128 files of 256 KB (32 MB) in /bm/vidbench with
 *      fat_write_file, each timed; mean MB/s, the best and the worst write.
 *   2. SD read: 16 of those files read back; MB/s.
 *   3. JPEG decode: 100 calls of jpeg_decode on a 320x180 and on a 480x270
 *      frame; mean, best and worst ms.
 *   4. RLE565 encode (the recorder's codec 4): bytes and ms for a 320x180
 *      pixel-art test frame and for a photo frame, an exact round trip, and the MB/s the card must
 *      take at 15 and 30 fps against the SD write mean of step 1.
 * quick (monitor line "video quick") runs a sixteenth of each count, for
 * the QEMU test. The files are deleted at the end. Esc / PS / Start+Select stops it.
 */
void bm_video_bench(int quick);

/* The pixel-art test frame of step 4 (w x h values). */
void vidbench_synth_frame(uint16_t *px, int w, int h);

/* The part of a byte count that makes a "x.yy" figure (for the report). */
uint32_t vidbench_mbps100(uint64_t bytes, uint32_t us);

#endif
