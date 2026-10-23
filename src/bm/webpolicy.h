/*
 * The rules of the web API for cartridges (web_get, image_load, file_write): which
 * addresses a cartridge may fetch, which file names it may write, and the pause
 * between two requests to one host. Plain C, no kernel calls: the kernel and the
 * PC tests use the same code. The request itself is in webget.c.
 */
#ifndef WEBPOLICY_H
#define WEBPOLICY_H

#include <stddef.h>
#include <stdint.h>

#define WEB_URL_MAX         1024        /* the longest address a cartridge may ask for */
#define WEB_MAX_REDIRECTS   3
#define WEB_MIN_GAP_US      1000000u    /* between two requests to one host */
#define WEB_MAX_KIB_DEFAULT 4096u       /* body cap; config key web_max_kib */
#define WEB_MAX_KIB_LIMIT   32768u
#define FILE_MAX_KIB_DEFAULT 8192u      /* file_write cap; config key file_write_max_kib */
#define FILE_MAX_KIB_LIMIT  32768u
#define FILE_MAX_FILES      128         /* files in the folder */
#define WEB_FILES_DIR       "/bm/web"            /* not "downloads": the FAT writer makes 8.3 names only */

/* 0 if url is an address a cartridge may fetch: "https://host[:port]/path?query", the
 * host letters, digits, "." and "-", no user name, no space, no "#fragment" and at most
 * WEB_URL_MAX characters. The host goes lower case into host (hn bytes). Else -1 and the
 * reason in err. */
int web_url_check(const char *url, char *host, size_t hn, char *err, size_t en);

/* 0 if path names a file a cartridge may write: a bare 8.3 name ("model.glb"), or the
 * same after "web/", "bm/web/" or "/bm/web/". No "..", no other folder,
 * no backslash, no leading dot, and not a .bm, .bme or .b16 file. out gets the name in
 * lower case (13 bytes). Else -1 and the reason in err. */
int web_file_name(const char *path, char out[13], char *err, size_t en);

/* The pause between two requests to one host. now_us is a free-running microsecond
 * counter. web_gap_left_us: how long to wait before a request to host (0: go now).
 * web_gap_mark: a request to host starts at now_us. */
uint32_t web_gap_left_us(const char *host, uint32_t now_us);
void web_gap_mark(const char *host, uint32_t now_us);
void web_gap_reset(void);

#ifdef BM_HOST_TEST
/* Tests only: also accept http:// (the PC tests have no TLS server for the web_get run). */
void web_policy_allow_http(int on);
#endif

/* A byte cap from a config value in KiB (NULL or not a number: def), kept between 1 and limit. */
size_t web_cap_bytes(const char *kib_text, unsigned def, unsigned limit);

#endif
