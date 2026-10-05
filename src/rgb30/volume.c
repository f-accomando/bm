/*
 * The RGB30's volume keys (+ and - on its side): they change the sound's
 * volume everywhere, in the menu, the pages and the games, with a bar over
 * the screen for a moment (notice.c); held, they go on. The level goes to
 * bm/config.txt (volume=, as on the Pi) once the keys rest for 2 s.
 */
#include "pad.h"
#include "audio/audio.h"
#include "kernel/config.h"
#include "kernel/notice.h"
#include "drivers/timer.h"
#include "lib/printf.h"

void volume_keys(uint32_t held, int beep)
{
    static uint32_t prev, next_ms, changed_ms;
    static int dirty;
    const uint32_t now = timer_ticks() / 1000;
    const uint32_t keys = held & (PAD_VOLUP | PAD_VOLDN);
    const int dir = keys == PAD_VOLUP ? 1 : keys == PAD_VOLDN ? -1 : 0;
    int step = 0;
    if (dir && (keys & ~prev)) {
        step = dir;
        next_ms = now + 400;                    /* held: again after 0.4 s, then quicker */
    } else if (dir && (int32_t)(now - next_ms) >= 0) {
        step = dir;
        next_ms = now + 120;
    }
    prev = keys;
    if (step) {
        int v = audio_volume() + step;
        v = v < 0 ? 0 : v > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : v;
        audio_set_volume(v);                    /* at the ends the bar shows again */
        char line[NOTICE_LEN];
        if (v)
            ksnprintf(line, sizeof line, "%d / %d", v, AUDIO_VOLUME_MAX);
        else
            ksnprintf(line, sizeof line, "muted");
        notice_flash("Volume", line, v * 1000 / AUDIO_VOLUME_MAX, 1200);
        kprintf("volume: %s\n", line);
        if (beep && v)
            audio_note(0, 880, 70, 4, 140);     /* a beep at the new volume (the menu only) */
        dirty = 1;
        changed_ms = now;
    }
    if (dirty && !keys && now - changed_ms > 2000) {
        config_save();
        dirty = 0;
    }
}
