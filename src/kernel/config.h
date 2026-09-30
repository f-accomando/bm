/*
 * Settings kept on the SD card in /bm33/config.txt ("key=value" lines):
 * keyboard layout, .bm drawing mode. Unknown keys are kept as they are.
 */
#ifndef CONFIG_H
#define CONFIG_H

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

#endif
