// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - RCP registers (SP, DP, MI, VI, AI, PI, RI, SI) and their
// timed side effects. Register layouts from n64brew.
//
// The SP registers and DMA live with the RSP (core/rsp), the DPC registers
// with the RDP (core/rdp). Timings marked "model" are approximations to be
// replaced by measured values.
#include "h64_system.h"

#include <stdlib.h>
#include <string.h>

#include "../cheat/h64_cheat.h"
#include "../common/h64_endian.h"
#include "../common/h64_log.h"
#include "../memory/h64_rdram.h"

#define CPU_HZ 93750000u
#define SI_DMA_CYCLES 4500u

static const u32 VI_CLOCK_NTSC = 48681812u;
static const u32 VI_CLOCK_PAL = 49656530u;
static const u32 VI_CLOCK_MPAL = 48628316u;

static u64 now(H64System *sys) { return sys->cpu.cycles; }

static void mi_update(H64System *sys)
{
    h64_cpu_set_ip(sys, 2, (sys->mi.intr & sys->mi.mask) != 0);
}

void h64_mi_raise(H64System *sys, u32 bits)
{
    int i;
    for (i = 0; i < 6; i++)
        if (bits & (1u << i)) sys->miRaised[i]++;
    sys->mi.intr |= bits;
    mi_update(sys);
}
void h64_mi_clear(H64System *sys, u32 bits) { sys->mi.intr &= ~bits; mi_update(sys); }

enum { VI_STATUS = 0, VI_ORIGIN, VI_WIDTH, VI_V_INTR, VI_V_CURRENT, VI_BURST, VI_V_SYNC, VI_H_SYNC, VI_H_SYNC_LEAP,
       VI_H_VIDEO, VI_V_VIDEO, VI_V_BURST, VI_X_SCALE, VI_Y_SCALE };

static u32 vi_halflines(H64System *sys)
{
    u32 v = sys->vi.regs[VI_V_SYNC] & 0x3FF;
    return v ? v : 525;
}

static void vi_advance(H64System *sys)
{
    while (now(sys) >= sys->vi.frameStart + sys->vi.frameCycles)
    {
        sys->vi.frameStart += sys->vi.frameCycles;
        sys->vi.frames++;
    }
}

static u32 vi_current(H64System *sys)
{
    u64 into;
    vi_advance(sys);
    into = now(sys) - sys->vi.frameStart;
    u32 line = (u32)(into * vi_halflines(sys) / sys->vi.frameCycles);
    if (line >= vi_halflines(sys)) line = vi_halflines(sys) - 1;
    return line & 0x3FF;
}

static void vi_schedule(H64System *sys)
{
    u32 vintr = sys->vi.regs[VI_V_INTR] & 0x3FF;
    u64 at;
    vi_advance(sys);
    if (sys->options.viIntrLine0 && vintr < vi_halflines(sys)) vintr = 0;
    at = sys->vi.frameStart + (u64)vintr * sys->vi.frameCycles / vi_halflines(sys);
    if (vintr >= vi_halflines(sys) || at <= now(sys))
    {
        if (vintr < vi_halflines(sys))
            at = sys->vi.frameStart + sys->vi.frameCycles + (u64)vintr * sys->vi.frameCycles / vi_halflines(sys);
        else
            at = sys->vi.frameStart + sys->vi.frameCycles;
    }
    h64_sched_set(&sys->sched, H64_EV_VI, at);
}

static u64 ai_buffer_cycles(H64System *sys, u32 len)
{
    u32 clock = sys->tvType == 0 ? VI_CLOCK_PAL : sys->tvType == 2 ? VI_CLOCK_MPAL : VI_CLOCK_NTSC;
    u32 rate = clock / ((sys->ai.dacrate & 0x3FFF) + 1);
    if (rate == 0) rate = 1;
    return (u64)(len / 4) * CPU_HZ / rate + 1;
}

static void ai_start_next(H64System *sys)
{
    if (sys->ai.fifoCount > 0)
    {
        sys->ai.bufferCycles = ai_buffer_cycles(sys, sys->ai.fifoLen[0]);
        h64_sched_set(&sys->sched, H64_EV_AI, now(sys) + sys->ai.bufferCycles);
    }
}

// Block model and timing follow ares (ISC licence, ares/n64/pi/dma.cpp, see

static const u32 *pi_bsd(H64System *sys, u32 cart)
{
    int dom2 = (cart >= 0x05000000u && cart < 0x06000000u) || (cart >= 0x08000000u && cart < 0x10000000u);
    return &sys->pi.regs[dom2 ? 9 : 5];
}

static u16 pi_bus_half(H64System *sys, u32 cart)
{
    if (cart >= 0x10000000u && cart - 0x10000000u + 1 < sys->rom.size)
        return h64_load_be16(sys->rom.data + (cart - 0x10000000u));
    return (u16)cart;
}

static u64 pi_dma_cycles(H64System *sys, u32 len)
{
    const u32 *bsd = pi_bsd(sys, sys->pi.regs[1]);
    u32 pageShift = (bsd[2] & 0xF) + 2, pageSize = 1u << pageShift, pageMask = pageSize - 1;
    u32 first = sys->pi.regs[1], last = first + len - 2;
    u32 firstPage = first >> pageShift, lastPage = last >> pageShift, pages = lastPage - firstPage + 1;
    u32 buffers = 0, partial = 0;
    u64 cycles;
    if (firstPage == lastPage)
    {
        if (len == 128) buffers = 1;
        else partial = len;
    }
    else
    {
        if ((first & pageMask) == 0) buffers++;
        else partial += pageSize - (first & pageMask);
        if (((last + 2) & pageMask) == 0) buffers++;
        else partial += (last & pageMask) + 2;
        if (firstPage + 1 < lastPage)
            buffers += (pages - 2) * pageSize / 128;
    }
    cycles = (u64)(14 + bsd[0] + 1) * pages + (u64)(bsd[1] + 1 + bsd[3] + 1) * len / 2 + buffers * 28 + partial;
    return cycles * 3 / 2;
}

static int save_dma(H64System *sys, int toRdram, u32 cart, u32 dram, u32 n)
{
#if H64_RDRAM_XOR
    u8 *tmp = (u8 *)malloc(n ? n : 1);
    int r;
    if (!tmp) return 0;
    if (toRdram)
    {
        r = h64_save_dma_to_rdram(sys->save, cart, tmp, n);
        if (r) h64_rdram_write(sys->rdram, dram, tmp, n);
    }
    else
    {
        h64_rdram_read(sys->rdram, dram, tmp, n);
        r = h64_save_dma_from_rdram(sys->save, cart, tmp, n);
    }
    free(tmp);
    return r;
#else
    if (toRdram) return h64_save_dma_to_rdram(sys->save, cart, sys->rdram + dram, n);
    return h64_save_dma_from_rdram(sys->save, cart, sys->rdram + dram, n);
#endif
}

static void pi_dma(H64System *sys, int toRdram, u32 value)
{
    u32 len = (value & 0x00FFFFFF) + 1;
    u64 cycles = pi_dma_cycles(sys, ((len - 1) | 1) + 1);
    H64_DEBUG("[pi] f%u DMA %s cart %08X dram %08X len %X", sys->vi.frames, toRdram ? "cart->rdram" : "rdram->cart",
              sys->pi.regs[1], sys->pi.regs[0], len);
    if (sys->pi.regs[1] >= 0x08000000u && sys->pi.regs[1] < 0x10000000u)
    {
        u32 dram = sys->pi.regs[0] & 0x00FFFFFEu, cart = sys->pi.regs[1];
        u32 n = dram + len <= H64_RDRAM_SIZE ? len : (dram < H64_RDRAM_SIZE ? H64_RDRAM_SIZE - dram : 0);
        if (save_dma(sys, toRdram, cart, dram, n) && toRdram) h64_jit_notify_write(sys, dram, n);
        sys->pi.regs[0] = (dram + len) & 0x00FFFFFF;
        sys->pi.regs[1] = cart + len;
        if (!toRdram) sys->pi.regs[2] = ((value & 0x00FFFFFF) | 1) + 1;
    }
    else if (toRdram && sys->options.piOddCart && sys->pi.cartOdd && sys->pi.regs[1] >= 0x10000000u &&
             sys->pi.regs[1] < 0x1FC00000u)
    {
        u32 dram = sys->pi.regs[0], rom = sys->pi.regs[1] - 0x10000000u + 1, i;
        h64_jit_notify_write(sys, dram, len);
        for (i = 0; i < len; i++)
            if (dram + i < H64_RDRAM_SIZE) h64_rdram_store8(sys->rdram, dram + i, (u8)(rom + i < sys->rom.size ? sys->rom.data[rom + i] : 0));
        sys->pi.regs[0] = (dram + len + 7) & 0x00FFFFF8;
        sys->pi.regs[1] = (sys->pi.regs[1] + len + 1) & ~1u;
        sys->pi.cartOdd = 0;
    }
    else if (toRdram)
    {
        u8 mem[128];
        s32 left = (s32)len, maxBlock = 128;
        int firstBlock = 1;
        u32 dram = sys->pi.regs[0], cart = sys->pi.regs[1];
        h64_jit_notify_write(sys, dram, len + 8);
        while (left > 0)
        {
            s32 misalign = (s32)(dram & 7), distEndOfRow = 0x800 - (s32)(dram & 0x7FF);
            s32 blockLen = maxBlock - misalign < distEndOfRow ? maxBlock - misalign : distEndOfRow;
            s32 curLen = left < blockLen ? left : blockLen, i;
            for (i = 0; i < curLen; i += 2)
            {
                u16 h = pi_bus_half(sys, cart);
                mem[i] = (u8)(h >> 8);
                mem[i + 1] = (u8)h;
                cart += 2;
                left -= 2;
            }
            if (firstBlock && curLen < 127 - misalign)
                for (i = 0; i < curLen - misalign; i++, dram++)
                {
                    if (dram < H64_RDRAM_SIZE) h64_rdram_store8(sys->rdram, dram, mem[i]);
                }
            else
                for (i = 0; i < curLen - misalign; i += 2, dram += 2)
                {
                    if (dram < H64_RDRAM_SIZE) h64_rdram_store8(sys->rdram, dram, mem[i]);
                    if (dram + 1 < H64_RDRAM_SIZE) h64_rdram_store8(sys->rdram, dram + 1, mem[i + 1]);
                }
            dram = (dram + 7) & ~7u;
            sys->pi.regs[3] = curLen <= 8 ? (u32)(127 - misalign) : 127u;
            firstBlock = 0;
            maxBlock = distEndOfRow < 8 ? 128 - misalign : 128;
        }
        sys->pi.regs[0] = dram & 0x00FFFFFF;
        sys->pi.regs[1] = cart;
    }
    else
    {
        sys->pi.regs[2] = ((value & 0x00FFFFFF) | 1) + 1;
    }
    sys->pi.regs[4] |= 1;
    h64_sched_set(&sys->sched, H64_EV_PI, now(sys) + cycles);
}

static void si_dma(H64System *sys, int toPif)
{
    u32 dram = sys->si.dramAddr & 0x00FFFFF8;
    u32 i;
    if (toPif)
    {
        for (i = 0; i < 64; i++)
            sys->pifRam[i] = dram + i < H64_RDRAM_SIZE ? h64_rdram_load8(sys->rdram, dram + i) : 0;
        h64_pif_run_commands(sys);
    }
    else
    {
        h64_pif_read_hook(sys);
        h64_jit_notify_write(sys, dram, 64);
        for (i = 0; i < 64; i++)
            if (dram + i < H64_RDRAM_SIZE)
                h64_rdram_store8(sys->rdram, dram + i, sys->pifRam[i]);
    }
    sys->si.status |= 1;
    h64_sched_set(&sys->sched, H64_EV_SI, now(sys) + SI_DMA_CYCLES);
}

enum { RDRAM_CONFIG, RDRAM_DEVICE_ID, RDRAM_DELAY, RDRAM_MODE, RDRAM_REF_INTERVAL, RDRAM_REF_ROW,
       RDRAM_RAS_INTERVAL, RDRAM_MIN_INTERVAL, RDRAM_ADDR_SELECT, RDRAM_DEVICE_MANUF, RDRAM_REGS };

static int rdram_module(H64System *sys, u32 paddr)
{
    u32 ri = (paddr >> 20) == 0x03F ? (paddr & 0x3FF) | (((paddr >> 10) & 0x1FF) << 11) | (((paddr >> 10) & 0x1FF) << 20)
                                     : (paddr & 0x7FF) | (((paddr >> 11) & 0x1FF) << 11) | (((paddr >> 20) & 0x3F) << 20);
    int m;
    for (m = 0; m < H64_RDRAM_MODULES; m++)
    {
        u32 sel = sys->ri.rdram[m][RDRAM_ADDR_SELECT], id = sys->ri.rdram[m][RDRAM_DEVICE_ID];
        u32 swap = ((sel >> 25) & 0x7F) | (((sel >> 15) & 1) << 7);
        u32 field = ((swap & ((ri >> 11) & 0x1FF)) | (~swap & ((ri >> 20) & 0x1FF))) & 0xFFFF;
        u32 want = ((id >> 26) & 0x3F) | (((id >> 23) & 1) << 6) | (((id >> 8) & 0xFF) << 7) | (((id >> 7) & 1) << 15);
        if (field == want) return m;
    }
    return -1;
}

static u32 rdram_cc(u32 mode)
{
    return ((mode >> 6) & 1) | ((mode >> 13) & 2) | ((mode >> 20) & 4) | ((mode >> 4) & 8) | ((mode >> 11) & 16) | ((mode >> 18) & 32);
}

int h64_rdram_read_lost(H64System *sys, u32 paddr)
{
    int m = rdram_module(sys, paddr);
    u32 mode;
    if (m < 0) return 1;
    mode = sys->ri.rdram[m][RDRAM_MODE] ^ 0xC0C0C0C0u;
    return (mode & 0x80000000u) && rdram_cc(mode) == 0;
}

static u32 rdram_reg_read(H64System *sys, u32 paddr)
{
    u32 reg = (paddr & 0x3FF) >> 2, v;
    int m;
    if (paddr & 0x80000u) return 0;
    m = rdram_module(sys, paddr);
    if (m < 0 || reg >= RDRAM_REGS) return 0;
    v = sys->ri.rdram[m][reg];
    if (reg == RDRAM_MODE) v ^= 0xC0C0C0C0u;
    return v;
}

static void rdram_reg_write(H64System *sys, u32 paddr, u32 value, u32 mask)
{
    u32 reg = (paddr & 0x3FF) >> 2;
    int m;
    if (reg >= RDRAM_REGS) return;
    if (paddr & 0x80000u)
    {
        for (m = 0; m < H64_RDRAM_MODULES; m++)
            sys->ri.rdram[m][reg] = (sys->ri.rdram[m][reg] & ~mask) | (value & mask);
    }
    else
    {
        m = rdram_module(sys, paddr);
        if (m >= 0) sys->ri.rdram[m][reg] = (sys->ri.rdram[m][reg] & ~mask) | (value & mask);
    }
    if (reg == RDRAM_MODE)
    {
        sys->ri.corrupt = 0;
        for (m = 0; m < H64_RDRAM_MODULES; m++)
        {
            u32 mode = sys->ri.rdram[m][RDRAM_MODE] ^ 0xC0C0C0C0u;
            if ((mode & 0x80000000u) && rdram_cc(mode) == 0) sys->ri.corrupt = 1;
        }
    }
}

u32 h64_mmio_read(H64System *sys, u32 paddr)
{
    u32 reg = (paddr & 0xFFFFF) >> 2;
    switch (paddr >> 20)
    {
    case 0x03F: { u32 v = rdram_reg_read(sys, paddr); H64_DEBUG("[rdreg] R %08X -> %08X", paddr, v); return v; }
    case 0x040:
        if (paddr == 0x04080000u) return h64_sp_pc_read(sys);
        if (paddr >= 0x04040000u && paddr < 0x04040020u) return h64_sp_read(sys, (paddr >> 2) & 7);
        return 0;
    case 0x041: return h64_dp_read(sys, reg & 7);
    case 0x043:
        switch (reg & 3)
        {
        case 0: return sys->mi.mode;
        case 1: return sys->mi.version;
        case 2: return sys->mi.intr;
        default: return sys->mi.mask;
        }
    case 0x044:
        reg &= 0xF;
        if (reg == VI_V_CURRENT) return vi_current(sys);
        return reg < 14 ? sys->vi.regs[reg] : 0;
    case 0x045:
        switch (reg & 7)
        {
        case 1:
            if (sys->ai.fifoCount == 0) return 0;
            {
                u64 end = sys->sched.when[H64_EV_AI];
                u64 left = end > now(sys) ? end - now(sys) : 0;
                u32 v = (u32)((u64)sys->ai.fifoLen[0] * left / (sys->ai.bufferCycles ? sys->ai.bufferCycles : 1)) & ~7u;
                sys->ai.statReads++;
                sys->ai.statReadSum += v;
                return v;
            }
        case 3:
            return (sys->ai.fifoCount >= 2 ? 0x80000001u : 0) | (sys->ai.fifoCount > 0 ? 0x40000000u : 0) |
                   0x00100000u;
        default: return 0;
        }
    case 0x046:
        if ((reg & 0xF) == 4)
            return sys->pi.regs[4] | (now(sys) < sys->pi.latchUntil ? 2u : 0u);
        return (reg & 0xF) < 13 ? sys->pi.regs[reg & 0xF] : 0;
    case 0x047: return sys->ri.regs[reg & 7];
    case 0x048:
        switch (reg & 7)
        {
        case 0: return sys->si.dramAddr;
        case 1: return sys->si.pifAddrRd;
        case 4: return sys->si.pifAddrWr;
        case 6: return sys->si.status | ((sys->mi.intr & MI_INTR_SI) ? 0x1000u : 0);
        }
        return 0;
    }
    return 0;
}

void h64_mmio_write(H64System *sys, u32 paddr, u32 value, u32 mask)
{
    u32 reg = (paddr & 0xFFFFF) >> 2;
    switch (paddr >> 20)
    {
    case 0x03F: rdram_reg_write(sys, paddr, value, mask); H64_DEBUG("[rdreg] W %08X <- %08X corrupt %u", paddr, value, sys->ri.corrupt); return;
    case 0x040:
        if (paddr == 0x04080000u) { h64_sp_pc_write(sys, value); return; }
        if (paddr >= 0x04040000u && paddr < 0x04040020u) h64_sp_write(sys, (paddr >> 2) & 7, value);
        return;
    case 0x041: h64_dp_write(sys, reg & 7, value); return;
    case 0x043:
        switch (reg & 3)
        {
        case 0:
        {
            u32 m = sys->mi.mode;
            m = (m & ~0x7Fu) | (value & 0x7F);
            if (value & 0x0080) m &= ~0x080u;
            if (value & 0x0100) m |= 0x080u;
            if (value & 0x0200) m &= ~0x100u;
            if (value & 0x0400) m |= 0x100u;
            if (value & 0x0800) h64_mi_clear(sys, MI_INTR_DP);
            if (value & 0x1000) m &= ~0x200u;
            if (value & 0x2000) m |= 0x200u;
            sys->mi.mode = m;
            return;
        }
        case 3:
        {
            int i;
            for (i = 0; i < 6; i++)
            {
                if (value & (1u << (2 * i))) sys->mi.mask &= ~(1u << i);
                if (value & (2u << (2 * i))) sys->mi.mask |= (1u << i);
            }
            mi_update(sys);
            return;
        }
        }
        return;
    case 0x044:
        reg &= 0xF;
        if (reg >= 14) return;
        if (reg == VI_V_CURRENT) { h64_mi_clear(sys, MI_INTR_VI); return; }
        sys->vi.regs[reg] = value;
        if (reg == VI_V_INTR || reg == VI_V_SYNC) vi_schedule(sys);
        return;
    case 0x045:
        switch (reg & 7)
        {
        case 0: sys->ai.dramAddr = value & 0xFFFFF8; return;
        case 1:
        {
            u32 len = value & 0x3FFF8;
            if (len == 0 || sys->ai.fifoCount >= 2) return;
            if (sys->aiSink && (sys->ai.control & 1))
            {
                u32 clock = sys->tvType == 0 ? VI_CLOCK_PAL : sys->tvType == 2 ? VI_CLOCK_MPAL : VI_CLOCK_NTSC;
                u32 addr = sys->ai.dramAddr & (H64_RDRAM_SIZE - 1);
                u32 n = addr + len <= H64_RDRAM_SIZE ? len : H64_RDRAM_SIZE - addr;
#if H64_RDRAM_XOR
                static u8 s_aiCopy[0x40000];
                h64_rdram_read(sys->rdram, addr, s_aiCopy, n);
                sys->aiSink(sys->aiUser, s_aiCopy, n, clock / ((sys->ai.dacrate & 0x3FFF) + 1));
#else
                sys->aiSink(sys->aiUser, sys->rdram + addr, n, clock / ((sys->ai.dacrate & 0x3FFF) + 1));
#endif
            }
            sys->ai.fifoLen[sys->ai.fifoCount++] = len;
            sys->ai.statQueued += len;
            sys->ai.statBuffers++;
            if (sys->ai.fifoCount == 1) ai_start_next(sys);
            return;
        }
        case 2: sys->ai.control = value & 1; return;
        case 3: h64_mi_clear(sys, MI_INTR_AI); return;
        case 4: sys->ai.dacrate = value & 0x3FFF; return;
        case 5: sys->ai.bitrate = value & 0xF; return;
        }
        return;
    case 0x046:
        reg &= 0xF;
        if (reg != 4 && ((sys->pi.regs[4] & 1) || now(sys) < sys->pi.latchUntil))
        {
            sys->pi.regs[4] |= 4;
            return;
        }
        switch (reg)
        {
        case 0: sys->pi.regs[0] = value & 0x00FFFFFE; return;
        case 1: sys->pi.regs[1] = value & ~1u; sys->pi.cartOdd = value & 1; return;
        case 2: sys->pi.regs[2] = value & 0x00FFFFFF; pi_dma(sys, 0, value); return;
        case 3: sys->pi.regs[3] = value & 0x00FFFFFF; pi_dma(sys, 1, value); return;
        case 4:
            if (value & 2) { h64_mi_clear(sys, MI_INTR_PI); sys->pi.regs[4] &= ~8u; }
            if (value & 1) { sys->pi.regs[4] &= ~5u; h64_sched_cancel(&sys->sched, H64_EV_PI); }
            return;
        case 7: sys->pi.regs[7] = value & 0xF; return;
        case 8: sys->pi.regs[8] = value & 0x3; return;
        default:
            if (reg < 13) sys->pi.regs[reg] = value & 0xFF;
            return;
        }
    case 0x047: sys->ri.regs[reg & 7] = value; return;
    case 0x048:
        switch (reg & 7)
        {
        case 0: sys->si.dramAddr = value & 0xFFFFF8; return;
        case 1: sys->si.pifAddrRd = value; si_dma(sys, 0); return;
        case 4: sys->si.pifAddrWr = value; si_dma(sys, 1); return;
        case 6: h64_mi_clear(sys, MI_INTR_SI); return;
        }
        return;
    }
}

void h64_device_event(H64System *sys, int ev)
{
    switch (ev)
    {
    case H64_EV_VI:
        if ((sys->vi.regs[VI_V_INTR] & 0x3FF) < vi_halflines(sys))
            h64_mi_raise(sys, MI_INTR_VI);
        if (sys->cheats) h64_cheat_frame(sys);
        if (sys->viHook) sys->viHook(sys);
        vi_schedule(sys);
        break;
    case H64_EV_AI:
        if (sys->ai.fifoCount > 0)
        {
            sys->ai.fifoLen[0] = sys->ai.fifoLen[1];
            sys->ai.fifoCount--;
            h64_mi_raise(sys, MI_INTR_AI);
            ai_start_next(sys);
        }
        break;
    case H64_EV_PI:
        sys->pi.regs[4] = (sys->pi.regs[4] & ~3u) | 8u;
        h64_mi_raise(sys, MI_INTR_PI);
        break;
    case H64_EV_SI:
        sys->si.status &= ~1u;
        h64_mi_raise(sys, MI_INTR_SI);
        break;
    case H64_EV_SP:
        h64_sp_dma_event(sys);
        break;
    case H64_EV_RSP:
        h64_rsp_slice_event(sys);
        break;
    case H64_EV_DP:
        h64_mi_raise(sys, MI_INTR_DP);
        break;
    case H64_EV_COMPARE:
        h64_cpu_set_ip(sys, 7, 1);
        h64_cpu_reschedule_compare(sys);
        break;
    }
}

void h64_devices_reset(H64System *sys)
{
    memset(&sys->mi, 0, sizeof(sys->mi));
    memset(&sys->vi, 0, sizeof(sys->vi));
    memset(&sys->ai, 0, sizeof(sys->ai));
    memset(&sys->pi, 0, sizeof(sys->pi));
    memset(&sys->ri, 0, sizeof(sys->ri));
    memset(&sys->si, 0, sizeof(sys->si));
    {
        int m;
        for (m = 0; m < H64_RDRAM_MODULES; m++)
        {
            sys->ri.rdram[m][RDRAM_CONFIG] = 0xB5190010u;
            sys->ri.rdram[m][RDRAM_DELAY] = 0x230B0223u;
            sys->ri.rdram[m][RDRAM_MODE] = 0xC4C0C0C0u;
            sys->ri.rdram[m][RDRAM_MIN_INTERVAL] = 0x0040C0E0u;
            sys->ri.rdram[m][RDRAM_DEVICE_MANUF] = 0x00000500u;
        }
    }
    sys->mi.version = 0x02020102u;
    sys->vi.regs[VI_V_SYNC] = sys->tvType == 0 ? 625 : 525;
    sys->vi.regs[VI_V_INTR] = 0x3FF;
    sys->vi.frameCycles = sys->tvType == 0 ? CPU_HZ / 50 : CPU_HZ / 60;
    sys->vi.frameStart = now(sys);
    vi_schedule(sys);
}
