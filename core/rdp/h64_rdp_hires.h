// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Hi-res texture keys ("Rice" checksums, as GLideN64 computes them for
// community texture packs: NAME#TEXCRC#FMT#SIZE[#PALCRC]_*.png).
// Ported from GLideN64 (GPL v2): src/GLideNHQ/TxUtil.cpp (RiceCRC32,
// checksum64), src/Textures.cpp (_loadHiresTexture), src/gDP.cpp (load info).
#ifndef H64_RDP_HIRES_H
#define H64_RDP_HIRES_H

#include "../common/h64_types.h"

struct H64System;

struct H64HiresKey
{
    u32 texCrc, palCrc;
    u8 fmt, size;
    u8 hasPal;
    u16 width, height;
};

void h64_rdp_hires_enable(H64System *sys, int on);
int h64_rdp_hires_enabled(H64System *sys);
void h64_rdp_hires_on_load(H64System *sys, u32 tileIndex, u32 sl, u32 tl, u32 sh, u32 th, int mode);
void h64_rdp_hires_on_use(H64System *sys, u32 tileIndex);
u32 h64_rdp_hires_count(H64System *sys);
const H64HiresKey *h64_rdp_hires_key(H64System *sys, u32 i);
int h64_rdp_hires_compute(H64System *sys, u32 tileIndex, H64HiresKey *out);
u32 h64_rice_crc32(const u8 *src, int width, int height, int size, int rowStride);

#endif
