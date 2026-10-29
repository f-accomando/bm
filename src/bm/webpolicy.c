/* The rules of the web API for cartridges: see webpolicy.h. */
#include "webpolicy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef BM_HOST_TEST
static int allow_http;
void web_policy_allow_http(int on)
{
    allow_http = on;
}
#endif

static int fail(char *err, size_t en, const char *why)
{
    if (err && en)
        snprintf(err, en, "%s", why);
    return -1;
}

int web_url_check(const char *url, char *host, size_t hn, char *err, size_t en)
{
    if (!url || !*url)
        return fail(err, en, "no address");
    if (strlen(url) > WEB_URL_MAX)
        return fail(err, en, "the address is over 1024 characters");
    int scheme = 8;
    if (strncmp(url, "https://", 8) != 0) {
#ifdef BM_HOST_TEST
        if (allow_http && strncmp(url, "http://", 7) == 0)
            scheme = 7;
        else
#endif
            return fail(err, en, "only https:// addresses (lower case) are allowed");
    }
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7F)
            return fail(err, en, "the address has a space, a control or a non-ASCII character: encode it with %XX");
        if (*p == '#')
            return fail(err, en, "remove the #fragment from the address");
        if (*p == '\\')
            return fail(err, en, "a backslash in the address");
    }
    const char *h = url + scheme;
    size_t n = strcspn(h, ":/?");
    if (n == 0 || n > 127)
        return fail(err, en, "no host name, or too long");
    if (memchr(h, '@', strcspn(h, "/?")))
        return fail(err, en, "a user name in the address is not allowed");
    for (size_t i = 0; i < n; i++) {
        const char c = h[i];
        const int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                       c == '-';
        if (!ok)
            return fail(err, en, "the host name has a character that is not a letter, a digit, \".\" or \"-\"");
    }
    if (h[0] == '.' || h[0] == '-' || h[n - 1] == '.' || h[n - 1] == '-' || memcmp(h, "..", 2) == 0)
        return fail(err, en, "a bad host name");
    for (size_t i = 0; i + 1 < n; i++)
        if (h[i] == '.' && h[i + 1] == '.')
            return fail(err, en, "a bad host name");
    if (h[n] == ':') {
        const char *q = h + n + 1;
        size_t k = strspn(q, "0123456789");
        long port = k && k <= 5 ? strtol(q, NULL, 10) : 0;
        if (port < 1 || port > 65535 || (q[k] && q[k] != '/' && q[k] != '?'))
            return fail(err, en, "a bad port");
    }
    if (host && hn) {
        if (n >= hn)
            return fail(err, en, "host name too long");
        for (size_t i = 0; i < n; i++)
            host[i] = (char)(h[i] >= 'A' && h[i] <= 'Z' ? h[i] + 32 : h[i]);
        host[n] = 0;
    }
    return 0;
}

int web_file_name(const char *path, char out[13], char *err, size_t en)
{
    if (!path || !*path)
        return fail(err, en, "no file name");
    static const char *const prefixes[] = { "/bm/web/", "bm/web/", "web/" };
    const char *name = path;
    for (size_t i = 0; i < sizeof prefixes / sizeof *prefixes; i++) {
        const size_t k = strlen(prefixes[i]);
        if (strncasecmp(path, prefixes[i], k) == 0) {
            name = path + k;
            break;
        }
    }
    if (strstr(name, ".."))
        return fail(err, en, "\"..\" is not allowed in a file name");
    if (name[0] == '/' || strchr(name, '/') || strchr(name, '\\'))
        return fail(err, en, "a cartridge writes only in bm/web: a bare 8.3 name, no other folder");
    int base = 0, ext = -1, n = 0;
    for (const char *p = name; *p; p++) {
        const char c = *p;
        if (c == '.') {
            if (ext >= 0 || !base)
                return fail(err, en, "a name like model.glb: 8 characters, a dot, 3 characters (no leading dot)");
            ext = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                   c == '-') {
            if (ext >= 0 ? ++ext > 3 : ++base > 8)
                return fail(err, en, "a name like model.glb: 8 characters, a dot, 3 characters");
        } else {
            return fail(err, en, "a file name has letters, digits, \"_\", \"-\" and one dot");
        }
        out[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    out[n] = 0;
    if (!base || ext == 0)
        return fail(err, en, "a name like model.glb: 8 characters, a dot, 3 characters");
    if (ext > 0) {
        const char *e = strrchr(out, '.') + 1;
        if (strcasecmp(e, "bm") == 0 || strcasecmp(e, "bme") == 0 || strcasecmp(e, "b16") == 0)
            return fail(err, en, "a cartridge cannot write .bm, .bme or .b16 files");
    }
    return 0;
}

/* ---- the pause per host: a small table, the oldest entry is replaced ---- */

#define GAP_HOSTS 8
static struct {
    uint32_t hash;
    uint32_t at;
    uint8_t used;
} gap[GAP_HOSTS];
static uint8_t gap_next;

static uint32_t host_hash(const char *h)
{
    uint32_t x = 2166136261u;
    for (; *h; h++)
        x = (x ^ (uint8_t)(*h >= 'A' && *h <= 'Z' ? *h + 32 : *h)) * 16777619u;
    return x;
}

uint32_t web_gap_left_us(const char *host, uint32_t now_us)
{
    const uint32_t hh = host_hash(host);
    for (int i = 0; i < GAP_HOSTS; i++)
        if (gap[i].used && gap[i].hash == hh) {
            const uint32_t since = now_us - gap[i].at;
            return since >= WEB_MIN_GAP_US ? 0 : WEB_MIN_GAP_US - since;
        }
    return 0;
}

void web_gap_mark(const char *host, uint32_t now_us)
{
    const uint32_t hh = host_hash(host);
    int slot = -1;
    for (int i = 0; i < GAP_HOSTS; i++)
        if (gap[i].used && gap[i].hash == hh)
            slot = i;
    if (slot < 0) {
        slot = gap_next;
        gap_next = (uint8_t)((gap_next + 1) % GAP_HOSTS);
    }
    gap[slot].hash = hh;
    gap[slot].at = now_us;
    gap[slot].used = 1;
}

void web_gap_reset(void)
{
    memset(gap, 0, sizeof gap);
    gap_next = 0;
}

int web_lan_url_check(const char *url, char *err, size_t en)
{
    unsigned b[4], port;
    char tail[16];
    int used = 0;
    if (!url || sscanf(url, "http://%u.%u.%u.%u:%u%15s%n", &b[0], &b[1], &b[2], &b[3], &port, tail, &used) != 6 ||
        url[used] || strcmp(tail, "/ca.pem") || b[0] > 255 || b[1] > 255 || b[2] > 255 || b[3] > 255 || !port ||
        port > 65535)
        return fail(err, en, "the relay's address is four numbers and a port (192.168.1.20:8443)");
    const int lan = b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168);
#ifdef BM_HOST_TEST
    if (allow_http && b[0] == 127)
        return 0;                                   /* the PC tests' server */
#endif
    if (!lan)
        return fail(err, en, "the relay must be on your LAN (10.x, 172.16-31.x or 192.168.x)");
    return 0;
}

size_t web_cap_bytes(const char *kib_text, unsigned def, unsigned limit)
{
    unsigned long k = def;
    if (kib_text && *kib_text) {
        char *end;
        unsigned long v = strtoul(kib_text, &end, 10);
        if (end != kib_text && !*end && v > 0)
            k = v;
    }
    if (k > limit)
        k = limit;
    return (size_t)k * 1024u;
}
