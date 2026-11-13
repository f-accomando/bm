#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

/* IEEE 802.3 CRC-32, same result as Python's zlib.crc32(). */
uint32_t crc32(const void *data, uint32_t len);
/* The same in pieces: crc32_update(0, ...) then the result of the piece
 * before (as zlib.crc32(data, crc)). */
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len);

#endif
