// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - graphics through ParaLLEl-RDP.
//
// The core hands every RDP command to the renderer (render/api.h); they go
// straight to ParaLLEl-RDP's command processor. The RDRAM is one buffer shared
// by the CPU and the GPU: ParaLLEl-RDP allocates it (host-visible, CPU-cached
// GPU memory) and the core uses its mapping as its RDRAM (H64Options.rdram), so
// nothing is copied either way. (The other way round, importing the core's
// memory with VK_EXT_external_memory_host, is advertised by RADV but the PS4
// winsys cannot do it; ParaLLEl-RDP then falls back to copying pages between
// two RDRAMs, which gave a black picture with the live core on the console.)
// At SYNC_FULL the CPU waits for the GPU, so a game that reads back what the
// RDP drew sees it. At each VI frame the VI registers go to ParaLLEl-RDP's VI
// and its picture (a GPU image) is drawn into the frame, 4:3 in the middle of
// the TV (ps4_gpu).
#include "ps4_video.h"
#include "ps4_gpu.h"
#include "ps4_log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "device.hpp"
#include "rdp_device.hpp"

#include "../../core/system/h64_system.h"
#include "../../core/rdp/h64_rdp.h"
#include "../../core/hle/h64_hle.h"
#include "../../render/api.h"

namespace {

struct Video
{
    RDP::CommandProcessor *gpu = nullptr;
    H64Renderer renderer = {};
    Vulkan::ImageHandle last;
    Vulkan::ImageHandle prev;
    int scale = 1;
    H64System *sys = nullptr;
    float n64Width = 320, n64Lines = 240;
};
Video *s_video;
Ps4VideoOptions s_opt = {1, 0, 0, 0, 0, 1, 0, 0, 100, 0, 0, 1, 1, 1, 0};
int s_sync = 1;
float s_scX0 = 0, s_scY0 = 0, s_scX1 = 4096, s_scY1 = 4096;
unsigned long long s_culled;

u64 s_state[64][2];
u8 s_stateLen[64];
u64 s_tileCmd[2][8];
u8 s_tileSet[2][8];
u32 s_cw = 320;
u32 s_cycle;

bool ws_wanted(const Video *v) { return s_opt.wide && v->sys && v->sys->options.hleGfx; }
bool ws_main(const Video *v)
{
    if (!ws_wanted(v)) return false;
    u32 viw = v->sys->vi.regs[2] & 0xFFF;
    return !viw || (float)s_cw >= (float)viw * 0.98f;
}
float ws_x(float px, float fbw) { return fbw * 0.5f + (px - fbw * 0.5f) * 0.75f; }
bool ws_wide(float w, float fbw) { return w >= fbw * 0.9f; }
u32 ws_fx(s32 x, bool whole)
{
    s32 c = (s32)s_cw * 2, r = c + ((x - c) * 3 + (x >= c ? 2 : -2)) / 4;
    if (whole) r = (r + 2) & ~3;
    return (u32)(r < 0 ? 0 : r > 0xFFF ? 0xFFF : r);
}

const u64 *ws_command(const Video *v, u32 op, const u64 *words, u64 *out)
{
    const float fbw = (float)s_cw;
    const bool border = v->sys->options.gfxWsBorder != 0;
    out[0] = words[0];
    out[1] = op == 0x24 || op == 0x25 ? words[1] : 0;
    if (op == 0x2D)
    {
        s32 lo = (s32)((words[0] >> 44) & 0xFFF), hi = (s32)((words[0] >> 12) & 0xFFF);
        if (ws_wide((hi - lo) * 0.25f, fbw) || (border && lo <= 64 && hi >= ((s32)s_cw - 16) * 4)) return words;
        out[0] = (words[0] & ~((0xFFFull << 44) | (0xFFFull << 12))) | ((u64)ws_fx(lo, false) << 44) | ((u64)ws_fx(hi, false) << 12);
        return out;
    }
    const bool whole = s_cycle >= 2;
    s32 l = (s32)((words[0] >> 12) & 0xFFF), r = (s32)((words[0] >> 44) & 0xFFF) + (whole ? 4 : 0);
    if (r <= l || ws_wide((r - l) * 0.25f, fbw) || (border && (l < 4 || r >= (s32)s_cw * 4))) return words;
    u32 nl, nr;
    if (op != 0x36 && s_cycle == 2)
    {
        s32 w = r - l, c = (s32)ws_fx((l + r) / 2, false) - w / 2;
        c = (c + 2) & ~3;
        if (c < 0) c = 0;
        nl = (u32)c;
        nr = (u32)(c + w);
    }
    else
    {
        nl = ws_fx(l, whole);
        nr = ws_fx(r, whole);
        if (op != 0x36)
        {
            const int sh = op == 0x25 ? 0 : 16;
            s32 d = (s16)(words[1] >> sh);
            d = d * 4 / 3;
            if (d > 32767) d = 32767;
            if (d < -32768) d = -32768;
            out[1] = (words[1] & ~(0xFFFFull << sh)) | ((u64)(u16)d << sh);
        }
    }
    if (whole) nr = nr >= 4 ? nr - 4 : 0;
    if (nr > 0xFFF) nr = 0xFFF;
    out[0] = (words[0] & ~((0xFFFull << 44) | (0xFFFull << 12))) | ((u64)nr << 44) | ((u64)nl << 12);
    return out;
}
double s_syncWaitMs;

void on_rdp(void *user, const u64 *words, u32 count)
{
    Video *v = (Video *)user;
    uint32_t w32[2 * 22];
    u64 ws[2];
    const u32 op = (u32)(words[0] >> 56) & 0x3F;
    if (op == 0x35 || op == 0x32)
    {
        const u32 t = (u32)(words[0] >> 24) & 7;
        s_tileCmd[op == 0x32][t] = words[0];
        s_tileSet[op == 0x32][t] = 1;
    }
    else if (count == 1 && ((op >= 0x2A && op <= 0x2F) || (op >= 0x37 && op <= 0x3F)))
    {
        s_state[op][0] = words[0];
        s_stateLen[op] = 1;
    }
    if (op == 0x3F) s_cw = (u32)((words[0] >> 32) & 0x3FF) + 1;
    else if (op == 0x2F) s_cycle = (u32)(words[0] >> 52) & 3;
    else if ((op == 0x2D || op == 0x36 || op == 0x24 || op == 0x25) && ws_main(v)) words = ws_command(v, op, words, ws);
    if (op == 0x2D)
    {
        s_scX0 = (float)((words[0] >> 44) & 0xFFF) * 0.25f;
        s_scY0 = (float)((words[0] >> 32) & 0xFFF) * 0.25f;
        s_scX1 = (float)((words[0] >> 12) & 0xFFF) * 0.25f;
        s_scY1 = (float)(words[0] & 0xFFF) * 0.25f;
    }
    for (u32 i = 0; i < count && i < 22; i++)
    {
        w32[2 * i] = (uint32_t)(words[i] >> 32);
        w32[2 * i + 1] = (uint32_t)words[i];
    }
    v->gpu->enqueue_command(2 * count, w32);
    if (s_sync && ((words[0] >> 56) & 0x3F) == 0x29)
    {
        double t0 = ps4_now_ms();
        v->gpu->wait_for_timeline(v->gpu->signal_timeline());
        s_syncWaitMs += ps4_now_ms() - t0;
    }
}

void on_triangle(void *user, const H64RenderVertex *a, const H64RenderVertex *b, const H64RenderVertex *c, u32 flags, u32 tile,
                 u32 levels)
{
    u64 words[22];
    H64RenderVertex wa, wb, wc;
    if (ws_main((const Video *)user))
    {
        const float fbw = (float)s_cw;
        float lo = a->x < b->x ? a->x : b->x, hi = a->x > b->x ? a->x : b->x;
        if (c->x < lo) lo = c->x;
        if (c->x > hi) hi = c->x;
        const bool flat = a->invw == 1.0f && b->invw == 1.0f && c->invw == 1.0f;
        if (!((lo > -0.6f && lo < 0.6f && hi > fbw - 0.6f && hi < fbw + 0.6f) || (flat && ws_wide(hi - lo, fbw))))
        {
            wa = *a; wb = *b; wc = *c;
            wa.x = ws_x(a->x, fbw);
            wb.x = ws_x(b->x, fbw);
            wc.x = ws_x(c->x, fbw);
            a = &wa; b = &wb; c = &wc;
        }
    }
    {
        float x0 = a->x < b->x ? a->x : b->x, x1 = a->x > b->x ? a->x : b->x;
        float y0 = a->y < b->y ? a->y : b->y, y1 = a->y > b->y ? a->y : b->y;
        if (c->x < x0) x0 = c->x;
        if (c->x > x1) x1 = c->x;
        if (c->y < y0) y0 = c->y;
        if (c->y > y1) y1 = c->y;
        if (x1 < s_scX0 - 1.0f || x0 > s_scX1 + 1.0f || y1 < s_scY0 - 1.0f || y0 > s_scY1 + 1.0f)
        {
            s_culled++;
            return;
        }
    }
    u32 n = h64_rdp_build_triangle(a, b, c, flags, tile, levels, words);
    if (n) on_rdp(user, words, n);
}

}

u8 *ps4_video_create(int scale)
{
    Vulkan::Device &device = ps4_gpu_device();
    s_video = new Video;
    Video &v = *s_video;
    double t0 = ps4_now_ms();
    RDP::CommandProcessorFlags flags = RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT;
    v.scale = scale >= 4 ? 4 : scale >= 2 ? 2 : 1;
    if (v.scale == 2) flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT;
    if (v.scale == 4) flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT;
    v.gpu = new RDP::CommandProcessor(device, nullptr, 0, H64_RDRAM_SIZE, H64_RDRAM_SIZE / 2, flags);
    s_scX0 = s_scY0 = 0;
    s_scX1 = s_scY1 = 4096;
    s_cw = 320;
    s_cycle = 0;
    memset(s_stateLen, 0, sizeof(s_stateLen));
    memset(s_tileSet, 0, sizeof(s_tileSet));
    if (!v.gpu->device_is_supported())
    {
        ps4_log("[video] ParaLLEl-RDP: this device is not supported");
        delete v.gpu;
        delete s_video;
        s_video = nullptr;
        return nullptr;
    }
    u8 *ram = (u8 *)v.gpu->begin_read_rdram();
    ps4_log("[video] ParaLLEl-RDP ready in %.0f ms, internal resolution x%d, shared RDRAM at %p (device type %d)", ps4_now_ms() - t0,
            v.scale, (void *)ram, (int)device.get_gpu_properties().deviceType);
    return ram;
}

int ps4_video_scale(void) { return s_video ? s_video->scale : 1; }

int ps4_video_rescale(H64System *sys, int scale)
{
    if (!s_video) return 0;
    Video &v = *s_video;
    scale = scale >= 4 ? 4 : scale >= 2 ? 2 : 1;
    if (scale == v.scale) return 1;
    double t0 = ps4_now_ms();
    const size_t hiddenSize = H64_RDRAM_SIZE / 2;
    u8 *keep = (u8 *)malloc(H64_RDRAM_SIZE + hiddenSize);
    if (!keep) return 0;
    v.gpu->wait_for_timeline(v.gpu->signal_timeline());
    memcpy(keep, v.gpu->begin_read_rdram(), H64_RDRAM_SIZE);
    memcpy(keep + H64_RDRAM_SIZE, v.gpu->begin_read_hidden_rdram(), hiddenSize);
    Vulkan::Device &device = ps4_gpu_device();
    RDP::CommandProcessorFlags flags = RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT;
    if (scale == 2) flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT;
    if (scale == 4) flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT;
    RDP::CommandProcessor *fresh = new RDP::CommandProcessor(device, nullptr, 0, H64_RDRAM_SIZE, H64_RDRAM_SIZE / 2, flags);
    if (!fresh->device_is_supported())
    {
        delete fresh;
        free(keep);
        ps4_log("[video] resolution x%d: the new RDP could not be made, x%d kept", scale, v.scale);
        return 0;
    }
    device.wait_idle();
    delete v.gpu;
    v.gpu = fresh;
    v.scale = scale;
    u8 *ram = (u8 *)v.gpu->begin_read_rdram();
    memcpy(ram, keep, H64_RDRAM_SIZE);
    v.gpu->end_write_rdram();
    memcpy(v.gpu->begin_read_hidden_rdram(), keep + H64_RDRAM_SIZE, hiddenSize);
    v.gpu->end_write_hidden_rdram();
    free(keep);
    sys->rdram = ram;
    sys->options.rdram = ram;
    h64_hle_rdram_moved(sys);
    for (u32 t = 0; t < 8; t++)
        for (u32 k = 0; k < 2; k++)
            if (s_tileSet[k][t])
            {
                uint32_t w[2] = {(uint32_t)(s_tileCmd[k][t] >> 32), (uint32_t)s_tileCmd[k][t]};
                v.gpu->enqueue_command(2, w);
            }
    static const u8 order[] = {0x2A, 0x2B, 0x2C, 0x2E, 0x2F, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x2D};
    for (u32 i = 0; i < sizeof(order); i++)
        if (s_stateLen[order[i]])
        {
            uint32_t w[2] = {(uint32_t)(s_state[order[i]][0] >> 32), (uint32_t)s_state[order[i]][0]};
            v.gpu->enqueue_command(2, w);
        }
    ps4_log("[video] internal resolution x%d now, in %.0f ms (RDRAM at %p)", scale, ps4_now_ms() - t0, (void *)ram);
    return 1;
}

void ps4_video_attach(H64System *sys)
{
    if (!s_video) return;
    Video &v = *s_video;
    v.sys = sys;
    sys->options.gfxKeepOffscreen = ws_wanted(&v);
    v.renderer.user = &v;
    v.renderer.rdp = on_rdp;
    v.renderer.triangle = on_triangle;
    v.renderer.reset = nullptr;
    sys->renderer = &v.renderer;
}

void ps4_video_scanout(H64System *sys)
{
    if (!s_video) return;
    Video &v = *s_video;
    for (unsigned r = 0; r < 14; r++) v.gpu->set_vi_register(RDP::VIRegister(r), sys->vi.regs[r]);
    RDP::ScanoutOptions opts = {};
    opts.vi.aa = s_opt.viAa != 0;
    opts.vi.divot_filter = s_opt.viAa != 0;
    opts.vi.scale = s_opt.viScale != 0;
    opts.vi.dither_filter = s_opt.viDither != 0;
    v.prev = s_opt.deflicker ? v.last : Vulkan::ImageHandle();
    v.last = v.gpu->scanout(opts);
    {
        u32 width = sys->vi.regs[2] & 0xFFF, vs = sys->vi.regs[10];
        u32 lines = ((vs & 0x3FF) - ((vs >> 16) & 0x3FF)) / 2;
        if (width) v.n64Width = (float)width;
        if (lines && lines <= 320) v.n64Lines = (float)lines;
    }
}

int ps4_video_shot(H64System *sys, const char *path)
{
    if (!s_video) return 0;
    Video &v = *s_video;
    std::vector<RDP::RGBA> px;
    unsigned w = 0, h = 0;
    for (unsigned r = 0; r < 14; r++) v.gpu->set_vi_register(RDP::VIRegister(r), sys->vi.regs[r]);
    RDP::ScanoutOptions opts = {};
    opts.vi.aa = s_opt.viAa != 0;
    opts.vi.divot_filter = s_opt.viAa != 0;
    opts.vi.scale = s_opt.viScale != 0;
    opts.vi.dither_filter = s_opt.viDither != 0;
    v.gpu->scanout_sync(px, w, h, opts);
    if (!w || !h) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const unsigned cw = w / 2, ch = h / 2, x0 = w / 4, y0 = h / 4;
    uint32_t head[2] = {cw, ch};
    fwrite(head, 4, 2, f);
    for (unsigned y = 0; y < ch; y++) fwrite(&px[(size_t)(y0 + y) * w + x0], 4, cw, f);
    fclose(f);
    return 1;
}

void ps4_video_set_sync(int on) { s_sync = on != 0; }
unsigned long long ps4_video_culled(void) { return s_culled; }
double ps4_video_sync_wait_ms(void) { return s_syncWaitMs; }

void ps4_video_set_options(const Ps4VideoOptions *o)
{
    s_opt = *o;
    if (s_video && s_video->sys) s_video->sys->options.gfxKeepOffscreen = ws_wanted(s_video);
    ps4_gpu_set_vsync(o->vsync != 0);
    if (!o->deflicker && s_video) s_video->prev.reset();
}

void ps4_video_draw(void)
{
    if (!s_video) return;
    Video &v = *s_video;
    if (!v.last) return;
    const float k = (s_opt.zoom >= 50 && s_opt.zoom <= 200 ? s_opt.zoom : 100) / 100.0f;
    const bool full = s_opt.stretch || ws_wanted(&v);
    const float h = PS4_TV_H * k, w = (full ? (float)PS4_TV_W : PS4_TV_H * 4.0f / 3.0f) * k;
    const float x = (PS4_TV_W - w) * 0.5f + s_opt.offsetX * (PS4_TV_W / 1280.0f);
    const float y = (PS4_TV_H - h) * 0.5f + s_opt.offsetY * (PS4_TV_H / 720.0f);
    static const float bright[3] = {1.0f, 0.8f, 0.65f};
    const int br = s_opt.brightness < 0 || s_opt.brightness > 2 ? 0 : s_opt.brightness;
    const bool crt = s_opt.screen == 2 || s_opt.screen == 3;
    Ps4PictureFx fx;
    fx.smooth = crt ? 0 : s_opt.smooth != 0;
    fx.sharpen = crt || s_opt.blur ? 0 : s_opt.sharpen;
    fx.blur = crt ? 0 : s_opt.blur;
    fx.screen = s_opt.screen < 0 || s_opt.screen > 5 ? 0 : s_opt.screen;
    fx.n64Width = v.n64Width;
    fx.n64Lines = v.n64Lines;
    fx.power = bright[br];
    fx.prevMix = s_opt.deflicker && v.prev ? 0.5f : 0.0f;
    if (!fx.smooth && !fx.sharpen && !fx.blur && !fx.screen && !br && fx.prevMix == 0.0f)
        ps4_gpu_picture(v.last->get_view(), x, y, w, h);
    else
        ps4_gpu_picture_fx(v.last->get_view(), fx.prevMix != 0.0f ? v.prev->get_view() : v.last->get_view(), x, y, w, h, &fx);
}

void ps4_video_sizes(H64System *sys, unsigned *w, unsigned *h, unsigned *n64w, unsigned *n64h)
{
    u32 vs = sys->vi.regs[10], ys = sys->vi.regs[13] & 0xFFF;
    *n64w = sys->vi.regs[2] & 0xFFF;
    *n64h = (((vs & 0x3FF) - ((vs >> 16) & 0x3FF)) / 2) * ys / 1024;
    *w = *n64w * (s_video ? s_video->scale : 1);
    *h = *n64h * (s_video ? s_video->scale : 1);
}

void ps4_video_info(unsigned *w, unsigned *h)
{
    *w = s_video && s_video->last ? s_video->last->get_width() : 0;
    *h = s_video && s_video->last ? s_video->last->get_height() : 0;
}

void ps4_video_sync(void)
{
    if (s_video) s_video->gpu->wait_for_timeline(s_video->gpu->signal_timeline());
}

void ps4_video_rdram_written(void)
{
    if (s_video) s_video->gpu->end_write_rdram();
}

void ps4_video_frame(H64System *sys)
{
    ps4_video_scanout(sys);
    ps4_gpu_begin_frame();
    ps4_video_draw();
    ps4_gpu_end_frame();
}

void ps4_video_shutdown(void)
{
    if (!s_video) return;
    ps4_gpu_device().wait_idle();
    s_video->last.reset();
    s_video->prev.reset();
    delete s_video->gpu;
    delete s_video;
    s_video = nullptr;
}
