/*
 * Files from the PC over the network (M18.8), TCP port 3334, with the
 * console's password: a cartridge saved on the SD card, a cartridge
 * played at once, or a new kernel.img followed by a reboot.
 *
 * Request: "BM3X", op ('S' save, 'P' play, 'K' kernel), u8 length +
 * password, u8 length + path ("carts/pong.bm"; 8.3 names), u32 size,
 * u32 crc32 (little endian), the data. Answers, two letters each: after
 * the header OK, or PW (password), SZ (size), BH (bad header); after the
 * data OK, CE (crc), WE (SD write error).
 */
#ifndef NETXFER_H
#define NETXFER_H

#include <stddef.h>
#include <stdint.h>

#define NETXFER_PORT 3334

int  netxfer_start(void);
/* Runs a finished transfer (SD writes, reboot); from net_poll. */
void netxfer_poll(void);
/* A cartridge sent to be played: the data (the caller frees it), 1; else 0. */
int  netxfer_take_play(uint8_t **data, size_t *len);
/* 1 once per cartridge waiting to be played (input_key turns it into
 * INPUT_NET_PLAY for the monitor). */
int  netxfer_play_announce(void);
/* Counts the files saved so far (the menu re-reads the SD when it changes). */
unsigned netxfer_saves(void);

#endif
