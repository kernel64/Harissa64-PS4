// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - front-end menus (Xbox 360): a generic list panel, the ROM
// browser shown at start-up and the settings screen.
// PS4: copied from the Xbox 360 front end (platform/xbox360/xb_menu.h, v2 at ac20c3d).
#ifndef XB_MENU_H
#define XB_MENU_H

#include "xb_compat.h"

#include "xb_config.h"

#define UI_MENU_MAX 16

struct UiMenu
{
    char title[128];
    char label[UI_MENU_MAX][96];
    char value[UI_MENU_MAX][64];
    int count, sel;
    char footer[192];
    int page;
    IDirect3DTexture9 *(*icon)(int row);
};
enum { UI_PAGE_LIST = 0, UI_PAGE_CONTROLS };

void UiMenuClear(UiMenu *m, const char *title, const char *footer);
int UiMenuAdd(UiMenu *m, const char *label, const char *value);
void UiMenuDraw(const UiMenu *m);
void UiMenuNavigate(UiMenu *m, WORD down);

enum { SET_NONE = 0, SET_CHANGED, SET_BACK, SET_SAVE_ALL, SET_SAVE_GAME, SET_ART_ALL, SET_LANGUAGE };
void SettingsBuild(UiMenu *m, const Config *c, int perGame);
int SettingsInput(UiMenu *m, Config *c, int perGame, WORD down);
void SettingsSetLangFile(const char *path);
#if XB_PS4
void SettingsSetMaxRes(int maxRes);
void SettingsSetBaseSize(unsigned w, unsigned h);
void SettingsSetCurScale(int scale);
#endif

int LanguageScreen(IDirect3DDevice9 *dev, int initial, int firstStart);
void LanguageScreenAuto(int ms);

u8 *RomFileLoad(const char *path, u32 *size);

struct H64Rom;
u32 RomReadHeader(const char *path, H64Rom *rom);

int RomFolderName(const char *path, char *out, size_t size);

void AboutDraw(void);
void AboutScreen(IDirect3DDevice9 *dev);

int RomBrowser(IDirect3DDevice9 *dev, Config *c, const char *settingsPath, char *path, size_t pathSize);

#endif
