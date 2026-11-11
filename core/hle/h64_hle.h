// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - RSP task HLE (high-level emulation of RSP microcode tasks).
//
// When the CPU starts the RSP on an OSTask, the HLE recognises the
// microcode and runs the whole task at once in C++; anything it does not
// recognise is left to the LLE RSP interpreter, so HLE is always optional.
// For now only audio tasks are recognised (alist ABIs and MusyX, port of
// mupen64plus-rsp-hle, see h64_hle_internal.h).
//
// Timing: the task's effects (RDRAM writes) happen when it starts, and the
// RSP stays busy (SP_STATUS halt clear) for a fixed time, after which it
// halts, sets its break/task-done bits and raises its interrupt like the
// real microcode. The time is an approximation (H64_HLE_AUDIO_CYCLES).
#ifndef H64_HLE_H
#define H64_HLE_H

#include "../common/h64_types.h"

struct H64System;
struct hle_t;

#define H64_HLE_AUDIO_CYCLES 20000u
#define H64_HLE_GFX_CYCLES 100000u
#define H64_HLE_STREAM_POLL 4000u

struct hle_t *h64_hle_create(H64System *sys);
void h64_hle_free(struct hle_t *hle);
void h64_hle_rdram_moved(H64System *sys);

int h64_hle_try_task(H64System *sys, u32 *statusBits, u32 *busyCycles, int *dpInterrupt);
int h64_hle_resume(H64System *sys, int *dpInterrupt);
int h64_hle_stream_poll(H64System *sys, u32 *busyCycles, int *dpInterrupt);
int h64_hle_gfx_measure(H64System *sys, u32 *verts, u32 *tris, u32 *commands);
u32 h64_hle_gfx_cost(H64System *sys);

void h64_hle_async_wait(H64System *sys);
int h64_hle_idle(H64System *sys);
int h64_hle_gfx_draw_due(H64System *sys, u64 *at);
void h64_hle_gfx_draw_pending(H64System *sys);
int h64_hle_async_finish(H64System *sys, int *ran, int *fullSync, u32 *statusBits);

void h64_hle_check_end(H64System *sys);

#endif
