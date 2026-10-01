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

/* The built-in editors: the SDK (menu, monitor 'e') and the Sound editor
 * (menu, monitor 'A'). */
void carts_editor(framebuffer_t *fb);
void carts_sound_editor(framebuffer_t *fb);

/* The built-in 3D studio: models and animations of a .bm (menu, monitor '3'). */
void carts_studio3d(framebuffer_t *fb);

/* bm Code, the code editor (Dev tab, monitor 'C'); `open`: a file, or NULL */
void carts_code(framebuffer_t *fb, const char *open);

/* A development tool built into the kernel and the games it tries with
 * cart_run(): the tool comes back after each game, with cart_arg(). */
void carts_tool_session(framebuffer_t *fb, const uint8_t *cart, size_t cart_len, const char *what,
                        const char *open);

#endif
