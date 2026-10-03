/*
 * Cartridges: the built-in ones plus the .bm files found on the SD
 * card (root directory and /carts), and the on-screen menu to pick one.
 */
#ifndef CARTS_H
#define CARTS_H

#include <stddef.h>
#include <stdint.h>
#include "drivers/fb.h"

/* Initialises the SD card and mounts FAT; prints one line. */
void carts_init(void);

/* Number of cartridges found (built-in + SD) after carts_init. */
int carts_count(void);

/* Prints the list on the console. */
void carts_list(void);

/* Menu on the screen; returns when the user quits it (Esc / q). */
void carts_menu(framebuffer_t *fb);

/* Plays a cartridge image (a .bm, by its magic; anything else is refused). */
void carts_play_buffer(framebuffer_t *fb, const uint8_t *data, size_t len);

/* The tools built into the kernel; each returns the name of the last tool
 * used (a tool can open another on its file: cart_tool). The SDK (menu,
 * monitor 'e') and the Sound editor (menu, monitor 'A'): */
const char *carts_editor(framebuffer_t *fb);
const char *carts_sound_editor(framebuffer_t *fb);

/* bm Studio: the 3D models of a .bm, tiles and blocks (menu, monitor '3'). */
const char *carts_studio(framebuffer_t *fb);

/* bm Animator: the skeletons and animations of the models (menu, monitor '6'). */
const char *carts_animator(framebuffer_t *fb);

/* bm Mesh: the meshes of a .bm, its models and those its code builds
 * (menu, monitor '4'). */
const char *carts_mesh(framebuffer_t *fb);

/* bm Pixel: the pixel art of a .bm, its sprite sheet (menu, monitor '5'). */
const char *carts_pixel(framebuffer_t *fb);

/* bm Code, the code editor (Dev tab, monitor 'C'); `open`: a file, or NULL */
const char *carts_code(framebuffer_t *fb, const char *open);

/* A development tool built into the kernel and the games it tries with
 * cart_run(): the tool comes back after each game, with cart_arg(); with
 * cart_tool() another tool opens on the file. */
const char *carts_tool_session(framebuffer_t *fb, const uint8_t *cart, size_t cart_len, const char *what,
                               const char *open);

#endif
