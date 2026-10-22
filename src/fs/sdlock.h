/*
 * The SD writer lock (multitasking stage 3, D1 of docs/design/multitasking-alternatives.md).
 * One writer holds the card at a time. A writer that gives the CPU back half way (a fiber, a
 * queued write) registers a pause hook: a second writer that begins calls the hook first, and
 * the first one stops cleanly (its write starts again later, from the start). A writer with no
 * hook cannot be paused: the second one gets -1 and tries again later.
 */
#ifndef SDLOCK_H
#define SDLOCK_H

#include <stddef.h>

/* Takes the card for `who` (a name; the same text ends it). `pause` is called, if another writer holds
 * the card and has a hook, to stop that writer; NULL: this writer cannot be paused.
 * Returns 0 (taken), or -1 if the holder cannot be paused. Call it again from the same
 * holder: -1, too (end first). */
int sd_writer_begin(const char *who, void (*pause)(void));
/* Gives the card back, if `who` still holds it (a writer that was paused finds the card taken by
 * the one that paused it: its end changes nothing). */
void sd_writer_end(const char *who);
/* The name of the holder, or NULL. */
const char *sd_writer_who(void);

/* fat_mkdirs(dir) and fat_write_file(dir, name, ...) as the writer `who`: a pausable writer that
 * holds the card is stopped first. If a writer that cannot be paused holds it, the write goes on
 * anyway (the FAT keeps each write whole: fat.c store). 0, or -1 (fat_error()). */
int sd_write_file(const char *who, const char *dir, const char *name, const void *data, size_t len);

#endif
