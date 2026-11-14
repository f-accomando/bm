#ifndef KERNEL_ROMTABS_H
#define KERNEL_ROMTABS_H

/*
 * romtabs.c: the NES and SNES tabs of the menu (Pi and RGB30). The ROMs of /nes (.nes) and /snes (.sfc, .smc), a title from the file name,
 * and a box cover made in the style of the console (menu_make_box_cover). A ROM starts its emulator cartridge (bm NES, bm SNES) with the
 * ROM path as cart_arg().
 */
#include "bm/gfx16.h"

#include <stdint.h>

enum { ROM_NES, ROM_SNES, ROM_PLATFORMS };
#define ROMTABS_MAX 128                 /* ROMs listed in each tab */

typedef struct {
    char path[80];                      /* "/snes/Super Mario World.sfc" */
    char title[48];                     /* the file name without the extension and the tags in brackets */
    g16_sheet_t cover;                  /* made once (px NULL: not yet) */
} rom_entry_t;

/* Reads the folders again; the covers of the ROMs that stay are kept. */
void romtabs_scan(void);
int romtabs_count(int plat);
rom_entry_t *romtabs_get(int plat, int i);
/* Makes the covers not made yet, the selection first, until timer_ticks() reaches until (the menu's free time). */
void romtabs_covers_tick(int plat, int sel, uint32_t until);
/* The file name of the emulator cartridge of a tab: "nes.bm" or "snes.bm". */
const char *romtabs_emulator(int plat);
/* The text of the tab when it has no ROM. */
const char *romtabs_empty_hint(int plat);

#endif
