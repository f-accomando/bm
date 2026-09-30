#include "config.h"
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
#define MAX_KEYS 32

static struct { char key[24], value[72]; } kv[MAX_KEYS];
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
        char key[24], value[72];
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

    const char *v;
    if ((v = config_get("layout")))
        hid_set_layout(v);
    if ((v = config_get("draw")))
        bm_set_via_ram(strcmp(v, "ram") == 0);
    kprintf("config: %s/%s, layout %s, .bm drawing %s\n", DIR, "config.txt", hid_layout(),
            bm_via_ram() ? "via RAM" : "direct");
}

void config_save(void)
{
    config_set("layout", hid_layout());
    config_set("draw", bm_via_ram() ? "ram" : "direct");

    char *buf = malloc(MAX_KEYS * 100 + 64);
    if (!buf)
        return;
    int n = ksnprintf(buf, 64, "# bm settings (key=value)\n");
    for (int i = 0; i < nkv; i++)
        n += ksnprintf(buf + n, 100, "%s=%s\n", kv[i].key, kv[i].value);
    if (fat_mkdirs(DIR) != 0 || fat_write_file(DIR, FILE_, buf, (size_t)n) != 0)
        kprintf("\x1b[91mconfig: cannot save (%s)\x1b[0m\n", fat_error());
    free(buf);
}
