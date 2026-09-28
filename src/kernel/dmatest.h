#ifndef DMATEST_H
#define DMATEST_H

#include "drivers/fb.h"

/* Monitor 'D': the DMA channel step by step, each step shown on screen
 * before it runs (a bus hang leaves the culprit visible), CPU against DMA
 * timings. On success .b33 "via RAM" frames are copied by DMA. */
void dma_test(framebuffer_t *fb);

#endif
