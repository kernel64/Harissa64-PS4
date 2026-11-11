// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - RSP task HLE, internal declarations.
//
// The audio task HLE is a port of mupen64plus-rsp-hle (GPL v2, commit
// 8a7a472, June 2026; see THIRD_PARTY.md). This header replaces its
// hle_internal.h, ucodes.h, memory.h, arithmetics.h, audio.h, common.h and
// hle_external.h.
//
// Memory model. Guest memory (DMEM, and RDRAM unless H64_RDRAM_SWAP puts it
// in host words: memory/h64_rdram.h) is a byte array in N64 order on every
// host, so guest accesses go through the endian helpers: dram_u16/u32
// and dmem_u16/u32 return small proxies that read and write big-endian
// values, and the bulk loads/stores convert element by element. The HLE's
// own buffers (alist_buffer, mp3_buffer) keep upstream's layout: native
// 32-bit words, with the HLE_S/HLE_S8/HLE_S16 address swizzles on
// little-endian hosts. Copies between guest memory and those buffers go
// through hle_dram_to_buffer / hle_buffer_to_dram, which apply the swizzle.
#ifndef H64_HLE_INTERNAL_H
#define H64_HLE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning(disable : 4244 4267 4305 4146 4310 4245 4389 4018 4800)
#endif

#include "../common/h64_types.h"
#include "../common/h64_endian.h"
#include "../memory/h64_rdram.h"
#include "h64_hle_alist.h"

#if H64_HOST_BIG_ENDIAN
#define HLE_S 0
#define HLE_S16 0
#define HLE_S8 0
#else
#define HLE_S 1
#define HLE_S16 2
#define HLE_S8 3
#endif

#define UNUSED(x)

struct H64System;

#define CACHED_UCODES_MAX_SIZE 16

typedef void (*ucode_func_t)(struct hle_t *hle);

struct ucode_info_t
{
    uint32_t uc_start;
    uint32_t uc_dstart;
    uint16_t uc_dsize;
    ucode_func_t uc_pfunc;
};

struct cached_ucodes_t
{
    struct ucode_info_t infos[CACHED_UCODES_MAX_SIZE];
    int count;
};

enum { N_SEGMENTS = 16 };
struct alist_audio_t
{
    uint32_t segments[N_SEGMENTS];
    uint16_t in, out, count;
    uint16_t dry_right, wet_left, wet_right;
    int16_t dry, wet;
    int16_t vol[2];
    int16_t target[2];
    int32_t rate[2];
    uint32_t loop;
    int16_t table[16 * 8];
};

struct alist_naudio_t
{
    int16_t dry, wet;
    int16_t vol[2];
    int16_t target[2];
    int32_t rate[2];
    uint32_t loop;
    int16_t table[16 * 8];
};

struct alist_nead_t
{
    uint16_t in, out, count;
    uint16_t env_values[3];
    uint16_t env_steps[3];
    uint32_t loop;
    int16_t table[16 * 8];
    uint16_t filter_count;
    uint32_t filter_lut_address[2];
};

struct hle_t
{
    unsigned char *dram;
    unsigned char *dmem;
    unsigned char *imem;
    H64System *sys;
    void *user_defined;

    unsigned int sp_status;
    int forwarded;
    u32 dirtyLo, dirtyHi;
    u32 dirtyRange[8][2];
    u32 dirtyCount;
    int streamFullSync;
    u8 *checkRam;
    u8 *checkMask;
    int checkPending;
    u32 checkLo, checkHi;
    u32 checkTasks, checkBad;
    u32 checkHist[5];
    ucode_func_t checkFunc;

    uint8_t alist_buffer[0x1000];
    struct alist_audio_t alist_audio;
    struct alist_naudio_t alist_naudio;
    struct alist_nead_t alist_nead;
    uint8_t mp3_buffer[0x1000];

    struct cached_ucodes_t cached_ucodes;
    struct H64Gfx *gfx;
    int gfxAsyncPending;
    int gfxPendingDraw;
    u64 gfxDrawAt;
    u8 gfxDrawHeader[64];
    struct HleGfxSlot
    {
        struct H64System *sys;
        struct H64GfxOut *out;
        int ran, fullSync, mustSync;
        u32 parseTicket, renderTicket;
        int parseOnSecond;
        int used;
    } gfxSlot[2];
    int gfxSlotNext;
    struct HleGfxSlot *gfxCur;
    struct HleGfxSlot *gfxLate;
    int gfxGuessValid, gfxGuessFullSync, gfxGuessWarned;
    int audioAsyncPending;
    ucode_func_t audioAsyncFunc;
};

#define SP_STATUS_HALT 0x1
#define SP_STATUS_BROKE 0x2
#define SP_STATUS_INTR_ON_BREAK 0x40
#define SP_STATUS_TASKDONE 0x200

void rsp_break(struct hle_t *hle, unsigned int setbits);

void HleWarnMessage(void *user_defined, const char *message, ...);
void HleVerboseMessage(void *user_defined, const char *message, ...);
int HleForwardTask(void *user_defined);

void alist_process_audio(struct hle_t *hle);
void alist_process_audio_ge(struct hle_t *hle);
void alist_process_audio_bc(struct hle_t *hle);
void alist_process_naudio(struct hle_t *hle);
void alist_process_naudio_bk(struct hle_t *hle);
void alist_process_naudio_dk(struct hle_t *hle);
void alist_process_naudio_mp3(struct hle_t *hle);
void alist_process_naudio_cbfd(struct hle_t *hle);
void alist_process_nead_mk(struct hle_t *hle);
void alist_process_nead_sfj(struct hle_t *hle);
void alist_process_nead_sf(struct hle_t *hle);
void alist_process_nead_fz(struct hle_t *hle);
void alist_process_nead_wrjb(struct hle_t *hle);
void alist_process_nead_ys(struct hle_t *hle);
void alist_process_nead_1080(struct hle_t *hle);
void alist_process_nead_oot(struct hle_t *hle);
void alist_process_nead_mm(struct hle_t *hle);
void alist_process_nead_mmb(struct hle_t *hle);
void alist_process_nead_ac(struct hle_t *hle);
void alist_process_nead_mats(struct hle_t *hle);
void alist_process_nead_efz(struct hle_t *hle);
void mp3_task(struct hle_t *hle, unsigned int index, uint32_t address);
void musyx_v1_task(struct hle_t *hle);
void jpeg_decode_PS0(struct hle_t *hle);
void jpeg_decode_PS(struct hle_t *hle);
void jpeg_decode_OB(struct hle_t *hle);
void musyx_v2_task(struct hle_t *hle);

enum
{
    TASK_TYPE = 0xfc0,
    TASK_FLAGS = 0xfc4,
    TASK_UCODE_BOOT = 0xfc8,
    TASK_UCODE_BOOT_SIZE = 0xfcc,
    TASK_UCODE = 0xfd0,
    TASK_UCODE_SIZE = 0xfd4,
    TASK_UCODE_DATA = 0xfd8,
    TASK_UCODE_DATA_SIZE = 0xfdc,
    TASK_DRAM_STACK = 0xfe0,
    TASK_DRAM_STACK_SIZE = 0xfe4,
    TASK_OUTPUT_BUFF = 0xfe8,
    TASK_OUTPUT_BUFF_SIZE = 0xfec,
    TASK_DATA_PTR = 0xff0,
    TASK_DATA_SIZE = 0xff4,
    TASK_YIELD_DATA_PTR = 0xff8,
    TASK_YIELD_DATA_SIZE = 0xffc
};

static inline unsigned int align(unsigned int x, unsigned amount)
{
    --amount;
    return (x + amount) & ~amount;
}

void hle_dram_dirty(struct hle_t *hle, u32 address, size_t count);
int h64_hle_re2_task(struct hle_t *hle, u32 ucode, u32 ucodeData);

struct HleRef16
{
    u8 *p;
    struct hle_t *h;
    u32 a;
    HleRef16() {}
    HleRef16(const HleRef16 &o) : p(o.p), h(o.h), a(o.a) {}
#if H64_RDRAM_XOR
    operator u16() const { return h ? h64_rdram_load16(h->dram, a) : h64_load_be16(p); }
    HleRef16 &operator=(u16 v)
    {
        if (h) { h64_rdram_store16(h->dram, a, v); hle_dram_dirty(h, a, 2); }
        else h64_store_be16(p, v);
        return *this;
    }
#else
    operator u16() const { return h64_load_be16(p); }
    HleRef16 &operator=(u16 v) { h64_store_be16(p, v); if (h) hle_dram_dirty(h, a, 2); return *this; }
#endif
    HleRef16 &operator=(const HleRef16 &o) { return *this = (u16)o; }
};
struct HleRef32
{
    u8 *p;
    struct hle_t *h;
    u32 a;
    HleRef32() {}
    HleRef32(const HleRef32 &o) : p(o.p), h(o.h), a(o.a) {}
#if H64_RDRAM_XOR
    operator u32() const { return h ? h64_rdram_load32(h->dram, a) : h64_load_be32(p); }
    HleRef32 &operator=(u32 v)
    {
        if (h) { h64_rdram_store32(h->dram, a, v); hle_dram_dirty(h, a, 4); }
        else h64_store_be32(p, v);
        return *this;
    }
#else
    operator u32() const { return h64_load_be32(p); }
    HleRef32 &operator=(u32 v) { h64_store_be32(p, v); if (h) hle_dram_dirty(h, a, 4); return *this; }
#endif
    HleRef32 &operator=(const HleRef32 &o) { return *this = (u32)o; }
};
struct HlePtr16
{
    u8 *p;
    struct hle_t *h;
    u32 a;
    HleRef16 operator*() const { HleRef16 r; r.p = p; r.h = h; r.a = a; return r; }
};
struct HlePtr32
{
    u8 *p;
    struct hle_t *h;
    u32 a;
    HleRef32 operator*() const { HleRef32 r; r.p = p; r.h = h; r.a = a; return r; }
};

static inline uint8_t *dmem_u8(struct hle_t *hle, uint16_t address) { return hle->dmem + (address & 0xfff); }
static inline HlePtr16 dmem_u16(struct hle_t *hle, uint16_t address)
{
    HlePtr16 r; r.a = address & 0xffe; r.p = hle->dmem + r.a; r.h = NULL; return r;
}
static inline HlePtr32 dmem_u32(struct hle_t *hle, uint16_t address)
{
    HlePtr32 r; r.a = address & 0xffc; r.p = hle->dmem + r.a; r.h = NULL; return r;
}
static inline const uint8_t *dram_u8(struct hle_t *hle, uint32_t address) { return hle->dram + ((address & 0x7fffff) ^ H64_RDRAM_S8); }
static inline HlePtr16 dram_u16(struct hle_t *hle, uint32_t address)
{
    HlePtr16 r; r.a = address & 0x7ffffe; r.p = hle->dram + r.a; r.h = hle; return r;
}
static inline HlePtr32 dram_u32(struct hle_t *hle, uint32_t address)
{
    HlePtr32 r; r.a = address & 0x7ffffc; r.p = hle->dram + r.a; r.h = hle; return r;
}

void load_u8(uint8_t *dst, const unsigned char *buffer, unsigned mask, unsigned address, size_t count);
void load_u16(uint16_t *dst, const unsigned char *buffer, unsigned mask, unsigned address, size_t count);
void load_u32(uint32_t *dst, const unsigned char *buffer, unsigned mask, unsigned address, size_t count);
void store_u8(unsigned char *buffer, unsigned mask, unsigned address, const uint8_t *src, size_t count);
void store_u16(unsigned char *buffer, unsigned mask, unsigned address, const uint16_t *src, size_t count);
void store_u32(unsigned char *buffer, unsigned mask, unsigned address, const uint32_t *src, size_t count);

#define HLE_DMEM_MASK 0xfffu
#define HLE_DRAM_MASK 0x7fffffu

static inline void dmem_load_u8(struct hle_t *hle, uint8_t *dst, uint16_t address, size_t count) { load_u8(dst, hle->dmem, HLE_DMEM_MASK, address, count); }
static inline void dmem_load_u16(struct hle_t *hle, uint16_t *dst, uint16_t address, size_t count) { load_u16(dst, hle->dmem, HLE_DMEM_MASK, address, count); }
static inline void dmem_load_u32(struct hle_t *hle, uint32_t *dst, uint16_t address, size_t count) { load_u32(dst, hle->dmem, HLE_DMEM_MASK, address, count); }
static inline void dmem_store_u8(struct hle_t *hle, const uint8_t *src, uint16_t address, size_t count) { store_u8(hle->dmem, HLE_DMEM_MASK, address, src, count); }
static inline void dmem_store_u16(struct hle_t *hle, const uint16_t *src, uint16_t address, size_t count) { store_u16(hle->dmem, HLE_DMEM_MASK, address, src, count); }
static inline void dmem_store_u32(struct hle_t *hle, const uint32_t *src, uint16_t address, size_t count) { store_u32(hle->dmem, HLE_DMEM_MASK, address, src, count); }
#if H64_RDRAM_XOR
static inline void dram_load_u8(struct hle_t *hle, uint8_t *dst, uint32_t address, size_t count)
{
    while (count != 0) { *(dst++) = h64_rdram_load8(hle->dram, address & HLE_DRAM_MASK); address += 1; --count; }
}
static inline void dram_load_u16(struct hle_t *hle, uint16_t *dst, uint32_t address, size_t count)
{
    while (count != 0) { *(dst++) = h64_rdram_load16(hle->dram, address & HLE_DRAM_MASK & ~1u); address += 2; --count; }
}
static inline void dram_load_u32(struct hle_t *hle, uint32_t *dst, uint32_t address, size_t count)
{
    while (count != 0) { *(dst++) = h64_rdram_load32(hle->dram, address & HLE_DRAM_MASK & ~3u); address += 4; --count; }
}
static inline void dram_store_u8(struct hle_t *hle, const uint8_t *src, uint32_t address, size_t count)
{
    u32 a = address;
    size_t n = count;
    while (n != 0) { h64_rdram_store8(hle->dram, a & HLE_DRAM_MASK, *(src++)); a += 1; --n; }
    hle_dram_dirty(hle, address & HLE_DRAM_MASK, count);
}
static inline void dram_store_u16(struct hle_t *hle, const uint16_t *src, uint32_t address, size_t count)
{
    u32 a = address;
    size_t n = count;
    while (n != 0) { h64_rdram_store16(hle->dram, a & HLE_DRAM_MASK & ~1u, *(src++)); a += 2; --n; }
    hle_dram_dirty(hle, address & HLE_DRAM_MASK, count * 2);
}
static inline void dram_store_u32(struct hle_t *hle, const uint32_t *src, uint32_t address, size_t count)
{
    u32 a = address;
    size_t n = count;
    while (n != 0) { h64_rdram_store32(hle->dram, a & HLE_DRAM_MASK & ~3u, *(src++)); a += 4; --n; }
    hle_dram_dirty(hle, address & HLE_DRAM_MASK, count * 4);
}
#else
static inline void dram_load_u8(struct hle_t *hle, uint8_t *dst, uint32_t address, size_t count) { load_u8(dst, hle->dram, HLE_DRAM_MASK, address, count); }
static inline void dram_load_u16(struct hle_t *hle, uint16_t *dst, uint32_t address, size_t count) { load_u16(dst, hle->dram, HLE_DRAM_MASK, address, count); }
static inline void dram_load_u32(struct hle_t *hle, uint32_t *dst, uint32_t address, size_t count) { load_u32(dst, hle->dram, HLE_DRAM_MASK, address, count); }
static inline void dram_store_u8(struct hle_t *hle, const uint8_t *src, uint32_t address, size_t count)
{
    store_u8(hle->dram, HLE_DRAM_MASK, address, src, count); hle_dram_dirty(hle, address & HLE_DRAM_MASK, count);
}
static inline void dram_store_u16(struct hle_t *hle, const uint16_t *src, uint32_t address, size_t count)
{
    store_u16(hle->dram, HLE_DRAM_MASK, address, src, count); hle_dram_dirty(hle, address & HLE_DRAM_MASK, count * 2);
}
static inline void dram_store_u32(struct hle_t *hle, const uint32_t *src, uint32_t address, size_t count)
{
    store_u32(hle->dram, HLE_DRAM_MASK, address, src, count); hle_dram_dirty(hle, address & HLE_DRAM_MASK, count * 4);
}
#endif

void hle_dram_to_buffer(struct hle_t *hle, uint8_t *buffer, unsigned offset, uint32_t address, size_t count);
void hle_buffer_to_dram(struct hle_t *hle, uint32_t address, const uint8_t *buffer, unsigned offset, size_t count);

static inline int16_t clamp_s16(int64_t x)
{
    x = (x < -32768) ? -32768 : x;
    x = (x > 32767) ? 32767 : x;
    return (int16_t)x;
}

static inline int32_t vmulf(int16_t x, int16_t y)
{
    return (((int32_t)(x)) * ((int32_t)(y)) + 0x4000) >> 15;
}

extern const int16_t RESAMPLE_LUT[64 * 4];

int32_t rdot(size_t n, const int16_t *x, const int16_t *y);

static inline int16_t adpcm_predict_sample(uint8_t byte, uint8_t mask, unsigned lshift, unsigned rshift)
{
    int16_t sample = (int16_t)((uint16_t)(byte & mask) << lshift);
    sample >>= rshift;
    return sample;
}

void adpcm_compute_residuals(int16_t *dst, const int16_t *src, const int16_t *cb_entry, const int16_t *last_samples,
                             size_t count);

#endif
