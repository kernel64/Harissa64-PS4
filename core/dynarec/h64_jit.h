// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - MIPS -> PowerPC dynamic recompiler.
//
// Blocks of up to H64_JIT_MAX_INSNS MIPS instructions, inside one 4 KB page,
// ending after a branch and its delay slot. Instructions are emitted as
// native PowerPC code when the recompiler knows them, otherwise as a call to
// the reference interpreter for that one instruction. All MIPS state stays
// in H64Cpu (no register caching yet), so the interpreter and the
// recompiled code can hand over at any instruction.
//
// Timing is exactly the interpreter's: the dispatcher runs a block only when
// no scheduler event falls inside it (otherwise it steps the interpreter),
// and a block leaves as soon as something changes that (an exception, a pc
// other than the expected one, a new event before the block's end, a pending
// interrupt, or a store into the block's own page). h64test --lockstep runs
// the interpreter next to it and compares the whole CPU state after every
// block.
//
// Execution needs a PowerPC host: Xbox 360 (XDK ABI: plain function
// addresses, 32-bit pointers) or ppc64 big-endian Linux (ELFv1: function
// descriptors, TOC in r2), used under QEMU for tests. Elsewhere the
// recompiler compiles (and its encoder is unit-tested) but cannot run.
#ifndef H64_JIT_H
#define H64_JIT_H

#include "../common/h64_types.h"

#if defined(_XBOX)
#define H64_JIT_ABI_XBOX 1
#define H64_JIT_CAN_RUN 1
#elif defined(__powerpc64__) && defined(__BIG_ENDIAN__) && (!defined(_CALL_ELF) || _CALL_ELF == 1)
#define H64_JIT_ABI_ELFV1 1
#define H64_JIT_CAN_RUN 1
#elif defined(__x86_64__) || defined(_M_X64)
#define H64_JIT_ABI_X64 1
#define H64_JIT_CAN_RUN 1
#else
#define H64_JIT_CAN_RUN 0
#endif

#define H64_JIT_MAX_INSNS 64
#define H64_JIT_PAGES (0x800000u >> 12)
#define H64_JIT_CHUNK_SHIFT 4

struct H64System;
struct H64JitBlock;

struct H64JitStats
{
    u64 blocksRun, blocksCompiled, interpSteps, invalidations, flushes, earlyExits, idleSkipped;
    u64 nativeInsns, helperInsns;
    u64 helperCalls;
    u64 linksMade, tlbUnlinks;
    u64 tlbStale;
    u64 codeBytes, hotBytes;
};

struct H64JitLink
{
    u32 *patch;
    u32 orig;
    int next;
    int tlb;
    int target;
    int tlbEntry;
    int tlbPrev, tlbNext;
};

struct H64Jit
{
    u64 blockEndCycles;
    u64 runEnd;
    u32 *lastExit;
    u32 lastExitTarget;
    void *staleBlock;
    u32 curPage;
    int curInvalidated;
    u16 *codeMap;
    u32 *indPc;
    u32 **indBody;
    s8 *indEnt;

    u8 *mem;
    u32 memSize, memUsed;
    u32 coldBase, coldUsed;
    void (*flushIcache)(void *addr, u32 len);

    H64JitBlock *blocks;
    u32 blockCount, blockCap;
    H64JitBlock *hash[8192];
    H64JitBlock *pageHead[H64_JIT_PAGES];

    void (*enter)(H64System *sys, u32 *body);
    u32 *rtExit;
    u32 *rtCheck[2][4];
    u32 *rtFpCheck[2][2];
    u32 *rtFpFinish[3];
    u32 *rtSlow;
    u32 *rtIndirect;
    u32 *rtTlbStale;
    u32 *rtLink;
    u32 *rtUnlinked;
    u32 *rtDtlb[2][4];
    u32 *rtFcStore, *rtFcLoad;

    H64JitLink *links;
    u32 linkCount, linkCap;
    int noLink;
    int fullExits;
    int noStatusFast;
    int noSuper;

    H64JitStats stats;
    u32 *opHist;
    int noNative;
    int noFpu;
    int noRegCache;
    int noFpCache;
    int noFpuGuard;
    void *dumpFile;
    int prof;
    int fastFpu;

    struct { u32 key, ppage, gen, asid; int entry; } fetch[256];
    u32 linkTlbGen;
    u32 seenEntryGen[32], seenAsidGen;
    int tlbHead[32];
    u32 tlbLinkCount;
    int freeLink;
    u32 rtCpi;
};

int h64_jit_init(H64System *sys, void *execMem, u32 size, void (*flushIcache)(void *addr, u32 len));
void h64_jit_free(H64System *sys);
void h64_jit_reset(H64System *sys);
void h64_jit_run(H64System *sys, u64 cycles);
void h64_jit_run_one(H64System *sys);
void h64_jit_invalidate(H64System *sys, u32 paddr, u32 len);
void h64_jit_log_invalidations(u32 count);

#endif
