// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - recompiler internals shared by h64_jit.cpp and h64_jit_gen.cpp.
#ifndef H64_JIT_INTERNAL_H
#define H64_JIT_INTERNAL_H

#include "h64_jit.h"

struct H64System;
struct H64Cpu;

typedef void (*H64JitFn)(H64System *sys);

struct H64JitBlock
{
    u32 vpc, paddr;
    u32 insns;
    int valid;
    int kernel;
    int idle;
    u32 pollBase, pollSize;
    s32 pollOff;
    H64JitFn fn;
    u32 *body;
    int linkHead;
    u32 hotWords;
    u32 execCount;
    H64JitBlock *hashNext, *pageNext;
};

H64JitBlock *h64_jit_compile(H64System *sys, u32 pc, u32 paddr);
void h64_jit_emit_runtime(H64System *sys);
void h64_jit_code_map(H64Jit *j, const H64JitBlock *b, int delta);
int h64_jit_kernel_mode(const H64Cpu *cpu);
int h64_jit_should_exit(H64System *sys);

enum { JR_SYS = 31, JR_CPU = 30, JR_RDRAM = 29, JR_TMP = 28 };

#endif
