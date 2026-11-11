// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - RDRAM access layer.
//
// Every access to RDRAM (sys->rdram, or a pointer to the same layout such as
// an RDRAM snapshot) goes through these helpers, with an N64 physical address.
// The layout is chosen at build time:
//  - H64_RDRAM_SWAP 0 (default): N64 byte order, like the rest of guest
//    memory; the helpers are h64_load_be* / h64_store_be* at ram + addr.
//  - H64_RDRAM_SWAP 1: 32-bit words in host order (mupen64plus' layout),
//    which ParaLLEl-RDP reads and writes from the GPU on the PS4. The byte at
//    N64 address a is at a ^ 3, the halfword at a ^ 2, an aligned word at a.
// On a big-endian host both layouts are the same bytes, so SWAP 1 changes
// nothing there (H64_RDRAM_XOR stays 0).
//
// Save states, hashes and RAM dumps always use N64 byte order: convert with
// h64_rdram_read / h64_rdram_write.
#ifndef H64_RDRAM_H
#define H64_RDRAM_H

#include "../common/h64_endian.h"

#ifndef H64_RDRAM_SWAP
#define H64_RDRAM_SWAP 0
#endif

#if H64_RDRAM_SWAP && !H64_HOST_BIG_ENDIAN
#define H64_RDRAM_XOR 1
#else
#define H64_RDRAM_XOR 0
#endif

#if H64_RDRAM_XOR
#define H64_RDRAM_S8 3
#define H64_RDRAM_S16 2
#else
#define H64_RDRAM_S8 0
#define H64_RDRAM_S16 0
#endif

#if !H64_RDRAM_XOR

static H64_INLINE u8 h64_rdram_load8(const u8 *ram, u32 a) { return ram[a]; }
static H64_INLINE u16 h64_rdram_load16(const u8 *ram, u32 a) { return h64_load_be16(ram + a); }
static H64_INLINE u32 h64_rdram_load32(const u8 *ram, u32 a) { return h64_load_be32(ram + a); }
static H64_INLINE u64 h64_rdram_load64(const u8 *ram, u32 a) { return h64_load_be64(ram + a); }
static H64_INLINE void h64_rdram_store8(u8 *ram, u32 a, u8 v) { ram[a] = v; }
static H64_INLINE void h64_rdram_store16(u8 *ram, u32 a, u16 v) { h64_store_be16(ram + a, v); }
static H64_INLINE void h64_rdram_store32(u8 *ram, u32 a, u32 v) { h64_store_be32(ram + a, v); }
static H64_INLINE void h64_rdram_store64(u8 *ram, u32 a, u64 v) { h64_store_be64(ram + a, v); }

static H64_INLINE void h64_rdram_read(const u8 *ram, u32 a, u8 *dst, u32 len) { memcpy(dst, ram + a, len); }
static H64_INLINE void h64_rdram_write(u8 *ram, u32 a, const u8 *src, u32 len) { memcpy(ram + a, src, len); }
static H64_INLINE void h64_rdram_move(u8 *ram, u32 dst, u32 src, u32 len) { memmove(ram + dst, ram + src, len); }
static H64_INLINE void h64_rdram_copy(u8 *dstRam, const u8 *srcRam, u32 a, u32 len) { memcpy(dstRam + a, srcRam + a, len); }

#else

static H64_INLINE u8 h64_rdram_load8(const u8 *ram, u32 a) { return ram[a ^ 3]; }
static H64_INLINE void h64_rdram_store8(u8 *ram, u32 a, u8 v) { ram[a ^ 3] = v; }

static H64_INLINE u16 h64_rdram_load16(const u8 *ram, u32 a)
{
    u16 v;
    if (a & 1) return (u16)((ram[a ^ 3] << 8) | ram[(a + 1) ^ 3]);
    memcpy(&v, ram + (a ^ 2), 2);
    return v;
}
static H64_INLINE void h64_rdram_store16(u8 *ram, u32 a, u16 v)
{
    if (a & 1) { ram[a ^ 3] = (u8)(v >> 8); ram[(a + 1) ^ 3] = (u8)v; return; }
    memcpy(ram + (a ^ 2), &v, 2);
}
static H64_INLINE u32 h64_rdram_load32(const u8 *ram, u32 a)
{
    u32 v;
    if (a & 3)
        return ((u32)ram[a ^ 3] << 24) | ((u32)ram[(a + 1) ^ 3] << 16) | ((u32)ram[(a + 2) ^ 3] << 8) | ram[(a + 3) ^ 3];
    memcpy(&v, ram + a, 4);
    return v;
}
static H64_INLINE void h64_rdram_store32(u8 *ram, u32 a, u32 v)
{
    if (a & 3)
    {
        ram[a ^ 3] = (u8)(v >> 24); ram[(a + 1) ^ 3] = (u8)(v >> 16);
        ram[(a + 2) ^ 3] = (u8)(v >> 8); ram[(a + 3) ^ 3] = (u8)v;
        return;
    }
    memcpy(ram + a, &v, 4);
}
static H64_INLINE u64 h64_rdram_load64(const u8 *ram, u32 a)
{
    return ((u64)h64_rdram_load32(ram, a) << 32) | h64_rdram_load32(ram, a + 4);
}
static H64_INLINE void h64_rdram_store64(u8 *ram, u32 a, u64 v)
{
    h64_rdram_store32(ram, a, (u32)(v >> 32));
    h64_rdram_store32(ram, a + 4, (u32)v);
}

static H64_INLINE void h64_rdram_read(const u8 *ram, u32 a, u8 *dst, u32 len)
{
    while (len && (a & 3)) { *dst++ = ram[a ^ 3]; a++; len--; }
    for (; len >= 4; a += 4, dst += 4, len -= 4)
    {
        u32 v; memcpy(&v, ram + a, 4);
        h64_store_be32(dst, v);
    }
    while (len) { *dst++ = ram[a ^ 3]; a++; len--; }
}
static H64_INLINE void h64_rdram_write(u8 *ram, u32 a, const u8 *src, u32 len)
{
    while (len && (a & 3)) { ram[a ^ 3] = *src++; a++; len--; }
    for (; len >= 4; a += 4, src += 4, len -= 4)
    {
        u32 v = h64_load_be32(src);
        memcpy(ram + a, &v, 4);
    }
    while (len) { ram[a ^ 3] = *src++; a++; len--; }
}
static H64_INLINE void h64_rdram_move(u8 *ram, u32 dst, u32 src, u32 len)
{
    u32 i;
    if (!((dst | src | len) & 3)) { memmove(ram + dst, ram + src, len); return; }
    if (dst <= src || dst >= src + len)
        for (i = 0; i < len; i++) ram[(dst + i) ^ 3] = ram[(src + i) ^ 3];
    else
        for (i = len; i-- > 0;) ram[(dst + i) ^ 3] = ram[(src + i) ^ 3];
}
static H64_INLINE void h64_rdram_copy(u8 *dstRam, const u8 *srcRam, u32 a, u32 len)
{
    u32 lo = a & ~3u, hi = (a + len + 3) & ~3u;
    memcpy(dstRam + lo, srcRam + lo, hi - lo);
}

#endif

#endif
