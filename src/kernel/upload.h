#ifndef UPLOAD_H
#define UPLOAD_H

#include "drivers/fb.h"

/* Receives a cartridge over the serial port (same framing as the
 * chainloader: "BMLD" size:u32 crc32:u32 data, replies OK/SE/CE/TO) and
 * plays it (a .bm cartridge, recognised by its magic). */
void upload_and_play(framebuffer_t *fb);

#endif
