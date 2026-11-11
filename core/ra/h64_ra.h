// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - what RetroAchievements (third_party/rcheevos) needs from the
// machine: the game's hash and its memory as the achievements address it.
#ifndef H64_RA_H
#define H64_RA_H

#include "../common/h64_types.h"

struct H64System;
struct H64Rom;

void h64_ra_rom_hash(const H64Rom *rom, char hash[33]);

u32 h64_ra_read(const H64System *sys, u32 address, u8 *buffer, u32 count);

#endif
