#include "rec.h"
#include "vid.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "platform.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define REC_DIR     "/bm/rec"
#define CHUNK_BYTES (256u * 1024u)      /* a chunk holds frames up to about this much */
#define STACK       16384
#define MIN_US      1000                /* the least a game waits for the writer when it has work */
#define PAUSE_US    300000              /* after the game's own write to the card */
#define MIN_RING    (1u << 20)
#define MAX_RLE_FPC 256u                /* frames in an RLE chunk, at most */

static struct {
    int on;
    rec_cfg_t cfg;
    uint8_t *ring;
    uint32_t slot_bytes, fpc, fb, nslots;   /* chunk size in the ring, frames a chunk, largest frame in bytes, chunks */
    uint32_t data_cap, fill_bytes;          /* frame bytes a chunk holds at most, and in the chunk being filled */
    uint16_t *row[2];                       /* RLE: the row being made and the row above */
    uint32_t started, finalized, written;   /* chunks begun, ready, on the card */
    uint32_t fill_n;
    int filling;
    uint32_t next_due, period;
    uint32_t pause_until;
    int paused;
    fiber_job_t job;
    uint16_t *xmap;
    int xmap_src, xmap_dst;
    rec_stats_t st;
    uint32_t t_start;
} R;

static const char *fmt_name(int f)
{
    return f == BMV_CODEC_RLE565 ? "rle565" : f == BMV_CODEC_RAW565 ? "rgb565" : "rgb332";
}

static void free_rows(void)
{
    free(R.row[0]);
    free(R.row[1]);
    R.row[0] = R.row[1] = NULL;
}

static const char *key_str(const char *k)
{
    const char *v = config_get(k);
    return v && *v ? v : NULL;
}

int rec_config(rec_cfg_t *cfg)
{
    cfg->w = 320;
    cfg->h = 180;
    cfg->fps = 15;
    cfg->format = BMV_CODEC_RLE565;
    cfg->ring_bytes = 64u << 20;
    const char *v = key_str("rec_fps");
    if (v) {
        const int f = atoi(v);
        cfg->fps = f < 1 ? 1 : f > 30 ? 30 : f;
    }
    if ((v = key_str("rec_size"))) {
        char *end = NULL;
        const int w = (int)strtol(v, &end, 10);
        const int h = end && (*end == 'x' || *end == 'X') ? (int)strtol(end + 1, NULL, 10) : 0;
        if (w >= 16 && h >= 16 && w <= 1920 && h <= 1080) {
            cfg->w = w;
            cfg->h = h;
        } else
            kprintf("rec: rec_size=%s is not WxH (16x16 to 1920x1080): 320x180\n", v);
    }
    if ((v = key_str("rec_format"))) {
        if (!strcmp(v, "rgb332"))
            cfg->format = BMV_CODEC_RGB332;
        else if (!strcmp(v, "rgb565"))
            cfg->format = BMV_CODEC_RAW565;
        else if (strcmp(v, "rle565"))
            kprintf("rec: rec_format=%s is not rle565, rgb565 or rgb332: rle565\n", v);
    }
    if ((v = key_str("rec_ring_mb"))) {
        const int mb = atoi(v);
        cfg->ring_bytes = (uint32_t)(mb < 1 ? 1 : mb > 256 ? 256 : mb) << 20;
    }
    v = key_str("rec");
    return v && (!strcmp(v, "on") || !strcmp(v, "1"));
}

int rec_active(void)
{
    return R.on;
}

int rec_due(void)
{
    return R.on && (int32_t)(timer_ticks() - R.next_due) >= 0;
}

const rec_stats_t *rec_stats(void)
{
    return &R.st;
}

/* the next free run number: one more than the highest Rnnn in /bm/rec */
static uint32_t next_run(void)
{
    uint32_t top = 0;
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, REC_DIR) == 0)
        while (fat_readdir(&d, &e))
            if ((e.name[0] == 'R' || e.name[0] == 'r') && e.name[1] >= '0' && e.name[1] <= '9' && e.name[2] >= '0' &&
                e.name[2] <= '9' && e.name[3] >= '0' && e.name[3] <= '9') {
                const uint32_t n = (uint32_t)((e.name[1] - '0') * 100 + (e.name[2] - '0') * 10 + (e.name[3] - '0'));
                if (n > top)
                    top = n;
            }
    return top + 1;
}

int rec_start(const rec_cfg_t *cfg)
{
    if (R.on)
        return 0;
    const uint32_t keep_stack_job = R.job.stack != NULL;
    fiber_job_t job = R.job;
    memset(&R, 0, sizeof R);
    if (keep_stack_job)
        R.job = job;                    /* (its stack is allocated once) */
    R.cfg = *cfg;
    if (cfg->format == BMV_CODEC_RLE565) {
        R.fb = (uint32_t)BMV_RLE565_MAX(cfg->w, cfg->h);
        R.fpc = MAX_RLE_FPC;
        R.data_cap = CHUNK_BYTES + R.fb;        /* a frame is started while less than CHUNK_BYTES is used */
        R.row[0] = malloc((size_t)cfg->w * sizeof(uint16_t));
        R.row[1] = malloc((size_t)cfg->w * sizeof(uint16_t));
        if (!R.row[0] || !R.row[1]) {
            free_rows();
            kprintf("rec: not enough memory for the rows\n");
            return -1;
        }
    } else {
        R.fb = (uint32_t)cfg->w * (uint32_t)cfg->h * (cfg->format == BMV_CODEC_RAW565 ? 2u : 1u);
        R.fpc = CHUNK_BYTES / R.fb + (CHUNK_BYTES % R.fb ? 1u : 0u);
        R.data_cap = R.fpc * R.fb;
    }
    R.slot_bytes = (BMV_HEADER + R.data_cap + R.fpc * 8u + 15u) & ~15u;
    uint32_t want = cfg->ring_bytes;
    while (want >= MIN_RING && !(R.ring = malloc(want)))
        want /= 2;
    if (!R.ring) {
        free_rows();
        kprintf("rec: not enough memory for the ring (%u MB asked)\n", (unsigned)(cfg->ring_bytes >> 20));
        return -1;
    }
    R.nslots = want / R.slot_bytes;
    if (R.nslots < 2) {
        free(R.ring);
        R.ring = NULL;
        free_rows();
        kprintf("rec: the ring holds less than two chunks (%u bytes a frame)\n", (unsigned)R.fb);
        return -1;
    }
    if (fat_mkdirs(REC_DIR) != 0) {
        free(R.ring);
        R.ring = NULL;
        free_rows();
        kprintf("rec: cannot make %s: %s\n", REC_DIR, fat_error());
        return -1;
    }
    R.st.slots = R.nslots;
    R.st.run = next_run();
    if (R.st.run > 999) {
        free(R.ring);
        R.ring = NULL;
        free_rows();
        kprintf("rec: %s holds 999 runs: delete some\n", REC_DIR);
        return -1;
    }
    R.period = 1000000u / (uint32_t)cfg->fps;
    R.t_start = R.next_due = timer_ticks();
    R.on = 1;
    kprintf("rec: run %u, %dx%d %s at %d fps, ring %u chunks of %u KB (%u MB), files %s/r%03u0001.bmv...\n",
            (unsigned)R.st.run, cfg->w, cfg->h, fmt_name(cfg->format), cfg->fps,
            (unsigned)R.nslots, (unsigned)(R.slot_bytes / 1024), (unsigned)(want >> 20), REC_DIR, (unsigned)R.st.run);
    return 0;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

/* the chunk being filled is complete: its header and its index */
static void finalize(void)
{
    uint8_t *s = R.ring + (size_t)((R.started - 1) % R.nslots) * R.slot_bytes;
    const uint32_t n = R.fill_n, index_off = BMV_HEADER + R.fill_bytes;
    uint8_t *tail = s + BMV_HEADER + R.data_cap;    /* the index entries made while filling */
    if (R.cfg.format == BMV_CODEC_RLE565)
        memmove(s + index_off, tail, (size_t)n * 8u);
    else
        for (uint32_t k = 0; k < n; k++) {
            put32(s + index_off + k * 8, BMV_HEADER + k * R.fb);
            put32(s + index_off + k * 8 + 4, R.fb);
        }
    memset(s, 0, BMV_HEADER);
    memcpy(s, "BMV1", 4);
    s[4] = BMV_VERSION;
    s[5] = (uint8_t)R.cfg.format;
    put16(s + 8, (uint32_t)R.cfg.w);
    put16(s + 10, (uint32_t)R.cfg.h);
    put16(s + 12, (uint32_t)R.cfg.fps);
    put32(s + 16, n);
    put32(s + 20, index_off);
    R.fill_n = 0;
    R.fill_bytes = 0;
    R.filling = 0;
    R.finalized = R.started;
}

static void xmap_make(int src, int dst)
{
    if (R.xmap && R.xmap_src == src && R.xmap_dst == dst)
        return;
    free(R.xmap);
    R.xmap = malloc((size_t)dst * sizeof *R.xmap);
    R.xmap_src = src;
    R.xmap_dst = dst;
    if (R.xmap)
        for (int x = 0; x < dst; x++)
            R.xmap[x] = (uint16_t)((uint32_t)x * (uint32_t)src / (uint32_t)dst);
}

void rec_frame(const uint16_t *px, uint32_t stride, int w, int h)
{
    if (!R.on || w < 1 || h < 1)
        return;
    const uint32_t now = timer_ticks();
    if ((int32_t)(now - R.next_due) < 0)
        return;
    R.next_due += R.period;
    if ((int32_t)(now - R.next_due) > 0)
        R.next_due = now + R.period;    /* late: no burst to catch up */
    if (!R.filling) {
        if (R.started - R.written >= R.nslots) {
            R.st.dropped_full++;
            return;
        }
        R.started++;
        R.filling = 1;
        R.fill_n = 0;
        R.fill_bytes = 0;
        if (R.started - R.written > R.st.slots_high)
            R.st.slots_high = R.started - R.written;
    }
    xmap_make(w, R.cfg.w);
    if (!R.xmap)
        return;
    uint8_t *slot = R.ring + (size_t)((R.started - 1) % R.nslots) * R.slot_bytes;
    uint8_t *dst = slot + BMV_HEADER + R.fill_bytes;
    const uint8_t *dst0 = dst;
    const uint32_t t_enc = timer_ticks();
    for (int y = 0; y < R.cfg.h; y++) {
        const uint16_t *row = px + (size_t)((uint32_t)y * (uint32_t)h / (uint32_t)R.cfg.h) * stride;
        if (R.cfg.format == BMV_CODEC_RLE565) {
            uint16_t *cur = R.row[y & 1];
            for (int x = 0; x < R.cfg.w; x++)
                cur[x] = row[R.xmap[x]];
            dst += bmv_rle565_row(dst, cur, y ? R.row[(y & 1) ^ 1] : NULL, R.cfg.w);
        } else if (R.cfg.format == BMV_CODEC_RAW565) {
            for (int x = 0; x < R.cfg.w; x++) {
                const uint16_t c = row[R.xmap[x]];
                *dst++ = (uint8_t)c;
                *dst++ = (uint8_t)(c >> 8);
            }
        } else {
            for (int x = 0; x < R.cfg.w; x++) {
                const uint16_t c = row[R.xmap[x]];
                *dst++ = (uint8_t)(((c >> 8) & 0xE0) | ((c >> 6) & 0x1C) | ((c >> 3) & 3));
            }
        }
    }
    const uint32_t fsz = (uint32_t)(dst - dst0), enc_us = timer_ticks() - t_enc;
    if (R.cfg.format == BMV_CODEC_RLE565) {
        uint8_t *e = slot + BMV_HEADER + R.data_cap + (size_t)R.fill_n * 8u;
        put32(e, BMV_HEADER + R.fill_bytes);
        put32(e + 4, fsz);
        R.st.enc_us += enc_us;
        if (enc_us > R.st.enc_worst_us)
            R.st.enc_worst_us = enc_us;
        if (fsz > R.st.frame_max)
            R.st.frame_max = fsz;
    }
    R.st.frame_bytes += fsz;
    R.fill_bytes += fsz;
    R.st.captured++;
    if (++R.fill_n == R.fpc || (R.cfg.format == BMV_CODEC_RLE565 && R.fill_bytes >= CHUNK_BYTES))
        finalize();
}

/* the oldest finished chunk to the card */
static void write_one(void)
{
    const uint8_t *s = R.ring + (size_t)(R.written % R.nslots) * R.slot_bytes;
    const uint32_t n = (uint32_t)s[16] | (uint32_t)s[17] << 8 | (uint32_t)s[18] << 16 | (uint32_t)s[19] << 24;
    const uint32_t index_off = (uint32_t)s[20] | (uint32_t)s[21] << 8 | (uint32_t)s[22] << 16 | (uint32_t)s[23] << 24;
    const size_t len = (size_t)index_off + (size_t)n * 8;
    char name[16];
    ksnprintf(name, sizeof name, "r%03u%04u.bmv", (unsigned)R.st.run % 1000u, (unsigned)(R.written + 1) % 10000u);
    const uint32_t t0 = timer_ticks();
    const int rc = R.written + 1 < 10000 ? fat_write_file(REC_DIR, name, s, len) : -1;
    const uint32_t dt = timer_ticks() - t0;
    if (rc != 0) {
        R.st.chunk_fail++;
        kprintf("rec: chunk %u not written: %s\n", (unsigned)(R.written + 1), R.written + 1 < 10000 ? fat_error() : "10000 chunks");
    } else {
        R.st.chunks++;
        R.st.bytes += len;
        R.st.write_us += dt;
        if (dt > R.st.write_worst_us)
            R.st.write_worst_us = dt;
    }
    R.written++;
}

static void writer(void *arg)
{
    (void)arg;
    while (R.written < R.finalized)
        write_one();
}

void rec_pump(uint32_t until)
{
    if (!R.on)
        return;
    if (!R.job.busy && R.written >= R.finalized)
        return;
    const uint32_t t0 = timer_ticks();
    if (R.paused && (int32_t)(t0 - R.pause_until) < 0)
        return;
    R.paused = 0;
    if ((int32_t)(until - t0) < MIN_US)
        until = t0 + MIN_US;
    if (!R.job.busy && fiber_job_start(&R.job, STACK, writer, NULL) != 0)
        return;
    fiber_job_run(&R.job, until);
    const uint32_t dt = timer_ticks() - t0;
    if (dt > R.st.pump_worst_us)
        R.st.pump_worst_us = dt;
}

void rec_note_game_io(void)
{
    if (!R.on)
        return;
    R.paused = 1;
    R.pause_until = timer_ticks() + PAUSE_US;
}

void rec_overlay_text(char *buf, int n)
{
    ksnprintf(buf, (size_t)n, "REC %u d%u", (unsigned)R.st.captured, (unsigned)R.st.dropped_full);
}

void rec_stop(void)
{
    if (!R.on)
        return;
    if (R.filling && R.fill_n)
        finalize();
    R.on = 0;                           /* (no new frames) */
    const uint32_t t_end = timer_ticks();
    while (R.written < R.finalized) {   /* the game is over: the rest, at the card's own pace */
        if (!R.job.busy && fiber_job_start(&R.job, STACK, writer, NULL) != 0) {
            while (R.written < R.finalized)
                write_one();
            break;
        }
        fiber_job_run(&R.job, timer_ticks() + 1000000u);
    }
    const rec_stats_t *s = &R.st;
    const uint32_t mbps100 = s->write_us ? (uint32_t)(s->bytes * 100u / s->write_us) : 0;
    const uint32_t secs10 = (t_end - R.t_start) / 100000u;
    kprintf("rec: run %u done: %u frames in %u.%u s, dropped %u (ring full), %u chunks (%u failed), %u KB\n",
            (unsigned)s->run, (unsigned)s->captured, (unsigned)(secs10 / 10), (unsigned)(secs10 % 10),
            (unsigned)s->dropped_full, (unsigned)s->chunks, (unsigned)s->chunk_fail, (unsigned)(s->bytes / 1024));
    if (s->captured) {
        const uint32_t mean = (uint32_t)(s->frame_bytes / s->captured);
        const uint32_t need100 =
            (uint32_t)((uint64_t)s->frame_bytes * (uint32_t)R.cfg.fps * 100u / s->captured / 1000000u);
        kprintf("rec: %s, %u bytes a frame (mean)", fmt_name(R.cfg.format), (unsigned)mean);
        if (R.cfg.format == BMV_CODEC_RLE565) {
            const uint32_t us = s->enc_us / s->captured;
            kprintf(", %u largest, encode %u.%02u ms a frame (mean), %u.%02u ms worst", (unsigned)s->frame_max,
                    (unsigned)(us / 1000), (unsigned)(us % 1000 / 10), (unsigned)(s->enc_worst_us / 1000),
                    (unsigned)(s->enc_worst_us % 1000 / 10));
        }
        kprintf(", %u.%02u MB/s at %d fps.\n", (unsigned)(need100 / 100), (unsigned)(need100 % 100), R.cfg.fps);
    }
    kprintf("rec: card %u.%02u MB/s while writing, worst chunk %u.%u ms, game waited at most %u.%u ms in a pump, "
            "ring high %u of %u chunks.\n",
            (unsigned)(mbps100 / 100), (unsigned)(mbps100 % 100), (unsigned)(s->write_worst_us / 1000),
            (unsigned)(s->write_worst_us % 1000 / 100), (unsigned)(s->pump_worst_us / 1000),
            (unsigned)(s->pump_worst_us % 1000 / 100), (unsigned)s->slots_high, (unsigned)s->slots);
    free(R.ring);
    R.ring = NULL;
    free_rows();
    free(R.xmap);
    R.xmap = NULL;
}
