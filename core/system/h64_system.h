// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - the whole machine: CPU, memories, RCP registers, PIF,
// cartridge and the scheduler that times them.
#ifndef H64_SYSTEM_H
#define H64_SYSTEM_H

#include "../common/h64_types.h"
#include "../cart/h64_rom.h"
#include "../cart/h64_save.h"
#include "../r4300/h64_cpu.h"
#include "../rdp/h64_rdp.h"
#include "../rsp/h64_rsp.h"
#include "../dynarec/h64_jit.h"
#include "../scheduler/h64_scheduler.h"

#define H64_RDRAM_SIZE 0x800000u

#define MI_INTR_SP 0x01u
#define MI_INTR_SI 0x02u
#define MI_INTR_AI 0x04u
#define MI_INTR_VI 0x08u
#define MI_INTR_PI 0x10u
#define MI_INTR_DP 0x20u

struct H64Pad { u16 buttons; s8 x, y; };

struct H64Mi { u32 mode, version, intr, mask; };
struct H64Vi { u32 regs[14]; u32 vIntr; u64 frameStart; u64 frameCycles; u32 frames; };
struct H64Ai
{
    u32 dramAddr, len, control, status, dacrate, bitrate; u32 fifoLen[2]; u32 fifoCount; u64 bufferCycles;
    u64 statQueued, statReads, statReadSum; u32 statBuffers;
};
struct H64Pi { u32 regs[13]; u32 latch; u64 latchUntil; u32 cartOdd; };
#define H64_RDRAM_MODULES 4
struct H64Ri
{
    u32 regs[8];
    u32 rdram[H64_RDRAM_MODULES][10];
    u32 corrupt;
};
struct H64System;
int h64_rdram_read_lost(H64System *sys, u32 paddr);
struct H64Si { u32 dramAddr, pifAddrRd, pifAddrWr, status; };

enum
{
    H64_PROF_GFX_HLE = 0,
    H64_PROF_AUDIO_HLE,
    H64_PROF_RENDER,
    H64_PROF_RSP_LLE,
    H64_PROF_JIT_COMPILE,
    H64_PROF_HELPER,
    H64_PROF_EVENTS,
    H64_PROF_ASYNC_WAIT,
    H64_PROF_BLOCKS,
    H64_PROF_TLB,
    H64_PROF_WAIT_TASK,
    H64_PROF_WAIT_DPC,
    H64_PROF_WAIT_START,
    H64_PROF_GFX_SNAPSHOT,
    H64_PROF_SNAP_BYTES,
    H64_PROF_GFX_VTX,
    H64_PROF_GFX_NVTX,
    H64_PROF_GFX_LIGHT,
    H64_PROF_GFX_NLIGHT,
    H64_PROF_TMEM_LOADS,
    H64_PROF_TMEM_WORDS,
    H64_PROF_TMEM_TLUT,
    H64_PROF_TMEM_NTLUT,
    H64_PROF_TMEM_TIME,
    H64_PROF_GFX_PARSE,
    H64_PROF_GFX_TRI,
    H64_PROF_GFX_NTRI,
    H64_PROF_COUNT
};

struct H64Options
{
    int hleBoot;
    int emux;
    int noRdpDraw;
    int hleAudio;
    int rdpStateOnly;
    int hleGfx;
    int gfxTiming;
    int gfxLateParse;
    int gfxViewportClip;
    int gfxKeepOffscreen;
    int gfxOtherModeInit;
    int gfxNoCopyBack;
    int gfxS2dex1;
    int gfxCopyBackFull;
    int gfxRdramCopy;
    int gfxCopyBackWhole;
    int gfxFrameToRdram;
    int gfxSmallToRdram;
    int gfxSlotByWidth;
    int gfxBigTexPool;
    int gfxCopyBackSameWidth;
    int gfxWsBorder;
    int piOddCart;
    int gfxShortTask;
    int gfxDeferDraw;
    int gfxNoOldFrame;
    int gfxSoft8bit;
    int gfxSoftTiny;
    int gfxModStHalf;
    int gfxXluNoBias;
    int gfxCimgFromRdram;
    int gfxDepthFillToRdram;
    int gfxCopyBackAlpha;
    int gfxAlphaDither;
    int gfxLodTile;
    int gfxDecalStencil;
    int gfxSkipZImage;
    int gfxViLineStart;
    int viIntrLine0;
    u32 gameCpi;
    u32 idleYieldPc;
    u32 idleRunQueue;
    u32 countWaitPc;
    u32 countWaitStep;
    u32 countWaitQueue;
    int gfxTallFrame;
    int gfxMaskToTile;
    int gfxKeepShown;
    int gfxRectPoint;
    int strictCpu;
    int gfxAlphaRef0Pass;
    int gfxStream;
    int gfxCpuWritten;
    int gfxCpuSprites;
    int gfxCostLog;
    int hleAudioCheck;
    u32 rdpLogFrame;
    u8 *rdram;
};

struct H64CpuSprite
{
    u32 addr, bytes;
    u8 *data;
};
#define H64_CPU_SPRITE_MAX (640 * 480 * 2)
#define H64_CPU_SPRITE_MARK 0x0002u

struct H64System
{
    H64Cpu cpu;
    H64Scheduler sched;
    H64Options options;

    u8 *rdram;
    u8 spMem[0x2000];
    u8 pifRam[64];
    H64Rom rom;
    H64SaveMem *save;

    H64Mi mi;
    H64Vi vi;
    H64Ai ai;
    H64Pi pi;
    H64Ri ri;
    H64Si si;
    H64Rsp rsp;
    H64RdpRegs dp;
    u64 dpCommand[22];
    u32 dpPendingWords;
    u64 dpCommands;
    struct H64DpcAsync *dpcAsync;
    H64Jit *jit;
    struct hle_t *hle;
    struct H64Renderer *renderer;
    u64 (*profClock)(void);
    u64 prof[H64_PROF_COUNT];
    struct H64RdpState *rdpState;
    u8 *rdramHidden;

    int tvType;
    H64Pad pad[4];
    u32 padMask;
    u32 rumblePaks;
    u8 rumbleMotor[4];
    void (*padHook)(H64System *sys);

    u8 isvBuffer[0x200];
    char isvLine[1024];
    int isvLineLen;
    void (*isvSink)(void *user, const char *line);
    void *isvUser;
    u32 (*asyncStart)(void *user, void (*job)(void *arg), void *arg);
    void (*asyncWait)(void *user);
    void (*asyncWaitTicket)(void *user, u32 ticket);
    void *asyncUser;
    u32 (*asyncParseStart)(void *user, void (*job)(void *arg), void *arg);
    void (*asyncParseWaitTicket)(void *user, u32 ticket, int fromWorker);
    u32 asyncGfxCycles;
    void (*asyncAudioStart)(void *user, void (*job)(void *arg), void *arg);
    void (*asyncAudioWait)(void *user);
    u32 asyncAudioCycles;
    void (*aiSink)(void *user, const u8 *samples, u32 len, u32 rate);
    void *aiUser;
    struct H64CheatList *cheats;
    void (*viHook)(H64System *sys);
    void *viUser;
    int cheatButton;

    int stop;
    int exitRequested;
    u32 miRaised[6];
    H64CpuSprite cpuSprite[4];
    u32 cpuSpriteNext;
    H64RspDecode rspDecode;
};

int h64_system_init(H64System *sys, const u8 *romFile, u32 romSize, const H64Options *opt);
void h64_system_free(H64System *sys);
void h64_system_reset(H64System *sys);
void h64_system_run_cycles(H64System *sys, u64 cycles);
void h64_system_step(H64System *sys);
void h64_system_state_hash(const H64System *sys, u32 *cpuHash, u32 *ramHash);

static inline void h64_jit_notify_write(H64System *sys, u32 paddr, u32 len)
{
    if (sys->jit && paddr < H64_RDRAM_SIZE && len &&
        (sys->jit->pageHead[paddr >> 12] || sys->jit->pageHead[((paddr + len - 1) & (H64_RDRAM_SIZE - 1)) >> 12] || len > 4096))
        h64_jit_invalidate(sys, paddr, len);
}

static inline u64 h64_prof_now(const H64System *sys) { return sys->profClock ? sys->profClock() : 0; }

static inline u64 h64_prof_nested(const H64System *sys)
{
    return (sys->asyncStart ? 0 : sys->prof[H64_PROF_GFX_HLE]) + (sys->asyncAudioStart ? 0 : sys->prof[H64_PROF_AUDIO_HLE]) +
           sys->prof[H64_PROF_RSP_LLE];
}

static inline void h64_prof_add_outer(H64System *sys, int slot, u64 t0, u64 nested)
{
    u64 spent = h64_prof_now(sys) - t0, inner = h64_prof_nested(sys) - nested;
    sys->prof[slot] += spent > inner ? spent - inner : 0;
}

void h64_mi_raise(H64System *sys, u32 bits);
void h64_mi_clear(H64System *sys, u32 bits);

int h64_bus_read32(H64System *sys, u32 paddr, u32 *value);
int h64_bus_write32(H64System *sys, u32 paddr, u32 value, u32 mask);
int h64_bus_read8(H64System *sys, u32 paddr, u8 *value);
int h64_bus_read16(H64System *sys, u32 paddr, u16 *value);
int h64_bus_read64(H64System *sys, u32 paddr, u64 *value);
int h64_bus_write8(H64System *sys, u32 paddr, u32 regValue);
int h64_bus_write16(H64System *sys, u32 paddr, u32 regValue);
int h64_bus_write64(H64System *sys, u32 paddr, u64 value);
void h64_debug_text(H64System *sys, const u8 *text, u32 len);

void h64_devices_reset(H64System *sys);
u32 h64_mmio_read(H64System *sys, u32 paddr);
void h64_mmio_write(H64System *sys, u32 paddr, u32 value, u32 mask);
void h64_device_event(H64System *sys, int ev);

void h64_pif_reset(H64System *sys);
void h64_pif_run_commands(H64System *sys);
void h64_pif_read_hook(H64System *sys);
void h64_pif_write_byte_hook(H64System *sys, u32 offset);
void h64_hle_boot(H64System *sys);

#endif
