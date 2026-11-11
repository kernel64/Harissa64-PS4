// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the test mode without Vulkan.
//
// When the Vulkan device cannot be created (a PS4 emulator on the PC such as
// shadPS4, which has no libSceGnmDriver module for RADV's winsys to load), the
// app still runs a game: the core's software RDP draws into RDRAM, the VI
// picture is copied by the CPU into two VideoOut buffers of direct memory. No
// front end (it draws with Vulkan): the game is the settings' lastrom, or
// roms/sm64.z64. Recompiler, HLE, sound, pad and saves are the real ones, so
// this checks the platform layer and the recompiler under the PS4's ABI; its
// speed says nothing about the console.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>

#include "../../core/system/h64_system.h"
#include "../../core/dynarec/h64_jit.h"
#include "../../core/system/h64_profile.h"
#include "../../core/common/h64_log.h"
#include "../../core/pif/h64_padmap.h"
#include "../../core/vi/h64_vi.h"

#include "ps4_audio.h"
#include "ps4_input.h"
#include "ps4_log.h"
#include "ps4_softtest.h"
#include "fe/xb_config.h"
#include "fe/xb_menu.h"

namespace {

const int kW = 1280, kH = 720;
int s_video = -1;
u32 *s_buf[2];
int s_cur;

bool video_open()
{
    const size_t size = (size_t)kW * kH * 4, align = 2u << 20, total = (2 * size + align - 1) & ~(align - 1);
    off_t off = 0;
    void *mem = nullptr;
    int rc = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(), total, align, 3  , &off);
    if (rc < 0) { ps4_log("[soft] sceKernelAllocateDirectMemory: %#x", rc); return false; }
    rc = sceKernelMapDirectMemory(&mem, total, 0x33  , 0, off, align);
    if (rc < 0) { ps4_log("[soft] sceKernelMapDirectMemory: %#x", rc); return false; }
    memset(mem, 0, total);
    s_buf[0] = (u32 *)mem;
    s_buf[1] = (u32 *)((u8 *)mem + size);
    s_video = sceVideoOutOpen(0xFF, 0, 0, nullptr);
    if (s_video < 0) { ps4_log("[soft] sceVideoOutOpen: %#x", s_video); return false; }
    OrbisVideoOutBufferAttribute attr;
    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, kW, kH, kW);
    void *addresses[2] = { s_buf[0], s_buf[1] };
    rc = sceVideoOutRegisterBuffers(s_video, 0, addresses, 2, &attr);
    if (rc < 0) { ps4_log("[soft] sceVideoOutRegisterBuffers: %#x", rc); return false; }
    return true;
}

void video_show(H64System *sys)
{
    static u8 rgb[640 * 480 * 3];
    int w = 0, h = 0;
    if (s_video < 0) return;
    if (h64_vi_render(sys, rgb, 640, 480, &w, &h) || w <= 0 || h <= 0) return;
    int k = kH / h < kW / w ? kH / h : kW / w;
    if (k < 1) k = 1;
    int x0 = (kW - w * k) / 2, y0 = (kH - h * k) / 2;
    u32 *dst = s_buf[s_cur];
    for (int y = 0; y < h * k && y0 + y < kH; y++)
    {
        const u8 *src = rgb + (size_t)(y / k) * w * 3;
        u32 *row = dst + (size_t)(y0 + y) * kW + x0;
        for (int x = 0; x < w * k && x0 + x < kW; x++)
        {
            const u8 *p = src + (x / k) * 3;
            row[x] = 0xFF000000u | ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
        }
    }
    while (sceVideoOutIsFlipPending(s_video) > 0) sceKernelUsleep(500);
    sceVideoOutSubmitFlip(s_video, s_cur, 1  , 0);
    s_cur ^= 1;
}

}

void ps4_softtest_run(void *jitMem, u32 jitBytes)
{
    static Config c;
    ConfigDefaults(&c);
    ConfigParseFile(&c, PS4_DATA_DIR "/harissa64.ini");
    const char *romPath = c.lastRom[0] ? c.lastRom : PS4_DATA_DIR "/roms/sm64.z64";
    ps4_log("[soft] no Vulkan: test mode with the software RDP and a CPU-drawn picture, game %s", romPath);
    u32 size = 0;
    u8 *data = RomFileLoad(romPath, &size);
    if (!data) { ps4_log("[soft] cannot read %s", romPath); return; }
    bool picture = video_open();
    H64Options opt;
    memset(&opt, 0, sizeof(opt));
    opt.hleBoot = 1;
    opt.emux = 1;
    opt.hleAudio = 1;
    opt.hleGfx = c.hle;
    H64System *sys = (H64System *)calloc(1, sizeof(H64System));
    if (!sys || h64_system_init(sys, data, size, &opt)) { ps4_log("[soft] not an N64 ROM"); return; }
    free(data);
    h64_apply_game_profile(sys);
    int jit = strcmp(c.cpu, "interp") != 0 && jitMem && !h64_jit_init(sys, jitMem, jitBytes, nullptr);
    if (jit && c.fastFpu)
    {
        sys->jit->fastFpu = 1;
        sys->cpu.fpuNoTraps = 1;
        h64_jit_reset(sys);
    }
    sys->cpu.cpi = c.cpi >= 1 && c.cpi <= 8 ? (u32)c.cpi : 2;
    if (sys->options.gameCpi && !c.cpiSet) sys->cpu.cpi = sys->options.gameCpi;
    ps4_input_init();
    if (ps4_audio_init() == 0) sys->aiSink = ps4_audio_sink;
    sys->padMask = 1;
    ps4_log("[soft] running: cpu %s%s, %u cycles an instruction, graphics %s, picture %s", jit ? "recompiler" : "interpreter",
            jit && c.fastFpu ? " (fast FPU)" : "", sys->cpu.cpi, opt.hleGfx ? "HLE" : "LLE", picture ? "on" : "off");

    u32 frame = sys->vi.frames, statFrames = 0;
    double statStart = ps4_now_ms(), emuMs = 0, showMs = 0, waitMs = 0;
    for (;;)
    {
        double t0 = ps4_now_ms();
        while (ps4_audio_queued_ms() > ps4_audio_target_ms()) sceKernelUsleep(1000);
        double t1 = ps4_now_ms();
        h64_system_run_cycles(sys, 200000);
        double t2 = ps4_now_ms();
        waitMs += t1 - t0;
        emuMs += t2 - t1;
        H64PadInput in;
        if (ps4_input_read(0, &in)) h64_padmap_apply(&c.padMap[0], &in, &sys->pad[0]);
        else memset(&sys->pad[0], 0, sizeof(sys->pad[0]));
        if (sys->vi.frames == frame) continue;
        frame = sys->vi.frames;
        video_show(sys);
        showMs += ps4_now_ms() - t2;
        statFrames++;
        double now = ps4_now_ms();
        if (now - statStart >= 5000)
        {
            ps4_log("[soft] %.1f VI/s (frame %u); of %.0f ms: emulation %.0f, picture %.0f, sound pacing %.0f; sound queue %d ms",
                    statFrames * 1000.0 / (now - statStart), sys->vi.frames, now - statStart, emuMs, showMs, waitMs,
                    ps4_audio_queued_ms());
            if (sys->jit)
                ps4_log("[soft] recompiler: %llu blocks entered, %llu interpreter steps, %llu blocks compiled, %u KB of code",
                        (unsigned long long)sys->jit->stats.blocksRun, (unsigned long long)sys->jit->stats.interpSteps,
                        (unsigned long long)sys->jit->stats.blocksCompiled, sys->jit->memUsed >> 10);
            statStart = now;
            statFrames = 0;
            emuMs = showMs = waitMs = 0;
        }
    }
}
