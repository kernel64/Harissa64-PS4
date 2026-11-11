// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the GPU and the TV.
//
// One Vulkan device (RADV on GNM, through Granite) serves ParaLLEl-RDP and
// the front end. Each frame is drawn into a 1920x1080 render target: the game
// picture (ps4_gpu_picture), then the front end (platform/ui, drawn through
// ps4_gpu_cmd), and the GPU copies it into one of two linear images that are
// VideoOut's framebuffers (VideoOut reads them at the GPU address, which on the
// PS4 is the CPU address vkMapMemory gives). The flip waits for the vertical
// blank.
#ifndef PS4_GPU_H
#define PS4_GPU_H

namespace Vulkan
{
class Device;
class CommandBuffer;
class ImageView;
}

#define PS4_TV_W 1920
#define PS4_TV_H 1080

int ps4_gpu_init(void);
void ps4_gpu_save_pipeline_cache(void);
void ps4_gpu_shutdown(void);
Vulkan::Device &ps4_gpu_device(void);

void ps4_gpu_begin_frame(void);
Vulkan::CommandBuffer &ps4_gpu_cmd(void);
int ps4_gpu_in_frame(void);
void ps4_gpu_picture(const Vulkan::ImageView &view, float x, float y, float w, float h);
struct Ps4PictureFx
{
    int smooth, sharpen, blur, screen;
    float n64Width, n64Lines, power, prevMix;
};
void ps4_gpu_picture_fx(const Vulkan::ImageView &view, const Vulkan::ImageView &prev, float x, float y, float w, float h,
                        const Ps4PictureFx *fx);
void ps4_gpu_end_frame(void);
void ps4_gpu_set_vsync(int on);
double ps4_gpu_flip_wait_ms(void);
#define PS4_BACKGROUND_PAUSE 0
void ps4_gpu_wait_foreground(void);
void ps4_gpu_flip_stats(unsigned *oneRefresh, unsigned *longer);

#endif
