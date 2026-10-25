/*
 * The game library: the cartridges of the SD card, as the menu of the Pi
 * (carts.c) and the menu of the RGB30 (src/rgb30/ui.c) list them. One model
 * and one set of functions for both. What differs between the two consoles
 * stays behind the hooks of lib_backend_t.
 */
#ifndef LIBRARY_H
#define LIBRARY_H

/* Compare two titles, ignoring the case of ASCII letters (sort order of the menus). */
int lib_text_cmp(const char *a, const char *b);

/* 1 if the two texts are the same, ignoring the case of ASCII letters. */
int lib_same_text(const char *a, const char *b);

#endif
