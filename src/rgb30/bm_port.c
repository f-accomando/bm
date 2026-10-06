/*
 * What the .bm runtime (src/bm) asks of the Pi that the RGB30 does not
 * have yet, so that the Pi's cartridges run here unchanged: the V3D (the
 * 3D is drawn by the ARM, as on the Pi without its GPU), the DMA engine
 * (the CPU copies), the firmware's clock queries, the services of the
 * image-to-3D tools, the development assistant's network. The controls
 * come from rgb30/bm_input.c, the sound from audio.c (rk_audio.c).
 */
#include "ai/lua_music.h"
#include "gpu/gpu3d.h"
#include "gpu/v3d.h"
#include "drivers/dma.h"
#include "drivers/prop.h"
#include "net/img3d.h"
#include "kernel/irq.h"
#include "usb/usb.h"
#include "lua.h"
#include "lauxlib.h"

#include <string.h>

/* --- the V3D: absent, so r3d draws on the ARM --- */

int gpu3d_init(void)                                    { return -1; }
const char *gpu3d_status(void)                          { return "no GPU driver on the RGB30 yet"; }
int gpu3d_ready(void)                                   { return 0; }
int gpu3d_failed(void)                                  { return 1; }
const r3d_backend_t *gpu3d_backend(void)                { return NULL; }
void gpu3d_set_fb(const void *mem, uint32_t size, uint32_t bus)
{
    (void)mem; (void)size; (void)bus;
}
void gpu3d_set_size(int w, int h)                       { (void)w; (void)h; }
int gpu3d_pending(void)                                 { return 0; }
void gpu3d_page(int uniform, uint16_t c)                { (void)uniform; (void)c; }
int gpu3d_cleared(void)                                 { return 0; }
void gpu3d_drop(void)                                   { }
int gpu3d_flush(const g16_t *g, int keep)               { (void)g; (void)keep; return 0; }
int gpu3d_submit(const g16_t *g, int keep)              { (void)g; (void)keep; return 0; }
int gpu3d_sync(void)                                    { return 0; }
int gpu3d_inflight(void)                                { return 0; }
void gpu3d_set_queue(int on)                            { (void)on; }
int gpu3d_queue(void)                                   { return 0; }
int gpu3d_queue_ok(void)                                { return 0; }
void gpu3d_set_msaa(int on)                             { (void)on; }
int gpu3d_msaa(void)                                    { return 0; }
int gpu3d_msaa_on(void)                                 { return 0; }
int gpu3d_vshader(void)                                 { return 0; }
void gpu3d_set_vshader(int on)                          { (void)on; }
int gpu3d_vshader_on(void)                              { return 0; }
void gpu3d_take_stats(gpu3d_stats_t *s)                 { memset(s, 0, sizeof *s); }
void gpu3d_peek_stats(gpu3d_stats_t *s)                 { memset(s, 0, sizeof *s); }
int v3d_init(void)                                      { return -1; }
const char *v3d_status(void)                            { return "no V3D (RGB30: Mali-G52)"; }

/* --- the DMA engine: none (the copies are the CPU's) --- */

int  dma_ready(void)                                    { return 0; }
void dma_fill(void *dst, uint32_t value, uint32_t len)
{
    uint32_t *p = dst;
    for (uint32_t i = 0; i < len / 4; i++)
        p[i] = value;
}
void dma_copy(void *dst, const void *src, uint32_t len) { memcpy(dst, src, len); }
int  dma_wait(void)                                     { return 0; }

/* --- the Pi firmware's clocks: unknown here --- */

int prop_query(uint32_t tag, uint32_t *vals, unsigned n)
{
    (void)tag; (void)vals; (void)n;
    return -1;
}
uint32_t prop_clock_rate(uint32_t clock_id)             { (void)clock_id; return 0; }
uint32_t prop_clock_max(uint32_t clock_id)              { (void)clock_id; return 0; }

/* --- image to 3D (Meshy), the assistant's networks: not on the RGB30 (its
 * music is: ai.music, src/ai/lua_music.c) --- */

const img3d_provider_t *img3d_provider(const char *name) { (void)name; return NULL; }
const char *img3d_provider_name(int i)                  { (void)i; return NULL; }
const char *img3d_key_name(const img3d_provider_t *p)   { (void)p; return "meshy_key"; }
int img3d_start(const img3d_provider_t *p, const char *key, const uint8_t *image, size_t len,
                const char *url, int polycount, char *task, size_t tasklen, char *err, size_t errlen)
{
    (void)p; (void)key; (void)image; (void)len; (void)url; (void)polycount; (void)task;
    (void)tasklen;
    strncpy(err, "not on the RGB30", errlen);
    return -1;
}
int img3d_status(const img3d_provider_t *p, const char *key, const char *task, int *progress,
                 char *model_url, size_t urllen, char *err, size_t errlen)
{
    (void)p; (void)key; (void)task; (void)progress; (void)model_url; (void)urllen;
    strncpy(err, "not on the RGB30", errlen);
    return -1;
}
int img3d_download(const char *url, size_t max, uint8_t **data, size_t *len, char *err, size_t errlen)
{
    (void)url; (void)max; (void)data; (void)len;
    strncpy(err, "not on the RGB30", errlen);
    return -1;
}

static int not_here(lua_State *L)
{
    return luaL_error(L, "not on the RGB30 yet");
}

static void module(lua_State *L, const char *name)
{
    lua_newtable(L);
    lua_pushcfunction(L, not_here);
    lua_setfield(L, -2, "unavailable");
    lua_setglobal(L, name);
}

void ai_set_lang(int lang, int follow)                  { (void)lang; (void)follow; }
int ai_lang(void)                                       { return 0; }
int ai_lang_follows(void)                               { return 1; }
/* the assistant's network is not here, its music is: ai.music picks the
 * recipe from the words alone (mus_guess) */
void ai_lua_open(lua_State *L)
{
    module(L, "ai");
    lua_getglobal(L, "ai");
    lua_pushcfunction(L, ai_lua_music);
    lua_setfield(L, -2, "music");
    lua_pushcfunction(L, ai_lua_music_recipes);
    lua_setfield(L, -2, "music_recipes");
    lua_pop(L, 1);
}
void nnet_lua_open(lua_State *L)                        { module(L, "nnet"); }

/* --- USB: the RGB30's port is not driven (no USB mouse or pad) --- */

const usb_info_t *usb_info(void)
{
    static const usb_info_t none;
    return &none;
}

/* --- the interrupt statistics of the Pi's overlay --- */

uint32_t irq_busy_us(int irq)                           { (void)irq; return 0; }
