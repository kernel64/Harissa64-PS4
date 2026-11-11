// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - physical address map (n64brew "Memory map").
//
//   0x00000000-0x03EFFFFF  RDRAM (8 MB present, the rest reads as 0)
//   0x03F00000-0x03FFFFFF  RDRAM registers
//   0x04000000-0x0403FFFF  RSP DMEM/IMEM (8 KB, mirrored)
//   0x04040000-0x048FFFFF  RCP registers (SP, DP, MI, VI, AI, PI, RI, SI)
//   0x05000000-0x0FFFFFFF  cartridge domains 1/2 (64DD, SRAM, FlashRAM)
//   0x10000000-0x1FBFFFFF  cartridge ROM (ISViewer at 0x13FF0000)
//   0x1FC00000-0x1FC007BF  PIF boot ROM (not available: HLE boot)
//   0x1FC007C0-0x1FC007FF  PIF RAM
//
// RDRAM, RSP memory and PIF RAM take byte-precise accesses. Smaller-than-32-
// bit writes to RCP registers reach the register as a 32-bit write of the
// data shifted into its byte lane (the RCP bus is 32 bits wide).
#include "../system/h64_system.h"

#include <string.h>

#include "../common/h64_endian.h"
#include "../common/h64_log.h"
#include "h64_rdram.h"

#define ISV_BASE 0x13FF0000u
#define ISV_END  0x13FF1000u

static void rdram_repeat(H64System *sys, u32 paddr, u64 data);
static u64 repeat32(u32 v);

static u32 open_bus(u32 paddr)
{
    return ((paddr & 0xFFFFu) << 16) | (paddr & 0xFFFFu);
}

void h64_debug_text(H64System *sys, const u8 *text, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++)
    {
        char c = (char)text[i];
        if (c == '\n' || sys->isvLineLen >= (int)sizeof(sys->isvLine) - 1)
        {
            sys->isvLine[sys->isvLineLen] = 0;
            if (sys->isvSink) sys->isvSink(sys->isvUser, sys->isvLine);
            else H64_INFO("[isv] %s", sys->isvLine);
            sys->isvLineLen = 0;
            if (c == '\n') continue;
        }
        if (c != '\r')
            sys->isvLine[sys->isvLineLen++] = c;
    }
}

static int isv_on(const H64System *sys) { return sys->rom.size <= ISV_BASE - 0x10000000u; }

static void isv_flush(H64System *sys, u32 len)
{
    if (len > sizeof(sys->isvBuffer))
        len = sizeof(sys->isvBuffer);
    h64_debug_text(sys, sys->isvBuffer, len);
}

int h64_bus_read32(H64System *sys, u32 paddr, u32 *value)
{
    paddr &= ~3u;
    if (paddr - 0x03F00000u < 0x00A00000u) h64_rsp_sync(sys);
    if (paddr < 0x03F00000u)
    {
        *value = paddr < H64_RDRAM_SIZE ? h64_rdram_load32(sys->rdram, paddr) : 0;
        if (sys->ri.corrupt && h64_rdram_read_lost(sys, paddr)) *value = 0;
        return 0;
    }
    if (paddr < 0x04000000u) { *value = h64_mmio_read(sys, paddr); return 0; }
    if (paddr < 0x04040000u) { *value = h64_load_be32(sys->spMem + (paddr & 0x1FFFu)); return 0; }
    if (paddr < 0x04900000u) { *value = h64_mmio_read(sys, paddr); return 0; }
    if (paddr < 0x05000000u) { *value = 0; return 0; }
    if (paddr < 0x10000000u)
    {
        sys->pi.regs[1] = (paddr + 4) & ~1u;
        if (paddr < 0x08000000u || !h64_save_read32(sys->save, paddr, value)) *value = open_bus(paddr);
        return 0;
    }
    if (paddr < 0x1FC00000u)
    {
        u32 off = paddr - 0x10000000u;
        if (paddr >= ISV_BASE && paddr < ISV_END && isv_on(sys))
        {
            if (paddr == ISV_BASE) *value = 0x49533634u;
            else if (paddr >= ISV_BASE + 0x20 && paddr < ISV_BASE + 0x20 + sizeof(sys->isvBuffer))
                *value = h64_load_be32(sys->isvBuffer + (paddr - ISV_BASE - 0x20));
            else *value = 0;
            return 0;
        }
        sys->pi.regs[1] = (paddr + 4) & ~1u;
        if (sys->cpu.cycles < sys->pi.latchUntil)
        {
            *value = sys->pi.latch;
            sys->pi.latchUntil = 0;
            return 0;
        }
        *value = off < sys->rom.size ? h64_load_be32(sys->rom.data + off) : open_bus(paddr);
        return 0;
    }
    if (paddr < 0x1FC007C0u) { *value = 0; return 0; }
    if (paddr < 0x1FC00800u) { *value = h64_load_be32(sys->pifRam + (paddr - 0x1FC007C0u)); return 0; }
    *value = 0;
    return 0;
}

int h64_bus_write32(H64System *sys, u32 paddr, u32 value, u32 mask)
{
    paddr &= ~3u;
    if (paddr - 0x03F00000u < 0x00A00000u) h64_rsp_sync(sys);
    if (paddr < 0x03F00000u)
    {
        if (paddr < H64_RDRAM_SIZE) h64_jit_notify_write(sys, (sys->mi.mode & 0x80) ? paddr & ~0x7FFu : paddr, (sys->mi.mode & 0x80) ? 0x800 : 4);
        if (paddr < H64_RDRAM_SIZE && (sys->mi.mode & 0x80) && mask == 0xFFFFFFFFu)
            rdram_repeat(sys, paddr, repeat32(value));
        else if (paddr < H64_RDRAM_SIZE)
        {
            u32 old = h64_rdram_load32(sys->rdram, paddr);
            h64_rdram_store32(sys->rdram, paddr, (old & ~mask) | (value & mask));
        }
        return 0;
    }
    if (paddr < 0x04000000u) { h64_mmio_write(sys, paddr, value, mask); return 0; }
    if (paddr < 0x04040000u)
    {
        u8 *p = sys->spMem + (paddr & 0x1FFFu);
        u32 old = h64_load_be32(p);
        h64_store_be32(p, (old & ~mask) | (value & mask));
        return 0;
    }
    if (paddr < 0x04900000u) { h64_mmio_write(sys, paddr, value, mask); return 0; }
    if (paddr < 0x10000000u)
    {
        if (paddr >= 0x08000000u) h64_save_write32(sys->save, paddr, value & mask);
        return 0;
    }
    if (paddr < 0x1FC00000u)
    {
        if (paddr >= ISV_BASE && paddr < ISV_END && isv_on(sys))
        {
            if (paddr == ISV_BASE + 0x14)
                isv_flush(sys, value & mask);
            else if (paddr >= ISV_BASE + 0x20 && paddr < ISV_BASE + 0x20 + sizeof(sys->isvBuffer))
            {
                u8 *p = sys->isvBuffer + (paddr - ISV_BASE - 0x20);
                u32 old = h64_load_be32(p);
                h64_store_be32(p, (old & ~mask) | (value & mask));
            }
        }
        if (sys->cpu.cycles >= sys->pi.latchUntil)
        {
            sys->pi.regs[1] = (paddr + 4) & ~1u;
            sys->pi.latch = value;
            sys->pi.latchUntil = sys->cpu.cycles + 250;
        }
        return 0;
    }
    if (paddr < 0x1FC007C0u) return 0;
    if (paddr < 0x1FC00800u)
    {
        u32 off = paddr - 0x1FC007C0u;
        u32 old = h64_load_be32(sys->pifRam + off);
        h64_store_be32(sys->pifRam + off, (old & ~mask) | (value & mask));
        h64_pif_write_byte_hook(sys, off);
        return 0;
    }
    return 0;
}

static int cart_sub_read(H64System *sys, u32 paddr, u32 *w)
{
    u32 off = (paddr & ~1u) - 0x10000000u;
    if (sys->cpu.cycles < sys->pi.latchUntil || off + 4 > sys->rom.size || (paddr >= ISV_BASE && paddr < ISV_END))
        return h64_bus_read32(sys, paddr, w);
    *w = h64_load_be32(sys->rom.data + off);
    sys->pi.regs[1] = (paddr + 4) & ~1u;
    return 0;
}

int h64_bus_read8(H64System *sys, u32 paddr, u8 *value)
{
    u32 w;
    if (paddr < H64_RDRAM_SIZE) { *value = sys->ri.corrupt && h64_rdram_read_lost(sys, paddr) ? 0 : h64_rdram_load8(sys->rdram, paddr); return 0; }
    if (paddr >= 0x10000000u && paddr < 0x1FC00000u)
    {
        cart_sub_read(sys, paddr, &w);
        *value = (u8)(w >> (8 * (3 - (paddr & 3))));
        return 0;
    }
    if (h64_bus_read32(sys, paddr, &w)) return -1;
    *value = (u8)(w >> (8 * (3 - (paddr & 3))));
    return 0;
}

int h64_bus_read16(H64System *sys, u32 paddr, u16 *value)
{
    u32 w;
    if (paddr < H64_RDRAM_SIZE) { *value = sys->ri.corrupt && h64_rdram_read_lost(sys, paddr) ? 0 : h64_rdram_load16(sys->rdram, paddr); return 0; }
    if (paddr >= 0x10000000u && paddr < 0x1FC00000u)
    {
        cart_sub_read(sys, paddr, &w);
        *value = (u16)(w >> (8 * (2 - (paddr & 2))));
        return 0;
    }
    if (h64_bus_read32(sys, paddr, &w)) return -1;
    *value = (u16)(w >> (8 * (2 - (paddr & 2))));
    return 0;
}

int h64_bus_read64(H64System *sys, u32 paddr, u64 *value)
{
    u32 hi, lo;
    if (paddr < H64_RDRAM_SIZE - 7) { *value = sys->ri.corrupt && h64_rdram_read_lost(sys, paddr) ? 0 : h64_rdram_load64(sys->rdram, paddr); return 0; }
    if (h64_bus_read32(sys, paddr, &hi) || h64_bus_read32(sys, paddr + 4, &lo)) return -1;
    *value = ((u64)hi << 32) | lo;
    return 0;
}

static void rdram_repeat(H64System *sys, u32 paddr, u64 data)
{
    u32 base = paddr & ~7u, end = base + (sys->mi.mode & 0x7F) + 1, a;
    sys->mi.mode &= ~0x80u;
    for (a = paddr; a < end; a++)
    {
        u32 b = (base & ~0x7FFu) | (a & 0x7FFu);
        if (b < H64_RDRAM_SIZE) h64_rdram_store8(sys->rdram, b, (u8)(data >> (8 * (7 - (a & 7)))));
    }
}

static u64 repeat32(u32 v) { return ((u64)v << 32) | v; }

int h64_bus_write8(H64System *sys, u32 paddr, u32 regValue)
{
    if (paddr < H64_RDRAM_SIZE) h64_jit_notify_write(sys, (sys->mi.mode & 0x80) ? paddr & ~0x7FFu : paddr, (sys->mi.mode & 0x80) ? 0x800 : 1);
    u32 shift = 8 * (3 - (paddr & 3));
    if (paddr < H64_RDRAM_SIZE && (sys->mi.mode & 0x80)) { rdram_repeat(sys, paddr, repeat32(regValue << shift)); return 0; }
    if (paddr < H64_RDRAM_SIZE) { h64_rdram_store8(sys->rdram, paddr, (u8)regValue); return 0; }
    return h64_bus_write32(sys, paddr, regValue << shift, 0xFFFFFFFFu);
}

int h64_bus_write16(H64System *sys, u32 paddr, u32 regValue)
{
    if (paddr < H64_RDRAM_SIZE) h64_jit_notify_write(sys, (sys->mi.mode & 0x80) ? paddr & ~0x7FFu : paddr, (sys->mi.mode & 0x80) ? 0x800 : 2);
    u32 shift = 8 * (2 - (paddr & 2));
    if (paddr < H64_RDRAM_SIZE && (sys->mi.mode & 0x80)) { rdram_repeat(sys, paddr, repeat32(regValue << shift)); return 0; }
    if (paddr < H64_RDRAM_SIZE) { h64_rdram_store16(sys->rdram, paddr, (u16)regValue); return 0; }
    return h64_bus_write32(sys, paddr, regValue << shift, 0xFFFFFFFFu);
}

int h64_bus_write64(H64System *sys, u32 paddr, u64 value)
{
    if (paddr < H64_RDRAM_SIZE) h64_jit_notify_write(sys, (sys->mi.mode & 0x80) ? paddr & ~0x7FFu : paddr, (sys->mi.mode & 0x80) ? 0x800 : 8);
    if (paddr < H64_RDRAM_SIZE && (sys->mi.mode & 0x80)) { rdram_repeat(sys, paddr, value); return 0; }
    if (paddr < H64_RDRAM_SIZE - 7) { h64_rdram_store64(sys->rdram, paddr, value); return 0; }
    return h64_bus_write32(sys, paddr, (u32)(value >> 32), 0xFFFFFFFFu);
}
