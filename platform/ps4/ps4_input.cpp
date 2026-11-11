// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - DualShock 4 input.
#include "ps4_input.h"
#include "ps4_log.h"

#include <string.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>

static int s_pad[4] = {-1, -1, -1, -1};
static int s_user[4] = {-1, -1, -1, -1};
static double s_scanAt;
static int s_rumble[4];
static double s_pulseEnd[4];

static void set_motors(int port, int on)
{
    OrbisPadVibeParam v;
    v.lgMotor = on ? 0xC0 : 0;
    v.smMotor = on ? 0x60 : 0;
    int rc = scePadSetVibration(s_pad[port], &v);
    static int logged;
    if (rc < 0 || logged < 12)
    {
        if (logged < 40) ps4_log("[input] rumble %s on controller %d: scePadSetVibration = %#x", on ? "on" : "off", port + 1, rc);
        logged++;
    }
    s_rumble[port] = on;
}

static void pulse_tick(int port)
{
    if (s_pulseEnd[port] != 0 && ps4_now_ms() >= s_pulseEnd[port])
    {
        s_pulseEnd[port] = 0;
        set_motors(port, 0);
    }
}

void ps4_input_rumble_pulse(int port, int ms)
{
    if (port < 0 || port > 3 || s_pad[port] < 0) return;
    s_pulseEnd[port] = ps4_now_ms() + ms;
    set_motors(port, 1);
}

int ps4_input_init(void)
{
    int rc = scePadInit();
    if (rc < 0) { ps4_log("[input] scePadInit: %#x", rc); return -1; }
    int user = 0;
    rc = sceUserServiceGetInitialUser(&user);
    if (rc < 0) { ps4_log("[input] no initial user: %#x", rc); return -1; }
    s_pad[0] = scePadOpen(user, 0, 0, NULL);
    s_user[0] = user;
    ps4_log("[input] user %#x, pad handle %#x", user, s_pad[0]);
    return s_pad[0] < 0 ? -1 : 0;
}

static void scan_users(void)
{
    OrbisUserServiceLoginUserIdList list;
    double now = ps4_now_ms();
    int i, p;
    if (now < s_scanAt) return;
    s_scanAt = now + 1000.0;
    for (i = 0; i < 4; i++) list.userId[i] = -1;
    if (sceUserServiceGetLoginUserIdList(&list) < 0) return;
    for (p = 1; p < 4; p++)
    {
        int here = 0;
        if (s_pad[p] < 0) continue;
        for (i = 0; i < 4; i++) if (list.userId[i] == s_user[p]) here = 1;
        if (!here)
        {
            ps4_log("[input] controller %d: user %#x logged out", p + 1, s_user[p]);
            scePadClose(s_pad[p]);
            s_pad[p] = -1;
            s_user[p] = -1;
            s_rumble[p] = 0;
            s_pulseEnd[p] = 0;
        }
    }
    for (i = 0; i < 4; i++)
    {
        int user = list.userId[i], known = 0, h;
        if (user < 0) continue;
        for (p = 0; p < 4; p++) if (s_user[p] == user) known = 1;
        if (known) continue;
        for (p = 1; p < 4 && s_pad[p] >= 0; p++) {}
        if (p == 4) break;
        h = scePadOpen(user, 0, 0, NULL);
        ps4_log("[input] controller %d: user %#x, pad handle %#x", p + 1, user, h);
        if (h < 0) continue;
        s_pad[p] = h;
        s_user[p] = user;
    }
}

static s16 axis(int v, int flip)
{
    int s = (v - 128) * 256;
    if (flip) s = -s;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return (s16)s;
}

void ps4_input_rumble(int port, int on)
{
    if (port < 0 || port > 3 || s_pad[port] < 0) return;
    pulse_tick(port);
    if (s_pulseEnd[port] != 0) return;
    on = on ? 1 : 0;
    if (on == s_rumble[port]) return;
    set_motors(port, on);
}

int ps4_input_read(int port, H64PadInput *out)
{
    memset(out, 0, sizeof(*out));
    if (port == 0) scan_users();
    if (port < 0 || port > 3 || s_pad[port] < 0) return 0;
    pulse_tick(port);
    OrbisPadData d;
    if (scePadReadState(s_pad[port], &d) < 0 || !d.connected) return 0;
    static const struct { u32 ps4; u16 xb; } map[] = {
        {ORBIS_PAD_BUTTON_UP, H64_XB_DPAD_UP},       {ORBIS_PAD_BUTTON_DOWN, H64_XB_DPAD_DOWN},
        {ORBIS_PAD_BUTTON_LEFT, H64_XB_DPAD_LEFT},   {ORBIS_PAD_BUTTON_RIGHT, H64_XB_DPAD_RIGHT},
        {ORBIS_PAD_BUTTON_OPTIONS, H64_XB_START},    {ORBIS_PAD_BUTTON_TOUCH_PAD, H64_XB_BACK},
        {ORBIS_PAD_BUTTON_L3, H64_XB_LEFT_THUMB},    {ORBIS_PAD_BUTTON_R3, H64_XB_RIGHT_THUMB},
        {ORBIS_PAD_BUTTON_L1, H64_XB_LEFT_SHOULDER}, {ORBIS_PAD_BUTTON_R1, H64_XB_RIGHT_SHOULDER},
        {ORBIS_PAD_BUTTON_CROSS, H64_XB_A},          {ORBIS_PAD_BUTTON_CIRCLE, H64_XB_B},
        {ORBIS_PAD_BUTTON_SQUARE, H64_XB_X},         {ORBIS_PAD_BUTTON_TRIANGLE, H64_XB_Y},
    };
    for (const auto &m : map)
        if (d.buttons & m.ps4) out->buttons |= m.xb;
    out->lt = d.analogButtons.l2;
    out->rt = d.analogButtons.r2;
    out->lx = axis(d.leftStick.x, 0);
    out->ly = axis(d.leftStick.y, 1);
    out->rx = axis(d.rightStick.x, 0);
    out->ry = axis(d.rightStick.y, 1);
    return 1;
}
