// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - big memory blocks (RDRAM, RDRAM snapshots).
//
// On the Xbox 360 they come in large pages: the CPU's TLB holds 1024
// entries, and an 8 MB block in 4 KB pages (2048 of them) missed it all the
// time (the graphics worker's texture loads cost ~200 cycles per 8 bytes).
// Elsewhere: 64 KB aligned blocks (the PS4's Vulkan imports RDRAM).
#ifndef H64_MEM_H
#define H64_MEM_H

#include "h64_types.h"

void *h64_big_alloc(size_t bytes);
void h64_big_free(void *p);

#endif
