/*
 * .bmz: a zlib-compressed copy of one .bm, .bme or resource file, for the transfer online (docs/BM-FORMAT.md,
 * design/compressed-sections.md). The .bmz files wait in /bmz. The console extracts each one into the folder of
 * its kind and the file on the SD card is the normal, raw one.
 *
 * Layout (little endian), 32 bytes of header and the zlib stream to the end of the file:
 *   0  4  magic "BMZ1"
 *   4  2  version, 1
 *   6  2  flags, 0 (reserved)
 *   8  4  size of the raw file
 *  12  4  CRC-32 of the raw file (as zlib.crc32)
 *  16 16  name of the raw file, lowercase 8.3, NUL ended ("pong.bm")
 */
#ifndef BMZ_H
#define BMZ_H

#include <stddef.h>
#include <stdint.h>

#define BMZ_HEAD     32
#define BMZ_MAX_RAW  (128u << 20)          /* 128 MB: above the 100 MB of the Market */
#define BMZ_DIR      "/bmz"

typedef struct {
    uint32_t raw_len, crc;
    char name[16];                          /* "pong.bm" */
    const char *dir;                        /* "/bm": where the raw file goes */
    const uint8_t *zdata;                   /* the zlib stream inside the file */
    size_t zlen;
} bmz_info_t;

enum {
    BMZ_OK = 0,
    BMZ_E_SHORT = -1,                       /* shorter than the header */
    BMZ_E_MAGIC = -2,                       /* not a .bmz */
    BMZ_E_VERSION = -3,                     /* a version this reader does not know, or flags set */
    BMZ_E_SIZE = -4,                        /* raw size zero or above BMZ_MAX_RAW */
    BMZ_E_NAME = -5,                        /* not a lowercase 8.3 name with a known extension */
    BMZ_E_INFLATE = -6,                     /* the stream is broken or has not the size in the header */
    BMZ_E_CRC = -7,                         /* the bytes do not match the CRC-32 */
    BMZ_E_KIND = -8,                        /* the raw file is not what its extension says */
};

/* The folder of an extension: ".bm" /bm, ".bme" /bme, ".bmm" /bmm, ".bmk" /bmk, ".bmi" /bmi, ".bms" /bms,
 * ".bmt" /bmt, ".bmc" /bmc. NULL for any other. */
const char *bmz_dir_of(const char *name);
/* Checks the header and fills `info` (it points into `file`). BMZ_OK or an error. */
int bmz_parse(const uint8_t *file, size_t len, bmz_info_t *info);
/* Inflates into `out` (info->raw_len bytes), checks the size, the CRC-32 and the magic of the raw file. */
int bmz_extract(const bmz_info_t *info, uint8_t *out);
/* A short text for an error code. */
const char *bmz_error(int code);

#endif
