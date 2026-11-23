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

/* 1 when name ends with ext (".bmr"), in any case. */
static int has_ext(const char *name, const char *ext)
{
    const size_t n = strlen(name), e = strlen(ext);
    if (n <= e)
        return 0;
    for (size_t i = 0; i < e; i++)
        if (lower((unsigned char)name[n - e + i]) != ext[i])
            return 0;
    return 1;
}

void sdk_file_stem(const char *name, char out[SDK_NAME_MAX])
{
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    size_t n = strlen(base);
    if (has_ext(base, ".bme") || has_ext(base, ".bmr"))
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

const char *sdk_file_ext(int target) { return target == SDK_BM ? "bme" : "bmr"; }
const char *sdk_file_dir(int target) { return target == SDK_BM ? BM_PROJECTS_DIR : SDK_BMR_DIR; }

void sdk_file_name(const char *name, int target, char out[SDK_FILE_MAX])
{
    char stem[SDK_NAME_MAX];
    sdk_file_stem(name, stem);
    snprintf(out, SDK_FILE_MAX, "%s.%s", stem, sdk_file_ext(target));
}

void sdk_file_path(const char *name, int target, char *out, size_t n)
{
    char file[SDK_FILE_MAX];
    sdk_file_name(name, target, file);
    snprintf(out, n, "%s/%s", sdk_file_dir(target), file);
}

int sdk_file_exists(const char *name, int target)
{
    char path[32];
    fat_entry_t e;
    sdk_file_path(name, target, path, sizeof path);
    return fat_find(path, &e) == 0 && !e.is_dir;
}

int sdk_file_save(const sdk_proj_t *p, const char *name, const char *source, size_t len, char *err, size_t en)
{
    char stem[SDK_NAME_MAX], path[32];
    sdk_file_stem(name, stem);
    sdk_file_path(stem, p->target, path, sizeof path);
    char *text = malloc(len + 512);
    if (!text) {
        snprintf(err, en, "not enough memory to save");
        return -1;
    }
    const int head = sdk_proj_text(p, text, 512);
    if (head < 0) {
        free(text);
        snprintf(err, en, "the project text is too long");
        return -1;
    }
    if (len)
        memcpy(text + head, source, len);
    size_t out_len = 0;
    uint8_t *out = bm_rewrite_with(NULL, 0, text, (size_t)head + len, stem, "", 640, NULL, 0, &out_len);
    free(text);
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
        char file[SDK_FILE_MAX];
        sdk_file_name(stem, p->target, file);
        ok = fat_mkdirs(sdk_file_dir(p->target)) == 0 &&
             fat_write_file(sdk_file_dir(p->target), file, out, out_len) == 0;
    }
    free(out);
    if (!ok) {
        snprintf(err, en, "%s", fat_error());
        return -1;
    }
    return 0;
}

int sdk_file_load(const char *file, sdk_proj_t *p, char **source, size_t *len, char *err, size_t en)
{
    char path[40];
    fat_entry_t e;
    char stem[SDK_NAME_MAX];
    sdk_file_stem(file, stem);
    /* the folder follows the extension: .bmr in /bmr, else /bme */
    snprintf(path, sizeof path, "%s/%s.%s", has_ext(file, ".bmr") ? SDK_BMR_DIR : BM_PROJECTS_DIR, stem,
             has_ext(file, ".bmr") ? "bmr" : "bme");
    if (fat_find(path, &e) != 0 || e.is_dir) {
        snprintf(err, en, "%s: not found", path);
        return -1;
    }
    uint8_t *data = NULL;
    size_t dlen = 0;
    if (fat_load(&e, &data, &dlen) != 0) {
        snprintf(err, en, "%s", fat_error());
        return -1;
    }
    bm_cart_t c;
    char why[64];
    memset(&c, 0, sizeof c);
    if (bm_parse(data, dlen, &c, why, sizeof why) != 0) {
        snprintf(err, en, "%s: %s", path, why);
        free(data);
        return -1;
    }
    const int rc = c.lua ? sdk_proj_parse(c.lua, c.lua_size, p) : -1;
    *source = NULL;
    *len = 0;
    if (rc == 0) {
        const size_t off = sdk_proj_source_offset(c.lua, c.lua_size);
        const size_t n = c.lua_size - off;
        if (n > 0 && (*source = malloc(n)) != NULL) {
            memcpy(*source, c.lua + off, n);
            *len = n;
        }
    }
    free(data);
    if (rc != 0) {
        snprintf(err, en, "%s is not a Shell SDK project", path);
        return -1;
    }
    return 0;
}

int sdk_file_list(char names[][SDK_FILE_MAX], int max)
{
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {                  /* /bmr (.bmr), then /bme (.bme) */
        const char *dir = pass == 0 ? SDK_BMR_DIR : BM_PROJECTS_DIR;
        const char *ext = pass == 0 ? ".bmr" : ".bme";
        fat_dir_t d;
        fat_entry_t e;
        if (fat_opendir(&d, dir) != 0)
            continue;
        while (n < max && fat_readdir(&d, &e)) {
            if (e.is_dir || !has_ext(e.name, ext))
                continue;
            char stem[SDK_NAME_MAX];
            sdk_file_stem(e.name, stem);
            snprintf(names[n++], SDK_FILE_MAX, "%s%s", stem, ext);
        }
    }
    return n;
}
