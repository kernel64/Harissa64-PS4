// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the GPU and the TV (see ps4_gpu.h).
//
// The VideoOut images are made the way DolphinPS4's probe-vk does (GPL v2+):
// linear R8G8B8A8 images in device-local, host-visible memory, bound at a
// 2 MiB aligned offset (VideoOut's rule) and registered with their mapped
// address. Granite wraps them (no view: they are only copy destinations).
#include "ps4_gpu.h"
#include "ps4_log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/SystemService.h>

#include "context.hpp"
#include "device.hpp"

#include "shaders/blit_vert.h"
#include "shaders/blit_frag.h"
#include "shaders/post_frag.h"

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

#define PIPELINE_CACHE_FILE PS4_DATA_DIR "/pipeline_cache.bin"

namespace {

struct Gpu
{
    Vulkan::Context ctx;
    Vulkan::Device device;
    Vulkan::ImageHandle target;
    Vulkan::ImageHandle tv[2];
    VkImage tvImage[2] = {};
    VkDeviceMemory tvMemory[2] = {};
    Vulkan::Program *blit = nullptr;
    Vulkan::Program *post = nullptr;
    int vsync = 1;
    double flipWaitMs = 0;
    uint64_t flipNum = 0, flipTime = 0;
    unsigned flipOne = 0, flipLong = 0;
    Vulkan::CommandBufferHandle cmd;
    int video = -1;
    int cur = 0;
    OrbisKernelEqueue flips = 0;
    bool flipPending[2] = {};
    bool inFrame = false;
    size_t cacheSaved = 0;
};
Gpu *s_gpu;

bool make_tv_images(Gpu &g)
{
    Vulkan::Device &dev = g.device;
    const VolkDeviceTable &t = dev.get_device_table();
    const VkPhysicalDeviceMemoryProperties &mem = dev.get_memory_properties();
    void *addresses[2];
    uint32_t pitch = PS4_TV_W;
    for (int i = 0; i < 2; i++)
    {
        VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {PS4_TV_W, PS4_TV_H, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_LINEAR;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (t.vkCreateImage(dev.get_device(), &info, nullptr, &g.tvImage[i]) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        t.vkGetImageMemoryRequirements(dev.get_device(), g.tvImage[i], &req);
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t type = UINT32_MAX;
        for (uint32_t m = 0; m < mem.memoryTypeCount && type == UINT32_MAX; m++)
            if ((req.memoryTypeBits & (1u << m)) && (mem.memoryTypes[m].propertyFlags & want) == want) type = m;
        if (type == UINT32_MAX) { ps4_log("[gpu] no memory type for the TV images"); return false; }
        const VkDeviceSize slack = 2 << 20;
        VkMemoryAllocateInfo ainfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ainfo.allocationSize = req.size + slack;
        ainfo.memoryTypeIndex = type;
        if (t.vkAllocateMemory(dev.get_device(), &ainfo, nullptr, &g.tvMemory[i]) != VK_SUCCESS) return false;
        uint8_t *base = nullptr;
        if (t.vkMapMemory(dev.get_device(), g.tvMemory[i], 0, VK_WHOLE_SIZE, 0, (void **)&base) != VK_SUCCESS) return false;
        VkDeviceSize offset = ((uintptr_t)base + slack - 1) / slack * slack - (uintptr_t)base;
        offset = (offset + req.alignment - 1) / req.alignment * req.alignment;
        if (t.vkBindImageMemory(dev.get_device(), g.tvImage[i], g.tvMemory[i], offset) != VK_SUCCESS) return false;
        VkImageSubresource sr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        VkSubresourceLayout layout;
        t.vkGetImageSubresourceLayout(dev.get_device(), g.tvImage[i], &sr, &layout);
        addresses[i] = base + offset + layout.offset;
        pitch = (uint32_t)(layout.rowPitch / 4);
        memset(addresses[i], 0, (size_t)layout.rowPitch * PS4_TV_H);

        Vulkan::ImageCreateInfo ginfo = {};
        ginfo.width = PS4_TV_W;
        ginfo.height = PS4_TV_H;
        ginfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        ginfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ginfo.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        g.tv[i] = dev.wrap_image(ginfo, g.tvImage[i]);
    }
    g.video = sceVideoOutOpen(0xFF, 0, 0, nullptr);
    if (g.video < 0) { ps4_log("[gpu] sceVideoOutOpen: %#x", g.video); return false; }
    OrbisVideoOutBufferAttribute attr;
    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0,
                                  PS4_TV_W, PS4_TV_H, pitch);
    int rc = sceVideoOutRegisterBuffers(g.video, 0, addresses, 2, &attr);
    if (rc < 0) { ps4_log("[gpu] sceVideoOutRegisterBuffers: %#x", rc); return false; }
    sceKernelCreateEqueue(&g.flips, "flip");
    sceVideoOutAddFlipEvent(g.flips, g.video, nullptr);
    ps4_log("[gpu] TV framebuffers at %p and %p, pitch %u", addresses[0], addresses[1], pitch);
    return true;
}

Vulkan::Program *make_blit(Vulkan::Device &dev)
{
    Vulkan::ResourceLayout vl = {}, fl = {};
    vl.push_constant_size = 32;
    fl.sets[0].sampled_image_mask = 1;
    fl.sets[0].fp_mask = 1;
    fl.output_mask = 1;
    Vulkan::Shader *vs = dev.request_shader(blit_vert, sizeof(blit_vert), &vl);
    Vulkan::Shader *fs = dev.request_shader(blit_frag, sizeof(blit_frag), &fl);
    return vs && fs ? dev.request_program(vs, fs) : nullptr;
}

Vulkan::Program *make_post(Vulkan::Device &dev)
{
    Vulkan::ResourceLayout vl = {}, fl = {};
    vl.push_constant_size = 32;
    fl.push_constant_size = 80;
    fl.sets[0].sampled_image_mask = 3;
    fl.sets[0].fp_mask = 3;
    fl.output_mask = 1;
    Vulkan::Shader *vs = dev.request_shader(blit_vert, sizeof(blit_vert), &vl);
    Vulkan::Shader *fs = dev.request_shader(post_frag, sizeof(post_frag), &fl);
    return vs && fs ? dev.request_program(vs, fs) : nullptr;
}

void wait_flip(Gpu &g, int i)
{
    double t0 = g.flipPending[i] ? ps4_now_ms() : 0;
    struct Timer
    {
        Gpu &g; double t0;
        ~Timer() { if (t0 != 0) g.flipWaitMs += ps4_now_ms() - t0; }
    } timer = {g, t0};
    while (g.flipPending[i])
    {
        OrbisKernelEvent ev;
        int count = 0;
        sceKernelWaitEqueue(g.flips, &ev, 1, &count, nullptr);
        g.flipPending[0] = g.flipPending[1] = false;
    }
}

}

int ps4_gpu_init(void)
{
    s_gpu = new Gpu;
    Gpu &g = *s_gpu;
    if (!Vulkan::Context::init_loader((PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr))
    {
        ps4_log("[gpu] Vulkan loader init failed");
        return -1;
    }
    Vulkan::Context::SystemHandles handles = {};
    g.ctx.set_system_handles(handles);
    if (!g.ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
    {
        ps4_log("[gpu] Vulkan instance/device creation failed");
        return -1;
    }
    g.device.set_context(g.ctx);
    {
        std::vector<uint8_t> blob;
        if (FILE *f = fopen(PIPELINE_CACHE_FILE, "rb"))
        {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (n > 0 && n < (64 << 20))
            {
                blob.resize((size_t)n);
                if (fread(blob.data(), 1, blob.size(), f) != blob.size()) blob.clear();
            }
            fclose(f);
        }
        bool ok = g.device.init_pipeline_cache(blob.empty() ? nullptr : blob.data(), blob.size());
        g.cacheSaved = ok ? g.device.get_pipeline_cache_size() : 0;
        ps4_log("[gpu] pipeline cache: %u KB read from the disk, %u KB kept by the driver%s", (unsigned)(blob.size() >> 10),
                (unsigned)(g.cacheSaved >> 10), ok ? "" : " (creation failed)");
    }
    ps4_log("[gpu] %s", g.device.get_gpu_properties().deviceName);
    Vulkan::ImageCreateInfo rt = Vulkan::ImageCreateInfo::render_target(PS4_TV_W, PS4_TV_H, VK_FORMAT_R8G8B8A8_UNORM);
    rt.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g.target = g.device.create_image(rt);
    g.blit = make_blit(g.device);
    g.post = make_post(g.device);
    if (!g.target || !g.blit || !g.post) { ps4_log("[gpu] render target or blit program failed"); return -1; }
    return make_tv_images(g) ? 0 : -1;
}

void ps4_gpu_save_pipeline_cache(void)
{
    if (!s_gpu) return;
    Gpu &g = *s_gpu;
    double t0 = ps4_now_ms();
    size_t size = g.device.get_pipeline_cache_size();
    if (!size || size == g.cacheSaved) return;
    std::vector<uint8_t> blob(size);
    if (!g.device.get_pipeline_cache_data(blob.data(), size)) return;
    FILE *f = fopen(PIPELINE_CACHE_FILE ".tmp", "wb");
    if (!f) { ps4_log("[gpu] pipeline cache: cannot write " PIPELINE_CACHE_FILE ".tmp"); return; }
    bool ok = fwrite(blob.data(), 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(PIPELINE_CACHE_FILE ".tmp", PIPELINE_CACHE_FILE) == 0;
    if (ok) g.cacheSaved = size;
    ps4_log("[gpu] pipeline cache: %u KB %s in %.0f ms", (unsigned)(size >> 10), ok ? "written" : "NOT written", ps4_now_ms() - t0);
}

void ps4_gpu_shutdown(void)
{
    if (!s_gpu) return;
    Gpu &g = *s_gpu;
    g.device.wait_idle();
    ps4_gpu_save_pipeline_cache();
    const VolkDeviceTable &t = g.device.get_device_table();
    for (int i = 0; i < 2; i++)
    {
        g.tv[i].reset();
        if (g.tvImage[i]) t.vkDestroyImage(g.device.get_device(), g.tvImage[i], nullptr);
        if (g.tvMemory[i]) t.vkFreeMemory(g.device.get_device(), g.tvMemory[i], nullptr);
    }
    g.target.reset();
    delete s_gpu;
    s_gpu = nullptr;
}

Vulkan::Device &ps4_gpu_device(void) { return s_gpu->device; }
Vulkan::CommandBuffer &ps4_gpu_cmd(void) { return *s_gpu->cmd; }

int ps4_gpu_in_frame(void) { return s_gpu && s_gpu->inFrame; }

void ps4_gpu_begin_frame(void)
{
    Gpu &g = *s_gpu;
    if (g.inFrame) return;
    g.inFrame = true;
    g.cmd = g.device.request_command_buffer();
    Vulkan::CommandBuffer &cmd = *g.cmd;
    cmd.image_barrier(*g.target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Vulkan::RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = &g.target->get_view();
    rp.clear_attachments = 1;
    rp.store_attachments = 1;
    rp.clear_color[0].float32[3] = 1.0f;
    cmd.begin_render_pass(rp);
}

void ps4_gpu_picture(const Vulkan::ImageView &view, float x, float y, float w, float h)
{
    Gpu &g = *s_gpu;
    if (!g.inFrame) return;
    Vulkan::CommandBuffer &cmd = *g.cmd;
    cmd.set_program(g.blit);
    cmd.set_opaque_state();
    cmd.set_depth_test(false, false);
    cmd.set_cull_mode(VK_CULL_MODE_NONE);
    cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    cmd.set_texture(0, 0, view, Vulkan::StockSampler::LinearClamp);
    struct { float rect[4], uv[4]; } push = {
        {x * 2.0f / PS4_TV_W - 1.0f, y * 2.0f / PS4_TV_H - 1.0f, (x + w) * 2.0f / PS4_TV_W - 1.0f,
         (y + h) * 2.0f / PS4_TV_H - 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f}};
    cmd.push_constants(&push, 0, sizeof(push));
    cmd.draw(4);
}

void ps4_gpu_picture_fx(const Vulkan::ImageView &view, const Vulkan::ImageView &prev, float x, float y, float w, float h,
                        const Ps4PictureFx *fx)
{
    Gpu &g = *s_gpu;
    if (!g.inFrame) return;
    Vulkan::CommandBuffer &cmd = *g.cmd;
    cmd.set_program(g.post);
    cmd.set_opaque_state();
    cmd.set_depth_test(false, false);
    cmd.set_cull_mode(VK_CULL_MODE_NONE);
    cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    cmd.set_texture(0, 0, view, Vulkan::StockSampler::LinearClamp);
    cmd.set_texture(0, 1, prev, Vulkan::StockSampler::LinearClamp);
    const float tw = (float)view.get_image().get_width(), th = (float)view.get_image().get_height();
    struct { float rect[4], uv[4], rcp[4], prm[4]; int32_t mode[4]; } push = {
        {x * 2.0f / PS4_TV_W - 1.0f, y * 2.0f / PS4_TV_H - 1.0f, (x + w) * 2.0f / PS4_TV_W - 1.0f,
         (y + h) * 2.0f / PS4_TV_H - 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {1.0f / tw, 1.0f / th, tw, th},
        {fx->n64Width, fx->n64Lines, fx->power, fx->prevMix},
        {fx->smooth, fx->sharpen, fx->blur, fx->screen}};
    cmd.push_constants(&push, 0, sizeof(push));
    cmd.draw(4);
}

void ps4_gpu_set_vsync(int on) { if (s_gpu) s_gpu->vsync = on ? 1 : 0; }
double ps4_gpu_flip_wait_ms(void) { return s_gpu ? s_gpu->flipWaitMs : 0; }

void ps4_gpu_end_frame(void)
{
    Gpu &g = *s_gpu;
    if (!g.inFrame) return;
    g.inFrame = false;
    Vulkan::CommandBuffer &cmd = *g.cmd;
    cmd.end_render_pass();
    wait_flip(g, g.cur);
    cmd.image_barrier(*g.target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    cmd.image_barrier(*g.tv[g.cur], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
    cmd.copy_image(*g.tv[g.cur], *g.target);
    cmd.image_barrier(*g.tv[g.cur], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
    Vulkan::Fence fence;
    g.device.submit(g.cmd, &fence);
    fence->wait();
    g.cmd.reset();
    g.device.next_frame_context();
    sceVideoOutSubmitFlip(g.video, g.cur, g.vsync ? 1   : 2  , 0);
    g.flipPending[g.cur] = true;
    g.cur ^= 1;
#if PS4_BACKGROUND_PAUSE
    ps4_gpu_wait_foreground();
#endif
    {
        OrbisVideoOutFlipStatus st;
        memset(&st, 0, sizeof(st));
        if (sceVideoOutGetFlipStatus(g.video, &st) == 0 && st.num != g.flipNum)
        {
            uint64_t n = st.num - g.flipNum, dt = st.ptime - g.flipTime;
            if (g.flipNum && n < 8 && dt / n < 25000) g.flipOne += (unsigned)n;
            else if (g.flipNum && n < 8) g.flipLong += (unsigned)n;
            g.flipNum = st.num;
            g.flipTime = st.ptime;
        }
    }
}

static bool in_background(void)
{
    unsigned char st[256];
    memset(st, 0, sizeof(st));
    typedef int32_t (*GetStatus)(void *);
    static int logged, seenFront;
    int rc = ((GetStatus)(void *)sceSystemServiceGetStatus)(st);
    if (!logged)
    {
        logged = 1;
        ps4_log("[app] system status: rc %#x, bytes %02X %02X %02X %02X  %02X %02X %02X %02X %02X", rc, st[0], st[1], st[2], st[3], st[4],
                st[5], st[6], st[7], st[8]);
    }
    if (rc != 0) return false;
    if (!st[5]) { seenFront = 1; return false; }
    return seenFront != 0;
}

void ps4_gpu_wait_foreground(void)
{
    if (!in_background()) return;
    double t0 = ps4_now_ms();
    ps4_log("[app] in the background: paused");
    while (in_background()) sceKernelUsleep(100000);
    ps4_log("[app] in front again after %.1f s", (ps4_now_ms() - t0) / 1000.0);
}

void ps4_gpu_flip_stats(unsigned *oneRefresh, unsigned *longer)
{
    *oneRefresh = *longer = 0;
    if (!s_gpu) return;
    *oneRefresh = s_gpu->flipOne;
    *longer = s_gpu->flipLong;
    s_gpu->flipOne = s_gpu->flipLong = 0;
}
