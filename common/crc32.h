#ifndef SG_CRC32_H
#define SG_CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t crc32_calc(const void *data, size_t len);

#endif
