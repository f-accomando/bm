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

/* A key whose value never goes out (..._token, _psk, _password, _key) */
int config_secret(const char *key);

/* A file of the console on the SD card ("config.txt", "ca.pem",
 * "save/1A2B3C4D.SAV"): in /bm, or in the folder of before the rename
 * until make install moves it. Returns 0 and fills e. */
int config_find_file(const char *name, fat_entry_t *e);

#endif
