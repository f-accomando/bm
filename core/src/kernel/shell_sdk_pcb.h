/*
 * Shell SDK, Project tab: the pictures. The PCB of a cartridge (ROM, mapper or coprocessor, save RAM,
 * battery), the SD card of a bm project and the small icons of the three platforms. A picture is a
 * box of console cells: the art is drawn in pixels on the framebuffer, the labels are console text.
 * Draw the pictures after the text of the screen: the text clears the cells it writes.
 */
#ifndef SHELL_SDK_PCB_H
#define SHELL_SDK_PCB_H

#include "shell_sdk_proj.h"

/* The PCB of the project in a box of w x h cells at ANSI (col, row): h of 8 or more, w of 30 or more. */
void sdk_pcb_draw(int target, const sdk_pcb_t *pcb, int col, int row, int w, int h);

/* A platform tile of w x h cells (13 x 4): the cartridge or the card, the CPU chip and the names. */
void sdk_tile_draw(int target, int selected, int col, int row, int w, int h);

#endif /* SHELL_SDK_PCB_H */
