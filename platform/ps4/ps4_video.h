// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - graphics: ParaLLEl-RDP (Vulkan compute, through RADV on
// GNM) draws the RDP commands into the emulated RDRAM; its VI picture is drawn
// into the frame on the GPU (ps4_gpu).
#ifndef PS4_VIDEO_H
#define PS4_VIDEO_H

struct H64System;

unsigned char *ps4_video_create(int scale);
struct Ps4VideoOptions
{
    int smooth;
    int sharpen;
    int blur;
    int screen;
    int brightness;
    int vsync;
    int deflicker;
    int stretch;
    int zoom;
    int offsetX, offsetY;
    int viAa;
    int viScale;
    int viDither;
    int wide;
};
void ps4_video_set_options(const Ps4VideoOptions *o);
void ps4_video_set_sync(int on);
double ps4_video_sync_wait_ms(void);
unsigned long long ps4_video_culled(void);
void ps4_video_sizes(H64System *sys, unsigned *w, unsigned *h, unsigned *n64w, unsigned *n64h);
int ps4_video_shot(H64System *sys, const char *path);
int ps4_video_rescale(H64System *sys, int scale);
int ps4_video_scale(void);
void ps4_video_attach(H64System *sys);
void ps4_video_scanout(H64System *sys);
void ps4_video_draw(void);
void ps4_video_info(unsigned *w, unsigned *h);
void ps4_video_sync(void);
void ps4_video_rdram_written(void);
void ps4_video_frame(H64System *sys);
void ps4_video_shutdown(void);

#endif
