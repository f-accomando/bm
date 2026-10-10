/*
 * Files from the PC over the network (M18.8), TCP port 3334, with the
 * console's password: a cartridge saved on the SD card, a cartridge
 * played at once, or a new kernel followed by a reboot (kernel.img, on
 * the Pi Zero 2 W kernel7.img).
 *
 * Request: "BMXF", op ('S' save, 'P' play, 'K' kernel), u8 length +
 * password, u8 length + path ("carts/pong.bm"; 8.3 names), u32 size,
 * u32 crc32 (little endian), the data. Answers, two letters each: after
 * the header OK, or PW (password), SZ (size), BH (bad header), BY (busy:
 * a file still waits to be written); after the data OK, CE (crc), WE (SD
 * write error), KA (a kernel for another Pi), and for S QD (queued: the
 * file arrived whole and the console writes it from its menu, a game that
 * is running is replaced once it is closed; 2026-10-10). On the RGB30 a
 * .bm / .b16 for carts/ goes to bm/, the folder its menu lists.
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
/* A new kernel (2026-10-04): NETXFER_K_RECEIVING with the bytes in so far
 * and its size, then NETXFER_K_RESTART with the seconds left before the
 * restart (3, counted down; the kernel is on the SD card); 0: none. Any
 * pointer may be NULL. */
enum { NETXFER_K_RECEIVING = 1, NETXFER_K_RESTART };
int  netxfer_kernel_state(uint32_t *done, uint32_t *total, int *secs);

/* S, the files written from the menu (2026-10-10): netxfer_write_tick
 * writes the one waiting, a slice until `until` (timer_ticks), in a fiber;
 * 1 while one is still waiting or being written. Only from the menu, when
 * nothing else uses the SD card (the games never: what they run is
 * replaced once they are closed). netxfer_write_pause stops a write half
 * way (the old file stays as it was; it starts again at the next tick):
 * before an application runs or the menu writes the card itself. */
int  netxfer_write_tick(uint32_t until);
void netxfer_write_pause(void);
int  netxfer_write_pending(void);
/* Whether the file at path ("/carts/PONG.BM", any case) is arriving or
 * waiting to be written (the menu shows a tag and does not start it). */
enum { NETXFER_RECEIVING = 1, NETXFER_QUEUED, NETXFER_WRITING };
int  netxfer_updating(const char *path);
/* The file arriving or being written: its state (above; 0 none), its name
 * ("PONG.BM", n bytes at most), the bytes in so far and its size. */
int  netxfer_file_state(char *name, size_t n, uint32_t *done, uint32_t *total);

#endif
