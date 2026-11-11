// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - renderer interface used by the core.
//
// The core produces two kinds of drawing work:
//  - raw RDP commands (1 to 22 big-endian 64-bit words, exactly as the RDP
//    receives them on its bus), from the LLE RSP or passed through by the
//    graphics HLE (state, texture loads, rectangles, syncs);
//  - triangles from the graphics HLE, as three screen-space vertices plus
//    the RDP state set by the preceding commands. They stand for the RDP
//    triangle command the real microcode would have built.
//
// Two renderers implement it: the software RDP (reference, render/soft),
// which turns HLE triangles into RDP triangle commands, and the Xenos
// renderer on the Xbox 360 (render/xenos), which draws them with the GPU.
// With no renderer installed the core uses the software RDP.
#ifndef H64_RENDER_API_H
#define H64_RENDER_API_H

#include "../core/common/h64_types.h"

struct H64RenderVertex
{
    float x, y;
    float z;
    float invw;
    float r, g, b, a;
    float s, t;
};

enum
{
    H64_TRI_ZBUFFER = 1,
    H64_TRI_TEXTURE = 2,
    H64_TRI_SHADE = 4
};

struct H64Renderer
{
    void *user;
    void (*rdp)(void *user, const u64 *words, u32 count);
    void (*triangle)(void *user, const H64RenderVertex *v0, const H64RenderVertex *v1, const H64RenderVertex *v2,
                     u32 flags, u32 tile, u32 levels);
    void (*reset)(void *user);
};

#endif
