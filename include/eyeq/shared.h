#pragma once

#include <stdint.h>
#define EYEQ_CRC_INITIAL 0xffffffff

uint32_t eyeq_crc32(uint32_t crc, const uint8_t *data, int len);
