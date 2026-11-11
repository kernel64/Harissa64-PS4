// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - graphics task HLE: runs the display list of a graphics
// OSTask in C++ (the job of the F3D/F3DEX/F3DEX2 microcodes on the RSP) and
// sends the result to the renderer (render/api.h): RDP commands, and
// triangles as screen-space vertices.
//
// Modelled on GLideN64's gSP and microcode handlers (GPL v2, studied and
// followed, see THIRD_PARTY.md); the output side is V2's own: everything is
// expressed as RDP state and triangles, so the software RDP draws HLE
// output too and the two RSP paths can be compared image by image.
//
// A task whose microcode is not recognised, or which switches to one
// (G_LOAD_UCODE), is left to the LLE RSP. RDP output is buffered during the
// task and only sent when the whole display list ran, so a task can still
// fall back to LLE half-way.
#ifndef H64_GFX_H
#define H64_GFX_H

#include "../common/h64_types.h"

struct H64System;
struct H64Gfx;

H64Gfx *h64_gfx_create(H64System *sys);
void h64_gfx_free(H64Gfx *gfx);

int h64_gfx_run_task(H64System *sys, H64Gfx *gfx, int *fullSync);
int h64_gfx_task_sync(H64System *sys, H64Gfx *gfx);
int h64_gfx_waiting(const H64Gfx *gfx);
int h64_gfx_resume(H64System *sys, H64Gfx *gfx, int *fullSync);
int h64_gfx_stream_waiting(const H64Gfx *gfx);
int h64_gfx_stream_resume(H64System *sys, H64Gfx *gfx, int *fullSync);
int h64_gfx_measure(H64System *sys, H64Gfx *gfx, u32 *verts, u32 *tris, u32 *commands);
u32 h64_gfx_cost(const H64Gfx *gfx);
struct H64GfxOut;
H64GfxOut *h64_gfx_out_create(void);
void h64_gfx_out_free(H64GfxOut *out);
int h64_gfx_parse_task(H64System *sys, H64Gfx *gfx, H64GfxOut *out, int *fullSync, int *mustSync);
int h64_gfx_package(H64System *sys, H64Gfx *gfx, H64GfxOut *out);
void h64_gfx_set_no_flush(H64Gfx *gfx, int on);
void h64_gfx_render(H64System *sys, H64Gfx *gfx, H64GfxOut *out);

int h64_gfx_task_known(H64System *sys, H64Gfx *gfx);

#endif
