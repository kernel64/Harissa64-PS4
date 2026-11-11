// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - RDP (Reality Display Processor): command interface.
//
// DPC registers, command fetch from RDRAM or DMEM (XBUS), assembly of
// multi-word commands and the interrupt after SYNC_FULL. Each complete
// command goes to h64_rdp_command(), where the renderer state lives.
#ifndef H64_RDP_H
#define H64_RDP_H

#include "../common/h64_types.h"

struct H64System;

#define DPC_XBUS      0x001u
#define DPC_FREEZE    0x002u
#define DPC_FLUSH     0x004u
#define DPC_START_GCLK 0x008u
#define DPC_TMEM_BUSY 0x010u
#define DPC_PIPE_BUSY 0x020u
#define DPC_CMD_BUSY  0x040u
#define DPC_CBUF_READY 0x080u
#define DPC_DMA_BUSY  0x100u
#define DPC_END_VALID 0x200u
#define DPC_START_VALID 0x400u

struct H64RdpRegs
{
    u32 start, end, current, status;
    u32 clock, bufBusy, pipeBusy, tmemCounter;
    u32 hleSyncPending;
};

struct H64Rdp;

void h64_rdp_reset(H64System *sys);
void h64_rdp_free(H64System *sys);
u32 h64_dp_read(H64System *sys, u32 reg);
void h64_dp_write(H64System *sys, u32 reg, u32 value);
void h64_rdp_hle_full_sync(H64System *sys);

void h64_rdp_command(H64System *sys, const u64 *words, u32 count);
void h64_rdp_async_free(H64System *sys);

struct H64RenderVertex;
u32 h64_rdp_build_triangle(const H64RenderVertex *a, const H64RenderVertex *b, const H64RenderVertex *c, u32 flags,
                           u32 tile, u32 levels, u64 *out);
void h64_rdp_decode_triangle(const u32 *w, int persp, H64RenderVertex *v);
u32 h64_rdp_decode_polygon(const u32 *w, int persp, H64RenderVertex *v);

#endif
