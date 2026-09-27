/*
 * Settings kept on the SD card in /bm33/config.txt ("key=value" lines):
 * keyboard layout, .b33 drawing mode. Unknown keys are kept as they are.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* Reads the file (if any) and applies it; prints one line. */
void config_load(void);

/* Writes the current settings; prints an error if it cannot. */
void config_save(void);

/* Value of a key read from the file, or NULL. */
const char *config_get(const char *key);

#endif
