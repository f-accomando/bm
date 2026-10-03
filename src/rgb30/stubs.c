/*
 * Settings of Pi features that are not on the RGB30 yet (kernel/config.c
 * reads and writes them in bm/config.txt): kept, so the file survives a
 * trip between the two consoles.
 */
#include "audio/audio.h"
#include "kernel/config.h"

#include <string.h>

static int via_ram, volume = AUDIO_VOLUME_MAX, perf, pointer_on = 1;

void bm_set_via_ram(int on)         { via_ram = on; }
int  bm_via_ram(void)               { return via_ram; }
void audio_set_volume(int level)    { volume = level; }
int  audio_volume(void)             { return volume; }
void bm_set_perf(int on)            { perf = on; }
int  bm_perf(void)                  { return perf; }

/* mouse= (the Pi's pointer, M32): read, kept */
void pointer_config(void)
{
    const char *v = config_get("mouse");
    pointer_on = !(v && (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "no")));
}
int pointer_enabled(void)           { return pointer_on; }

/* the Pi 1 B's Ethernet (net.c's other data path): not on the RGB30 */
#include "usb/smsc95xx.h"
static const unsigned char no_mac[6];
const unsigned char *eth_mac(void)          { return no_mac; }
int  eth_linked(void)                       { return 0; }
void eth_poll(void)                         { }
int  eth_recv(void *buf, int max)           { (void)buf; (void)max; return 0; }
int  eth_send(const void *frame, int len)   { (void)frame; (void)len; return -1; }
