/*
 * The file commands of the bm> prompt: ls, cd, pwd, cat, cp, rm, mkdir (SYS-19, step 2).
 * They use fat.h on the SD card. A word is a path from the current folder, or from the
 * root when it starts with '/'. A new name is an 8.3 name (fat.h).
 */
#include "monitor_fs.h"
#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PATH_MAX_BM 128

static char cwd[PATH_MAX_BM] = "/";

int mfs_resolve(const char *base, const char *arg, char *out, unsigned cap)
{
    char tmp[2 * PATH_MAX_BM];
    if (!arg || !arg[0])
        arg = ".";
    if (arg[0] == '/')
        ksnprintf(tmp, sizeof tmp, "%s", arg);
    else
        ksnprintf(tmp, sizeof tmp, "%s/%s", base, arg);
    /* the names one after the other: "." goes, ".." takes the last one back */
    unsigned n = 0;
    char *seg[32];
    char *p = tmp;
    while (*p) {
        while (*p == '/')
            *p++ = 0;
        if (!*p)
            break;
        char *s = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = 0;
        if (!strcmp(s, "."))
            continue;
        if (!strcmp(s, "..")) {
            if (n)
                n--;
            continue;
        }
        if (n == 32)
            return -1;
        seg[n++] = s;
    }
    unsigned len = 0;
    out[0] = 0;
    if (n == 0) {
        if (cap < 2)
            return -1;
        strcpy(out, "/");
        return 0;
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned l = (unsigned)strlen(seg[i]);
        if (len + 1 + l + 1 > cap)
            return -1;
        out[len++] = '/';
        memcpy(out + len, seg[i], l);
        len += l;
        out[len] = 0;
    }
    return 0;
}

static int path_of(const char *word, char *out, unsigned cap)
{
    if (mfs_resolve(cwd, word, out, cap) != 0) {
        kprintf("the path is too long\n");
        return -1;
    }
    return 0;
}

int mfs_path(const char *word, char *out, unsigned cap)
{
    return mfs_resolve(cwd, word, out, cap);
}

int mfs_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("%s\n", cwd);
    return 0;
}

int mfs_cd(int argc, char **argv)
{
    char path[PATH_MAX_BM];
    if (path_of(argc > 1 ? argv[1] : "/", path, sizeof path))
        return 1;
    if (strcmp(path, "/") != 0) {
        fat_entry_t e;
        if (fat_find(path, &e) != 0) {
            kprintf("cd: %s: not found\n", path);
            return 1;
        }
        if (!e.is_dir) {
            kprintf("cd: %s: not a folder\n", path);
            return 1;
        }
    }
    strcpy(cwd, path);
    return 0;
}

int mfs_ls(int argc, char **argv)
{
    char path[PATH_MAX_BM];
    if (path_of(argc > 1 ? argv[1] : ".", path, sizeof path))
        return 1;
    fat_dir_t d;
    fat_entry_t e;
    if (strcmp(path, "/") != 0) {
        if (fat_find(path, &e) != 0) {
            kprintf("ls: %s: not found\n", path);
            return 1;
        }
        if (!e.is_dir) {                        /* a file: its line */
            kprintf("%8lu  %s\n", (unsigned long)e.size, e.name);
            return 0;
        }
    }
    if (fat_opendir(&d, path) != 0) {
        kprintf("ls: %s: %s\n", path, fat_error());
        return 1;
    }
    unsigned files = 0, dirs = 0;
    unsigned long bytes = 0;
    while (fat_readdir(&d, &e)) {
        if (e.is_dir) {
            kprintf("   <dir>  %s/\n", e.name);
            dirs++;
        } else {
            kprintf("%8lu  %s\n", (unsigned long)e.size, e.name);
            files++;
            bytes += e.size;
        }
    }
    kprintf("%u files, %lu bytes; %u folders\n", files, bytes, dirs);
    return 0;
}

#define CAT_MAX 16384u

int mfs_cat(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("cat: needs a file\n");
        return 1;
    }
    char path[PATH_MAX_BM];
    if (path_of(argv[1], path, sizeof path))
        return 1;
    fat_entry_t e;
    if (fat_find(path, &e) != 0) {
        kprintf("cat: %s: not found\n", path);
        return 1;
    }
    if (e.is_dir) {
        kprintf("cat: %s: a folder\n", path);
        return 1;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_load_part(&e, CAT_MAX, &data, &len) != 0) {
        kprintf("cat: %s: %s\n", path, fat_error());
        return 1;
    }
    char line[96];
    unsigned n = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\r')
            continue;
        if (c == '\n' || n >= sizeof line - 2) {
            line[n] = 0;
            kprintf("%s\n", line);
            n = 0;
            if (c == '\n')
                continue;
        }
        line[n++] = c == '\t' ? ' ' : (c >= ' ' && c < 127) ? (char)c : '.';
    }
    if (n) {
        line[n] = 0;
        kprintf("%s\n", line);
    }
    if (e.size > len)
        kprintf("... %lu more bytes\n", (unsigned long)(e.size - len));
    free(data);
    return 0;
}

static const char *base_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* the files the firmware reads to start the card: not removed from here */
static int boot_file(const char *path)
{
    const char *b = base_of(path);
    if (strchr(path + 1, '/'))
        return 0;
    return !strcasecmp(b, "kernel.img") || !strcasecmp(b, "config.txt") || !strcasecmp(b, "bootcode.bin") ||
           !strncasecmp(b, "start", 5) || !strncasecmp(b, "fixup", 5);
}

int mfs_rm(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("rm: needs a file\n");
        return 1;
    }
    char path[PATH_MAX_BM];
    if (path_of(argv[1], path, sizeof path))
        return 1;
    fat_entry_t e;
    if (fat_find(path, &e) != 0) {
        kprintf("rm: %s: not found\n", path);
        return 1;
    }
    if (e.is_dir) {
        kprintf("rm: %s: a folder (only files go)\n", path);
        return 1;
    }
    if (boot_file(path)) {
        kprintf("rm: %s: a file the card needs to start: not removed here\n", path);
        return 1;
    }
    if (fat_delete(path) != 0) {
        kprintf("rm: %s: %s\n", path, fat_error());
        return 1;
    }
    kprintf("removed %s\n", path);
    return 0;
}

int mfs_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("mkdir: needs a name\n");
        return 1;
    }
    char path[PATH_MAX_BM];
    if (path_of(argv[1], path, sizeof path))
        return 1;
    if (fat_mkdirs(path) != 0) {
        kprintf("mkdir: %s: %s (a name is 8.3)\n", path, fat_error());
        return 1;
    }
    return 0;
}

int mfs_cp(int argc, char **argv)
{
    if (argc < 3) {
        kprintf("cp: needs a file and a destination\n");
        return 1;
    }
    char from[PATH_MAX_BM], to[PATH_MAX_BM];
    if (path_of(argv[1], from, sizeof from) || path_of(argv[2], to, sizeof to))
        return 1;
    fat_entry_t s, t;
    if (fat_find(from, &s) != 0) {
        kprintf("cp: %s: not found\n", from);
        return 1;
    }
    if (s.is_dir) {
        kprintf("cp: %s: a folder (only files)\n", from);
        return 1;
    }
    int exists = strcmp(to, "/") == 0 || fat_find(to, &t) == 0;
    int to_dir = strcmp(to, "/") == 0 || (exists && t.is_dir);
    if (to_dir) {                               /* into the folder, with its name */
        char tmp[PATH_MAX_BM];
        if (ksnprintf(tmp, sizeof tmp, "%s/%s", strcmp(to, "/") ? to : "", base_of(from)) >= (int)sizeof tmp) {
            kprintf("cp: the path is too long\n");
            return 1;
        }
        strcpy(to, tmp);
        exists = fat_find(to, &t) == 0;
        if (exists && t.is_dir) {
            kprintf("cp: %s: a folder\n", to);
            return 1;
        }
    }
    if (!strcmp(from, to)) {
        kprintf("cp: the same file\n");
        return 1;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_load(&s, &data, &len) != 0) {
        kprintf("cp: %s: %s\n", from, fat_error());
        return 1;
    }
    int r;
    if (exists) {
        r = fat_replace(to, data, len);
    } else {
        char dir[PATH_MAX_BM];
        strcpy(dir, to);
        char *slash = strrchr(dir, '/');
        const char *name = to + (slash - dir) + 1;
        if (slash == dir)
            slash[1] = 0;                       /* the root */
        else
            *slash = 0;
        r = fat_write_file(dir, name, data, len);
    }
    free(data);
    if (r != 0) {
        kprintf("cp: %s: %s (a new name is 8.3)\n", to, fat_error());
        return 1;
    }
    kprintf("copied %lu bytes to %s\n", (unsigned long)len, to);
    return 0;
}
