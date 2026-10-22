/*
 * file_write for cartridges (bm/web on the SD card), and the queue behind its async form
 * (multitasking stage 3). A write is sync (the call returns when the file is on the card) or
 * queued: the data is copied, a writer in a fiber puts it on the card in pieces (one run of
 * clusters, up to 32 KiB, per piece) while the cartridge goes on drawing. Pieces are driven by
 * webfile_pump, which the cartridge's own web_poll / file_state calls run (about 4 ms each).
 * One writer holds the card (sdlock.h): any other SD writer that begins pauses the queued one;
 * its job starts again from the first byte later. The rules (name, cap, 128 files) are in
 * webpolicy.h.
 */
#ifndef WEBFILE_H
#define WEBFILE_H

#include <stddef.h>
#include <stdint.h>

#define WF_JOBS 4

typedef enum { WF_NONE, WF_QUEUED, WF_WRITING, WF_DONE, WF_ERROR } wf_state_t;

/* name: 8.3 upper case (web_file_name). Sync write: 0, or -1 with the reason in err. */
int webfile_write(const char *name, const uint8_t *data, size_t len, size_t cap, char *err, size_t en);
/* Queues a write (a copy of data is kept). 0, or -1 with the reason (over the cap, queue full,
 * no memory). A job of the same name that waits is replaced; one that writes is stopped first. */
int webfile_queue(const char *name, const uint8_t *data, size_t len, size_t cap, char *err, size_t en);
/* The same, the caller gives the malloc'd buffer (no copy); it is freed on failure, too. */
int webfile_queue_own(const char *name, uint8_t *data, size_t len, size_t cap, char *err, size_t en);
/* Forgets the job of that name (stops it if it writes): its data is freed, no result stays. */
void webfile_forget(const char *name);
/* Works the queue for about budget_us. 0 if it could not run (nothing waits, or another writer
 * holds the card), else 1. */
int webfile_pump(uint32_t budget_us);
/* The state of the newest job of that name; for WF_ERROR the reason in err, for the others the
 * bytes on the card so far in *done (may be NULL). */
wf_state_t webfile_state(const char *name, char *err, size_t en, size_t *done);
/* Everything queued written now (the cartridge ends, a sync write must follow). */
void webfile_flush(void);
/* Flush, then every job and result forgotten (the cartridge ends). */
void webfile_reset(void);
/* Any job waiting or writing. */
int webfile_busy(void);

#endif
