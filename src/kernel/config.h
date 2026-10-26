/*
 * Settings kept on the SD card in /bm/config.txt ("key=value" lines):
 * keyboard layout, .bm drawing mode, volume. Unknown keys are kept as
 * they are.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "fs/fat.h"

/* Reads the file (if any) and applies it; prints one line. */
void config_load(void);

/* Writes the current settings; prints an error if it cannot. */
void config_save(void);

/* Sets a key (written by the next config_save). */
void config_set(const char *key, const char *value);

/* Removes a key (from the file at the next config_save). */
void config_unset(const char *key);

/* Value of a key read from the file, or NULL. */
const char *config_get(const char *key);

/* Lines "key=value" from the PC (easy_install.sh, bm_net.py --config):
 * each sets a key, "key=" removes it; the settings the console uses at
 * once (layout, draw, perf, volume) are applied and the file is written
 * if something changed. Nothing changes if a line is not "key=value"
 * (key: letters, digits, _; at most 23 and 127 characters): -1. Writes
 * into out the settings as they are now (config_list); returns how many
 * keys changed. */
int config_merge(const char *text, size_t len, char *out, size_t out_len);

/* The settings, a "key=value" a line, the secrets' values hidden;
 * returns the length written (what fits in len). */
size_t config_list(char *out, size_t len);

/* 1 if bm/config.txt has gpu3d_qpu=1 (default 0). Only then the kernel runs the QPU programs
 * (the fade hook at boot, the 3D Bench QPU row). */
int gpu_qpu_key(void);

/* A key whose value never goes out (..._token, _psk, _password, _key) */
int config_secret(const char *key);

/* perf= (the dev kit's overlay) as bm_set_perf takes it: "off" 0, "top" 1,
 * "bottom" 2; the old 1, 2, 3 (simple, detailed, functions) are the top */
int config_perf(const char *v);

/* The bridge gate (bm/bridge.txt: off, on or fast; config.c says who may write it).
 * dev_mode(): 1 when the bridge is fast (a kernel and a file are written at once). */
enum { BRIDGE_OFF, BRIDGE_ON, BRIDGE_FAST };
int bridge_load(void);                      /* config_load calls it; 1 if bm/bridge.txt was there */
int bridge_mode(void);
const char *bridge_label(int mode);         /* "off", "on", "fast" */
int bridge_set(int mode);                   /* Settings only: writes the file; 0, or -1 */
int bridge_file_name(const char *path);     /* 1 if a path names the bridge file */
int dev_mode(void);

/* screen recorder: 1 if rec=1 / rec=on in bm/config.txt */
int rec_mode(void);
void rec_set_mode(int on);

/* A file of the console on the SD card ("config.txt", "ca.pem",
 * "save/1a2b3c4d.sav"): in /bm, or in the folder of before the rename
 * until make install moves it. Returns 0 and fills e. */
int config_find_file(const char *name, fat_entry_t *e);

#endif
