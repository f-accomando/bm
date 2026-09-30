#ifndef UPLOAD_H
#define UPLOAD_H

#include "drivers/fb.h"

/* Receives a cartridge over the serial port (same framing as the
 * chainloader: "BM33" size:u32 crc32:u32 data, replies OK/SE/CE/TO) and
 * plays it: .bm native or .cart s32, detected by the magic. */
void upload_and_play(framebuffer_t *fb);

#endif
