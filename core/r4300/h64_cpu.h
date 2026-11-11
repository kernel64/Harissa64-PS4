// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - NEC VR4300 (R4300i) CPU state and reference interpreter.
//
// Written from the VR4300 user manual and n64brew. The interpreter is the
// reference implementation: simple, one instruction at a time, every check
// done (address errors, overflow traps, TLB, interrupts before each
// instruction). The dynarec (M3) is compared against it in lockstep.
#ifndef H64_CPU_H
#define H64_CPU_H

#include "../common/h64_types.h"

struct H64System;

enum
{
    CP0_INDEX = 0, CP0_RANDOM = 1, CP0_ENTRYLO0 = 2, CP0_ENTRYLO1 = 3, CP0_CONTEXT = 4, CP0_PAGEMASK = 5,
    CP0_WIRED = 6, CP0_BADVADDR = 8, CP0_COUNT = 9, CP0_ENTRYHI = 10, CP0_COMPARE = 11, CP0_STATUS = 12,
    CP0_CAUSE = 13, CP0_EPC = 14, CP0_PRID = 15, CP0_CONFIG = 16, CP0_LLADDR = 17, CP0_WATCHLO = 18,
    CP0_WATCHHI = 19, CP0_XCONTEXT = 20, CP0_PARITYERROR = 26, CP0_CACHEERROR = 27, CP0_TAGLO = 28,
    CP0_TAGHI = 29, CP0_ERROREPC = 30
};

#define SR_IE  0x00000001u
#define SR_EXL 0x00000002u
#define SR_ERL 0x00000004u
#define SR_KSU 0x00000018u
#define SR_UX  0x00000020u
#define SR_SX  0x00000040u
#define SR_KX  0x00000080u
#define SR_IM  0x0000FF00u
#define SR_BEV 0x00400000u
#define SR_FR  0x04000000u
#define SR_CU0 0x10000000u
#define SR_CU1 0x20000000u

enum
{
    EXC_INT = 0, EXC_MOD = 1, EXC_TLBL = 2, EXC_TLBS = 3, EXC_ADEL = 4, EXC_ADES = 5, EXC_IBE = 6, EXC_DBE = 7,
    EXC_SYS = 8, EXC_BP = 9, EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12, EXC_TR = 13, EXC_FPE = 15, EXC_WATCH = 23
};

#define FCR31_RM_MASK  0x00000003u
#define FCR31_FLAGS    0x0000007Cu
#define FCR31_ENABLES  0x00000F80u
#define FCR31_CAUSE    0x0003F000u
#define FCR31_C        0x00800000u
#define FCR31_FS       0x01000000u
#define FCR31_WRITABLE 0x0183FFFFu

struct H64TlbEntry
{
    u32 pageMask;
    u64 entryHi;
    u32 entryLo0;
    u32 entryLo1;
};

struct H64Cpu
{
    u64 gpr[32];
    u64 hi, lo;

    u32 lastOp;
    u64 pc;
    u64 nextPc;
    int branchPending;

    u64 curPc;
    int curInDelaySlot;

    u64 cop0[32];
    u32 countOffset;
    u64 cop0Latch;
    u64 cop2Latch;
    int llbit;

    u64 fgr[32];
    u32 fcr31;
    int fpuNoTraps;
    int noOvTrap;
    int laxAlign64;
    double jitFpMin[2];
    u64 jitScratch;

    H64TlbEntry tlb[32];

    u64 cycles;
    u32 cpi;
    u64 instructions;
    int exceptionRaised;

    u32 pcHistory[32];
    u32 pcHistoryPos;
    u32 jumpFrom[16], jumpTo[16];
    u32 jumpPos;

    void (*excHook)(void *user, int code);
    u32 watchPc;
    u32 jumpLimit;
    void (*watchHook)(void *user);
    void (*jumpHook)(void *user, u32 from, u32 to);
    u32 nopRun;
    void (*nopHook)(void *user);
    void *excUser;
    u32 tlbGen;
    u32 tlbEntryGen[32];
    u32 asidGen;
    u32 jitPage[256];
    s8 jitPageEnt[256];
    u32 dtlbR[256];
    u32 dtlbW[256];
    u32 dtlbPhys[256];
    s8 dtlbEnt[256];
};

#define H64_JIT_PAGE_KEY(vpc, paddr) ((((vpc) >> 20) << 20) | ((((paddr) >> 12) & 0x7FFFFu) << 1) | 1u)

void h64_cpu_reset(H64Cpu *cpu);
void h64_cpu_step(H64System *sys);

u32 h64_cpu_count(const H64Cpu *cpu);
void h64_cpu_set_ip(H64System *sys, int bit, int on);
void h64_cpu_reschedule_compare(H64System *sys);

int h64_cpu_translate_debug(H64Cpu *cpu, u64 vaddr, u32 *paddr);
int h64_cpu_probe_fetch(const H64Cpu *cpu, u32 a, u32 *paddr, int *entry);
int h64_cpu_probe_entry(const H64Cpu *cpu, u32 a, int i, u32 *paddr);
struct H64System;
void h64_cpu_step_op(struct H64System *sys, u32 op);
struct H64System;
int h64_cpu_peek_op(struct H64System *sys, u32 *op);

#endif
