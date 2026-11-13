#include "vid.h"
#include "jpeg.h"

#include <stdio.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static int bad(char *err, size_t errlen, const char *what)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", what);
    return -1;
}

int bmv_open(bmv_t *v, const uint8_t *d, size_t len, char *err, size_t errlen)
{
    if (len < BMV_HEADER || d[0] != 'B' || d[1] != 'M' || d[2] != 'V' || d[3] != '1')
        return bad(err, errlen, "not a .bmv clip");
    if (d[4] != BMV_VERSION)
        return bad(err, errlen, "a .bmv version this console does not know");
    bmv_t t = { .data = d, .len = len };
    t.codec = d[5];
    t.audio_channels = d[6];
    t.audio_bits = d[7];
    t.width = (int)rd16(d + 8);
    t.height = (int)rd16(d + 10);
    t.fps = (int)rd16(d + 12);
    t.audio_rate = rd16(d + 14);
    t.frames = rd32(d + 16);
    const uint32_t index_off = rd32(d + 20), audio_off = rd32(d + 24), audio_len = rd32(d + 28);
    if (t.codec != BMV_CODEC_MJPEG && t.codec != BMV_CODEC_RAW565 && t.codec != BMV_CODEC_RGB332 &&
        t.codec != BMV_CODEC_RLE565)
        return bad(err, errlen, "a codec this console does not know");
    if (t.width < 1 || t.height < 1 || t.width > 1920 || t.height > 1080)
        return bad(err, errlen, "a bad picture size");
    if (t.fps < 1 || t.fps > 240)
        return bad(err, errlen, "a bad frame rate");
    if (t.frames < 1 || t.frames > 1000000u)
        return bad(err, errlen, "a bad frame count");
    if (t.audio_channels) {
        if (t.audio_channels > 2 || (t.audio_bits != 8 && t.audio_bits != 16) || t.audio_rate < 4000 ||
            t.audio_rate > 48000)
            return bad(err, errlen, "a bad audio format");
        const uint32_t sf = (uint32_t)t.audio_channels * (uint32_t)t.audio_bits / 8;
        if (audio_len % sf)
            return bad(err, errlen, "the audio is not a whole number of samples");
    } else if (audio_len)
        return bad(err, errlen, "audio bytes without an audio format");
    const uint64_t index_end = (uint64_t)index_off + (uint64_t)t.frames * 8;
    if (index_off < BMV_HEADER || index_end > len)
        return bad(err, errlen, "the frame index is outside the file");
    if (audio_len && (audio_off < BMV_HEADER || (uint64_t)audio_off + audio_len > len))
        return bad(err, errlen, "the audio is outside the file");
    t.index = d + index_off;
    t.audio = d + audio_off;
    t.audio_len = audio_len;
    for (uint32_t i = 0; i < t.frames; i++) {
        const uint32_t off = rd32(t.index + (size_t)i * 8), n = rd32(t.index + (size_t)i * 8 + 4);
        if (off < BMV_HEADER || n < (t.codec == BMV_CODEC_RLE565 ? 3u : 4u) || (uint64_t)off + n > len)
            return bad(err, errlen, "a frame is outside the file");
        if (t.codec == BMV_CODEC_RLE565 && n > BMV_RLE565_MAX(t.width, t.height))
            return bad(err, errlen, "an RLE frame larger than the worst case");
        if (t.codec != BMV_CODEC_MJPEG && t.codec != BMV_CODEC_RLE565 && n != (uint32_t)t.width * (uint32_t)t.height * (t.codec == BMV_CODEC_RAW565 ? 2u : 1u))
            return bad(err, errlen, "a raw frame of the wrong size");
    }
    *v = t;
    return 0;
}

int bmv_frame(const bmv_t *v, uint32_t i, const uint8_t **p, size_t *n)
{
    if (i >= v->frames)
        return -1;
    *p = v->data + rd32(v->index + (size_t)i * 8);
    *n = rd32(v->index + (size_t)i * 8 + 4);
    return 0;
}

uint32_t bmv_frame_at(const bmv_t *v, uint32_t ms)
{
    const uint64_t i = (uint64_t)ms * (uint32_t)v->fps / 1000u;
    return i >= v->frames ? v->frames - 1 : (uint32_t)i;
}

uint32_t bmv_duration_ms(const bmv_t *v)
{
    return (uint32_t)((uint64_t)v->frames * 1000u / (uint32_t)v->fps);
}

size_t bmv_rle565_row(uint8_t *out, const uint16_t *cur, const uint16_t *prev, int w)
{
    uint8_t *o = out;
    int x = 0;
    while (x < w) {
        const int room = w - x < 64 ? w - x : 64;
        int n = 1;
        if (prev && cur[x] == prev[x]) {                /* copy above: one byte for up to 64 pixels */
            while (n < room && cur[x + n] == prev[x + n])
                n++;
            *o++ = (uint8_t)(0x80 | (n - 1));
        } else if (x + 1 < w && cur[x + 1] == cur[x]) { /* repeat: three bytes */
            while (n < room && cur[x + n] == cur[x])
                n++;
            *o++ = (uint8_t)(0x40 | (n - 1));
            *o++ = (uint8_t)cur[x];
            *o++ = (uint8_t)(cur[x] >> 8);
        } else {                                        /* literal, until a run or a copy starts */
            while (n < room) {
                const int i = x + n;
                if (i + 1 < w && cur[i] == cur[i + 1])
                    break;
                if (prev && cur[i] == prev[i] && (i + 1 >= w || cur[i + 1] == prev[i + 1]))
                    break;
                n++;
            }
            *o++ = (uint8_t)(n - 1);
            for (int k = 0; k < n; k++) {
                *o++ = (uint8_t)cur[x + k];
                *o++ = (uint8_t)(cur[x + k] >> 8);
            }
        }
        x += n;
    }
    return (size_t)(o - out);
}

size_t bmv_rle565_encode(uint8_t *out, const uint16_t *px, int w, int h)
{
    size_t n = 0;
    for (int y = 0; y < h; y++)
        n += bmv_rle565_row(out + n, px + (size_t)y * (size_t)w, y ? px + (size_t)(y - 1) * (size_t)w : NULL, w);
    return n;
}

int bmv_rle565_decode(const uint8_t *p, size_t n, uint16_t *out, int w, int h)
{
    const size_t total = (size_t)w * (size_t)h;
    size_t pos = 0, i = 0;
    while (pos < total) {
        if (i >= n)
            return -1;
        const unsigned c = p[i++], cnt = (c & 63u) + 1u;
        if (cnt > total - pos)
            return -1;
        switch (c >> 6) {
        case 0:
            if ((size_t)cnt * 2u > n - i)
                return -1;
            for (unsigned k = 0; k < cnt; k++, i += 2)
                out[pos++] = (uint16_t)(p[i] | p[i + 1] << 8);
            break;
        case 1: {
            if (n - i < 2)
                return -1;
            const uint16_t v = (uint16_t)(p[i] | p[i + 1] << 8);
            i += 2;
            for (unsigned k = 0; k < cnt; k++)
                out[pos++] = v;
            break;
        }
        case 2:
            if (pos < (size_t)w)
                return -1;
            for (unsigned k = 0; k < cnt; k++, pos++)
                out[pos] = out[pos - (size_t)w];
            break;
        default:
            return -1;
        }
    }
    return i == n ? 0 : -1;
}

int bmv_decode(const bmv_t *v, uint32_t i, uint16_t *out, char *err, size_t errlen)
{
    const uint8_t *p;
    size_t n;
    int w, h;
    if (bmv_frame(v, i, &p, &n) != 0)
        return bad(err, errlen, "no such frame");
    if (v->codec == BMV_CODEC_RAW565) {
        for (size_t k = 0; k < (size_t)v->width * (size_t)v->height; k++)
            out[k] = (uint16_t)(p[2 * k] | p[2 * k + 1] << 8);
        return 0;
    }
    if (v->codec == BMV_CODEC_RLE565) {
        if (bmv_rle565_decode(p, n, out, v->width, v->height) != 0)
            return bad(err, errlen, "a bad RLE frame");
        return 0;
    }
    if (v->codec == BMV_CODEC_RGB332) {
        for (size_t k = 0; k < (size_t)v->width * (size_t)v->height; k++) {
            const unsigned r = p[k] >> 5, g = p[k] >> 2 & 7, b = p[k] & 3;
            out[k] = (uint16_t)(((r << 2 | r >> 1) << 11) | ((g << 3 | g) << 5) | (b << 3 | b << 1 | b >> 1));
        }
        return 0;
    }
    if (jpeg_decode565(p, n, out, v->width, v->height, &w, &h, err, errlen) != 0)
        return -1;
    if (w != v->width || h != v->height)
        return bad(err, errlen, "a frame of another size than the clip");
    return 0;
}

size_t bmv_audio_span(const bmv_t *v, uint32_t from_ms, uint32_t to_ms, const uint8_t **p)
{
    if (!v->audio_channels || to_ms <= from_ms)
        return 0;
    const uint32_t sf = (uint32_t)v->audio_channels * (uint32_t)v->audio_bits / 8;
    uint64_t a = (uint64_t)from_ms * v->audio_rate / 1000u, b = (uint64_t)to_ms * v->audio_rate / 1000u;
    const uint64_t total = v->audio_len / sf;
    if (a > total)
        a = total;
    if (b > total)
        b = total;
    *p = v->audio + a * sf;
    return (size_t)((b - a) * sf);
}
