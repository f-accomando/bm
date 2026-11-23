/*
 * Shell SDK: the project files (see shell_sdk_file.h). Original code.
 */
#include "shell_sdk_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/bm.h"
#include "bm/project.h"
#include "fs/fat.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

void sdk_file_stem(const char *name, char out[SDK_NAME_MAX])
{
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    size_t n = strlen(base);
    if (n > 4 && lower((unsigned char)base[n - 4]) == '.' && lower((unsigned char)base[n - 3]) == 'b' &&
        lower((unsigned char)base[n - 2]) == 'm' && lower((unsigned char)base[n - 1]) == 'e')
        n -= 4;                                     /* the extension */
    size_t k = 0;
    for (size_t i = 0; i < n && k < SDK_NAME_MAX - 1; i++) {
        const int c = lower((unsigned char)base[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            out[k++] = (char)c;
    }
    if (k == 0) {
        memcpy(out, "project", 8);
        return;
    }
    out[k] = 0;
}

void sdk_file_path(const char *name, char *out, size_t n)
{
    char stem[SDK_NAME_MAX];
    sdk_file_stem(name, stem);
    snprintf(out, n, "%s/%s.bme", BM_PROJECTS_DIR, stem);
}

int sdk_file_exists(const char *name)
{
    char path[32];
    fat_entry_t e;
    sdk_file_path(name, path, sizeof path);
    return fat_find(path, &e) == 0 && !e.is_dir;
}

int sdk_file_save(const sdk_proj_t *p, const char *name, char *err, size_t en)
{
    char stem[SDK_NAME_MAX], path[32], text[512];
    sdk_file_stem(name, stem);
    sdk_file_path(stem, path, sizeof path);
    const int len = sdk_proj_text(p, text, sizeof text);
    if (len < 0) {
        snprintf(err, en, "the project text is too long");
        return -1;
    }
    size_t out_len = 0;
    uint8_t *out = bm_rewrite_with(NULL, 0, text, (size_t)len, stem, "", 640, NULL, 0, &out_len);
    if (!out) {
        snprintf(err, en, "not enough memory to save");
        return -1;
    }
    out[18] |= BM_FLAG_PROJECT;
    fat_entry_t e;
    int ok;
    if (fat_find(path, &e) == 0 && !e.is_dir) {
        ok = fat_replace(path, out, out_len) == 0;
    } else {
        char file[16];
        snprintf(file, sizeof file, "%s.bme", stem);
        ok = fat_mkdirs(BM_PROJECTS_DIR) == 0 && fat_write_file(BM_PROJECTS_DIR, file, out, out_len) == 0;
    }
    free(out);
    if (!ok) {
        snprintf(err, en, "%s", fat_error());
        return -1;
    }
    return 0;
}

int sdk_file_load(const char *name, sdk_proj_t *p, char *err, size_t en)
{
    char path[32];
    fat_entry_t e;
    sdk_file_path(name, path, sizeof path);
    if (fat_find(path, &e) != 0 || e.is_dir) {
        snprintf(err, en, "%s: not found", path);
        return -1;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_load(&e, &data, &len) != 0) {
        snprintf(err, en, "%s", fat_error());
        return -1;
    }
    bm_cart_t c;
    char why[64];
    memset(&c, 0, sizeof c);
    if (bm_parse(data, len, &c, why, sizeof why) != 0) {
        snprintf(err, en, "%s: %s", path, why);
        free(data);
        return -1;
    }
    const int rc = c.lua ? sdk_proj_parse(c.lua, c.lua_size, p) : -1;
    free(data);
    if (rc != 0) {
        snprintf(err, en, "%s is not a Shell SDK project", path);
        return -1;
    }
    return 0;
}

int sdk_file_list(char names[][SDK_NAME_MAX], int max)
{
    fat_dir_t d;
    fat_entry_t e;
    int n = 0;
    if (fat_opendir(&d, BM_PROJECTS_DIR) != 0)
        return 0;
    while (n < max && fat_readdir(&d, &e)) {
        if (e.is_dir || !bm_is_project(e.name))
            continue;
        sdk_file_stem(e.name, names[n]);
        n++;
    }
    return n;
}
