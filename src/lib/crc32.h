#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

/* IEEE 802.3 CRC-32, same result as Python's zlib.crc32(). */
uint32_t crc32(const void *data, uint32_t len);

#endif
