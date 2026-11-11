// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - sound.
//
// The AI buffers (big-endian 16-bit stereo at the game's DAC rate) go into a
// ring; a thread resamples them to the 48 kHz the PS4 plays and hands
// 256-frame blocks to sceAudioOutOutput, which blocks until the previous one
// plays. As on the Xbox, the queue paces the emulation: the main loop waits
// while more than ps4_audio_target_ms() are queued.
#ifndef PS4_AUDIO_H
#define PS4_AUDIO_H

#include "../../core/common/h64_types.h"

int ps4_audio_init(void);
void ps4_audio_shutdown(void);
void ps4_audio_sink(void *user, const u8 *samples, u32 len, u32 rate);
int ps4_audio_queued_ms(void);
int ps4_audio_target_ms(void);
void ps4_audio_set_target_ms(int ms);
#define PS4_AUDIO_DEFAULT_MS 120

#endif
