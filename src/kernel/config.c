#include "config.h"
#include "pointer.h"
#include "audio/audio.h"
#include "bm/runtime.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "lib/printf.h"
#include "usb/hid.h"

#include <stdlib.h>
#include <string.h>

#define DIR   "/bm"
/* the folder's name before the project was renamed bm: still read */
#define OLD_DIR "/bm33"
#define FILE_ "config.txt"
#define BRIDGE_FILE "bridge.txt"
#define MAX_KEYS 64
#define LINE (int)(sizeof kv[0].key + sizeof kv[0].value + 2)     /* "key=value\n" */

/* a value up to 127 characters: a GitHub fine-grained token has 93 */
static struct { char key[24], value[128]; } kv[MAX_KEYS];
static int nkv;

int gpu_qpu_key(void)
{
    const char *v = config_get("gpu3d_qpu");
    return v && strcmp(v, "1") == 0;
}

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

/* perf=: the dev kit's overlay, "off", "top" or "bottom"; the old numbers
 * (1 simple, 2 detailed, 3 functions: one overlay now) are the top */
int config_perf(const char *v)
{
    if (!v || !v[0] || !strcmp(v, "off") || !strcmp(v, "0"))
        return 0;
    return !strcmp(v, "bottom") ? 2 : 1;
}

/* The bridge gate (the PC's tools: bm_net.py, tools/pi.py): one word, off, on or fast, in /bm/bridge.txt, a file of its own.
 * Nothing the network gives can write it: config_merge refuses the keys dev and bridge, and the file transfer refuses the
 * name (bridge_file_name). The user sets it in Settings > System > Bridge, or puts the file on the SD card from a PC.
 * off: no transfer, setting, kernel, screenshot or file read is accepted (the default); on: they are, a kernel is staged
 * and the menu asks; fast: a kernel and a file are written at once (the old dev mode). */
static int bridge_state;

static int bridge_word(const char *t, size_t len)
{
    while (len && (t[len - 1] == '\n' || t[len - 1] == '\r' || t[len - 1] == ' '))
        len--;
    while (len && (*t == ' ' || *t == '\n' || *t == '\r')) {
        t++;
        len--;
    }
    if (len == 4 && memcmp(t, "fast", 4) == 0)
        return BRIDGE_FAST;
    if (len == 2 && memcmp(t, "on", 2) == 0)
        return BRIDGE_ON;
    return BRIDGE_OFF;                          /* off, an empty file, anything else */
}

int bridge_load(void)
{
    char path[32];
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bridge_state = BRIDGE_OFF;
    ksnprintf(path, sizeof path, "%s/%s", DIR, BRIDGE_FILE);
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0)
        return 0;
    bridge_state = bridge_word((const char *)data, len);
    free(data);
    return 1;
}

int bridge_mode(void)
{
    return bridge_state;
}

const char *bridge_label(int mode)
{
    return mode == BRIDGE_FAST ? "fast" : mode == BRIDGE_ON ? "on" : "off";
}

int bridge_set(int mode)
{
    if (mode < BRIDGE_OFF || mode > BRIDGE_FAST)
        return -1;
    const char *w = bridge_label(mode);
    char text[8];
    const int n = ksnprintf(text, sizeof text, "%s\n", w);
    if (fat_mkdirs(DIR) != 0 || sd_write_file("bridge", DIR, BRIDGE_FILE, text, (size_t)n) != 0) {
        kprintf("\x1b[91mbridge: cannot save (%s)\x1b[0m\n", fat_error());
        return -1;
    }
    bridge_state = mode;
    return 0;
}

/* 1 if the last part of a path names the bridge file, in any folder, in any case ("bm/../bm/Bridge.TXT" too) */
int bridge_file_name(const char *path)
{
    const char *last = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\')
            last = c + 1;
    return strcasecmp(last, BRIDGE_FILE) == 0;
}

int dev_mode(void)
{
    return bridge_state == BRIDGE_FAST;
}

int rec_mode(void)
{
    const char *v = config_get("rec");
    if (!v || !v[0])
        return 0;
    return !strcmp(v, "1") || !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "true");
}

void rec_set_mode(int on)
{
    config_set("rec", on ? "on" : "off");
    config_save();
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
        bm_set_perf(config_perf(v));
    if ((v = config_get("volume")) && v[0] >= '0' && v[0] <= '9')
        audio_set_volume(atoi(v));
    if ((v = config_get("sound")))
        audio_retro(AUDIO_RETRO_USER, strcmp(v, "8bit") == 0);    /* hifi (the default) or 8bit */
    if ((v = config_get("sound_depth")))
        audio_set_depth((unsigned)atoi(v));     /* 16, 24 (the default) or 32 bits */
    pointer_config();
}

void config_load(void)
{
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    nkv = 0;
    const int had_bridge = bridge_load();
    if (config_find_file(FILE_, &e) != 0 || fat_load(&e, &data, &len) != 0)
        return;                                 /* no file: defaults */
    parse((const char *)data, len);
    free(data);
    apply();
    /* one time, from a kernel that had dev=1 in config.txt: that was dev mode, it becomes bridge fast. Only while there is no
     * bridge.txt, when the bridge is off and the network writes nothing; the key goes away either way. */
    const char *old_dev = config_get("dev");
    if (old_dev) {
        if (!had_bridge && (!strcmp(old_dev, "1") || !strcmp(old_dev, "on") || !strcmp(old_dev, "yes") || !strcmp(old_dev, "true"))
            && bridge_set(BRIDGE_FAST) == 0)
            kprintf("bridge: fast, from dev=1 of an older kernel (Settings > System > Bridge)\n");
        config_unset("dev");
        config_save();
    }
    kprintf("config: %s/%s, layout %s, .bm drawing %s, volume %d/%d%s\n", DIR, "config.txt", hid_layout(),
            bm_via_ram() ? "via RAM" : "direct", audio_volume(), AUDIO_VOLUME_MAX,
            pointer_enabled() ? "" : ", mouse off");
}

void config_save(void)
{
    config_set("layout", hid_layout());
    config_set("draw", bm_via_ram() ? "ram" : "direct");
    config_set("perf", bm_perf() == 2 ? "bottom" : bm_perf() ? "top" : "off");
    char vol[8];
    ksnprintf(vol, sizeof vol, "%d", audio_volume());
    config_set("volume", vol);

    char *buf = malloc(MAX_KEYS * LINE + 64);
    if (!buf)
        return;
    int n = ksnprintf(buf, 64, "# bm settings (key=value)\n");
    for (int i = 0; i < nkv; i++)
        n += ksnprintf(buf + n, LINE, "%s=%s\n", kv[i].key, kv[i].value);
    if (sd_write_file("config", DIR, FILE_, buf, (size_t)n) != 0)
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
        if ((kl == 3 && strncasecmp(text + s, "dev", 3) == 0) || (kl == 6 && strncasecmp(text + s, "bridge", 6) == 0) ||
            (kl == 8 && strncasecmp(text + s, "autoexec", 8) == 0))
            return -3;                          /* the bridge gate is not a setting of the PC */
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
