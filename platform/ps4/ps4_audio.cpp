// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - sound.
#include "ps4_audio.h"
#include "ps4_log.h"

#include <atomic>
#include <pthread.h>
#include <string.h>
#include <orbis/AudioOut.h>

namespace {

const u32 kOutRate = 48000;
const u32 kBlock = 256;
const u32 kRing = 1u << 16;
int s_targetMs = PS4_AUDIO_DEFAULT_MS;

s16 s_ring[kRing * 2];
std::atomic<u32> s_head{0}, s_tail{0};
std::atomic<u32> s_rate{32000};
std::atomic<bool> s_run{false};
int s_port = -1;
pthread_t s_thread;

u32 queued_frames() { return s_head.load(std::memory_order_acquire) - s_tail.load(std::memory_order_acquire); }

void *audio_thread(void *)
{
    static s16 out[kBlock * 2];
    double pos = 0.0;
    while (s_run.load())
    {
        u32 rate = s_rate.load();
        double fill = (double)queued_frames() * 1000.0 / rate;
        double target = s_targetMs;
        double adjust = 1.0 + 0.005 * ((fill - target) / target);
        if (adjust < 0.995) adjust = 0.995;
        if (adjust > 1.005) adjust = 1.005;
        double step = (double)rate / kOutRate * adjust;
        u32 tail = s_tail.load(std::memory_order_relaxed);
        u32 avail = s_head.load(std::memory_order_acquire) - tail;
        for (u32 i = 0; i < kBlock; i++)
        {
            if (avail < 2)
            {
                out[2 * i] = out[2 * i + 1] = 0;
                continue;
            }
            const s16 *a = &s_ring[(tail % kRing) * 2];
            const s16 *b = &s_ring[((tail + 1) % kRing) * 2];
            out[2 * i] = (s16)(a[0] + (b[0] - a[0]) * pos);
            out[2 * i + 1] = (s16)(a[1] + (b[1] - a[1]) * pos);
            pos += step;
            while (pos >= 1.0 && avail >= 2)
            {
                pos -= 1.0;
                tail++;
                avail--;
            }
        }
        s_tail.store(tail, std::memory_order_release);
        sceAudioOutOutput(s_port, out);
    }
    return nullptr;
}

}

int ps4_audio_init(void)
{
    if (s_port >= 0)
    {
        s_tail.store(s_head.load());
        return 0;
    }
    int rc = sceAudioOutInit();
    if (rc < 0 && rc != (int)0x8026000E  ) { ps4_log("[audio] sceAudioOutInit: %#x", rc); }
    s_port = sceAudioOutOpen(0xFF  , ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kBlock, kOutRate,
                             ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (s_port < 0) { ps4_log("[audio] sceAudioOutOpen: %#x", s_port); return -1; }
    s_tail.store(s_head.load());
    s_run = true;
    pthread_create(&s_thread, nullptr, audio_thread, nullptr);
    ps4_log("[audio] output at %u Hz, %u-frame blocks", kOutRate, kBlock);
    return 0;
}

void ps4_audio_shutdown(void)
{
    if (s_port < 0) return;
    s_tail.store(s_head.load());
}

void ps4_audio_sink(void *, const u8 *samples, u32 len, u32 rate)
{
    if (s_port < 0 || len < 4 || rate < 4000 || rate > 96000) return;
    s_rate.store(rate);
    u32 frames = len / 4;
    u32 head = s_head.load(std::memory_order_relaxed);
    if (queued_frames() + frames >= kRing) return;
    for (u32 i = 0; i < frames; i++)
    {
        const u8 *p = samples + 4 * i;
        s16 *d = &s_ring[((head + i) % kRing) * 2];
        d[0] = (s16)((p[0] << 8) | p[1]);
        d[1] = (s16)((p[2] << 8) | p[3]);
    }
    s_head.store(head + frames, std::memory_order_release);
}

int ps4_audio_queued_ms(void)
{
    if (s_port < 0) return -1;
    return (int)((u64)queued_frames() * 1000 / s_rate.load());
}

int ps4_audio_target_ms(void) { return s_targetMs; }

void ps4_audio_set_target_ms(int ms)
{
    s_targetMs = ms < 50 ? 50 : ms > 500 ? 500 : ms;
}
