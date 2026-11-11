// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - recompiler: block cache, dispatcher, code generation.
#include "h64_jit.h"
#include "h64_jit_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../common/h64_endian.h"
#include "../common/h64_log.h"
#include "../system/h64_system.h"
#include "../memory/h64_rdram.h"

#if defined(H64_JIT_ABI_X64)
#include <xmmintrin.h>
#define X64_FTZ_DAZ 0x8040u
#endif
#define JIT_LINK_TLB 1

static u32 hash_of(u32 pc) { return (pc >> 2) & 8191; }

void h64_jit_reset(H64System *sys)
{
    H64Jit *j = sys->jit;
    if (!j) return;
    memset(j->hash, 0, sizeof(j->hash));
    memset(j->pageHead, 0, sizeof(j->pageHead));
    memset(j->fetch, 0xFF, sizeof(j->fetch));
    j->tlbLinkCount = 0;
    memset(j->tlbHead, 0xFF, sizeof(j->tlbHead));
    memset(j->indPc, 0xFF, 1024 * sizeof(u32));
    memset(j->indBody, 0, 1024 * sizeof(u32 *));
    memset(j->indEnt, 0xFF, 1024);
    memset(j->codeMap, 0, (H64_RDRAM_SIZE >> H64_JIT_CHUNK_SHIFT) * sizeof(u16));
    j->blockCount = 0;
    j->linkCount = 0;
    j->freeLink = -1;
    j->lastExit = 0;
    j->staleBlock = 0;
    memset(sys->cpu.jitPage, 0, sizeof(sys->cpu.jitPage));
    memset(sys->cpu.dtlbR, 0, sizeof(sys->cpu.dtlbR));
    memset(sys->cpu.dtlbW, 0, sizeof(sys->cpu.dtlbW));
    j->memUsed = 0;
    j->coldBase = (j->memSize / 12 * 5) & ~63u;
    j->coldUsed = j->coldBase;
    h64_jit_emit_runtime(sys);
    j->curInvalidated = 1;
    j->stats.flushes++;
}

int h64_jit_init(H64System *sys, void *execMem, u32 size, void (*flushIcache)(void *addr, u32 len))
{
    H64Jit *j;
    if (!H64_JIT_CAN_RUN || !execMem || size < 65536) return -1;
    j = (H64Jit *)calloc(1, sizeof(H64Jit));
    if (!j) return -1;
    j->mem = (u8 *)execMem;
    j->memSize = size;
    j->flushIcache = flushIcache;
    j->blockCap = size / 512 > 32768 ? size / 512 : 32768;
    j->blocks = (H64JitBlock *)calloc(j->blockCap, sizeof(H64JitBlock));
    j->linkCap = 2 * j->blockCap;
    j->links = (H64JitLink *)calloc(j->linkCap, sizeof(H64JitLink));
    j->codeMap = (u16 *)calloc(H64_RDRAM_SIZE >> H64_JIT_CHUNK_SHIFT, sizeof(u16));
    j->indPc = (u32 *)calloc(1024, sizeof(u32));
    j->indBody = (u32 **)calloc(1024, sizeof(u32 *));
    j->indEnt = (s8 *)malloc(1024);
    if (!j->blocks || !j->links || !j->codeMap || !j->indPc || !j->indBody || !j->indEnt)
    {
        free(j->blocks); free(j->links); free(j->codeMap); free(j->indPc); free(j->indBody); free(j->indEnt); free(j);
        return -1;
    }
    j->runEnd = ~0ull;
#if !defined(H64_JIT_ABI_X64)
    if (offsetof(H64System, sched) + offsetof(H64Scheduler, next) > 32760 || offsetof(H64System, jit) > 32760 ||
        offsetof(H64System, mi) > 32760)
    {
        H64_WARN("[jit] H64System fields out of reach of linked exits: block linking off");
        j->noLink = 1;
    }
#endif
    sys->jit = j;
    h64_jit_reset(sys);
    j->stats.flushes = 0;
    return 0;
}

void h64_jit_free(H64System *sys)
{
    if (!sys->jit) return;
    free(sys->jit->blocks);
    free(sys->jit->links);
    free(sys->jit->codeMap);
    free(sys->jit->indPc);
    free(sys->jit->indBody);
    free(sys->jit->indEnt);
    free(sys->jit);
    sys->jit = 0;
}

void h64_jit_code_map(H64Jit *j, const H64JitBlock *b, int delta)
{
    u32 a = b->paddr >> H64_JIT_CHUNK_SHIFT, e = (b->paddr + b->insns * 4 - 1) >> H64_JIT_CHUNK_SHIFT;
    for (; a <= e && a < (H64_RDRAM_SIZE >> H64_JIT_CHUNK_SHIFT); a++) j->codeMap[a] = (u16)(j->codeMap[a] + delta);
}

static void tlb_list_add(H64Jit *j, int i, int e)
{
    H64JitLink *l = &j->links[i];
    l->tlbEntry = e;
    l->tlbPrev = -1;
    l->tlbNext = j->tlbHead[e];
    if (l->tlbNext >= 0) j->links[l->tlbNext].tlbPrev = i;
    j->tlbHead[e] = i;
    j->tlbLinkCount++;
}

static void tlb_list_remove(H64Jit *j, int i)
{
    H64JitLink *l = &j->links[i];
    if (l->tlbEntry < 0) return;
    if (l->tlbPrev >= 0) j->links[l->tlbPrev].tlbNext = l->tlbNext;
    else j->tlbHead[l->tlbEntry] = l->tlbNext;
    if (l->tlbNext >= 0) j->links[l->tlbNext].tlbPrev = l->tlbPrev;
    l->tlbEntry = -1;
    j->tlbLinkCount--;
}

static void free_link(H64Jit *j, int i)
{
    H64JitLink *l = &j->links[i];
    tlb_list_remove(j, i);
    l->patch = 0;
    l->next = j->freeLink;
    j->freeLink = i;
}

static void unlink_block(H64Jit *j, H64JitBlock *b)
{
    int i = b->linkHead;
    while (i >= 0)
    {
        H64JitLink *l = &j->links[i];
        int next = l->next;
        *l->patch = l->orig;
        if (j->flushIcache) j->flushIcache(l->patch, 4);
        free_link(j, i);
        i = next;
    }
    b->linkHead = -1;
    {
        u32 k = (b->vpc >> 2) & 1023;
        if (j->indBody[k] == b->body) { j->indBody[k] = 0; j->indPc[k] = 0xFFFFFFFFu; j->indEnt[k] = -1; }
    }
}

static u32 s_invalLog;

void h64_jit_log_invalidations(u32 count) { s_invalLog = count; }

void h64_jit_invalidate(H64System *sys, u32 paddr, u32 len)
{
    H64Jit *j = sys->jit;
    u32 first, last, p;
    if (!j || !len || paddr >= H64_RDRAM_SIZE) return;
    first = paddr >> 12;
    last = (paddr + len - 1) >> 12;
    if (last >= H64_JIT_PAGES) last = H64_JIT_PAGES - 1;
    for (p = first; p <= last; p++)
    {
        H64JitBlock **link = &j->pageHead[p], *b;
        int hit = 0;
        while ((b = *link) != 0)
        {
            if (b->paddr < paddr + len && paddr < b->paddr + b->insns * 4)
            {
                b->valid = 0;
                unlink_block(j, b);
                h64_jit_code_map(j, b, -1);
                *link = b->pageNext;
                hit = 1;
            }
            else
                link = &b->pageNext;
        }
        if (!hit) continue;
        j->stats.invalidations++;
        if (s_invalLog)
        {
            s_invalLog--;
            H64_INFO("[jit] write %06X+%u drops blocks of page %05X (cpu pc %08X, frame %u)", paddr, len, p << 12,
                     (u32)sys->cpu.pc, sys->vi.frames);
        }
        j->curInvalidated = 1;
    }
}

int h64_jit_kernel_mode(const H64Cpu *cpu)
{
    u32 sr = (u32)cpu->cop0[CP0_STATUS];
    return (sr & (SR_EXL | SR_ERL)) || (sr & SR_KSU) == 0;
}

static H64JitBlock *lookup(H64Jit *j, u32 pc, u32 paddr, int kernel)
{
    H64JitBlock *b = j->hash[hash_of(pc)];
    for (; b; b = b->hashNext)
        if (b->valid && b->vpc == pc && b->paddr == paddr && b->kernel == (kernel && !j->noNative)) return b;
    return 0;
}

static int interrupt_pending(const H64Cpu *cpu)
{
    u32 sr = (u32)cpu->cop0[CP0_STATUS];
    return (sr & SR_IE) && !(sr & (SR_EXL | SR_ERL)) && (cpu->cop0[CP0_CAUSE] & sr & SR_IM);
}

int h64_jit_should_exit(H64System *sys)
{
    H64Jit *j = sys->jit;
    return j->curInvalidated || sys->sched.next < j->blockEndCycles || interrupt_pending(&sys->cpu) || sys->stop ||
           sys->cpu.tlbGen != j->linkTlbGen;
}

static void process_events(H64System *sys)
{
    u64 t0, nested;
    if (sys->sched.next > sys->cpu.cycles) return;
    t0 = h64_prof_now(sys);
    nested = h64_prof_nested(sys);
    while (sys->sched.next <= sys->cpu.cycles)
    {
        int ev = h64_sched_pop_due(&sys->sched, sys->cpu.cycles);
        if (ev < 0) break;
        h64_device_event(sys, ev);
    }
    h64_prof_add_outer(sys, H64_PROF_EVENTS, t0, nested);
}

static void link_exit(H64Jit *j, u32 *patch, H64JitBlock *b, int tlb)
{
    H64JitLink *l;
    s32 off;
    u32 word;
    int i;
    if (!b->body) return;
#if defined(H64_JIT_ABI_X64)
    off = (s32)((u8 *)b->body - ((u8 *)patch + 4));
    word = (u32)off;
#else
    off = (s32)((u8 *)b->body - (u8 *)patch);
    if (off < -0x2000000 || off >= 0x2000000) return;
    word = 0x48000000u | ((u32)off & 0x03FFFFFCu);
#endif
    if (*patch == word) return;
    if (j->freeLink >= 0)
    {
        i = j->freeLink;
        j->freeLink = j->links[i].next;
    }
    else if (j->linkCount < j->linkCap)
        i = (int)j->linkCount++;
    else
        return;
    l = &j->links[i];
    l->patch = patch;
    l->orig = *patch;
    l->tlb = tlb >= 0;
    l->target = (int)(b - j->blocks);
    l->tlbEntry = -1;
    if (tlb >= 0) tlb_list_add(j, i, tlb);
    l->next = b->linkHead;
    b->linkHead = i;
    j->stats.linksMade++;
    *patch = word;
    if (j->flushIcache) j->flushIcache(patch, 4);
}

static int run_queue_empty(H64System *sys)
{
    const H64Cpu *cpu = &sys->cpu;
    u32 head, p, k, prio;
    if (sys->options.idleRunQueue + 4 > H64_RDRAM_SIZE) return 0;
    head = h64_rdram_load32(sys->rdram, sys->options.idleRunQueue);
    if (head >= 0x80000000u && head < 0xA0000000u) p = head & 0x1FFFFFFFu;
    else
    {
        k = (head >> 12) & 255;
        if (cpu->dtlbR[k] != ((head & ~0xFFFu) | 1u)) return 0;
        p = cpu->dtlbPhys[k] | (head & 0xFFFu);
    }
    if (p + 8 > H64_RDRAM_SIZE) return 0;
    prio = h64_rdram_load32(sys->rdram, p + 4);
    return prio == 0xFFFFFFFFu;
}

static int mesg_queue_empty(H64System *sys)
{
    u32 q = sys->options.countWaitQueue;
    if (!q) return 1;
    return q + 12 <= H64_RDRAM_SIZE && h64_rdram_load32(sys->rdram, q + 8) == 0;
}

void h64_jit_run_one(H64System *sys)
{
    H64Jit *j = sys->jit;
    H64Cpu *cpu = &sys->cpu;
    H64JitBlock *b;
    u32 pc32, paddr;
    u32 *lastExit = j->lastExit, lastTarget = j->lastExitTarget;
    int tlbEntry = -1;
    u64 flushes;

#if defined(H64_JIT_ABI_X64)
    if (j->fastFpu && (_mm_getcsr() & X64_FTZ_DAZ) != X64_FTZ_DAZ) _mm_setcsr(_mm_getcsr() | X64_FTZ_DAZ);
#endif
    if (cpu->cpi != j->rtCpi) h64_jit_reset(sys);
    flushes = j->stats.flushes;
    lastExit = j->lastExit;
    j->lastExit = 0;
    process_events(sys);
    j->linkTlbGen = cpu->tlbGen;
    pc32 = (u32)cpu->pc;
    if (cpu->pc == (u64)(s64)(s32)pc32 && pc32 >= 0x80000000u && pc32 < 0xC0000000u)
        paddr = pc32 & 0x1FFFFFFFu;
    else if (cpu->pc != (u64)(s64)(s32)pc32)
        paddr = 0xFFFFFFFFu;
    else
    {
        u32 key = (pc32 >> 12) | (((u32)cpu->cop0[CP0_STATUS] & 0x1E) << 19);
        u32 k = (pc32 >> 12) & 255;
        int e = j->fetch[k].entry;
        if (j->fetch[k].key == key && e >= 0 && j->fetch[k].gen == cpu->tlbEntryGen[e] && j->fetch[k].asid == cpu->asidGen)
        {
            paddr = j->fetch[k].ppage | (pc32 & 0xFFFu);
            tlbEntry = e;
        }
        else if (h64_cpu_probe_fetch(cpu, pc32, &paddr, &e))
        {
            j->fetch[k].key = key;
            j->fetch[k].entry = e;
            j->fetch[k].gen = e >= 0 ? cpu->tlbEntryGen[e] : 0;
            j->fetch[k].asid = cpu->asidGen;
            j->fetch[k].ppage = paddr & ~0xFFFu;
            tlbEntry = e;
        }
        else
            paddr = 0xFFFFFFFFu;
    }
    if (tlbEntry >= 0 && paddr < H64_RDRAM_SIZE)
    {
        cpu->jitPage[(pc32 >> 12) & 255] = H64_JIT_PAGE_KEY(pc32, paddr);
        cpu->jitPageEnt[(pc32 >> 12) & 255] = (s8)tlbEntry;
    }
    if (j->staleBlock)
    {
        H64JitBlock *s = (H64JitBlock *)j->staleBlock;
        u64 t0 = h64_prof_now(sys);
        j->staleBlock = 0;
        j->stats.tlbStale++;
        if (s->vpc == pc32 && paddr < H64_RDRAM_SIZE && s->paddr != paddr)
        {
            j->stats.tlbUnlinks++;
            unlink_block(j, s);
        }
        sys->prof[H64_PROF_TLB] += h64_prof_now(sys) - t0;
    }
    if (cpu->branchPending || interrupt_pending(cpu) || (pc32 & 3) || paddr >= H64_RDRAM_SIZE)
    {
        h64_cpu_step(sys);
        j->stats.interpSteps++;
        return;
    }
    b = lookup(j, pc32, paddr, h64_jit_kernel_mode(cpu));
    if (!b)
    {
        {
            u64 t0 = h64_prof_now(sys);
            b = h64_jit_compile(sys, pc32, paddr);
            sys->prof[H64_PROF_JIT_COMPILE] += h64_prof_now(sys) - t0;
        }
        if (!b)
        {
            h64_cpu_step(sys);
            j->stats.interpSteps++;
            return;
        }
    }
    if (b->idle == 3 && b->valid && !interrupt_pending(cpu) && sys->sched.next != H64_NEVER &&
        sys->sched.next > cpu->cycles + cpu->cpi && run_queue_empty(sys))
    {
        u64 k = (sys->sched.next - cpu->cycles) / cpu->cpi - 1;
        if (k > (1u << 24)) k = 1u << 24;
        cpu->cycles += k * cpu->cpi;
        cpu->instructions += k;
        j->stats.idleSkipped += k;
    }
    if (b->idle == 4 && b->valid && !interrupt_pending(cpu) && sys->sched.next != H64_NEVER &&
        sys->sched.next > cpu->cycles + cpu->cpi && mesg_queue_empty(sys))
    {
        u64 k = (sys->sched.next - cpu->cycles) / cpu->cpi - 1;
        u32 step = sys->options.countWaitStep;
        if (step && k > step / cpu->cpi) k = step / cpu->cpi;
        cpu->cycles += k * cpu->cpi;
        cpu->instructions += k;
        j->stats.idleSkipped += k;
    }
    if (sys->sched.next < cpu->cycles + (u64)b->insns * cpu->cpi)
    {
        h64_cpu_step(sys);
        j->stats.interpSteps++;
        return;
    }
    if (lastExit && lastTarget == pc32 && flushes == j->stats.flushes && !j->noLink && b->kernel && b->valid && !b->idle &&
        (JIT_LINK_TLB || tlbEntry < 0))
        link_exit(j, lastExit, b, -1);
    if (b->kernel && b->valid && !b->idle && !j->noLink && b->body &&
        ((pc32 >= 0x80000000u && pc32 < 0xC0000000u) || (JIT_LINK_TLB && tlbEntry >= 0)))
    {
        u32 k = (pc32 >> 2) & 1023;
        j->indPc[k] = pc32;
        j->indBody[k] = b->body;
        j->indEnt[k] = (s8)((pc32 >= 0x80000000u && pc32 < 0xC0000000u) ? -1 : tlbEntry);
    }
    j->blockEndCycles = cpu->cycles + (u64)b->insns * cpu->cpi;
    j->curPage = b->paddr >> 12;
    j->curInvalidated = 0;
    j->stats.blocksRun++;
    {
        u64 t0 = h64_prof_now(sys);
        j->enter(sys, b->body);
        sys->prof[H64_PROF_BLOCKS] += h64_prof_now(sys) - t0;
    }

    if (b->idle && b->valid && cpu->pc == (u64)(s64)(s32)b->vpc && !cpu->branchPending && !interrupt_pending(cpu) &&
        sys->sched.next != H64_NEVER && sys->sched.next > cpu->cycles)
    {
        u64 k = (sys->sched.next - cpu->cycles) / ((u64)b->insns * cpu->cpi);
        if (b->idle == 2)
        {
            u64 a = cpu->gpr[b->pollBase] + (u64)(s64)b->pollOff;
            u32 a32 = (u32)a;
            if (a != (u64)(s64)(s32)a32 || (a32 >> 30) != 2 || (a32 & (b->pollSize - 1)) || (a32 & 0x1FFFFFFF) >= H64_RDRAM_SIZE)
                k = 0;
        }
        if (k > (1u << 24)) k = 1u << 24;
        if (k)
        {
            cpu->cycles += k * b->insns * cpu->cpi;
            cpu->instructions += k * b->insns;
            j->stats.idleSkipped += k * b->insns;
        }
    }
}

void h64_jit_run(H64System *sys, u64 cycles)
{
    u64 end = sys->cpu.cycles + cycles;
    sys->stop = 0;
    sys->jit->runEnd = end;
    while (sys->cpu.cycles < end && !sys->stop)
        h64_jit_run_one(sys);
    sys->jit->runEnd = ~0ull;
}
