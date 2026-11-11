// Harissa64 V2 - RSP (Reality Signal Processor), low-level emulation.
//
// Reference interpreter: scalar unit (a 32-bit MIPS subset), vector unit
// (32 registers of 8 x 16-bit elements, 48-bit accumulators, flags,
// reciprocal unit), SP registers and SP DMA. Ported from ares (ISC licence,
// ares/n64/rsp, commit a776c509, see THIRD_PARTY.md), scalar paths only.
//
// Timing: ares's pipeline model (ares/n64/rsp/rsp.cpp, rsp.hpp, decoder.cpp
// at commit 652d6537): one issue group per RCP cycle (62.5 MHz, 2 RCP cycles
// every 3 CPU cycles), a vector and a scalar instruction may issue together,
// and a group stalls when it reads a register a recent load wrote, stores
// right after a load, or sits in a taken branch's delay slot. A running RSP advances in slices driven by the scheduler (H64_EV_RSP every
// H64_RSP_SLICE CPU cycles) and catches up to the exact cycle whenever the
// CPU touches the RCP (h64_rsp_sync from the bus). Its effects on the CPU
// therefore don't depend on how CPU instructions are grouped (interpreter
// step by step, or dynarec blocks), which keeps the two in lockstep.
#ifndef H64_RSP_H
#define H64_RSP_H

#define H64_RSP_SLICE 512u

#include "../common/h64_types.h"

struct H64System;

struct H64SpDma
{
    u32 memAddr;
    u32 dramAddr;
    u32 length;
    u32 count;
    u32 skip;
    int toRdram;
};

struct H64RspOp
{
    u32 rUse, rDef;
    u32 vUse, vDef;
    u32 vfake;
    u16 flags;
    u8 vcUse, vcDef;
};

struct H64RspStage
{
    u32 rWrite, vWrite;
    u32 load;
};

struct H64Rsp
{
    u32 r[32];
    u32 pc, nextPc;
    u16 vr[32][8];
    u16 acch[8], accm[8], accl[8];
    u16 vcoh[8], vcol[8], vcch[8], vccl[8], vce[8];
    s16 divin, divout;
    int divdp;
    u16 reciprocals[512];
    u16 inverseSquareRoots[512];

    u32 status;
    u32 semaphore;
    H64SpDma pending, current;
    int dmaFull, dmaBusy;

    s32 cycleFrac;
    H64RspStage stage[3];
    int singleIssue;
    int delaySlot;
    int branchTaken;
    u32 delayTarget;
    u64 stalls, dualIssues;
    u64 syncedCycles;
    int inSync;
    u64 instructions;
    u32 tasks;
    int hleBusy;
    int hleWait;
    int hleExtended;
    u32 hleStatus;
    int hleDpInterrupt;
    u32 hleTasks;
    u64 hleStart;
    u64 clocks;
    int gfxMeasure;
    u64 gfxClocks0;
    u32 gfxVerts, gfxTris, gfxCommands;
};

struct H64RspDecode
{
    u32 word[1024];
    H64RspOp info[1024];
};

void h64_rsp_reset(H64System *sys);
void h64_rsp_advance(H64System *sys, u32 cpuCycles);
void h64_rsp_sync(H64System *sys);
void h64_rsp_slice_event(H64System *sys);
void h64_rsp_step(H64System *sys);
u32 h64_rsp_issue(H64System *sys);

u32 h64_sp_read(H64System *sys, u32 reg);
void h64_sp_write(H64System *sys, u32 reg, u32 value);
u32 h64_sp_pc_read(H64System *sys);
void h64_sp_pc_write(H64System *sys, u32 value);
void h64_sp_dma_event(H64System *sys);

void h64_rsp_vu_init_tables(H64Rsp *rsp);
void h64_rsp_cop2(H64System *sys, u32 op);
void h64_rsp_lwc2(H64System *sys, u32 op);
void h64_rsp_swc2(H64System *sys, u32 op);

#endif
