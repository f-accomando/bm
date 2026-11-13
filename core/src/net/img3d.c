#include "img3d.h"
#include "http.h"
#include "bm/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct img3d_provider {
    const char *name;
    const char *key_name;
    char base[128];             /* the API's address */
};

static img3d_provider_t providers[] = {
    { "meshy", "meshy_key", "https://api.meshy.ai/openapi/v1" },
};
#define NPROV ((int)(sizeof providers / sizeof providers[0]))

const img3d_provider_t *img3d_provider(const char *name)
{
    for (int i = 0; i < NPROV; i++)
        if (name && strcmp(providers[i].name, name) == 0)
            return &providers[i];
    return NULL;
}

const char *img3d_provider_name(int i)
{
    return i >= 0 && i < NPROV ? providers[i].name : NULL;
}

const char *img3d_key_name(const img3d_provider_t *p)
{
    return p ? p->key_name : NULL;
}

void img3d_set_base(const img3d_provider_t *p, const char *base)
{
    for (int i = 0; i < NPROV; i++)
        if (&providers[i] == p) {
            strncpy(providers[i].base, base, sizeof providers[i].base - 1);
            providers[i].base[sizeof providers[i].base - 1] = 0;
        }
}

/* -------------------------------------------------------------- helpers */

static void say(char *err, size_t errlen, const char *what)
{
    if (err && errlen) {
        strncpy(err, what, errlen - 1);
        err[errlen - 1] = 0;
    }
}

typedef struct {
    uint8_t *data;
    size_t len, cap, max;
} buf_t;

static int collect(void *ctx, const uint8_t *data, size_t len)
{
    buf_t *b = ctx;
    if (b->len + len > b->max)
        return -1;
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + len + 1)
            cap *= 2;
        uint8_t *n = realloc(b->data, cap);
        if (!n)
            return -1;
        b->data = n;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
    return 0;
}

static size_t base64(const uint8_t *in, size_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[v >> 18];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

/* a request to the service: the JSON answer parsed, or NULL with err */
static json_t *call(const img3d_provider_t *p, const char *key, const char *method, const char *path,
                    const char *body, char *err, size_t errlen)
{
    char url[256], headers[256];
    snprintf(url, sizeof url, "%s%s", p->base, path);
    snprintf(headers, sizeof headers, "Authorization: Bearer %s\r\n%s", key,
             body ? "Content-Type: application/json\r\n" : "");
    http_req_t req;
    memset(&req, 0, sizeof req);
    req.method = method;
    req.headers = headers;
    req.body = body;
    req.body_len = body ? strlen(body) : 0;
    req.timeout_ms = 30000;
    buf_t b = { NULL, 0, 0, 1 << 20 };
    http_info_t info;
    int status = http_request(url, &req, collect, &b, &info);
    if (status < 0) {
        char msg[256];
        snprintf(msg, sizeof msg, "%s: %s", p->name, info.error);
        say(err, errlen, msg);
        free(b.data);
        return NULL;
    }
    json_t *js = b.data ? json_parse((const char *)b.data, b.len, NULL, 0) : NULL;
    if (status < 200 || status >= 300) {
        char msg[256];
        const char *detail = json_str(js, "message", NULL);
        if (status == 401 || status == 403)
            snprintf(msg, sizeof msg, "%s refuses the key (%s in bm/config.txt): HTTP %d", p->name, p->key_name, status);
        else if (status == 402)
            snprintf(msg, sizeof msg, "%s: no credits left on the account (HTTP 402)", p->name);
        else
            snprintf(msg, sizeof msg, "%s: HTTP %d%s%s", p->name, status, detail ? ": " : "", detail ? detail : "");
        say(err, errlen, msg);
        json_free(js);
        free(b.data);
        return NULL;
    }
    free(b.data);
    if (!js)
        say(err, errlen, "the service's answer is not JSON");
    return js;
}

/* ---------------------------------------------------------------- meshy */

int img3d_start(const img3d_provider_t *p, const char *key, const uint8_t *image, size_t len, const char *url,
                int polycount, char *task, size_t tasklen, char *err, size_t errlen)
{
    if (!p || !key || !key[0]) {
        say(err, errlen, "no key for the service");
        return -1;
    }
    if (!image && !url) {
        say(err, errlen, "no picture");
        return -1;
    }
    const char *media = NULL;
    if (image) {
        if (len > 3 && image[0] == 0xFF && image[1] == 0xD8)
            media = "image/jpeg";
        else if (len > 8 && memcmp(image, "\x89PNG", 4) == 0)
            media = "image/png";
        else {
            say(err, errlen, "the picture must be a PNG or a JPEG");
            return -1;
        }
        if (len > 12u << 20) {
            say(err, errlen, "the picture is bigger than 12 MB");
            return -1;
        }
    }
    size_t cap = (image ? len / 3 * 4 + 8 : strlen(url)) + 512;
    char *body = malloc(cap);
    if (!body) {
        say(err, errlen, "no memory for the picture");
        return -1;
    }
    size_t o = (size_t)snprintf(body, cap, "{\"image_url\":\"");
    if (image) {
        o += (size_t)snprintf(body + o, cap - o, "data:%s;base64,", media);
        o += base64(image, len, body + o);
    } else {
        for (const char *c = url; *c && o < cap - 300; c++)
            if (*c != '"' && *c != '\\' && (unsigned char)*c >= 32)
                body[o++] = *c;
        body[o] = 0;
    }
    snprintf(body + o, cap - o, "\",\"ai_model\":\"meshy-5\",\"topology\":\"triangle\",\"target_polycount\":%d,"
             "\"should_remesh\":true,\"should_texture\":true,\"enable_pbr\":false,\"symmetry_mode\":\"auto\"}",
             polycount > 0 ? polycount : 2000);
    json_t *js = call(p, key, "POST", "/image-to-3d", body, err, errlen);
    free(body);
    if (!js)
        return -1;
    const char *id = json_str(js, "result", NULL);
    if (!id || !id[0]) {
        say(err, errlen, "the service gave no job id");
        json_free(js);
        return -1;
    }
    strncpy(task, id, tasklen - 1);
    task[tasklen - 1] = 0;
    json_free(js);
    return 0;
}

int img3d_status(const img3d_provider_t *p, const char *key, const char *task, int *progress, char *model_url,
                 size_t urllen, char *err, size_t errlen)
{
    *progress = 0;
    if (!p || !task || !task[0]) {
        say(err, errlen, "no job");
        return -1;
    }
    for (const char *c = task; *c; c++)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '-' || *c == '_')) {
            say(err, errlen, "a bad job id");
            return -1;
        }
    char path[160];
    snprintf(path, sizeof path, "/image-to-3d/%s", task);
    json_t *js = call(p, key, "GET", path, NULL, err, errlen);
    if (!js)
        return -1;
    const char *status = json_str(js, "status", "");
    *progress = (int)json_num(js, "progress", 0);
    int ret;
    if (strcmp(status, "SUCCEEDED") == 0) {
        const char *glb = json_str(json_get(js, "model_urls"), "glb", NULL);
        if (!glb) {
            say(err, errlen, "the job is done but there is no .glb");
            ret = -1;
        } else {
            strncpy(model_url, glb, urllen - 1);
            model_url[urllen - 1] = 0;
            *progress = 100;
            ret = 1;
        }
    } else if (strcmp(status, "FAILED") == 0 || strcmp(status, "CANCELED") == 0 || strcmp(status, "EXPIRED") == 0) {
        char msg[256];
        const char *why = json_str(json_get(js, "task_error"), "message", NULL);
        snprintf(msg, sizeof msg, "%s: the job %s%s%s", p->name, status[0] == 'F' ? "failed" : "was cancelled",
                 why ? ": " : "", why ? why : "");
        say(err, errlen, msg);
        ret = -1;
    } else
        ret = 0;                                /* PENDING, IN_PROGRESS */
    json_free(js);
    return ret;
}

int img3d_download(const char *url, size_t max, uint8_t **data, size_t *len, char *err, size_t errlen)
{
    buf_t b = { NULL, 0, 0, max };
    http_req_t req;
    memset(&req, 0, sizeof req);
    req.timeout_ms = 60000;
    http_info_t info;
    int status = http_request(url, &req, collect, &b, &info);
    if (status < 0 || status != 200) {
        char msg[256];
        if (status < 0)
            snprintf(msg, sizeof msg, "download: %s", info.error);
        else
            snprintf(msg, sizeof msg, "download: HTTP %d", status);
        say(err, errlen, msg);
        free(b.data);
        return -1;
    }
    *data = b.data;
    *len = b.len;
    return 0;
}
