/* bmhost: the state shared by the runner (bmhost.c) and the replaced
 * kernel services (stubs.c). */
#ifndef BMHOST_H
#define BMHOST_H

#include <stdint.h>

typedef struct {
    const char *sd;             /* the directory that plays the SD card */
    int quiet;                  /* no kernel log on stderr */
    uint32_t pad[4];            /* HID_* bits held by each player */
    float stick[4][2], rstick[4][2];
    uint8_t keys[16];           /* keyboard usages held */
    int nkeys;
    char typed[4096];           /* serial characters still to come */
    int typed_len, typed_pos;
    int quit_now;
    int source;                 /* hid_last_source() */
} host_t;

extern host_t host;

/* real time on the PC, microseconds */
double host_real_us(void);
/* a frame is on screen (called by fb_flip) */
void host_frame(const uint16_t *px, int w, int h, int stride);
/* samples of the synthesizer, 48 kHz mono */
void host_audio(const int16_t *s, unsigned n);

#endif
