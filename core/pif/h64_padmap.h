// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - controller remapping: a host gamepad (Xbox 360 layout) as
// an N64 controller.
//
// Portable (no platform headers) so that the unit tests check it. The Xbox
// front end copies XINPUT_GAMEPAD into H64PadInput; its button bits are
// XInput's (H64_XB_* below, checked against XINPUT_GAMEPAD_* in main.cpp).
//
// A mapping gives each N64 button a set of sources (any of them pressed
// presses it) and picks the stick that drives the analog stick. The default
// mapping is V1's: X, Y, LT and RT all give Z, the right stick gives the C
// buttons, the left stick the analog stick.
//
// Settings keys (xb_config.cpp): map_a=xa,xb ... (one per N64 control,
// sources by name, "none" for no source), map_stick=left|right|dpad|none
// (dpad: the D-pad moves the stick and presses no button), map_invert=none|x|y|xy,
// map_deadzone=N (0..32767 raw stick units, 7000 by default), map_range=N
// (percent of the N64 stick's usual 80, 100 by default); map=default resets
// the mapping first. Ports 2-4 use the same keys with a p2_ / p3_ / p4_ prefix.
#ifndef H64_PADMAP_H
#define H64_PADMAP_H

#include "../common/h64_types.h"

struct H64Pad;

#define H64_XB_DPAD_UP        0x0001
#define H64_XB_DPAD_DOWN      0x0002
#define H64_XB_DPAD_LEFT      0x0004
#define H64_XB_DPAD_RIGHT     0x0008
#define H64_XB_START          0x0010
#define H64_XB_BACK           0x0020
#define H64_XB_LEFT_THUMB     0x0040
#define H64_XB_RIGHT_THUMB    0x0080
#define H64_XB_LEFT_SHOULDER  0x0100
#define H64_XB_RIGHT_SHOULDER 0x0200
#define H64_XB_A              0x1000
#define H64_XB_B              0x2000
#define H64_XB_X              0x4000
#define H64_XB_Y              0x8000

struct H64PadInput
{
    u16 buttons;
    u8 lt, rt;
    s16 lx, ly, rx, ry;
};

enum
{
    H64_SRC_A = 0, H64_SRC_B, H64_SRC_X, H64_SRC_Y, H64_SRC_LB, H64_SRC_RB, H64_SRC_LT, H64_SRC_RT, H64_SRC_START,
    H64_SRC_DUP, H64_SRC_DDOWN, H64_SRC_DLEFT, H64_SRC_DRIGHT, H64_SRC_LTHUMB, H64_SRC_RTHUMB,
    H64_SRC_LUP, H64_SRC_LDOWN, H64_SRC_LLEFT, H64_SRC_LRIGHT, H64_SRC_RUP, H64_SRC_RDOWN, H64_SRC_RLEFT, H64_SRC_RRIGHT,
    H64_SRC_COUNT
};

enum
{
    H64_N64_A = 0, H64_N64_B, H64_N64_Z, H64_N64_START, H64_N64_DUP, H64_N64_DDOWN, H64_N64_DLEFT, H64_N64_DRIGHT,
    H64_N64_L, H64_N64_R, H64_N64_CUP, H64_N64_CDOWN, H64_N64_CLEFT, H64_N64_CRIGHT,
    H64_N64_COUNT
};

enum { H64_STICK_LEFT = 0, H64_STICK_RIGHT, H64_STICK_DPAD, H64_STICK_NONE, H64_STICK_COUNT };
enum { H64_INVERT_X = 1, H64_INVERT_Y = 2 };

#define H64_PAD_DEADZONE_DEFAULT 7000
#define H64_PAD_RANGE_DEFAULT 100
#define H64_PAD_TRIGGER_THRESHOLD 64
#define H64_PAD_STICK_THRESHOLD 16000

struct H64PadMap
{
    u32 src[H64_N64_COUNT];
    int stick;
    int deadzone;
    int range;
    int invert;
};

void h64_padmap_default(H64PadMap *m);
int h64_padmap_is_default(const H64PadMap *m);
u32 h64_padmap_sources(const H64PadInput *in);
void h64_padmap_apply(const H64PadMap *m, const H64PadInput *in, H64Pad *out);

const char *h64_padmap_source_name(int src);
const char *h64_padmap_source_label(int src);
const char *h64_padmap_n64_name(int n64);
const char *h64_padmap_n64_label(int n64);
u16 h64_padmap_n64_bit(int n64);
int h64_padmap_parse_sources(const char *text, u32 *mask);
void h64_padmap_format_sources(u32 mask, char *out, int size);
int h64_padmap_parse_key(H64PadMap *m, const char *key, const char *value);

#endif
