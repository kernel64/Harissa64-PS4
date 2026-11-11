// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 - per-game settings built into the emulator.
//
// A fix that changes how the emulator behaves (not a plain bug fix) is turned
// on only for the games it was made and checked for, so that it cannot change
// any other game. The table is keyed by the cartridge's game code without the
// region letter ("NKT" for every Mario Kart 64).
#ifndef H64_PROFILE_H
#define H64_PROFILE_H

#include "../common/h64_types.h"

struct H64System;

enum
{
    H64_GP_LATE_PARSE = 1,
    H64_GP_VIEWPORT_CLIP = 2,
    H64_GP_CPU_WRITTEN = 4,
    H64_GP_IPL3_BOOT = 8,
    H64_GP_OTHERMODE_INIT = 16,
    H64_GP_NO_COPY_BACK = 32,
    H64_GP_S2DEX1 = 64,
    H64_GP_COPY_BACK_FULL = 128,
    H64_GP_RDRAM_COPY = 256,
    H64_GP_COPY_BACK_WHOLE = 512,
    H64_GP_FRAME_TO_RDRAM = 1024,
    H64_GP_SMALL_TO_RDRAM = 2048,
    H64_GP_SLOT_BY_WIDTH = 4096,
    H64_GP_PI_ODD_CART = 8192,
    H64_GP_WS_BORDER = 16384,
    H64_GP_COPY_BACK_SAME_WIDTH = 32768,
    H64_GP_BOOT_TIME = 65536,
    H64_GP_ALPHA_REF0_PASS = 1 << 17,
    H64_GP_TALL_FRAME = 1 << 18,
    H64_GP_FPU_NO_TRAPS = 1 << 19,
    H64_GP_LAX_ALIGN64 = 1 << 22,
    H64_GP_RECT_POINT = 1 << 23,
    H64_GP_NO_OV_TRAP = 1 << 25,
    H64_GP_KEEP_SHOWN = 1 << 29,
    H64_GP_MASK_TO_TILE = 1u << 30,
    H64_GP_SHORT_GFX_TASK = 1 << 20,
    H64_GP_BIG_TEX_POOL = 1 << 24,
    H64_GP_GFX_STREAM = 1 << 21,
    H64_GP_NO_OLD_FRAME = 1 << 26,
    H64_GP_SOFT_8BIT = 1 << 28,
    H64_GP_DEPTH_FILL_RDRAM = 1 << 27,
    H64_GP_CPU_SPRITES = 1u << 31
};

u32 h64_game_profile(const char *gameCode, const char **name);

void h64_apply_game_profile(H64System *sys);

void h64_apply_profile_flags(H64System *sys, u32 flags);

#endif
