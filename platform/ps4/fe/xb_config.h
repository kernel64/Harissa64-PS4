// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - front-end configuration (Xbox 360).
// PS4: copied from the Xbox 360 front end (platform/xbox360/xb_config.h, v2 at ac20c3d).
//
// Read in layers: defaults, game:\harissa64v2.ini (developer keys and
// scripted runs), then the menu's files: <drive>:\config\settings.ini (all
// games) and <drive>:\config\<game folder>.ini (one game's profile).
#ifndef XB_CONFIG_H
#define XB_CONFIG_H

#include "xb_compat.h"
#include "../../../core/common/h64_types.h"
#include "../../../core/pif/h64_input_script.h"
#include "../../../core/pif/h64_padmap.h"

struct Config
{
    char mode[32];
    char cpu[32];
    char rom[256];
    char lastRom[256];
    char romDir[256];
    int hle;
    int softRenderer;
    int xenosDebug;
    u32 pauseAt;
    H64InputScript input;
    u32 shots[64];
    int shotCount;
    u32 exitAfter;
    u32 stallMs;
    u32 trace, traceStep;
    int fpuFlags;
    int jitFpu;
    int regCache;
    int fpCache;
    int strict;
    int statusFast;
    int fullExits;
    int superblocks;
    int fastFpu;
    int smooth;
    int resScale;
    int texFilter;
    int sharpen;
    int blur;
    int screen;
    int brightness;
    int vsync;
    int deflicker;
#if XB_PS4
    int viAa;
    int viScale;
    int viDither;
    int resForce;
    u32 rescaleAt;
    u32 shotAt, shotN;
    int wide;
#endif
    int zoom;
    int offsetX, offsetY;
    int stretch;
    int hdTextures;
    char cheats[256];
    char hdPack[48];
    u32 hdBudget;
    char hdCatalog[256];
    int showFps;
    int logCpu;
    int gameFlags;
    int updateCheck;
    int artDownload;
    int browserDemo;
    char browserSel[128];
    int filesOpen;
    int artAll;
    int asyncGfx;
    int parseWorker;
    int asyncAudio;
    u32 audioCycles;
    u32 gfxCycles;
    int rdpSync;
    int gfxTiming;
    int lateParse;
    int cpi;
    int cpiSet;
    int audioMs;
    int wavDump;
    int stateSlot;
    int loadState;
    u32 saveStateAt;
    u32 menuAt;
    u32 aboutAt;
    u32 cheatsAt;
    int cheatOn;
    int achievements;
    u32 achAt;
    int netTest;
    u32 raToast;
    int noNet;
    int raTest;
    int hdPage;
    int controlsTest;
    int autoStart;
    int autoIndex;
    u32 browserAt;
    int xenia;
    char lang[8];
    char uiLang[8];
    int langPick;
    int langAuto;
    int rumble;
    H64PadMap padMap[4];
};

void ConfigDefaults(Config *c);
int ConfigParseFile(Config *c, const char *path);
int ConfigWriteMenuKeys(const Config *c, const char *path, int withLastRom);
int ConfigWriteLang(const char *path, const char *code);
int ConfigWriteKey(const char *path, const char *key, const char *value);

#endif
