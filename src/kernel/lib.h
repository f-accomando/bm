/*
 * The Lib tab (docs/RISORSE.md): the resources on the SD card in groups
 * (models, images, sounds, maps, palettes, kits), from the resource files
 * of /bm/lib and from the cartridges (/carts, the root, the sound packs
 * of /bm/sounds). The list keeps names and a few numbers; the file of the
 * selected resource is read again when it is shown (lib_open).
 */
#ifndef LIB_H
#define LIB_H

#include <stdint.h>

#include "bm/bm.h"
#include "bm/gfx16.h"

enum { LIB_MODELS, LIB_IMAGES, LIB_SOUNDS, LIB_MAPS, LIB_PALETTES, LIB_KITS, LIB_GROUPS };

/* what an item is in its file */
enum { LIB_W_MODEL, LIB_W_ZONE, LIB_W_SHEET, LIB_W_SONG, LIB_W_SFX, LIB_W_SOUND, LIB_W_MAP, LIB_W_PALETTE,
       LIB_W_KIT };

typedef struct {
    char path[64];              /* on the SD card */
    char file[16];              /* its name, as the list shows it */
    char title[49];             /* from the header (the file name if none) */
    char author[33];
    uint8_t kind;               /* BM_RES_CART, or the kind of a resource file */
} lib_source_t;

typedef struct {
    char name[20];
    char tag[12];               /* on the right: "A" animated, "S"/"E"/"I" song/effect/instrument, "64x32"... */
    uint16_t source;
    uint8_t group, what;
    uint16_t index;             /* the model, zone, song... number in its file */
    uint16_t n[3];              /* model: vertices, faces, clips; zone: w, h, frames; sheet, map: w, h;
                                   palette: colours; song: positions, bpm; effect: steps */
} lib_item_t;

/* the group names, as the tab shows them */
extern const char *const lib_groups[LIB_GROUPS];

/* The work of the tab in the menu's free time (2026-10-04: in a fiber, the
 * menu never waits): the list is read when it is out of date, then the
 * file of the item the preview waits for, a slice at a time until `until`. */
void lib_tick(uint32_t until);
/* Stops that work now (it goes on later): before the list changes, an
 * application runs, the card is written. */
void lib_job_stop(void);
/* The list is out of date: lib_ready() says so until lib_tick() has read it. */
void lib_invalidate(void);
int lib_ready(void);
/* While it is read: the files read and those to read. */
void lib_progress(int *done, int *total);

int lib_items(void);
const lib_item_t *lib_item(int i);
const lib_source_t *lib_source(int s);
int lib_sources(void);

/* The file of a source, read and parsed (kept until another is opened):
 * NULL if it cannot be read. Outside the tab's fiber it stops it first. */
const bm_cart_t *lib_open(int source);
/* The same only if it is open already (never reads). */
const bm_cart_t *lib_peek(int source);

/* The lines under the preview of an item: its name, its numbers, where it
 * is; with_info also author and licence, and tags (INFO of the part, else
 * of the file) if its file is open already. */
void lib_details(const lib_item_t *it, char lines[5][48], int with_info);

/* The INFO type of an item ("model", "sprite", "song"...). */
const char *lib_info_type(const lib_item_t *it);

/* libview.c: the preview of an item in the box (menu_lib_t.preview; ctx is
 * the item). What it needs is made by lib_tick (lib_view_work, in the
 * fiber) when the item changes; until then a message. */
void lib_preview(g16_t *g, int x, int y, int w, int h, void *ctx);
/* 1 once the preview of the item is made (or cannot be: nothing to wait for) */
int lib_preview_ready(const lib_item_t *it);
int lib_view_pending(void);
void lib_view_work(void);
/* Y on an item: "Play" or "Stop" for the sounds (not while a game is
 * suspended: its bank waits in the player), else NULL. */
const char *lib_play_label(const lib_item_t *it);
void lib_play(const lib_item_t *it);
void lib_stop(void);
/* forgets what the preview made (the list is read again) */
void lib_view_reset(void);

#endif
