/*
 * Data blobs of an installed app (the DATA section of its .bm, src/bm/bm.h). The Assistant app carries the
 * dictionaries of the word completion ("words.lua") and the knowledge file of the Assistant ("assist.bin"): the
 * shipped kernel (CLEAN_KERNEL=1) does not embed them.
 */
#ifndef APPDATA_H
#define APPDATA_H

#include <stdint.h>

/* The blob `blob` of the app `app` ("assistant") installed in /bm, or NULL when the app or the blob is not there.
 * The file stays in memory for the whole session: the pointer is valid until the power goes off. */
const uint8_t *appdata_get(const char *app, const char *blob, uint32_t *len);

#endif
