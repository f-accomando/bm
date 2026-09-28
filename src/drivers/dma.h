/*
 * BCM2835 DMA controller: channel allocation (shared with the HDMI audio)
 * and one channel for memory fills and copies, so the CPU does not have to
 * read the SDRAM back (on the ARM1176 reads are ~4x slower than writes).
 * Addresses are ARM addresses of cached or uncached RAM; the caller cleans
 * the data cache of a cached source first (dcache_clean_all).
 */
#ifndef DMA_H
#define DMA_H

#include <stdint.h>

/* First free channel of pref[] among those the firmware leaves to the ARM;
 * -1 if none. */
int dma_channel_claim(const uint8_t *pref, unsigned n);

/* Bus address alias for [arm_start, arm_start + len): the framebuffer is
 * given by the firmware with its own alias, used as is for DMA. */
void dma_map_region(uint32_t arm_start, uint32_t len, uint32_t bus_start);

/* Claims a full (non-lite) channel for dma_fill / dma_copy. */
int dma_init(void);
int dma_ready(void);
int dma_channel(void);

/* Asynchronous: they start the transfer and return at once. len is in
 * bytes, a multiple of 4; dst 4-byte aligned. A new call waits for the
 * previous transfer. */
void dma_fill(void *dst, uint32_t value, uint32_t len);
void dma_copy(void *dst, const void *src, uint32_t len);
int  dma_busy(void);
int  dma_wait(void);                    /* 0, or -1 if it timed out (channel reset) */

#endif
