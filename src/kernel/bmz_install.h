/*
 * The .bmz files in /bmz (src/bm/bmz.h) extracted into the folder of their kind (/bm, /bme, /bmm, /bmk, ...).
 * A file that extracts well (size, CRC-32 and kind checked, written and found again on the card) is deleted from
 * /bmz. A file that fails stays, with its reason in the log. An existing file of the same name is replaced.
 */
#ifndef BMZ_INSTALL_H
#define BMZ_INSTALL_H

#include <stddef.h>

/* Extracts one .bmz ("/bmz/pong.bmz"). 0 on success; else a BMZ_E_* code, or -100 when the card or the memory
 * failed. `msg` says what happened (the path of the new file, or the reason). The .bmz is not deleted here. */
int bmz_install_file(const char *path, char *msg, size_t n);
/* Every .bmz in /bmz. Returns how many were extracted. `failed`, if not NULL, gets how many were not. */
int bmz_install_all(int *failed);

#endif
