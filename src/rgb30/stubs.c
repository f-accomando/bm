/*
 * Settings of Pi features that are not on the RGB30 yet (kernel/config.c
 * reads and writes them in bm/config.txt): kept, so the file survives a
 * trip between the two consoles.
 */
#include "audio/audio.h"

static int volume = AUDIO_VOLUME_MAX;

void audio_set_volume(int level)    { volume = level; }
int  audio_volume(void)             { return volume; }

/* the Pi 1 B's Ethernet (net.c's other data path): not on the RGB30 */
#include "usb/smsc95xx.h"
static const unsigned char no_mac[6];
const unsigned char *eth_mac(void)          { return no_mac; }
int  eth_linked(void)                       { return 0; }
void eth_poll(void)                         { }
int  eth_recv(void *buf, int max)           { (void)buf; (void)max; return 0; }
int  eth_send(const void *frame, int len)   { (void)frame; (void)len; return -1; }

/* the Pi's fibers (the Market's jobs, src/kernel/fiber.c): none on the
 * RGB30, so net_wait_step always polls the network in place */
#include "kernel/fiber.h"
fiber_t *fiber_current(void)        { return 0; }
void fiber_yield(void)              { }
int  fiber_cancelled(void)          { return 0; }
