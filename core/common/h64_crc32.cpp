// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
#include "h64_crc32.h"

u32 h64_crc32(u32 crc, const u8 *data, size_t size)
{
    size_t i;
    int bit;
    crc = ~crc;
    for (i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
