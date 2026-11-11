// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the test mode without Vulkan (see ps4_softtest.cpp).
#ifndef PS4_SOFTTEST_H
#define PS4_SOFTTEST_H

#include "../../core/common/h64_types.h"

void ps4_softtest_run(void *jitMem, u32 jitBytes);

#endif
