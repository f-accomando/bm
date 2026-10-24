/*
 * crt.h: the CRT filter of Settings (settings.c, the same code on the Pi and
 * the RGB30). The runtime applies the filter to each game frame.
 */
#ifndef KERNEL_CRT_H
#define KERNEL_CRT_H

int  crt_mode(void);
const char *crt_choice(void);
int  crt_filter_is_active(void);
void crt_apply_filter(void *pixels, int w, int h, int pitch_bytes);

#endif
