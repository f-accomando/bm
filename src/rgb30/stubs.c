/*
 * Settings of Pi features that are not on the RGB30 yet (kernel/config.c
 * reads and writes them in bm/config.txt): kept, so the file survives a
 * trip between the two consoles.
 */
#include <string.h>

#include "audio/audio.h"

static char layout[8] = "us";
static int via_ram, volume = AUDIO_VOLUME_MAX;

void hid_set_layout(const char *name)
{
    strncpy(layout, name, sizeof layout - 1);
}

const char *hid_layout(void)        { return layout; }
void bm_set_via_ram(int on)         { via_ram = on; }
int  bm_via_ram(void)               { return via_ram; }
void audio_set_volume(int level)    { volume = level; }
int  audio_volume(void)             { return volume; }
