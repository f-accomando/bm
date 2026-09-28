/*
 * The graphical cartridge menu: every cartridge is a small 3D memory card
 * (the shape of a Sony Memory Stick Duo) with its cover printed on the
 * front and copper contacts on the back, on a carousel; a list of titles
 * on the right, details and keys at the bottom. 640x360 RGB565, drawn with
 * the .b33 software 3D (textured faces).
 */
#ifndef MENU3D_H
#define MENU3D_H

#include <stdint.h>

#include "drivers/fb.h"
#include "b33/gfx16.h"

typedef struct {
    const char *title;
    const char *author;
    const char *path;           /* file on the SD card, or the built-in name */
    const char *kind;           /* "b33" or "s32" */
    uint32_t size;              /* bytes */
    const g16_sheet_t *cover;   /* 128x80, or NULL for a plain label */
} menu_item_t;

/* Switches the screen to the menu mode; -1 if it cannot (the caller keeps
 * the text menu). */
int  menu3d_open(framebuffer_t *fb);
/* One frame: draws and shows it, waits for the next frame time. */
void menu3d_frame(framebuffer_t *fb, const menu_item_t *items, int n, int sel,
                  const char *header, const char *footer_note);
/* Back to the console mode. */
void menu3d_close(framebuffer_t *fb);

/* A cover for cartridges without one: the title on a coloured label. */
int  menu3d_make_cover(g16_sheet_t *s, const char *title, const char *kind);
/* A cover from RGBA8888 pixels (the .b33 COVER section). */
int  menu3d_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h);

#endif
