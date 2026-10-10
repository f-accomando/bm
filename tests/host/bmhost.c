/*
 * bmhost: a .bm cartridge on the PC, with the console's own runtime
 * (src/bm/runtime.c: Lua, gfx16, r3d, the sound player) and the kernel's
 * services replaced by tests/host/stubs.c. Frames go to PNG files or to a
 * raw RGB stream for ffmpeg, the sound to a WAV file; the input comes from
 * a script. The clock is virtual: frame n is always at n/60 s, so a run is
 * the same every time (the reel of a game, a test with screenshots).
 *
 *   bmhost CART.bm [options]
 *     --sd DIR          the SD card (default: a temporary directory)
 *     --seconds S       stop after S seconds of game (default 10)
 *     --shots DIR       PNG of the frames chosen by --every / --from / "shot"
 *     --every K         one frame in K (default: only "shot" lines)
 *     --from F          the first frame for --every
 *     --video FILE      every frame as raw rgb24 (ffmpeg -f rawvideo)
 *     --wav FILE        the sound, 48 kHz mono
 *     --input FILE      the input script (see below)
 *     --quiet           no kernel log
 *     --tool            the cartridge is one of bm's tools (SDK, bm Pixel...): it may
 *                       change a .bm that is already on the card
 *     --realtime        60 frames a second, as the console (two bmhost
 *                       playing a match on the network: BMHOST_NET_ID)
 *     --clock-scale K   the clock runs at the PC's real time x K inside a
 *                       frame (stat(1) estimates the Pi's cost with K ~ 21;
 *                       the run is not the same every time any more)
 *
 * Input script, one event per line, "frame command arguments":
 *     0 pad 1 r2 left       buttons held by player 1 (exactly these; "none")
 *     0 stick 1 0.5 -1      left stick of player 1; rstick for the right one
 *     10 keys 0x1A 0x04     keyboard usages held ("none")
 *     20 type hello\n       serial characters (\n is Enter)
 *     30 shot name          that frame as DIR/name.png
 *     40 source ds4         what lastinput() says: keyboard, ds4, pad
 *     40 device 2 ds4       what player 2 plays with (controller()): keyboard,
 *                           ds4, xbox, pad, builtin, none
 *     900 quit              leave the cartridge (Start+Select)
 *     900 ps                the PS button (Ctrl+Esc): online() asks first
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "host.h"
#include "lua.h"
#include "gpu/gpu3d.h"
#include "audio/audio.h"
#include "kernel/input.h"
#include "bm/runtime.h"
#include "drivers/fb.h"
#include "lib/crc32.h"
#include "usb/hid.h"

/* ---------------------------------------------------------------- PNG */

static void put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t h[8];
    put32be(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    uint8_t *tmp = malloc(len + 4);
    memcpy(tmp, type, 4);
    if (len) memcpy(tmp + 4, data, len);
    uint32_t c = crc32(tmp, len + 4);
    free(tmp);
    if (len) fwrite(data, 1, len, f);
    put32be(h, c);
    fwrite(h, 1, 4, f);
}

/* An RGB PNG with "stored" (uncompressed) deflate blocks: no zlib needed. */
static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32be(ihdr, (uint32_t)w);
    put32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    const size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (1 + w * 3)] = 0;
        memcpy(raw + (size_t)y * (1 + w * 3) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    const size_t blocks = (raw_len + 65534) / 65535;
    uint8_t *z = malloc(2 + raw_len + blocks * 5 + 4);
    size_t n = 0;
    z[n++] = 0x78; z[n++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t len = raw_len - off < 65535 ? raw_len - off : 65535;
        z[n++] = off + len >= raw_len ? 1 : 0;
        z[n++] = (uint8_t)len; z[n++] = (uint8_t)(len >> 8);
        z[n++] = (uint8_t)~len; z[n++] = (uint8_t)(~len >> 8);
        memcpy(z + n, raw + off, len);
        n += len;
    }
    put32be(z + n, b << 16 | a);
    n += 4;
    chunk(f, "IDAT", z, (uint32_t)n);
    chunk(f, "IEND", NULL, 0);
    free(z);
    free(raw);
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- run */

static struct {
    const char *shots, *video_path, *wav_path;
    int every, from, realtime;
    FILE *video, *wav;
    uint32_t wav_samples;
    long frame;                 /* frames shown so far */
    char **lines;               /* the input script */
    int nlines, next_line;
    char shot_name[128];
} run;

static uint32_t pad_bits(char *s)
{
    static const struct { const char *n; uint32_t b; } names[] = {
        { "left", HID_LEFT }, { "right", HID_RIGHT }, { "up", HID_UP }, { "down", HID_DOWN },
        { "a", HID_A }, { "b", HID_B }, { "x", HID_X }, { "y", HID_Y }, { "start", HID_START },
        { "select", HID_SELECT }, { "l1", HID_L1 }, { "r1", HID_R1 }, { "l2", HID_L2 },
        { "r2", HID_R2 }, { "l3", HID_L3 }, { "r3", HID_R3 },
    };
    uint32_t b = 0;
    for (char *t = strtok(s, " \t\r\n"); t; t = strtok(NULL, " \t\r\n"))
        for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!strcasecmp(t, names[i].n))
                b |= names[i].b;
    return b;
}

/* the events of the script up to frame f */
static void events(long f)
{
    while (run.next_line < run.nlines) {
        char *line = run.lines[run.next_line];
        char cmd[32];
        long at;
        int used = 0;
        if (sscanf(line, "%ld %31s %n", &at, cmd, &used) < 2) {
            run.next_line++;
            continue;
        }
        if (at > f)
            return;
        run.next_line++;
        char *arg = line + used;
        int p = 1;
        if (!strcmp(cmd, "pad")) {
            sscanf(arg, "%d %n", &p, &used);
            if (p >= 1 && p <= 4)
                host.pad[p - 1] = pad_bits(arg + used);
        } else if (!strcmp(cmd, "stick") || !strcmp(cmd, "rstick")) {
            float x = 0, y = 0;
            sscanf(arg, "%d %f %f", &p, &x, &y);
            if (p >= 1 && p <= 4) {
                float *s = cmd[0] == 'r' ? host.rstick[p - 1] : host.stick[p - 1];
                s[0] = x;
                s[1] = y;
            }
        } else if (!strcmp(cmd, "keys")) {
            host.nkeys = 0;
            for (char *t = strtok(arg, " \t\r\n"); t && host.nkeys < 16; t = strtok(NULL, " \t\r\n"))
                if (strcmp(t, "none"))
                    host.keys[host.nkeys++] = (uint8_t)strtol(t, NULL, 0);
        } else if (!strcmp(cmd, "type")) {
            if (host.typed_pos == host.typed_len)
                host.typed_pos = host.typed_len = 0;    /* all read: from the start again */
            for (char *c = arg; *c && *c != '\n' && host.typed_len < (int)sizeof host.typed - 1; c++) {
                if (c[0] == '\\' && c[1] == 'n') {
                    host.typed[host.typed_len++] = '\r';
                    c++;
                } else if (c[0] == '\\' && c[1] == 'x' && isxdigit((unsigned char)c[2]) &&
                           isxdigit((unsigned char)c[3])) {
                    char hex[3] = { c[2], c[3], 0 };    /* \x13 Ctrl+S, \xfa F2: the codes of keyp() */
                    host.typed[host.typed_len++] = (char)strtol(hex, NULL, 16);
                    c += 3;
                } else {
                    host.typed[host.typed_len++] = *c;
                }
            }
        } else if (!strcmp(cmd, "shot")) {
            sscanf(arg, "%127s", run.shot_name);
        } else if (!strcmp(cmd, "source")) {
            host.source = strstr(arg, "ds4") ? HID_SOURCE_DS4 : strstr(arg, "pad") ? HID_SOURCE_PAD
                        : HID_SOURCE_KEYBOARD;
        } else if (!strcmp(cmd, "device")) {
            char kind[16] = "";
            sscanf(arg, "%d %15s", &p, kind);
            if (p >= 1 && p <= 4)
                host.dev[p - 1] = !strcmp(kind, "keyboard") ? INPUT_DEV_KEYBOARD
                                : !strcmp(kind, "ds4") ? INPUT_DEV_PAD | INPUT_DEV_DS4
                                : !strcmp(kind, "xbox") ? INPUT_DEV_PAD | INPUT_DEV_XBOX
                                : !strcmp(kind, "builtin") ? INPUT_DEV_PAD | INPUT_DEV_BUILTIN
                                : !strcmp(kind, "pad") ? INPUT_DEV_PAD : INPUT_DEV_NONE;
        } else if (!strcmp(cmd, "quit")) {
            host.quit_now = HID_QUIT_KEY;   /* Start+Select */
        } else if (!strcmp(cmd, "ps")) {
            host.quit_now = HID_QUIT_PS;    /* PS, Ctrl+Esc */
        }
    }
}

/* BMHOST_LUAPROF=1: the Lua functions (and lines) the runtime's count hook
 * finds running, every 1000 instructions of the VM, written at the end */
#define PROF_N 4096
static struct prof { char src[LUA_IDSIZE + 4]; int line, fline; long n; } prof[PROF_N];
static long prof_total;
static int prof_on = -1;

void bm_lua_sample(lua_State *L, lua_Debug *ar)
{
    if (prof_on < 0)
        prof_on = getenv("BMHOST_LUAPROF") != NULL;
    if (!prof_on || run.frame < 2 || !lua_getinfo(L, "Sl", ar))
        return;
    prof_total++;
    for (int k = 0; k < 2; k++) {       /* the function (line 0), and the line */
        const int line = k ? ar->currentline : 0;
        unsigned h = (unsigned)ar->linedefined * 31u + (unsigned)line * 7u;
        for (const char *c = ar->short_src; *c; c++) h = h * 33u + (unsigned char)*c;
        for (int i = 0; i < PROF_N; i++) {
            struct prof *p = &prof[(h + (unsigned)i) % PROF_N];
            if (!p->n) {
                snprintf(p->src, sizeof p->src, "%s", ar->short_src);
                p->fline = ar->linedefined;
                p->line = line;
            } else if (p->fline != ar->linedefined || p->line != line || strcmp(p->src, ar->short_src)) {
                continue;
            }
            p->n++;
            break;
        }
    }
}

static int prof_cmp(const void *a, const void *b)
{
    const struct prof *x = a, *y = b;
    return (y->n > x->n) - (y->n < x->n);
}

static void prof_report(void)
{
    if (prof_on <= 0 || !prof_total)
        return;
    qsort(prof, PROF_N, sizeof prof[0], prof_cmp);
    fprintf(stderr, "bmhost: Lua profile, %ld samples (functions, then lines)\n", prof_total);
    for (int k = 0; k < 2; k++) {
        int shown = 0;
        for (int i = 0; i < PROF_N && shown < 40; i++)
            if (prof[i].n && (prof[i].line != 0) == k) {
                if (k)
                    fprintf(stderr, "  %5.1f%%  %s:%d line %d\n", 100.0 * prof[i].n / prof_total, prof[i].src,
                            prof[i].fline, prof[i].line);
                else
                    fprintf(stderr, "  %5.1f%%  %s:%d\n", 100.0 * prof[i].n / prof_total, prof[i].src,
                            prof[i].fline);
                shown++;
            }
    }
}

static uint8_t *rgb;
static double t_flip, cpu_sum, cpu_max;

void host_frame(const uint16_t *px, int w, int h, int stride)
{
    const long f = run.frame++;
    const double now = host_real_us();
    if (t_flip > 0) {                   /* poll, _update, _draw: the PC's cost of a frame */
        const double us = now - t_flip;
        cpu_sum += us;
        if (us > cpu_max) cpu_max = us;
        /* BMHOST_SLOW=ms: the frames that take longer, with their number */
        static double slow = -1;
        if (slow < 0) slow = getenv("BMHOST_SLOW") ? atof(getenv("BMHOST_SLOW")) : 0;
        if (slow > 0 && us > slow * 1000)
            fprintf(stderr, "bmhost: frame %ld took %.1f ms\n", f, us / 1000);
    }
    const int want = run.shot_name[0] || (run.shots && run.every > 0 && f >= run.from &&
                                          (f - run.from) % run.every == 0);
    if (want || run.video) {
        rgb = realloc(rgb, (size_t)w * h * 3);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint16_t c = px[y * stride + x];
                uint8_t *o = rgb + ((size_t)y * w + x) * 3;
                o[0] = (uint8_t)((c >> 11) * 255 / 31);
                o[1] = (uint8_t)((c >> 5 & 63) * 255 / 63);
                o[2] = (uint8_t)((c & 31) * 255 / 31);
            }
    }
    if (run.video)
        fwrite(rgb, 1, (size_t)w * h * 3, run.video);
    if (want && run.shots) {
        char path[512];
        if (run.shot_name[0])
            snprintf(path, sizeof path, "%s/%s.png", run.shots, run.shot_name);
        else
            snprintf(path, sizeof path, "%s/%06ld.png", run.shots, f);
        write_png(path, rgb, w, h);
    }
    run.shot_name[0] = 0;
    events(f + 1);
    if (run.realtime) {
        /* 60 frames a second of the PC's clock (matches on the network) */
        static double next;
        double t = host_real_us();
        if (next == 0 || t > next + 100000)
            next = t;
        next += 1000000.0 / 60;
        if (next > t)
            usleep((useconds_t)(next - t));
    }
    t_flip = host_real_us();
}

void host_audio(const int16_t *s, unsigned n)
{
    if (run.wav) {
        fwrite(s, 4, n, run.wav);           /* stereo frames */
        run.wav_samples += n;
    }
}

static void wav_header(FILE *f, uint32_t samples)
{
    uint8_t h[44];
    const uint32_t rate = AUDIO_RATE, bytes = samples * 4;
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + bytes;
    memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);              /* PCM */
    s = 2; memcpy(h + 22, &s, 2);                       /* stereo */
    memcpy(h + 24, &rate, 4);
    v = rate * 4; memcpy(h + 28, &v, 4);
    s = 4; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = malloc((size_t)n + 1);
    *len = fread(d, 1, (size_t)n, f);
    d[*len] = 0;
    fclose(f);
    return d;
}

int main(int argc, char **argv)
{
    const char *cart = NULL, *input = NULL;
    double seconds = 10;
    char tmp_sd[] = "/tmp/bmhost-sd-XXXXXX";
    host.source = HID_SOURCE_KEYBOARD;
    for (int p = 0; p < 4; p++)
        host.dev[p] = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--sd") && v) host.sd = v, i++;
        else if (!strcmp(a, "--seconds") && v) seconds = atof(v), i++;
        else if (!strcmp(a, "--shots") && v) run.shots = v, i++;
        else if (!strcmp(a, "--every") && v) run.every = atoi(v), i++;
        else if (!strcmp(a, "--from") && v) run.from = atoi(v), i++;
        else if (!strcmp(a, "--video") && v) run.video_path = v, i++;
        else if (!strcmp(a, "--wav") && v) run.wav_path = v, i++;
        else if (!strcmp(a, "--input") && v) input = v, i++;
        else if (!strcmp(a, "--quiet")) host.quiet = 1;
        else if (!strcmp(a, "--tool")) bm_set_tool(1);      /* as bm's own tools in the kernel: saves where it is told */
        else if (!strcmp(a, "--clock-scale") && v) host.clock_scale = atof(v), i++;
        else if (!strcmp(a, "--realtime")) run.realtime = 1;
        else if (a[0] != '-') cart = a;
        else {
            fprintf(stderr, "bmhost: unknown option %s\n", a);
            return 2;
        }
    }
#ifdef BMHOST_AI
    {   /* the assistant (build/assist.bin, or BMHOST_AI_WEIGHTS) for the `ai` table */
        extern const uint8_t *ai_host_blob;
        extern uint32_t ai_host_len;
        const char *wp = getenv("BMHOST_AI_WEIGHTS");
        FILE *wf = fopen(wp ? wp : "build/assist.bin", "rb");
        if (wf) {
            fseek(wf, 0, SEEK_END);
            long wl = ftell(wf);
            fseek(wf, 0, SEEK_SET);
            uint8_t *blob = aligned_alloc(4, ((size_t)wl + 4) & ~(size_t)3);
            if (blob && fread(blob, 1, (size_t)wl, wf) == (size_t)wl) {
                ai_host_blob = blob;
                ai_host_len = (uint32_t)wl;
            }
            fclose(wf);
        } else {
            fprintf(stderr, "bmhost-ai: no assistant (make build/assist.bin, run from the repo root, or set BMHOST_AI_WEIGHTS)\n");
        }
    }
#endif
    if (!cart) {
        fprintf(stderr, "usage: bmhost CART.bm [--sd DIR] [--seconds S] [--shots DIR [--every K] [--from F]]\n"
                        "              [--video FILE] [--wav FILE] [--input SCRIPT] [--quiet]\n");
        return 2;
    }
    if (!host.sd) {
        if (!mkdtemp(tmp_sd)) {
            perror("mkdtemp");
            return 1;
        }
        host.sd = tmp_sd;
    }
    if (input) {
        size_t len;
        char *text = read_file(input, &len);
        if (!text) {
            fprintf(stderr, "bmhost: cannot read %s\n", input);
            return 1;
        }
        for (char *l = strtok(text, "\n"); l; l = strtok(NULL, "\n")) {
            if (l[0] == '#' || !l[0])
                continue;
            run.lines = realloc(run.lines, sizeof *run.lines * (size_t)(run.nlines + 1));
            run.lines[run.nlines++] = l;
        }
    }
    if (run.video_path && !(run.video = fopen(run.video_path, "wb"))) {
        perror(run.video_path);
        return 1;
    }
    if (run.wav_path) {
        if (!(run.wav = fopen(run.wav_path, "wb"))) {
            perror(run.wav_path);
            return 1;
        }
        wav_header(run.wav, 0);
    }
    size_t len;
    uint8_t *data = (uint8_t *)read_file(cart, &len);
    if (!data) {
        fprintf(stderr, "bmhost: cannot read %s\n", cart);
        return 1;
    }

    audio_init();                       /* the synthesizer; no HDMI here, as in QEMU */
    events(0);
    framebuffer_t fb;
    fb_init(&fb, 640, 360, 2);
    bm_stats_t st;
    clock_t c0 = clock();
    bm_play(&fb, data, len, (uint32_t)(seconds + 0.999), &st);
    double cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;

    if (run.wav) {
        wav_header(run.wav, run.wav_samples);
        fclose(run.wav);
    }
    if (run.video)
        fclose(run.video);
    prof_report();
    fprintf(stderr, "bmhost: \"%s\" %ld frames, %s, a frame takes %.3f ms on this PC "
                    "(max %.3f), %u KiB of Lua, %.1f s\n",
            st.title, run.frame, st.ok ? "ok" : "ERROR",
            run.frame > 1 ? cpu_sum / 1000.0 / (double)(run.frame - 1) : 0.0, cpu_max / 1000.0, st.lua_kb, cpu);
    gpu3d_stats_t g;
    gpu3d_take_stats(&g);
    if (g.jobs && run.frame > 0)        /* bmhost-gpu: what the emulated V3D was given */
        fprintf(stderr, "bmhost: GPU %u jobs (%.2f a frame), %u triangles (%.0f a frame), %u jobs with the depth "
                        "kept, %u pages cleared not loaded, %u with MSAA\n",
                (unsigned)g.jobs, (double)g.jobs / (double)run.frame, (unsigned)g.tris,
                (double)g.tris / (double)run.frame, (unsigned)g.zjobs, (unsigned)g.cleared, (unsigned)g.msjobs);
    if ((g.queued || st.d2_ops) && run.frame > 0)
        fprintf(stderr, "bmhost: frame queue: %.2f jobs a frame started and waited for later, %.1f 2D drawings "
                        "a frame recorded meanwhile, %.2f zclear() a frame inside a job\n",
                (double)g.queued / (double)run.frame, (double)st.d2_ops / (double)run.frame,
                (double)g.zinjob / (double)run.frame);
    if (g.quads2d && run.frame > 0)     /* M37: the 2D over the 3D in the GPU's job */
        fprintf(stderr, "bmhost: 2D on the GPU: %.1f quads a frame\n", (double)g.quads2d / (double)run.frame);
    if (g.glmeshes && run.frame > 0)
        fprintf(stderr, "bmhost: vertex shader: %.0f meshes and %.0f triangles a frame\n",
                (double)g.glmeshes / (double)run.frame, (double)g.gltris / (double)run.frame);
    return st.ok ? 0 : 1;
}
