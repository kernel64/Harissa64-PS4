// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - cartridge ROM image.
//
// Loads a ROM from memory in any of the three common dump orders and keeps it
// in N64 (big-endian, ".z64") order. Parses the header and identifies the CIC
// chip from the CRC-32 of the IPL3 boot code (ROM offsets 0x40-0xFFF).
#ifndef H64_ROM_H
#define H64_ROM_H

#include "../common/h64_types.h"

enum H64Cic
{
    H64_CIC_UNKNOWN = 0,
    H64_CIC_6101,
    H64_CIC_6102,
    H64_CIC_6103,
    H64_CIC_6105,
    H64_CIC_6106,
    H64_CIC_7102,
    H64_CIC_8303
};

enum H64RomOrder
{
    H64_ROM_ORDER_Z64 = 0,
    H64_ROM_ORDER_V64 = 1,
    H64_ROM_ORDER_N64 = 2
};

struct H64Rom
{
    u8 *data;
    u32 size;
    int sourceOrder;
    u32 ipl3Crc;
    int cic;
    u32 cicSeed;
    u32 entryPoint;
    u32 crc1, crc2;
    char name[21];
    char gameCode[5];
    u8 region;
    u8 version;
};

int h64_rom_load(H64Rom *rom, const u8 *file, u32 size);
void h64_rom_free(H64Rom *rom);

const char *h64_cic_name(int cic);

int h64_rom_tv_type(const H64Rom *rom);

#endif
