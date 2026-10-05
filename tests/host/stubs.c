/*
 * bmhost: the kernel services the .bm runtime (src/bm/runtime.c) uses,
 * replaced for the PC. The SD card is a directory, the clock is virtual
 * (it moves by whole frames, so a run is the same every time), the input
 * comes from a script, the framebuffer is three pages in memory and the
 * sound is rendered into a WAV file by the console's own synthesizer.
 */
#include "host.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#ifdef BMHOST_GPU
#include "gpu/v3d.h"
#include "../gpu/v3d_emu.h"
#endif
#include "drivers/fb.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "kernel/config.h"
#include "kernel/crumbs.h"
#include "kernel/reports.h"
#include "kernel/input.h"
#include "kernel/irq.h"
#include "kernel/pointer.h"
#include "net/img3d.h"
#include "usb/hid.h"
#include "script/luavm.h"
#include "n8lua.h"
#include "ai/lua_ai.h"
#include "audio/n8snd.h"

#include "lauxlib.h"
#include "lua.h"

host_t host;

/* ---------------------------------------------------------------- clock */

double host_real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* Virtual microseconds: each reading moves the clock by 1 us (the frame
 * loop's wait for the next frame ends), and every page flip moves it to the
 * next frame boundary. Frame n is at n/60 s whatever the PC's speed, so a
 * run is the same every time; stat(1) is not the cost on the PC (bmhost
 * prints that). */
static uint32_t vclock = 1000000, readings;
static double frame_real;           /* --clock-scale: real time at the start of the frame */

uint32_t timer_ticks(void)
{
    if (host.clock_scale > 0) {
        /* profiling: the PC's real time, scaled to guess the Pi's */
        double d = (host_real_us() - frame_real) * host.clock_scale;
        return vclock + (uint32_t)d + readings++;
    }
    return vclock + readings++;
}

void timer_delay_us(uint32_t us) { vclock += us; }
void timer_delay_ms(uint32_t ms) { vclock += ms * 1000; }

static void next_frame(void)
{
    if (host.clock_scale > 0)
        vclock += (uint32_t)((host_real_us() - frame_real) * host.clock_scale);
    vclock += readings;
    readings = 0;
    vclock += 16667 - (vclock - 1000000) % 16667;
    frame_real = host_real_us();
}

/* ---------------------------------------------------------------- log */

void uart_putc(char c)
{
    if (!host.quiet)
        fputc(c, stderr);
}

/* ---------------------------------------------------------------- SD card */

static char fat_err[128] = "ok";

const char *fat_error(void) { return fat_err; }

static void host_path(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s/%s", host.sd, path[0] == '/' ? path + 1 : path);
}

/* every entry found remembers its path in a ring (fat_load takes the entry) */
#define PATHS 64
static char paths[PATHS][1100];
static unsigned path_next;

static int remember(const char *p)
{
    unsigned i = path_next++ % PATHS;
    snprintf(paths[i], sizeof paths[i], "%s", p);
    return (int)i;
}

int fat_find(const char *path, fat_entry_t *e)
{
    char hp[512];
    struct stat st;
    host_path(path, hp, sizeof hp);
    if (stat(hp, &st) != 0) {
        snprintf(fat_err, sizeof fat_err, "not found: %s", path);
        return -1;
    }
    memset(e, 0, sizeof *e);
    const char *base = strrchr(path, '/');
    snprintf(e->name, sizeof e->name, "%s", base ? base + 1 : path);
    e->size = (uint32_t)st.st_size;
    e->is_dir = S_ISDIR(st.st_mode);
    e->cluster = (uint32_t)remember(hp) + 2;
    return 0;
}

int fat_read_head(const fat_entry_t *e, uint8_t buf[512])
{
    FILE *f = fopen(paths[(e->cluster - 2) % PATHS], "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, 512, f);
    fclose(f);
    return n == 512 || n == e->size ? 0 : -1;
}

int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    FILE *f = fopen(paths[(e->cluster - 2) % PATHS], "rb");
    if (!f) {
        snprintf(fat_err, sizeof fat_err, "cannot open");
        return -1;
    }
    *data = malloc(e->size ? e->size : 1);
    *len = fread(*data, 1, e->size, f);
    fclose(f);
    return 0;
}

#define DIRS 8
static DIR *dirs[DIRS];
static char dir_paths[DIRS][512];

int fat_opendir(fat_dir_t *d, const char *path)
{
    char hp[512];
    host_path(path, hp, sizeof hp);
    for (int i = 0; i < DIRS; i++)
        if (!dirs[i]) {
            dirs[i] = opendir(hp);
            if (!dirs[i])
                return -1;
            snprintf(dir_paths[i], sizeof dir_paths[i], "%s", hp);
            d->cluster = (uint32_t)i + 2;
            d->index = 0;
            return 0;
        }
    return -1;
}

int fat_readdir(fat_dir_t *d, fat_entry_t *e)
{
    int i = (int)d->cluster - 2;
    if (i < 0 || i >= DIRS || !dirs[i])
        return 0;
    for (struct dirent *de; (de = readdir(dirs[i]));) {
        if (de->d_name[0] == '.')
            continue;
        char hp[1100];
        struct stat st;
        snprintf(hp, sizeof hp, "%s/%s", dir_paths[i], de->d_name);
        if (stat(hp, &st) != 0)
            continue;
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%.127s", de->d_name);
        e->size = (uint32_t)st.st_size;
        e->is_dir = S_ISDIR(st.st_mode);
        e->cluster = (uint32_t)remember(hp) + 2;
        return 1;
    }
    closedir(dirs[i]);
    dirs[i] = NULL;
    return 0;
}

int fat_mkdirs(const char *path)
{
    char hp[512];
    host_path(path, hp, sizeof hp);
    for (char *p = hp + strlen(host.sd) + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(hp, 0755);
            *p = '/';
        }
    mkdir(hp, 0755);
    return 0;
}

static int write_host(const char *hp, const void *data, size_t len)
{
    FILE *f = fopen(hp, "wb");
    if (!f || fwrite(data, 1, len, f) != len) {
        snprintf(fat_err, sizeof fat_err, "cannot write %s", hp);
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int fat_write_file(const char *dir, const char *name, const void *data, size_t len)
{
    char p[600], hp[700];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    host_path(p, hp, sizeof hp);
    return write_host(hp, data, len);
}

int fat_replace(const char *path, const void *data, size_t len)
{
    char hp[512];
    host_path(path, hp, sizeof hp);
    return write_host(hp, data, len);
}

int fat_delete(const char *path)
{
    char hp[512];
    host_path(path, hp, sizeof hp);
    return remove(hp) == 0 ? 0 : -1;
}

int config_find_file(const char *name, fat_entry_t *e)
{
    char p[300];
    snprintf(p, sizeof p, "/bm/%s", name[0] == '/' ? name + 1 : name);
    return fat_find(p, e) == 0 ? 0 : fat_find(name, e);
}

void config_save(void) { }
void config_set(const char *key, const char *value) { (void)key; (void)value; }
/* bm/config.txt: BMHOST_CONFIG="key=value,key=value" (gpu3d_vs=2, gpu3d_queue=1...) */
const char *config_get(const char *key)
{
    static char val[64];
    const char *c = getenv("BMHOST_CONFIG");
    const size_t n = strlen(key);
    while (c && *c) {
        if (!strncmp(c, key, n) && c[n] == '=') {
            const char *v = c + n + 1, *e = strchr(v, ',');
            const size_t len = e ? (size_t)(e - v) : strlen(v);
            snprintf(val, sizeof val, "%.*s", (int)len, v);
            return val;
        }
        c = strchr(c, ',');
        if (c)
            c++;
    }
    return NULL;
}

/* ---------------------------------------------------------------- input */

int input_remote_getc(void)
{
    if (host.typed_pos < host.typed_len)
        return (unsigned char)host.typed[host.typed_pos++];
    return -1;
}

uint32_t input_players(uint32_t out[INPUT_PLAYERS], int text, int *quit, int *local)
{
    uint32_t any = 0;
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        out[p] = host.pad[p];
        any |= out[p];
    }
    (void)text;
    *quit = host.quit_now;
    host.quit_now = 0;
    *local = 0;
    return any;
}

void input_stick(int p, uint32_t b, float *x, float *y)
{
    *x = host.stick[p][0];
    *y = host.stick[p][1];
    if (*x == 0 && *y == 0) {
        float dx = (b & HID_RIGHT ? 1.0f : 0) - (b & HID_LEFT ? 1.0f : 0);
        float dy = (b & HID_DOWN ? 1.0f : 0) - (b & HID_UP ? 1.0f : 0);
        if (dx != 0 && dy != 0) { dx *= 0.7071f; dy *= 0.7071f; }
        *x = dx;
        *y = dy;
    }
}

void input_stick_r(int p, float *x, float *y)
{
    *x = host.rstick[p][0];
    *y = host.rstick[p][1];
}

/* player 1, and the players given a controller by "device P kind" */
unsigned input_connected(void)
{
    unsigned m = 1;
    for (int p = 1; p < INPUT_PLAYERS; p++)
        if (host.dev[p] > 0)
            m |= 1u << p;
    return m;
}

/* "device P kind" in the script, else player 1 plays with what "source" says */
int input_device(int p)
{
    if (p < 0 || p >= INPUT_PLAYERS)
        return INPUT_DEV_NONE;
    if (host.dev[p] >= 0)
        return host.dev[p];
    if (p != 0)
        return INPUT_DEV_NONE;
    return host.source == HID_SOURCE_DS4 ? INPUT_DEV_PAD | INPUT_DEV_DS4
         : host.source == HID_SOURCE_PAD ? INPUT_DEV_PAD : INPUT_DEV_KEYBOARD;
}

uint32_t input_ok_bit(int back) { return back ? HID_B : HID_A; }
uint32_t input_face_shown(uint32_t bit) { return bit; }
void input_flush(void) { }
int input_local_player(void) { return 0; }
uint32_t hid_buttons(void) { return host.pad[0]; }
int hid_getc(void) { return -1; }
int hid_last_source(void) { return host.source; }
void hid_text_mode(int on) { (void)on; }

int hid_usage_held(uint8_t u)
{
    for (int i = 0; i < host.nkeys; i++)
        if (host.keys[i] == u)
            return 1;
    return 0;
}

int hid_keys_held(uint8_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < host.nkeys && n < max; i++)
        out[n++] = host.keys[i];
    return n;
}

/* the system pointer (M32): no mouse on the PC */
static const pointer_t no_pointer;
int pointer_enabled(void) { return 0; }
void pointer_env(int on, int w, int h) { (void)on; (void)w; (void)h; }
const pointer_t *pointer_update(void) { return &no_pointer; }
const pointer_t *pointer_get(void) { return &no_pointer; }
void pointer_draw(uint16_t *px, uint32_t stride, int w, int h) { (void)px; (void)stride; (void)w; (void)h; }

/* no image-to-3D service from bmhost (the PC tests have their own) */
const img3d_provider_t *img3d_provider(const char *name) { (void)name; return NULL; }
const char *img3d_provider_name(int i) { (void)i; return NULL; }
const char *img3d_key_name(const img3d_provider_t *p) { (void)p; return "meshy_key"; }

static int no_img3d(char *err, size_t errlen)
{
    snprintf(err, errlen, "no image-to-3D service in bmhost");
    return -1;
}

int img3d_start(const img3d_provider_t *p, const char *key, const uint8_t *image, size_t len, const char *url,
                int polycount, char *task, size_t tasklen, char *err, size_t errlen)
{
    (void)p; (void)key; (void)image; (void)len; (void)url; (void)polycount; (void)task; (void)tasklen;
    return no_img3d(err, errlen);
}

int img3d_status(const img3d_provider_t *p, const char *key, const char *task, int *progress, char *model_url,
                 size_t urllen, char *err, size_t errlen)
{
    (void)p; (void)key; (void)task; (void)progress; (void)model_url; (void)urllen;
    return no_img3d(err, errlen);
}

int img3d_download(const char *url, size_t max, uint8_t **data, size_t *len, char *err, size_t errlen)
{
    (void)url; (void)max; (void)data; (void)len;
    return no_img3d(err, errlen);
}

/* ---------------------------------------------------------------- video */

void console_suspend(int suspend) { (void)suspend; }

static uint8_t *pages;

int fb_init_depth(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers, uint32_t depth)
{
#ifndef BMHOST_GPU
    free(pages);
#endif
    memset(fb, 0, sizeof *fb);
    fb->width = width;
    fb->height = height;
    fb->depth = depth;
    fb->pitch = width * depth / 8;
    fb->buffers = buffers;
    fb->size = fb->pitch * height;
#ifdef BMHOST_GPU
    /* the pages where the emulated V3D reaches them (its arena, which
     * gives nothing back: the largest so far is used again, screen()) */
    static size_t have;
    const size_t need = (size_t)buffers * fb->size;
    if (need > have) {
        pages = test_aligned_alloc(4096, need);
        have = pages ? need : 0;
    }
    if (pages) {
        memset(pages, 0, need);
        fb->bus = v3d_bus(pages);
    }
#else
    pages = calloc(buffers, fb->size);
#endif
    fb->mem = pages;
    fb->shown = 0;
    fb->base = pages + fb->size;
    return pages ? 0 : -1;
}

int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers)
{
    return fb_init_depth(fb, width, height, buffers, 32);
}

int fb_flip(framebuffer_t *fb)
{
#ifdef BMHOST_GPU
    /* counting the ARM's instructions (tests/overbit/frames.py): from the
     * first frame shown (the GPU's probe has run) the emulated V3D takes
     * the jobs and does not run them */
    emu_skip = getenv("BMHOST_EMU_SKIP") != NULL;
#endif
    if (fb->depth == 16)
        host_frame((const uint16_t *)fb->base, (int)fb->width, (int)fb->height, (int)(fb->pitch / 2));
    fb->shown = (uint32_t)((fb->base - fb->mem) / fb->size);
    fb->base = fb->mem + ((fb->shown + 1) % fb->buffers) * fb->size;
    next_frame();
    return 0;
}

/* ---------------------------------------------------------------- the rest */

int dma_ready(void) { return 0; }
void dma_copy(void *dst, const void *src, uint32_t len) { memcpy(dst, src, len); }
void dma_fill(void *dst, uint32_t value, uint32_t len) { memset(dst, (int)value, len); }
int dma_wait(void) { return 0; }
int dma_channel_claim(const uint8_t *pref, unsigned n) { (void)pref; (void)n; return -1; }
void crumb_frame(uint32_t frame) { (void)frame; }
void irq_register(unsigned irq, irq_fn fn, void *arg) { (void)irq; (void)fn; (void)arg; }
void irq_enable(unsigned irq) { (void)irq; }
void irq_disable(unsigned irq) { (void)irq; }

#ifndef BMHOST_GPU
/* no V3D on the PC: the ARM's rasterizer draws the 3D, as in QEMU (make
 * bmhost-gpu links src/gpu/gpu3d.c with the V3D emulator instead) */
const char *gpu3d_status(void) { return "no V3D (bmhost)"; }
int gpu3d_init(void) { return -1; }
int gpu3d_ready(void) { return 0; }
int gpu3d_failed(void) { return 0; }
const r3d_backend_t *gpu3d_backend(void) { return NULL; }
void gpu3d_set_fb(const void *mem, uint32_t size, uint32_t bus) { (void)mem; (void)size; (void)bus; }
void gpu3d_set_size(int w, int h) { (void)w; (void)h; }
int gpu3d_pending(void) { return 0; }
void gpu3d_page(int uniform, uint16_t c) { (void)uniform; (void)c; }
int gpu3d_cleared(void) { return 0; }
void gpu3d_drop(void) { }
int gpu3d_flush(const g16_t *g, int keep) { (void)g; (void)keep; return 0; }
void gpu3d_set_msaa(int on) { (void)on; }
int gpu3d_msaa(void) { return 0; }
int gpu3d_msaa_on(void) { return 0; }
int gpu3d_vshader(void) { return 0; }
void gpu3d_set_vshader(int on) { (void)on; }
int gpu3d_vshader_on(void) { return 0; }
void gpu3d_take_stats(gpu3d_stats_t *s) { memset(s, 0, sizeof *s); }
void gpu3d_peek_stats(gpu3d_stats_t *s) { memset(s, 0, sizeof *s); }
int gpu3d_submit(const g16_t *g, int keep) { (void)g; (void)keep; return 0; }
int gpu3d_inflight(void) { return 0; }
int gpu3d_sync(void) { return 0; }
int gpu3d_sync_page(const void *px) { (void)px; return 0; }
int gpu3d_queue2(void) { return 0; }
void gpu3d_set_queue(int on) { (void)on; }
int gpu3d_queue(void) { return 0; }
void gpu3d_set_wc(int on) { (void)on; }
void gpu3d_set_bilinear(int on) { (void)on; }
void gpu3d_set_tex16(int on) { (void)on; }
int gpu3d_tex16(void) { return 0; }
void gpu3d_set_fs2(int on) { (void)on; }
int gpu3d_fs2(void) { return 0; }
void gpu3d_set_sort(int on) { (void)on; }
int gpu3d_sort(void) { return 0; }
int gpu3d_rect2d(const g16_t *g, int x0, int y0, int x1, int y1, uint16_t c)
{
    (void)g; (void)x0; (void)y0; (void)x1; (void)y1; (void)c;
    return 0;
}
int gpu3d_blit2d(const g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh, int dx, int dy, int zoom,
                 int flip_x, int flip_y)
{
    (void)g; (void)s; (void)sx; (void)sy; (void)sw; (void)sh; (void)dx; (void)dy; (void)zoom; (void)flip_x;
    (void)flip_y;
    return 0;
}
int gpu3d_text2d(const g16_t *g, int x, int y, const char *str, uint16_t c, int scale)
{
    (void)g; (void)x; (void)y; (void)str; (void)c; (void)scale;
    return 0;
}
int gpu3d_bilinear(void) { return 0; }
int gpu3d_wc(void) { return 0; }
int gpu3d_queue_ok(void) { return 0; }
#endif

/* nano8 and the assistant are not part of the host runner */
void n8lua_set_io(const n8lua_io_t *io) { (void)io; }
int luaopen_n8(lua_State *L) { lua_newtable(L); return 1; }
void n8lua_close(void) { }
void ai_lua_open(lua_State *L) { (void)L; }
void n8snd_pause(int on) { (void)on; }

/* The last stage of the console's audio render (src/audio/audio.c): the
 * mixed samples of the synthesizer, which go to the WAV file. */
void n8snd_mix(int16_t *out, unsigned n, float gain)
{
    (void)gain;
    host_audio(out, n);
}

/* ---------------------------------------------------------------- Lua */

static size_t mem_used;

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    if (!ptr)
        osize = 0;
    if (nsize == 0) {
        free(ptr);
        mem_used -= osize;
        return NULL;
    }
    if (nsize > osize && mem_used - osize + nsize > (64u << 20))
        return NULL;
    void *p = realloc(ptr, nsize);
    if (p)
        mem_used = mem_used - osize + nsize;
    return p;
}

static int panic_handler(lua_State *l)
{
    fprintf(stderr, "Lua PANIC: %s\n", lua_tostring(l, -1));
    return 0;
}

lua_State *luavm_newstate(void)
{
    lua_State *l = lua_newstate(lua_alloc, NULL);
    if (l)
        lua_atpanic(l, panic_handler);
    return l;
}

size_t luavm_mem(void) { return mem_used; }

/* the reports (src/kernel/reports.c): BMHOST_REPORTS=dir keeps them there as
 * <kind>.txt; the log says so either way */
int reports_text(const char *kind, const char *text, size_t len)
{
    const char *dir = getenv("BMHOST_REPORTS");
    fprintf(stderr, "bmhost: report %s, %zu bytes\n", kind, len);
    if (!dir)
        return 0;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.txt", dir, kind);
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(text, 1, len, f);
    fclose(f);
    return 0;
}
