// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - VI output.
//
// h64_vi_render: the VI stage (fetch with coverage, anti-alias, dither and
// divot filters, scaling, gamma), producing what the console sends to the TV:
// 640 x 240 (288 PAL) per progressive frame, 640 x 480 (576) interlaced.
// h64_vi_capture: the raw framebuffer the VI points at, for debugging.
#ifndef H64_VI_H
#define H64_VI_H

#include "../common/h64_types.h"

struct H64System;

int h64_vi_render(H64System *sys, u8 *rgb, int maxW, int maxH, int *w, int *h);
int h64_vi_capture(H64System *sys, u8 *rgb, int maxW, int maxH, int *w, int *h);

#endif
