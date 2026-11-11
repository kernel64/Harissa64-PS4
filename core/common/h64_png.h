// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - PNG decoding and image downscaling, for the box art shown by
// the ROM browser (the pictures come from the network or the user's files:
// every size and offset is checked, a bad file is only refused).
//
// Supports every PNG colour type (grey, RGB, palette, grey + alpha, RGBA), bit
// depths 1 to 16, tRNS transparency and Adam7 interlacing. Ancillary chunks
// (gamma, colour profiles, text) are ignored. Inflate is zlib's.
#ifndef H64_PNG_H
#define H64_PNG_H

#include "h64_types.h"

u8 *h64_png_decode(const u8 *data, u32 size, int *w, int *h, u32 maxPixels);

void h64_image_fit(int w, int h, int maxW, int maxH, int *outW, int *outH);

u8 *h64_image_scale(const u8 *src, int sw, int sh, int dw, int dh);

#endif
