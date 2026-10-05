#include "config.h"
#include "pointer.h"
#include "audio/audio.h"
#include "bm/runtime.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "usb/hid.h"

#include <stdlib.h>
#include <string.h>

#define DIR   "/bm"
/* the folder's name before the project was renamed bm: still read */
#define OLD_DIR "/bm33"
#define FILE_ "CONFIG.TXT"
#define MAX_KEYS 64
#define LINE (int)(sizeof kv[0].key + sizeof kv[0].value + 2)     /* "key=value\n" */

/* a value up to 127 characters: a GitHub fine-grained token has 93 */
static struct { char key[24], value[128]; } kv[MAX_KEYS];
static int nkv;

const char *config_get(const char *key)
{
    for (int i = 0; i < nkv; i++)
        if (strcmp(kv[i].key, key) == 0)
            return kv[i].value;
    return NULL;
}

void config_set(const char *key, const char *value)
{
    for (int i = 0; i < nkv; i++)
        if (strcmp(kv[i].key, key) == 0) {
            ksnprintf(kv[i].value, sizeof kv[i].value, "%s", value);
            return;
        }
    if (nkv < MAX_KEYS) {
        ksnprintf(kv[nkv].key, sizeof kv[nkv].key, "%s", key);
        ksnprintf(kv[nkv].value, sizeof kv[nkv].value, "%s", value);
        nkv++;
    }
}

void config_unset(const char *key)
{
    for (int i = 0; i < nkv; i++)
        if (strcmp(kv[i].key, key) == 0) {
            memmove(&kv[i], &kv[i + 1], (size_t)(nkv - i - 1) * sizeof kv[0]);
            nkv--;
            return;
        }
}

static void parse(const char *text, size_t len)
{
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && text[i] != '\n') i++;
        size_t end = i++;
        while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ')) end--;
        if (end == start || text[start] == '#')
            continue;
        const char *eq = memchr(text + start, '=', end - start);
        if (!eq)
            continue;
        char key[sizeof kv[0].key], value[sizeof kv[0].value];
        size_t kl = (size_t)(eq - (text + start)), vl = end - (size_t)(eq - text) - 1;
        if (kl == 0 || kl >= sizeof key || vl >= sizeof value)
            continue;
        memcpy(key, text + start, kl);
        key[kl] = 0;
        memcpy(value, eq + 1, vl);
        value[vl] = 0;
        config_set(key, value);
    }
}

int config_find_file(const char *name, fat_entry_t *e)
{
    char path[96];
    ksnprintf(path, sizeof path, "%s/%s", DIR, name);
    if (fat_find(path, e) == 0)
        return 0;
    ksnprintf(path, sizeof path, "%s/%s", OLD_DIR, name);
    return fat_find(path, e);
}

/* The settings the console uses at once */
static void apply(void)
{
    const char *v;
    if ((v = config_get("layout")))
        hid_set_layout(v);
    if ((v = config_get("draw")))
        bm_set_via_ram(strcmp(v, "ram") == 0);
    if ((v = config_get("perf")))
        bm_set_perf(strcmp(v, "1") == 0);
    if ((v = config_get("volume")) && v[0] >= '0' && v[0] <= '9')
        audio_set_volume(atoi(v));
    pointer_config();
}

void config_load(void)
{
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    nkv = 0;
    if (config_find_file(FILE_, &e) != 0 || fat_load(&e, &data, &len) != 0)
        return;                                 /* no file: defaults */
    parse((const char *)data, len);
    free(data);
    apply();
    kprintf("config: %s/%s, layout %s, .bm drawing %s, volume %d/%d%s\n", DIR, "config.txt", hid_layout(),
            bm_via_ram() ? "via RAM" : "direct", audio_volume(), AUDIO_VOLUME_MAX,
            pointer_enabled() ? "" : ", mouse off");
}

void config_save(void)
{
    config_set("layout", hid_layout());
    config_set("draw", bm_via_ram() ? "ram" : "direct");
    config_set("perf", bm_perf() ? "1" : "0");
    char vol[8];
    ksnprintf(vol, sizeof vol, "%d", audio_volume());
    config_set("volume", vol);

    char *buf = malloc(MAX_KEYS * LINE + 64);
    if (!buf)
        return;
    int n = ksnprintf(buf, 64, "# bm settings (key=value)\n");
    for (int i = 0; i < nkv; i++)
        n += ksnprintf(buf + n, LINE, "%s=%s\n", kv[i].key, kv[i].value);
    if (fat_mkdirs(DIR) != 0 || fat_write_file(DIR, FILE_, buf, (size_t)n) != 0)
        kprintf("\x1b[91mconfig: cannot save (%s)\x1b[0m\n", fat_error());
    free(buf);
}

int config_secret(const char *key)
{
    static const char *const ends[] = { "_token", "_psk", "_password", "_key" };
    size_t kl = strlen(key);
    for (size_t i = 0; i < sizeof ends / sizeof ends[0]; i++) {
        size_t el = strlen(ends[i]);
        if (kl >= el && strcmp(key + kl - el, ends[i]) == 0)
            return 1;
    }
    return 0;
}

size_t config_list(char *out, size_t len)
{
    size_t n = 0;
    if (!len)
        return 0;
    out[0] = 0;
    for (int i = 0; i < nkv; i++) {
        char line[LINE + 32];
        if (config_secret(kv[i].key))
            ksnprintf(line, sizeof line, "%s=(hidden, %u characters)\n", kv[i].key, (unsigned)strlen(kv[i].value));
        else
            ksnprintf(line, sizeof line, "%s=%s\n", kv[i].key, kv[i].value);
        size_t l = strlen(line);
        if (n + l >= len)
            break;
        memcpy(out + n, line, l + 1);
        n += l;
    }
    return n;
}

/* The next line of text from *at: start and end without "\r" and the
 * spaces at the end; 0 at the end of the text */
static int next_line(const char *text, size_t len, size_t *at, size_t *start, size_t *end)
{
    if (*at >= len)
        return 0;
    size_t i = *at;
    *start = i;
    while (i < len && text[i] != '\n')
        i++;
    *end = i;
    *at = i + 1;
    while (*end > *start && (text[*end - 1] == '\r' || text[*end - 1] == ' '))
        (*end)--;
    return 1;
}

int config_merge(const char *text, size_t len, char *out, size_t out_len)
{
    size_t at = 0, s, e;
    /* every line checked first: a wrong one and nothing changes */
    while (next_line(text, len, &at, &s, &e)) {
        if (e == s || text[s] == '#')
            continue;
        const char *eq = memchr(text + s, '=', e - s);
        if (!eq)
            return -1;
        size_t kl = (size_t)(eq - (text + s)), vl = e - s - kl - 1;
        if (kl == 0 || kl >= sizeof kv[0].key || vl >= sizeof kv[0].value)
            return -1;
        for (size_t i = s; i < e; i++) {
            const char c = text[i];
            if ((unsigned char)c < ' ' || c == 0x7f)
                return -1;
            if (i < s + kl && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return -1;
        }
    }
    int changed = 0;
    at = 0;
    while (next_line(text, len, &at, &s, &e)) {
        if (e == s || text[s] == '#')
            continue;
        char key[sizeof kv[0].key], value[sizeof kv[0].value];
        const char *eq = memchr(text + s, '=', e - s);
        size_t kl = (size_t)(eq - (text + s)), vl = e - s - kl - 1;
        memcpy(key, text + s, kl);
        key[kl] = 0;
        memcpy(value, eq + 1, vl);
        value[vl] = 0;
        const char *was = config_get(key);
        if (!vl) {                              /* "key=": the key goes */
            if (was) {
                config_unset(key);
                changed++;
                kprintf("config: %s removed\n", key);
            }
        } else if (!was || strcmp(was, value) != 0) {
            if (!was && nkv >= MAX_KEYS) {
                kprintf("\x1b[91mconfig: no room for %s (%d keys)\x1b[0m\n", key, MAX_KEYS);
                continue;
            }
            config_set(key, value);
            changed++;
            kprintf("config: %s=%s\n", key, config_secret(key) ? "(hidden)" : value);
        }
    }
    if (changed) {
        apply();
        config_save();
    }
    config_list(out, out_len);
    return changed;
}
