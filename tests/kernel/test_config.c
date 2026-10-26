/*
 * bm/config.txt on the PC (src/kernel/config.c): the file read, a GitHub
 * fine-grained token (93 characters) kept, and the settings from the
 * network (config_merge, netxfer's C, easy_install.sh): keys set and
 * removed, the ones the console uses at once applied, nothing changed by
 * a wrong line, the secrets hidden in what goes back to the PC.
 */
#include "kernel/config.h"
#include "fs/fat.h"
#include "kernel/fiber.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

/* ---- what config.c uses */
void uart_putc(char c) { (void)c; }
static const char *layout = "it";
void hid_set_layout(const char *name) { layout = name && name[0] == 'u' && name[1] == 's' ? "us" : "it"; }
const char *hid_layout(void) { return layout; }
static int via_ram, perf, volume = 7;
void bm_set_via_ram(int on) { via_ram = on; }
int bm_via_ram(void) { return via_ram; }
void bm_set_perf(int on) { perf = on; }
int bm_perf(void) { return perf; }
void audio_set_volume(int level) { volume = level < 0 ? 0 : level > 10 ? 10 : level; }
int audio_volume(void) { return volume; }
void audio_retro(int who, int on) { (void)who; (void)on; }
void audio_set_depth(unsigned bits) { (void)bits; }
void pointer_config(void) {}
int pointer_enabled(void) { return 1; }

/* the SD card: bm/config.txt only */
static char file[8192];
static size_t file_len;
static int has_file, writes;
static char bfile[16];                  /* /bm/bridge.txt */
static int has_bridge, on_bridge;       /* on_bridge: the last fat_find was the bridge file */
int fat_find(const char *path, fat_entry_t *e)
{
    (void)e;
    on_bridge = strcmp(path, "/bm/bridge.txt") == 0;
    if (on_bridge)
        return has_bridge ? 0 : -1;
    return has_file && strcmp(path, "/bm/config.txt") == 0 ? 0 : -1;
}
int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    (void)e;
    const char *src = on_bridge ? bfile : file;
    const size_t n = on_bridge ? strlen(bfile) : file_len;
    *data = malloc(n + 1);
    memcpy(*data, src, n);
    *len = n;
    return 0;
}
int fat_mkdirs(const char *path) { (void)path; return 0; }
int fat_write_file(const char *dir, const char *name, const void *data, size_t len)
{
    if (strcmp(dir, "/bm") == 0 && strcmp(name, "bridge.txt") == 0) {
        memcpy(bfile, data, len);
        bfile[len] = 0;
        has_bridge = 1;
        return 0;
    }
    check(strcmp(dir, "/bm") == 0 && strcmp(name, "config.txt") == 0, "written as /bm/config.txt");
    memcpy(file, data, len);
    file[len] = 0;
    file_len = len;
    has_file = 1;
    writes++;
    return 0;
}
const char *fat_error(void) { return "test"; }
/* src/fs/sdlock.c is linked in (config_save writes through it); it asks if a fiber runs */
fiber_t *fiber_current(void) { return NULL; }

static void put_file(const char *text)
{
    snprintf(file, sizeof file, "%s", text);
    file_len = strlen(file);
    has_file = 1;
}

static int merge(const char *lines, char *out, size_t out_len)
{
    return config_merge(lines, strlen(lines), out, out_len);
}

int main(void)
{
    char fine[94] = "github_pat_", text[1024], out[2048];       /* "github_pat_" and 82 more */
    for (int i = 11; i < 93; i++)
        fine[i] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_"[i % 63];
    fine[93] = 0;

    /* the file read */
    snprintf(text, sizeof text, "# mine\r\nwifi_ssid=Casa \r\ngithub_token=%s\nvolume=4\nlayout=us\nnot a line\n=x\n", fine);
    put_file(text);
    config_load();
    check(config_get("wifi_ssid") && strcmp(config_get("wifi_ssid"), "Casa") == 0, "a key read, the \\r and spaces off");
    check(config_get("github_token") && strcmp(config_get("github_token"), fine) == 0,
          "a token of 93 characters kept whole");
    check(volume == 4 && strcmp(layout, "us") == 0, "volume and layout applied");
    check(!config_get("not a line") && !config_get(""), "lines without a key skipped");

    /* the settings from the PC */
    writes = 0;
    int r = merge("wifi_psk=s3cret pass\nvolume=9\nlayout=it\nreport_upload=0\n", out, sizeof out);
    check(r == 4, "four keys changed");
    check(writes == 1, "  the file written once");
    check(volume == 9 && strcmp(layout, "it") == 0, "  volume and layout applied at once");
    check(strstr(file, "volume=9\n") && strstr(file, "layout=it\n") && strstr(file, "report_upload=0\n") &&
          strstr(file, "wifi_psk=s3cret pass\n") && strstr(file, fine), "  in the file, with what was there");
    check(strstr(out, "wifi_ssid=Casa\n") && strstr(out, "report_upload=0\n"), "  the settings sent back");
    check(strstr(out, "wifi_psk=(hidden, 11 characters)\n") && strstr(out, "github_token=(hidden, 93 characters)\n") &&
          !strstr(out, "s3cret") && !strstr(out, "github_pat"), "  the secrets hidden");

    writes = 0;
    r = merge("volume=9\n", out, sizeof out);
    check(r == 0 && writes == 0, "the same value: nothing written");

    r = merge("report_upload=\r\n# a comment\n\nmissing=\n", out, sizeof out);
    check(r == 1 && !config_get("report_upload") && !strstr(file, "report_upload"), "\"key=\" removes the key");
    check(!strstr(out, "report_upload"), "  not in the settings sent back");

    /* a wrong line: nothing changes */
    static const char *const bad[] = {
        "volume=3\nno equals sign\n", "volume=3\nbad key=1\n", "volume=3\n=1\n",
        "volume=3\nkey_far_too_long_for_it_x=1\n", "volume=3\nx=a\tb\n",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        writes = 0;
        r = merge(bad[i], out, sizeof out);
        check(r == -1 && writes == 0 && volume == 9, "a wrong line: -1, nothing changed");
    }
    char longv[200];
    memset(longv, 'a', sizeof longv);
    memcpy(longv, "name=", 5);
    longv[5 + 128] = 0;
    check(merge(longv, out, sizeof out) == -1 && !config_get("name"), "a value of 128 characters refused");
    longv[5 + 127] = 0;
    check(merge(longv, out, sizeof out) == 1 && strlen(config_get("name")) == 127, "one of 127 taken");

    /* the file read back gives the same settings */
    char before[2048];
    config_list(before, sizeof before);
    config_load();
    config_list(out, sizeof out);
    check(strcmp(before, out) == 0, "the file read back: the same settings");

    /* the list cut at a whole line */
    size_t n = config_list(out, 40);
    check(n < 40 && n > 0 && out[n - 1] == '\n' && strlen(out) == n, "a short buffer: whole lines");

    /* as many keys as there is room for, then no more */
    for (int i = 0; i < 70; i++) {
        snprintf(text, sizeof text, "key%d=%d\n", i, i);
        merge(text, out, sizeof out);
    }
    check(config_get("key40") && !config_get("key69") && config_get("wifi_ssid"),
          "64 keys at most, the old ones kept");

    check(config_secret("github_token") && config_secret("wifi_psk") && config_secret("net_password") &&
          config_secret("meshy_key") && !config_secret("wifi_ssid") && !config_secret("key"),
          "secrets by their names");

    /* the dev kit's overlay: off, top, bottom; the old pages (simple,
     * detailed, functions) are the top */
    check(config_perf("off") == 0 && config_perf("0") == 0 && config_perf("") == 0 && config_perf("top") == 1 &&
          config_perf("bottom") == 2, "perf: off, top, bottom");
    check(config_perf("1") == 1 && config_perf("2") == 1 && config_perf("3") == 1, "perf: the old 1, 2, 3 are the top");
    put_file("perf=2\n");
    config_load();
    check(perf == 1, "  perf=2 (detailed) read as the top");
    perf = 2;
    config_save();
    check(strstr(file, "perf=bottom\n") != NULL, "  saved as a word");

    /* the bridge gate: bm/bridge.txt, off by default, never set by the PC */
    check(bridge_mode() == BRIDGE_OFF && !dev_mode(), "bridge: off with no file");
    has_bridge = 1;
    strcpy(bfile, "on\n");
    config_load();
    check(bridge_mode() == BRIDGE_ON && !dev_mode(), "  the file says on");
    strcpy(bfile, "fast\r\n");
    config_load();
    check(bridge_mode() == BRIDGE_FAST && dev_mode(), "  fast is the old dev mode");
    strcpy(bfile, "banana");
    config_load();
    check(bridge_mode() == BRIDGE_OFF, "  an unknown word is off");
    check(bridge_set(BRIDGE_FAST) == 0 && bridge_mode() == BRIDGE_FAST && strcmp(bfile, "fast\n") == 0,
          "  Settings sets fast and the file says it");
    check(bridge_set(7) == -1 && bridge_mode() == BRIDGE_FAST, "  a wrong mode changes nothing");
    char bout[256];
    const char *dev1 = "dev=1\n", *br = "volume=3\nBridge=fast\n";
    check(config_merge(dev1, strlen(dev1), bout, sizeof bout) == -3, "  the PC cannot set dev");
    check(config_merge(br, strlen(br), bout, sizeof bout) == -3 && volume != 3, "  nor bridge, and nothing else changes");
    check(config_get("dev") == NULL, "  and dev is not a key");
    check(bridge_file_name("bm/bridge.txt") && bridge_file_name("/BM/Bridge.TXT") && bridge_file_name("bm/../bm/bridge.txt") &&
          bridge_file_name("bm33/bridge.txt") && bridge_file_name("bridge.txt"), "  the file name is refused in any spelling");
    check(!bridge_file_name("bm/config.txt") && !bridge_file_name("bm/bridge.bm") && !bridge_file_name("bm/mybridge.txt2"),
          "  other names pass");
    /* a kernel before the gate left dev=1 in config.txt: bridge fast, once, only without a bridge file */
    has_bridge = 0;
    strcpy(file, "volume=4\ndev=1\n");
    file_len = strlen(file);
    config_load();
    check(bridge_mode() == BRIDGE_FAST && has_bridge && strstr(bfile, "fast") != NULL, "  dev=1 of an older kernel becomes fast");
    check(strstr(file, "dev=") == NULL, "  and the key goes away");
    strcpy(bfile, "off\n");
    strcpy(file, "volume=4\ndev=1\n");
    file_len = strlen(file);
    config_load();
    check(bridge_mode() == BRIDGE_OFF && strstr(file, "dev=") == NULL, "  with a bridge file, dev=1 is only dropped");

    if (fails) {
        printf("config: %d of %d FAILED\n", fails, checks);
        return 1;
    }
    printf("config: all %d passed\n", checks);
    return 0;
}
