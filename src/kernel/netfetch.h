/*
 * "net fetch" in the Monitor: a cartridge (.bm), a project (.bme), a map kit (.bmk) or a model file (.bmm) from the web
 * onto the SD card.
 * This part has no hardware: what to fetch and where it goes. src/kernel/monitor.c downloads, checks the
 * bytes as a cartridge (bm_parse_any) and writes; tests/kernel/test_netfetch.c tests this file on the PC.
 *
 *   net fetch https://host/path/name.bm [NAME] [replace]
 *   net fetch gh:OWNER/REPO/PATH[@REF] [NAME] [replace]     raw.githubusercontent.com, REF default HEAD
 *
 * A .bm goes to /bm, a .bme to /bme, a .bmk (a map kit) to /bmk, a .bmm (models) to /bmm. NAME (8.3) replaces the file's own name.
 */
#ifndef NETFETCH_H
#define NETFETCH_H

#include <stddef.h>

#define FETCH_MAX (64u << 20)            /* the largest file: 64 MB (Yharnam is 6 MB) */
#define FETCH_GH_HOST "raw.githubusercontent.com"
#define FETCH_KIT 1                      /* .bmk: a BMRES file of kind map kit */
#define FETCH_MODEL 2                    /* .bmm: a BMRES file of kind models */

typedef struct {
    char url[1100];
    char dir[8];                        /* "/bm", "/bme", "/bmk" or "/bmm" */
    char name[13];                      /* 8.3, lower case */
    char path[24];                      /* dir/name */
    int res;                            /* 0: a cartridge file (.bm, .bme); FETCH_KIT or FETCH_MODEL: a resource file */
    int github;                         /* the host is raw.githubusercontent.com: github_token may go with it */
} fetch_plan_t;

/* 0 and the plan, or -1 and the reason in err. src: an https address or gh:OWNER/REPO/PATH[@REF];
 * name: NULL for the last part of the address. */
int fetch_plan(const char *src, const char *name, fetch_plan_t *p, char *err, size_t en);

#endif
