// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 - RetroAchievements (retroachievements.org) on the console:
// rcheevos' rc_client (third_party/rcheevos) with our HTTPS calls, the
// account, the running game's achievements and what they give on the Xbox
// side (xb_ach.h).
//
// Off unless the player turns it on (Settings > Achievements). The player
// types the account's name and password on the Xbox keyboard; only the token
// the server answers with is kept (<drive>:\config\ra.ini). Softcore only:
// save states stay allowed, and nothing is checked once a cheat was on.
#ifndef XB_RA_H
#define XB_RA_H

#define XB_HAS_RA 1

#include "xb_ui.h"

struct H64System;

enum { XB_RA_OFF = 0, XB_RA_OUT, XB_RA_BUSY, XB_RA_IN };

void XbRaInit(const char *drive, int enabled, void (*toast)(const char *text, int ms, const char *badge));
void XbRaSetEnabled(int on);
int XbRaState(void);
const char *XbRaUser(void);
void XbRaAccount(void);
int XbRaBusy(void);
void XbRaPump(void);

void XbRaGameStart(H64System *sys);
void XbRaGameEnd(H64System *sys);
void XbRaReset(void);
void XbRaStateSaved(const char *statePath);
void XbRaStateLoaded(const char *statePath);

struct XbRaItem
{
    const char *title, *description;
    int points, unlocked;
    char badge[24];
};
int XbRaListBegin(void);
void XbRaListItem(int index, XbRaItem *item);
void XbRaListEnd(void);

void XbRaBadgesPump(IDirect3DDevice9 *dev);
IDirect3DTexture9 *XbRaBadge(const char *name);

void XbRaTestToast(void);

int XbRaSelfTest(void);

#endif
