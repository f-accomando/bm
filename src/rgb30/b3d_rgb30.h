/* The 3D Bench on the RGB30 (b3d_rgb30.c): the menu's Dev tab. */
#ifndef B3D_RGB30_H
#define B3D_RGB30_H

#include "drivers/fb.h"
#include "plat.h"

void rgb30_bench3d(framebuffer_t *fb);
/* only the tests listed (ids, commas between), all profiles; NULL = the whole bench (a part is not compared with the old reports) */
void rgb30_bench3d_part(framebuffer_t *fb, const char *only_tests);

#endif
