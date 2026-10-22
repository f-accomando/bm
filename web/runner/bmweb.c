/*
 * bmweb: the browser runner of the web player (docs/design/bm-web-player.md, stage 3).
 * It plays a .bm cartridge with the console's own runtime (src/bm/runtime.c) and the PC's
 * kernel stubs (tests/host/stubs.c), built with Emscripten and -sASYNCIFY.
 *
 *   bmweb_start(cart, len)   plays until the cartridge ends (a long call: Asyncify gives
 *                            the page its time between frames)
 *   bmweb_input(pad, lx, ly, rx, ry)   buttons (HID_* bits of src/usb/hid.h) and sticks
 *   bmweb_quit()             leave the cartridge (as Start+Select)
 *
 * The page gives four JavaScript hooks (web/player.js): Module.bmFrame(rgbaPtr, w, h),
 * Module.bmAudio(floatPtr, frames), Module.bmPace() (a promise: the next 60 Hz time) and
 * Module.bmDone() (the cartridge has ended).
 */
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "host.h"
#include "audio/audio.h"
#include "bm/runtime.h"
#include "drivers/fb.h"
#include "usb/hid.h"

EM_JS(void, js_frame, (const uint8_t *rgba, int w, int h), {
    if (Module.bmFrame) Module.bmFrame(rgba, w, h);
});
EM_JS(void, js_audio, (const float *s, int n), {
    if (Module.bmAudio) Module.bmAudio(s, n);
});
EM_JS(void, js_done, (void), {
    if (Module.bmDone) Module.bmDone();
});
EM_ASYNC_JS(void, js_pace, (void), {
    if (Module.bmPace) await Module.bmPace();
});

static uint8_t *rgba;
static size_t rgba_size;

void host_frame(const uint16_t *px, int w, int h, int stride)
{
    const size_t need = (size_t)w * h * 4;
    if (need > rgba_size) {
        rgba = realloc(rgba, need);
        rgba_size = need;
    }
    uint8_t *o = rgba;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++, o += 4) {
            const uint16_t c = px[y * stride + x];
            o[0] = (uint8_t)((c >> 11) * 255 / 31);
            o[1] = (uint8_t)((c >> 5 & 63) * 255 / 63);
            o[2] = (uint8_t)((c & 31) * 255 / 31);
            o[3] = 255;
        }
    js_frame(rgba, w, h);
    js_pace();                          /* the page draws and waits for the next frame time */
}

void host_audio(const float *s, unsigned n)
{
    js_audio(s, (int)n);
}

EMSCRIPTEN_KEEPALIVE void bmweb_input(uint32_t pad, float lx, float ly, float rx, float ry)
{
    host.pad[0] = pad;
    host.stick[0][0] = lx;
    host.stick[0][1] = ly;
    host.rstick[0][0] = rx;
    host.rstick[0][1] = ry;
}

EMSCRIPTEN_KEEPALIVE void bmweb_quit(void)
{
    host.quit_now = HID_QUIT_KEY;
}

EMSCRIPTEN_KEEPALIVE int bmweb_start(const uint8_t *cart, int len)
{
    host.quiet = 0;                     /* the kernel log goes to the browser console */
    host.source = HID_SOURCE_KEYBOARD;
    host.sd = "/sd";                    /* the SD card: memory file system */
    mkdir("/sd", 0777);
    mkdir("/sd/bm", 0777);
    mkdir("/sd/bm/save", 0777);
    for (int p = 0; p < 4; p++)
        host.dev[p] = -1;
    audio_init();
    framebuffer_t fb;
    fb_init(&fb, 640, 360, 2);
    bm_stats_t st;
    bm_play(&fb, cart, (size_t)len, 31536000, &st);    /* a year: until the cartridge quits */
    js_done();                          /* an export that sleeps returns nothing: the page is told here */
    return 0;
}
