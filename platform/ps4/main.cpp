// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the app: start-up, the Harissa64 front end (language list
// on the first start, ROM browser, settings, files), then the chosen game on
// the interpreter with ParaLLEl-RDP for the graphics, the DualShock 4 as
// controller 1 and the sound; back to the game list after it.
//
// The front end is the Xbox 360 version's (platform/ps4/fe, copied files);
// this file follows the Xbox main.cpp (platform/xbox360/main.cpp, v2 at
// ac20c3d: ChooseLanguage, MessageScreen, RunGame, the browser loop of main)
// without its Xenos renderer, recompiler and workers.
//
// In a game, the touch pad button is the Xbox's BACK: a short press opens the
// menu (resume, save/load state, slot, settings, reset, game list, quit),
// held with R1/L1 it saves/loads a state, with the D-pad it changes the slot,
// held for 3 seconds it goes back to the game list.
#include <x86intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include <orbis/libkernel.h>
#include <orbis/UserService.h>

#include "../../core/system/h64_system.h"
#include "../../core/cheat/h64_cheat.h"
#include "../../core/dynarec/h64_jit.h"
#include "../../core/system/h64_profile.h"
#include "../../core/cart/h64_save.h"
#include "../../core/savestate/h64_state.h"
#include "../../core/common/h64_log.h"
#include "../../core/common/h64_version.h"
#include "../../core/pif/h64_padmap.h"
#include "../../core/vi/h64_vi.h"
#include "../../render/api.h"

#include "ps4_audio.h"
#include "ps4_gpu.h"
#include "ps4_input.h"
#include "ps4_log.h"
#include "ps4_video.h"
#include "ps4_softtest.h"
#include "fe/xb_config.h"
#include "fe/xb_drives.h"
#include "fe/xb_lang.h"
#include "fe/xb_menu.h"
#include "fe/xb_ach.h"
#include "fe/xb_ra.h"
#include "fe/xb_net.h"
#include "fe/xb_ui.h"

extern "C" {
int sceSystemServiceLoadExec(const char *path, char *const argv[]);
int sceSysmoduleLoadModuleInternal(uint32_t id);
}

static void quit(int code)
{
    ps4_log("[main] leaving (%d)", code);
    ps4_audio_shutdown();
    ps4_video_shutdown();
    ps4_gpu_shutdown();
    ps4_log_close();
    sceSystemServiceLoadExec("exit", nullptr);
    _exit(code);
}

#define SETTINGS_PATH PS4_DATA_DIR "/harissa64.ini"
#define CONFIG_DIR PS4_DATA_DIR "/config"
#define SAVES_DIR PS4_DATA_DIR "/saves"
static char s_settingsPath[160];
static char s_profilePath[192];

static void ChooseLanguage(IDirect3DDevice9 *dev, const Config *cfg, const char *settingsPath, int canAsk)
{
    static Config menu;
    int lang;
    if (cfg->uiLang[0])
    {
        lang = LangFromCode(cfg->uiLang);
        LangSet(lang < 0 ? LANG_EN : lang);
        H64_INFO("[lang] menus in %s (uilang=)", LangCode(LangGet()));
        return;
    }
    menu = *cfg;
    if (settingsPath[0]) ConfigParseFile(&menu, settingsPath);
    lang = LangFromCode(menu.lang);
    if ((lang >= 0 && !cfg->langPick) || !dev || (!canAsk && !cfg->langPick))
    {
        LangSet(lang >= 0 ? lang : LANG_EN);
        H64_INFO("[lang] menus in %s%s", LangCode(LangGet()), lang >= 0 ? "" : " (none chosen yet)");
        return;
    }
    if (cfg->langPick >= 2) lang = cfg->langPick - 2;
    LanguageScreenAuto(cfg->langAuto);
    lang = LanguageScreen(dev, lang >= 0 ? lang : LangFromConsole(), 1);
    LangSet(lang);
    if (settingsPath[0] && !ConfigWriteLang(settingsPath, LangCode(lang))) H64_WARN("[lang] cannot write %s", settingsPath);
    H64_INFO("[lang] menus in %s (chosen at start)", LangCode(lang));
}

static void MessageScreen(IDirect3DDevice9 *dev, const char *title, const char *text, D3DCOLOR color)
{
    for (;;)
    {
        XINPUT_STATE in;
        static char wrapped[2048];
        const float x = UI_SAFE_X, y = 140, w = UI_WIDTH - 2 * UI_SAFE_X, h = 470;
        dev->Clear(0, NULL, D3DCLEAR_TARGET, UI_COL_BG_BOTTOM, 1.0f, 0);
        UiBegin(dev);
        UiBackdrop();
        UiBrand(UI_SAFE_X, 72, 34);
        UiPanel(x, y, w, h, 22);
        UiRoundRect(x + 28, y + 34, 5, 40, 2.5f, color, color);
        UiText(UI_BOLD, 30, x + 52, y + 54, UI_COL_TEXT, title, UI_LEFT);
        UiTextWrap(UI_REGULAR, 21, w - 104, 12, text, wrapped, sizeof(wrapped));
        UiText(UI_REGULAR, 21, x + 52, y + 120, UI_COL_TEXT2, wrapped, UI_LEFT);
        UiScreenFooter(Tr(T_FOOT_CONTINUE));
        UiEnd();
        dev->Present(NULL, NULL, NULL, NULL);
        if (XInputGetState(0, &in) == ERROR_SUCCESS && (in.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)) break;
        Sleep(16);
    }
    for (;;)
    {
        XINPUT_STATE in;
        if (XInputGetState(0, &in) != ERROR_SUCCESS || !(in.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)) break;
        Sleep(16);
    }
}

static char s_osd[160];
static DWORD s_osdUntil;
#define OSD_MS 2000
static int s_osdLife = OSD_MS;
static char s_osdBadge[24];
static int s_showFps;
static char s_fpsText[160];

static void OsdT(int id, int num)
{
    char en[160];
    snprintf(s_osd, sizeof(s_osd), Tr(id), num);
    snprintf(en, sizeof(en), TrIn(LANG_EN, id), num);
    s_osdBadge[0] = 0;
    s_osdLife = OSD_MS;
    s_osdUntil = GetTickCount() + OSD_MS;
    H64_INFO("[main] %s", en);
}

static void ApplyGraphics(const Config *c)
{
    Ps4VideoOptions o;
    o.smooth = c->smooth;
    o.sharpen = c->sharpen;
    o.blur = c->blur;
    o.screen = c->screen;
    o.brightness = c->brightness;
    o.vsync = c->vsync;
    o.deflicker = c->deflicker;
    o.stretch = c->stretch;
    o.zoom = c->zoom;
    o.offsetX = c->offsetX;
    o.offsetY = c->offsetY;
    o.viAa = c->viAa;
    o.viScale = c->viScale;
    o.viDither = c->viDither;
    o.wide = c->wide;
    ps4_video_set_options(&o);
}

extern "C" int32_t h64_avail_direct(off_t start, off_t end, size_t align, off_t *at, size_t *size) __asm__("sceKernelAvailableDirectMemorySize");
extern "C" int32_t h64_avail_flexible(size_t *size) __asm__("sceKernelAvailableFlexibleMemorySize");
static u64 DirectFree(off_t lo, off_t hi, int depth)
{
    off_t at = 0;
    size_t size = 0;
    if (lo >= hi || depth > 12 || h64_avail_direct(lo, hi, 0, &at, &size) < 0 || !size) return 0;
    return size + DirectFree(lo, at, depth + 1) + DirectFree(at + (off_t)size, hi, depth + 1);
}
static void MemoryMb(u32 *used, u32 *total)
{
    static u64 flexTotal;
    u64 direct = sceKernelGetDirectMemorySize(), freeBytes = DirectFree(0, (off_t)direct, 0);
    size_t flex = 0;
    h64_avail_flexible(&flex);
    if (flex > flexTotal) flexTotal = flex;
    if (flexTotal < (448u << 20)) flexTotal = 448u << 20;
    *total = (u32)((direct + flexTotal) >> 20);
    *used = (u32)((direct + flexTotal - freeBytes - flex) >> 20);
}

static void OsdText(const char *text, int ms, const char *badge)
{
    strncpy(s_osdBadge, badge ? badge : "", sizeof(s_osdBadge) - 1);
    s_osdBadge[sizeof(s_osdBadge) - 1] = 0;
    strncpy(s_osd, text, sizeof(s_osd) - 1);
    s_osd[sizeof(s_osd) - 1] = 0;
    s_osdLife = ms;
    s_osdUntil = GetTickCount() + (DWORD)ms;
}

static UiMenu s_menu;
static int s_menuOpen;
static int s_aboutOpen;

static void DrawOverlay(void)
{
    s32 left = (s32)(s_osdUntil - GetTickCount());
    int fps = s_showFps && s_fpsText[0], osd = s_osd[0] && left >= 0;
    if (!fps && !osd && !s_menuOpen) return;
    if ((osd && s_osdBadge[0]) || (s_menuOpen && s_menu.icon)) XbRaBadgesPump(XbDevice());
    UiBegin(XbDevice());
    if (fps && s_showFps >= 2) UiPerfPanel(s_fpsText);
    else if (fps) UiFpsBadge(s_fpsText);
    if (s_menuOpen && s_aboutOpen) AboutDraw();
    else if (s_menuOpen) UiMenuDraw(&s_menu);
    if (osd && s_osdBadge[0]) UiToastBadge(s_osd, XbRaBadge(s_osdBadge), (float)(s_osdLife - left), (float)s_osdLife);
    else if (osd) UiToast(s_osd, (float)(s_osdLife - left), (float)s_osdLife);
    UiEnd();
}

static void PresentFrame(void)
{
    ps4_gpu_begin_frame();
    ps4_video_draw();
    DrawOverlay();
    ps4_gpu_end_frame();
}

static char s_saveDir[192];
static double s_saveDirtyAt;

static void SavesOpen(H64System *sys)
{
    char folder[64];
    h64_save_folder_name(&sys->rom, folder, sizeof(folder));
    snprintf(s_saveDir, sizeof(s_saveDir), "%s/%s", SAVES_DIR, folder);
    sceKernelMkdir(s_saveDir, 0777);
    strncat(s_saveDir, "/", sizeof(s_saveDir) - strlen(s_saveDir) - 1);
    s_saveDirtyAt = 0;
    H64_INFO("[save] folder %s: %d file(s) loaded", s_saveDir, h64_save_load(sys->save, s_saveDir));
}

static void SavesTick(H64System *sys, int leaving)
{
    if (!sys->save->dirty) { s_saveDirtyAt = 0; return; }
    double now = ps4_now_ms();
    if (!s_saveDirtyAt) s_saveDirtyAt = now;
    if (leaving || now - s_saveDirtyAt >= 2000.0)
    {
        int n = h64_save_store(sys->save, s_saveDir);
        H64_INFO("[save] %d file(s) written to %s", n, s_saveDir);
        s_saveDirtyAt = 0;
    }
}

static int s_stateSlot = 1;
static int s_statePending;

static void StatePath(char *out, size_t size)
{
    snprintf(out, size, "%sstate%d.h64s", s_saveDir, s_stateSlot);
}

static int SaveStateNow(H64System *sys)
{
    std::vector<u8> st;
    char path[256];
    FILE *f;
    int ok;
    if (!h64_state_quiet(sys)) return 0;
    ps4_video_sync();
    h64_state_save(sys, &st);
    StatePath(path, sizeof(path));
    f = fopen(path, "wb");
    ok = f && !st.empty() && fwrite(&st[0], 1, st.size(), f) == st.size();
    if (f) fclose(f);
    H64_INFO("[state] %s: %u bytes %s", path, (unsigned)st.size(), ok ? "written" : "NOT written");
    OsdT(ok ? T_STATE_SAVED : T_STATE_WRITE_FAILED, s_stateSlot);
    if (ok) XbRaStateSaved(path);
    return 1;
}

static void LoadStateNow(H64System *sys)
{
    char path[256];
    u32 size = 0;
    u8 *data = NULL;
    FILE *f;
    StatePath(path, sizeof(path));
    f = fopen(path, "rb");
    if (f)
    {
        long n;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < (64l << 20) && (data = (u8 *)malloc((size_t)n)) != NULL)
        {
            if (fread(data, 1, (size_t)n, f) == (size_t)n) size = (u32)n;
            else { free(data); data = NULL; }
        }
        fclose(f);
    }
    if (!data) { OsdT(T_NO_STATE, s_stateSlot); return; }
    ps4_video_sync();
    if (h64_state_load(sys, data, size)) OsdT(T_STATE_LOAD_FAILED, s_stateSlot);
    else
    {
        ps4_video_rdram_written();
        OsdT(T_STATE_LOADED, s_stateSlot);
        XbRaStateLoaded(path);
    }
    free(data);
}

enum { RG_DASHBOARD = 0, RG_BROWSER, RG_CONTINUE };
#if XB_HAS_RA
enum { M_RESUME = 0, M_SAVE, M_LOAD, M_SLOT, M_CHEATS, M_ACH, M_SETTINGS, M_RESET, M_ROMS, M_DASHBOARD };
#else
enum { M_RESUME = 0, M_SAVE, M_LOAD, M_SLOT, M_CHEATS, M_SETTINGS, M_RESET, M_ROMS, M_DASHBOARD };
#endif

static void BuildGameMenu(void)
{
    char slot[16];
    UiMenuClear(&s_menu, Tr(T_PAUSED), Tr(T_FOOT_PAUSED));
    UiMenuAdd(&s_menu, Tr(T_RESUME), "");
    UiMenuAdd(&s_menu, Tr(T_SAVE_STATE), "");
    UiMenuAdd(&s_menu, Tr(T_LOAD_STATE), "");
    snprintf(slot, sizeof(slot), "< %d >", s_stateSlot);
    UiMenuAdd(&s_menu, Tr(T_STATE_SLOT), slot);
    UiMenuAdd(&s_menu, Tr(T_CHEATS), "");
#if XB_HAS_RA
    UiMenuAdd(&s_menu, Tr(T_ACHIEVEMENTS), "");
#endif
    UiMenuAdd(&s_menu, Tr(T_SETTINGS), "");
    UiMenuAdd(&s_menu, Tr(T_RESET_GAME), "");
    UiMenuAdd(&s_menu, Tr(T_BACK_TO_LIST), "");
    UiMenuAdd(&s_menu, Tr(T_QUIT_DASHBOARD), "");
}

static void ResetGame(H64System *sys)
{
    ps4_video_sync();
    h64_system_reset(sys);
    if (sys->renderer && sys->renderer->reset) sys->renderer->reset(sys->renderer->user);
    OsdT(T_GAME_RESET, 0);
    XbRaReset();
}

static u8 *ReadWhole(const char *path, u32 *len)
{
    FILE *f = fopen(path, "rb");
    u8 *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = n > 0 && n < (8 << 20) ? (u8 *)malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *len = data ? (u32)n : 0;
    return data;
}

static void CheatsLoad(H64System *sys, const Config *c)
{
    char path[192], folder[64];
    u8 *text;
    u32 len;
    int db = 0, own = 0;
    sys->cheats = h64_cheat_new();
    text = ReadWhole(PS4_DATA_DIR "/cheats/mupencheat.txt", &len);
    if (!text) text = ReadWhole("/app0/cheats/mupencheat.txt", &len);
    if (text)
    {
        db = h64_cheat_load_db(sys->cheats, (const char *)text, len, sys->rom.crc1, sys->rom.crc2, sys->rom.region);
        free(text);
    }
    else H64_WARN("[cheat] no cheat database (cheats/mupencheat.txt)");
    h64_save_folder_name(&sys->rom, folder, sizeof(folder));
    snprintf(path, sizeof(path), PS4_DATA_DIR "/cheats/%s.txt", folder);
    text = ReadWhole(path, &len);
    if (text)
    {
        own = h64_cheat_load_plain(sys->cheats, (const char *)text, len);
        free(text);
    }
    h64_cheat_states_read(sys->cheats, c->cheats);
    if (c->cheatOn >= 0) h64_cheat_set(sys->cheats, c->cheatOn, 1);
    H64_INFO("[cheat] %d cheats from the database, %d from %s, %d on", db, own, path, h64_cheat_active(sys->cheats));
}

#define CHEAT_ROWS 10
static int s_cheatSel;
static int s_cheatsOpen;

static void BuildCheats(H64System *sys)
{
    char title[64], name[128];
    int n = h64_cheat_count(sys->cheats), top, i;
    if (s_cheatSel >= n) s_cheatSel = n ? n - 1 : 0;
    top = s_cheatSel / CHEAT_ROWS * CHEAT_ROWS;
    snprintf(title, sizeof(title), "%s  %d / %d", Tr(T_CHEATS), n ? s_cheatSel + 1 : 0, n);
    UiMenuClear(&s_menu, title, Tr(T_FOOT_SELECT));
    for (i = top; i < n && i < top + CHEAT_ROWS; i++)
    {
        H64CheatInfo ci;
        char value[64], fit[96];
        const char *v;
        size_t k;
        h64_cheat_info(sys->cheats, i, &ci);
        v = !ci.state ? Tr(T_OFF) : ci.options ? h64_cheat_option_name(sys->cheats, i, ci.state - 1) : Tr(T_ON);
        UiTextFit(UI_REGULAR, 21, 190, v, fit, sizeof(fit));
        snprintf(value, sizeof(value), "< %s >", fit);
        strncpy(name, ci.name, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        for (k = 0; name[k]; k++)
            if (name[k] == '\\') name[k] = ' ';
        UiTextFit(UI_BOLD, 22, 660 - 88 - 30 - (UiTextWidth(UI_REGULAR, 21, value) + 16), name, fit, sizeof(fit));
        UiMenuAdd(&s_menu, fit, value);
    }
    s_menu.sel = n ? s_cheatSel - top : 0;
}

static int CheatsInput(H64System *sys, Config *c, WORD down)
{
    int n = h64_cheat_count(sys->cheats);
    if (down & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_BACK))
    {
        h64_cheat_states_write(sys->cheats, c->cheats, sizeof(c->cheats));
        if (s_profilePath[0] && !ConfigWriteKey(s_profilePath, "cheats", c->cheats)) H64_WARN("[cheat] cannot write %s", s_profilePath);
        H64_INFO("[cheat] %d on: %s", h64_cheat_active(sys->cheats), c->cheats);
        return 1;
    }
    if (!n) return 0;
    if (down & XINPUT_GAMEPAD_DPAD_UP) s_cheatSel = (s_cheatSel + n - 1) % n;
    if (down & XINPUT_GAMEPAD_DPAD_DOWN) s_cheatSel = (s_cheatSel + 1) % n;
    if (down & XINPUT_GAMEPAD_LEFT_SHOULDER) s_cheatSel = s_cheatSel >= CHEAT_ROWS ? s_cheatSel - CHEAT_ROWS : 0;
    if (down & XINPUT_GAMEPAD_RIGHT_SHOULDER) s_cheatSel = s_cheatSel + CHEAT_ROWS < n ? s_cheatSel + CHEAT_ROWS : n - 1;
    if (down & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A))
    {
        H64CheatInfo ci;
        int states;
        h64_cheat_info(sys->cheats, s_cheatSel, &ci);
        states = (ci.options ? ci.options : 1) + 1;
        h64_cheat_set(sys->cheats, s_cheatSel, (ci.state + ((down & XINPUT_GAMEPAD_DPAD_LEFT) ? states - 1 : 1)) % states);
    }
    BuildCheats(sys);
    return 0;
}

static int s_achSel, s_achCount;
static char s_achBadge[UI_MENU_MAX][24];
static IDirect3DTexture9 *AchIcon(int row) { return XbRaBadge(s_achBadge[row]); }

static void BuildAch(void)
{
    char title[64];
    int n = s_achCount, top, i;
    if (s_achSel >= n) s_achSel = n ? n - 1 : 0;
    top = s_achSel / CHEAT_ROWS * CHEAT_ROWS;
    snprintf(title, sizeof(title), "%s  %d / %d", Tr(T_ACHIEVEMENTS), n ? s_achSel + 1 : 0, n);
    UiMenuClear(&s_menu, title, Tr(T_FOOT_SELECT));
    s_menu.icon = AchIcon;
    for (i = top; i < n && i < top + CHEAT_ROWS; i++)
    {
        XbRaItem it;
        char value[64], fit[128];
        XbRaListItem(i, &it);
        strcpy(s_achBadge[i - top], it.badge);
        if (it.unlocked) strcpy(value, Tr(T_RA_DONE));
        else sprintf(value, "%d", it.points);
        UiTextFit(UI_BOLD, 22, 660 - 88 - 30 - 48 - (UiTextWidth(UI_REGULAR, 21, value) + 16), it.title, fit, sizeof(fit));
        UiMenuAdd(&s_menu, fit, value);
    }
    s_menu.sel = n ? s_achSel - top : 0;
}

static int AchOpen(const Config *c)
{
    s_achCount = XbRaListBegin();
    if (s_achCount) { BuildAch(); return 1; }
    XbRaListEnd();
    OsdT(!c->achievements ? T_RA_OFF_HINT : XbRaState() == XB_RA_IN ? T_RA_NONE : T_RA_NOT_CONNECTED, 0);
    return 0;
}

static int AchInput(WORD down)
{
    int n = s_achCount;
    if (down & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_BACK)) { XbRaListEnd(); return 1; }
    if (down & XINPUT_GAMEPAD_DPAD_UP) s_achSel = (s_achSel + n - 1) % n;
    if (down & XINPUT_GAMEPAD_DPAD_DOWN) s_achSel = (s_achSel + 1) % n;
    if (down & XINPUT_GAMEPAD_LEFT_SHOULDER) s_achSel = s_achSel >= CHEAT_ROWS ? s_achSel - CHEAT_ROWS : 0;
    if (down & XINPUT_GAMEPAD_RIGHT_SHOULDER) s_achSel = s_achSel + CHEAT_ROWS < n ? s_achSel + CHEAT_ROWS : n - 1;
    if (down & XINPUT_GAMEPAD_A)
    {
        XbRaItem it;
        XbRaListItem(s_achSel, &it);
        if (it.description[0]) OsdText(it.description, 5000, it.badge);
    }
    BuildAch();
    return 0;
}

static int GameMenu(H64System *sys, Config *c)
{
    UiInput in;
    int settings = 0, cheats = 0, ach = 0, sel, result = -1;
    H64_INFO("[menu] opened at VI %u", sys->vi.frames);
    BuildGameMenu();
    if (s_cheatsOpen && h64_cheat_count(sys->cheats)) { cheats = 1; BuildCheats(sys); }
    s_cheatsOpen = 0;
    UiInputInit(&in);
    s_menuOpen = 1;
    while (result < 0)
    {
        WORD down = UiInputPoll(&in);
        XbRaPump();
        if (s_aboutOpen)
        {
            if (down & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_X)) s_aboutOpen = 0;
        }
        else if (ach)
        {
            if (AchInput(down))
            {
                ach = 0;
                BuildGameMenu();
                s_menu.sel = M_ACH;
            }
        }
        else if (cheats)
        {
            if (CheatsInput(sys, c, down))
            {
                cheats = 0;
                BuildGameMenu();
                s_menu.sel = M_CHEATS;
            }
        }
        else if (settings)
        {
            int r = SettingsInput(&s_menu, c, s_profilePath[0] != 0, down);
            if (r == SET_CHANGED)
            {
                ps4_audio_set_target_ms(c->audioMs);
                s_showFps = c->showFps;
                ApplyGraphics(c);
            }
            else if (r == SET_SAVE_ALL || r == SET_SAVE_GAME)
            {
                const char *path = r == SET_SAVE_ALL ? s_settingsPath : s_profilePath;
                if (!path[0]) OsdT(T_NOT_SAVED_SCRIPT, 0);
                else OsdT(ConfigWriteMenuKeys(c, path, r == SET_SAVE_ALL) ? (r == SET_SAVE_ALL ? T_SAVED_ALL : T_SAVED_GAME)
                                                                         : T_SETTINGS_WRITE_FAILED, 0);
            }
            if (r == SET_BACK || r == SET_SAVE_ALL || r == SET_SAVE_GAME)
            {
                settings = 0;
                BuildGameMenu();
                s_menu.sel = M_SETTINGS;
            }
        }
        else
        {
            UiMenuNavigate(&s_menu, down);
            sel = s_menu.sel;
            if (down & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START)) result = RG_CONTINUE;
            else if (down & XINPUT_GAMEPAD_X) s_aboutOpen = 1;
            if (sel == M_SLOT && (down & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A)))
            {
                s_stateSlot += (down & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : 1;
                if (s_stateSlot < 1) s_stateSlot = 9;
                if (s_stateSlot > 9) s_stateSlot = 1;
                snprintf(s_menu.value[M_SLOT], sizeof(s_menu.value[M_SLOT]), "< %d >", s_stateSlot);
            }
            else if (down & XINPUT_GAMEPAD_A)
            {
                switch (sel)
                {
                case M_RESUME: result = RG_CONTINUE; break;
                case M_SAVE: s_statePending = 1; result = RG_CONTINUE; break;
                case M_LOAD: LoadStateNow(sys); result = RG_CONTINUE; break;
                case M_CHEATS:
                    if (h64_cheat_count(sys->cheats)) { cheats = 1; BuildCheats(sys); }
                    else OsdT(T_NO_CHEATS, 0);
                    break;
#if XB_HAS_RA
                case M_ACH: ach = AchOpen(c); break;
#endif
                case M_SETTINGS: settings = 1; SettingsBuild(&s_menu, c, s_profilePath[0] != 0); break;
                case M_RESET: ResetGame(sys); result = RG_CONTINUE; break;
                case M_ROMS: result = RG_BROWSER; break;
                case M_DASHBOARD: result = RG_DASHBOARD; break;
                }
            }
        }
        ps4_gpu_begin_frame();
        ps4_video_draw();
        DrawOverlay();
        ps4_gpu_end_frame();
    }
    s_menuOpen = 0;
    s_aboutOpen = 0;
    H64_INFO("[menu] closed: %s", result == RG_CONTINUE ? "resume" : result == RG_BROWSER ? "ROM list" : "dashboard");
    return result;
}

#define JIT_BYTES (64u << 20)
static void *JitMemory(void)
{
    static void *mem;
    static int tried;
    if (!tried)
    {
        void *p = nullptr;
        int rc = sceKernelMapFlexibleMemory(&p, JIT_BYTES, 7  , 0);
        if (rc != 0 || !p)
        {
            p = nullptr;
            rc = sceKernelMmap(nullptr, JIT_BYTES, 7, 0x1002  , -1, 0, &p);
        }
        tried = 1;
        ps4_log("[jit] %u MB of executable memory: rc=%#x at %p", JIT_BYTES >> 20, rc, p);
        if (rc == 0 && p) mem = p;
    }
    return mem;
}

static int ResAuto(const Config *c) { return XB_HAS_RES_AUTO && c->resScale >= 4; }

static const char *const s_hiResGames[] = { "NIJ", "NY2", "NRT" };

static int MaxScaleOfRom(const u8 *data, u32 size)
{
    char code[3];
    u32 k;
    if (size < 0x40) return 4;
    for (k = 0; k < 3; k++)
    {
        u32 at = 0x3B + k;
        if (data[0] == 0x37) at ^= 1;
        else if (data[0] == 0x40) at ^= 3;
        code[k] = (char)data[at];
    }
    for (k = 0; k < sizeof(s_hiResGames) / sizeof(s_hiResGames[0]); k++)
        if (!memcmp(code, s_hiResGames[k], 3)) return 2;
    return 4;
}

static int RunGame(IDirect3DDevice9 *dev, Config *c, const char *romPath)
{
    int result = RG_BROWSER;
    u32 size = 0;
    u8 *data = RomFileLoad(romPath, &size);
    if (!data)
    {
        MessageScreen(dev, Tr(T_CANNOT_READ), romPath, D3DCOLOR_XRGB(240, 60, 60));
        return RG_BROWSER;
    }
    int scale = c->resScale >= 3 ? 4 : c->resScale == 2 ? 2 : 1, maxScale = MaxScaleOfRom(data, size);
    if (ResAuto(c))
    {
        scale = maxScale;
        ps4_log("[video] automatic resolution: x%d to start with", scale);
    }
    else if (scale > maxScale && c->resForce)
        ps4_log("[video] internal resolution x%d, above the x%d recommended for this game (the user's choice)", scale, maxScale);
    else if (scale > maxScale)
    {
        ps4_log("[video] internal resolution x%d, the one recommended for this game, instead of x%d", maxScale, scale);
        scale = maxScale;
    }
    SettingsSetMaxRes(maxScale >= 4 ? 3 : maxScale);
    u8 *rdram = ps4_video_create(scale);
    if (!rdram)
    {
        free(data);
        MessageScreen(dev, Tr(T_RENDERER_FAILED), "See " PS4_DATA_DIR "/harissa64.log", D3DCOLOR_XRGB(240, 60, 60));
        return RG_BROWSER;
    }
    memset(rdram, 0, H64_RDRAM_SIZE);
    H64Options opt;
    memset(&opt, 0, sizeof(opt));
    opt.hleBoot = 1;
    opt.emux = 1;
    opt.hleAudio = 1;
    opt.hleGfx = c->hle;
    opt.rdram = rdram;
    H64System *sys = (H64System *)calloc(1, sizeof(H64System));
    if (!sys || h64_system_init(sys, data, size, &opt))
    {
        free(sys);
        free(data);
        ps4_video_shutdown();
        MessageScreen(dev, Tr(T_NOT_N64), romPath, D3DCOLOR_XRGB(240, 60, 60));
        return RG_BROWSER;
    }
    free(data);
    h64_apply_game_profile(sys);
    if (c->gameFlags >= 0)
    {
        h64_apply_profile_flags(sys, (u32)c->gameFlags);
        H64_INFO("[profile] gameflags=%X from the ini", c->gameFlags);
    }
    int jit = strcmp(c->cpu, "interp") != 0;
    if (jit && (!JitMemory() || h64_jit_init(sys, JitMemory(), JIT_BYTES, nullptr)))
    {
        H64_ERROR("[jit] no recompiler: running on the interpreter");
        jit = 0;
    }
    if (jit && c->fastFpu)
    {
        sys->jit->fastFpu = 1;
        sys->cpu.fpuNoTraps = 1;
        h64_jit_reset(sys);
    }
    sys->cpu.cpi = c->cpi >= 1 && c->cpi <= 8 ? (u32)c->cpi : 2;
    if (sys->options.gameCpi && !c->cpiSet)
    {
        sys->cpu.cpi = sys->options.gameCpi;
        H64_INFO("[profile] %u CPU cycles an instruction", sys->cpu.cpi);
    }
    ps4_video_attach(sys);
    ApplyGraphics(c);
    int rdpSync = c->rdpSync >= 0 ? c->rdpSync != 0 : !sys->options.hleGfx;
    ps4_video_set_sync(rdpSync);
    ps4_audio_set_target_ms(c->audioMs);
    if (ps4_audio_init() == 0)
    {
        sys->aiSink = ps4_audio_sink;
        sys->aiUser = nullptr;
    }
    sys->padMask = 1;
    s_showFps = c->showFps;
    s_fpsText[0] = 0;
    s_osd[0] = 0;
    SavesOpen(sys);
    CheatsLoad(sys, c);
    s_cheatSel = 0;
    s_achSel = 0;
    XbRaSetEnabled(c->achievements);
    XbRaGameStart(sys);
    sys->rumblePaks = h64_rom_rumble(&sys->rom) ? (sys->save->pak && !c->rumble ? 0xEu : 0xFu) : 0;
    H64_INFO("[main] rumble pak: %s%s", !sys->rumblePaks ? "not supported by the game" : !sys->save->pak ? "every port" :
             c->rumble ? "every port, with the Controller Pak on port 1" : "ports 2-4 (Controller Pak on port 1)",
             sys->rumblePaks && !c->rumble ? ", motors off (setting)" : "");
    s_stateSlot = c->stateSlot >= 1 && c->stateSlot <= 9 ? c->stateSlot : 1;
    s_statePending = 0;
    s_menuOpen = s_aboutOpen = 0;
    ps4_log("[main] running %s: cpu %s%s, %u cycles an instruction, graphics %s, audio HLE, sound queue target %d ms", romPath,
            jit ? "recompiler" : "interpreter", jit && c->fastFpu ? " (fast FPU)" : "", sys->cpu.cpi, sys->options.hleGfx ? "HLE" : "LLE", ps4_audio_target_ms());
    ps4_log("[main] RDP: %s", rdpSync ? "the CPU waits for the GPU at each SYNC_FULL" : "the CPU does not wait for the GPU (rdpsync=1 to wait)");

    sys->profClock = [](void) -> u64 { return __rdtsc(); };
    u64 profBase[H64_PROF_COUNT] = {0}, profTsc = __rdtsc();
    double waitMs = 0, presentMs = 0, syncMs0 = ps4_video_sync_wait_ms();
    int autoScale = scale, autoSlow = 0, autoSkip = 3, wasAuto = ResAuto(c);
    u64 statBlocks = 0, statSteps = 0;

    u32 frame = sys->vi.frames, presented = 0;
    double statStart = ps4_now_ms(), fpsStart = statStart;
    double paceTotal = 0, fpsPace0 = 0, fpsFlip0 = ps4_gpu_flip_wait_ms();
    u32 statFrames = 0, fpsFrames = 0;
    u64 statCommands = 0;
    int backDown = 0, backHinted = 0, backUsed = 0, menuRequest = 0;
    double backHeldMs = 0, backPolledAt = 0;
    u16 prevButtons = 0;
    for (;;)
    {
        if (ps4_audio_queued_ms() > ps4_audio_target_ms())
        {
            double t0 = ps4_now_ms();
            while (ps4_audio_queued_ms() > ps4_audio_target_ms()) sceKernelUsleep(1000);
            waitMs += ps4_now_ms() - t0;
            paceTotal += ps4_now_ms() - t0;
        }
        else if (ps4_audio_queued_ms() < 0 && sys->vi.frameCycles)
        {
            static u64 cyc0;
            static double t0;
            static H64System *paced;
            double period = sys->tvType == 0 ? 20.0 : 1000.0 / 60.0;
            double now = ps4_now_ms(), emu;
            if (paced != sys || now - t0 - (double)(sys->cpu.cycles - cyc0) * period / (double)sys->vi.frameCycles > 250.0)
            {
                paced = sys;
                cyc0 = sys->cpu.cycles;
                t0 = now;
            }
            emu = (double)(sys->cpu.cycles - cyc0) * period / (double)sys->vi.frameCycles;
            if (emu > now - t0 + 1.0)
            {
                double ms = emu - (now - t0);
                sceKernelUsleep((unsigned)((ms > 50.0 ? 50.0 : ms) * 1000.0));
                waitMs += ps4_now_ms() - now;
                paceTotal += ps4_now_ms() - now;
            }
        }
        h64_system_run_cycles(sys, 200000);

        H64PadInput in;
        int connected = ps4_input_read(0, &in);
        if (connected && (in.buttons & H64_XB_BACK))
        {
            u16 down = (u16)(in.buttons & ~prevButtons);
            double nowMs = ps4_now_ms();
            if (!backDown)
            {
                backDown = 1;
                backUsed = 0;
                backHeldMs = 0;
                backHinted = 0;
                H64_INFO("[pad] BACK (touch pad) down at VI %u", presented);
            }
            else
                backHeldMs += nowMs - backPolledAt > 100.0 ? 100.0 : nowMs - backPolledAt;
            backPolledAt = nowMs;
            if (down & H64_XB_RIGHT_SHOULDER) { s_statePending = 1; backUsed = 1; }
            if (down & H64_XB_LEFT_SHOULDER) { LoadStateNow(sys); backUsed = 1; }
            if (down & (H64_XB_DPAD_LEFT | H64_XB_DPAD_RIGHT))
            {
                s_stateSlot += (down & H64_XB_DPAD_RIGHT) ? 1 : -1;
                if (s_stateSlot < 1) s_stateSlot = 9;
                if (s_stateSlot > 9) s_stateSlot = 1;
                OsdT(T_SLOT, s_stateSlot);
                backUsed = 1;
            }
            if (!backUsed && backHeldMs >= 1000.0 && !backHinted)
            {
                OsdT(T_KEEP_HOLDING, 0);
                backHinted = 1;
            }
            if (!backUsed && backHeldMs >= 3000.0)
            {
                H64_INFO("[pad] BACK held 3 s: back to the ROM list");
                result = RG_BROWSER;
                break;
            }
            memset(&sys->pad[0], 0, sizeof(sys->pad[0]));
        }
        else
        {
            if (backDown)
            {
                H64_INFO("[pad] BACK up after %.0f ms%s", backHeldMs, backUsed ? " (used with R1/L1/D-pad)" : "");
                if (!backUsed && backHeldMs < 600.0) menuRequest = 1;
            }
            backDown = 0;
            if (connected) h64_padmap_apply(&c->padMap[0], &in, &sys->pad[0]);
            else memset(&sys->pad[0], 0, sizeof(sys->pad[0]));
        }
        prevButtons = connected ? in.buttons : 0;
        {
            u32 mask = 1;
            int p;
            for (p = 1; p < 4; p++)
            {
                H64PadInput more;
                int on = ps4_input_read(p, &more);
                if (on && !backDown)
                {
                    mask |= 1u << p;
                    h64_padmap_apply(&c->padMap[p], &more, &sys->pad[p]);
                }
                else
                {
                    if (on) mask |= 1u << p;
                    memset(&sys->pad[p], 0, sizeof(sys->pad[p]));
                }
                ps4_input_rumble(p, c->rumble && on && sys->rumbleMotor[p]);
            }
            if (mask != sys->padMask)
            {
                H64_INFO("[pad] controllers on ports: %s%s%s%s", "1", mask & 2 ? " 2" : "", mask & 4 ? " 3" : "", mask & 8 ? " 4" : "");
                sys->padMask = mask;
            }
        }
        sys->cheatButton = connected && (in.buttons & H64_XB_RIGHT_THUMB) != 0;
        ps4_input_rumble(0, c->rumble && connected && sys->rumbleMotor[0]);
        if (s_statePending && SaveStateNow(sys)) s_statePending = 0;
        {
            int want;
            if (ResAuto(c))
            {
                if (!wasAuto) { autoScale = maxScale; autoSlow = 0; autoSkip = 1; }
                wasAuto = 1;
                want = autoScale;
            }
            else
            {
                wasAuto = 0;
                want = c->resScale >= 3 ? 4 : c->resScale == 2 ? 2 : 1;
                if (want > maxScale && !c->resForce) want = maxScale;
            }
            if (want != ps4_video_scale() && h64_state_quiet(sys))
            {
                if (!ps4_video_rescale(sys, want))
                {
                    if (ResAuto(c)) autoScale = ps4_video_scale();
                    else c->resScale = ps4_video_scale() == 4 ? 3 : ps4_video_scale();
                }
            }
        }
        if (menuRequest)
        {
            menuRequest = 0;
            for (int p = 0; p < 4; p++) ps4_input_rumble(p, 0);
            memset(&sys->pad[0], 0, sizeof(sys->pad[0]));
            {
                unsigned rw, rh, vw, vh;
                ps4_video_sizes(sys, &rw, &rh, &vw, &vh);
                SettingsSetBaseSize(vw, vh);
                SettingsSetCurScale(ps4_video_scale());
            }
            result = GameMenu(sys, c);
            if (result != RG_CONTINUE) break;
            result = RG_BROWSER;
            prevButtons = 0xFFFF;
            continue;
        }

        if (sys->vi.frames == frame) continue;
        frame = sys->vi.frames;
        {
            double t0 = ps4_now_ms();
            ps4_video_scanout(sys);
            PresentFrame();
            presentMs += ps4_now_ms() - t0;
        }
        presented++;
        if (c->shotN && presented >= c->shotAt && presented < c->shotAt + c->shotN)
        {
            char shot[160];
            sceKernelMkdir(PS4_DATA_DIR "/shots", 0777);
            snprintf(shot, sizeof(shot), PS4_DATA_DIR "/shots/vi%05u.rgba", presented);
            if (!ps4_video_shot(sys, shot)) H64_WARN("[shot] nothing written for VI %u", presented);
            else if (presented == c->shotAt) H64_INFO("[shot] %u pictures from VI %u in " PS4_DATA_DIR "/shots", c->shotN, presented);
        }
        if (c->rescaleAt && presented == c->rescaleAt) c->resScale = c->resScale == 2 ? 3 : 2;
        if (c->raToast && presented == c->raToast) XbRaTestToast();
        statFrames++;
        fpsFrames++;
        SavesTick(sys, 0);

        double now = ps4_now_ms();
        if (now - fpsStart >= 1000.0)
        {
            double fps = fpsFrames * 1000.0 / (now - fpsStart);
            if (s_showFps >= 2)
            {
                double flip = ps4_gpu_flip_wait_ms();
                double load = 100.0 * (1.0 - ((paceTotal - fpsPace0) + (flip - fpsFlip0)) / (now - fpsStart));
                u32 used = 0, total = 0;
                MemoryMb(&used, &total);
                snprintf(s_fpsText, sizeof(s_fpsText), "%.1f FPS\nCPU %d%%\nRAM %u / %u MB", fps,
                         (int)(load > 100.0 ? 100.0 : load < 0.0 ? 0.0 : load), used, total);
                if (s_showFps >= 3)
                {
                    unsigned rw, rh, vw, vh;
                    ps4_video_sizes(sys, &rw, &rh, &vw, &vh);
                    snprintf(s_fpsText + strlen(s_fpsText), sizeof(s_fpsText) - strlen(s_fpsText), "\n%ux%u (N64 %ux%u)", rw, rh, vw, vh);
                }
            }
            else
                snprintf(s_fpsText, sizeof(s_fpsText), "%.1f FPS", fps);
            fpsStart = now;
            fpsFrames = 0;
            fpsPace0 = paceTotal;
            fpsFlip0 = ps4_gpu_flip_wait_ms();
        }
        if (now - statStart >= 5000)
        {
            unsigned pw, ph;
            ps4_video_info(&pw, &ph);
            static unsigned long long culled0;
            ps4_log("[main] %.1f VI/s, sound queue %d ms, picture %ux%u, %llu RDP commands, %llu triangles outside the scissor not sent",
                    statFrames * 1000.0 / (now - statStart), ps4_audio_queued_ms(), pw, ph,
                    (unsigned long long)(sys->dpCommands - statCommands), ps4_video_culled() - culled0);
            culled0 = ps4_video_culled();
            {
                unsigned one, longer;
                ps4_gpu_flip_stats(&one, &longer);
                ps4_log("[flip] %u pictures stayed one refresh on the TV, %u longer", one, longer);
            }
            {
                u64 tsc = __rdtsc();
                double k = (now - statStart) / (double)(tsc - profTsc), ms[H64_PROF_COUNT];
                for (int i = 0; i < H64_PROF_COUNT; i++)
                {
                    ms[i] = (double)(sys->prof[i] - profBase[i]) * k;
                    profBase[i] = sys->prof[i];
                }
                profTsc = tsc;
                ps4_log("[prof] of %.0f ms: blocks %.0f, helper %.0f, events %.0f, compile %.0f, gfx HLE %.0f (renderer %.0f, of which GPU wait %.0f), "
                        "audio HLE %.0f, RSP LLE %.0f, sound pacing %.0f, scanout+present %.0f",
                        now - statStart, ms[H64_PROF_BLOCKS], ms[H64_PROF_HELPER], ms[H64_PROF_EVENTS], ms[H64_PROF_JIT_COMPILE],
                        ms[H64_PROF_GFX_HLE], ms[H64_PROF_RENDER], ps4_video_sync_wait_ms() - syncMs0, ms[H64_PROF_AUDIO_HLE],
                        ms[H64_PROF_RSP_LLE], waitMs, presentMs);
                syncMs0 = ps4_video_sync_wait_ms();
                if (sys->jit)
                {
                    const H64JitStats *st = &sys->jit->stats;
                    ps4_log("[prof] recompiler: %llu blocks entered, %llu interpreter steps, %llu blocks compiled, %llu flushes, %u KB of code",
                            (unsigned long long)(st->blocksRun - statBlocks), (unsigned long long)(st->interpSteps - statSteps),
                            (unsigned long long)st->blocksCompiled, (unsigned long long)st->flushes, sys->jit->memUsed >> 10);
                    statBlocks = st->blocksRun;
                    statSteps = st->interpSteps;
                }
                if (ResAuto(c) && ps4_video_scale() > 1)
                {
                    const double span = now - statStart, nominal = sys->tvType == 0 ? 50.0 : 60.0;
                    const int slow = span < 6000.0 && statFrames * 1000.0 / span < nominal * 0.95 && presentMs > span * 0.5 &&
                                     waitMs < span * 0.05;
                    if (autoSkip) { autoSkip--; autoSlow = 0; }
                    else if (!slow) autoSlow = 0;
                    else if (++autoSlow >= (ps4_video_scale() == 2 ? 3 : 2))
                    {
                        autoScale = ps4_video_scale() / 2;
                        autoSlow = 0;
                        autoSkip = 1;
                        ps4_log("[video] automatic resolution: x%d (%.1f VI/s, the picture %.0f ms of %.0f)", autoScale,
                                statFrames * 1000.0 / span, presentMs, span);
                        OsdT(T_PS4_RES_LOWERED, autoScale);
                    }
                }
                waitMs = presentMs = 0;
            }
            {
                static int tick;
                if (++tick >= 12) { tick = 0; ps4_gpu_save_pipeline_cache(); }
            }
            statCommands = sys->dpCommands;
            statStart = now;
            statFrames = 0;
        }
    }

    for (int p = 0; p < 4; p++) ps4_input_rumble(p, 0);
    double t0 = ps4_now_ms(), t1, t2, t3;
    SavesTick(sys, 1);
    t1 = ps4_now_ms();
    ps4_audio_shutdown();
    t2 = ps4_now_ms();
    sys->renderer = nullptr;
    sys->aiSink = nullptr;
    XbRaGameEnd(sys);
    if (sys->cheats) { h64_cheat_free(sys->cheats); sys->cheats = NULL; }
    h64_system_free(sys);
    free(sys);
    t3 = ps4_now_ms();
    ps4_video_shutdown();
    ps4_gpu_save_pipeline_cache();
    H64_INFO("[main] game ended: %s (saves %.0f ms, sound %.0f, system %.0f, RDP %.0f)", result == RG_BROWSER ? "ROM list" : "dashboard",
             t1 - t0, t2 - t1, t3 - t2, ps4_now_ms() - t3);
    return result;
}

int main()
{
    ps4_log_open();
    ps4_log("==== Harissa64 %s for PS4, built " __DATE__ " " __TIME__ " ====", H64_VERSION_STRING);
    static const struct { uint32_t id; const char *name; } modules[] = {
        {0x80000010, "SystemService"}, {0x80000011, "UserService"}, {0x80000022, "VideoOut"},
        {0x80000024, "Pad"},           {0x80000001, "AudioOut"},
    };
    for (const auto &m : modules)
    {
        int rc = sceSysmoduleLoadModuleInternal(m.id);
        if (rc) ps4_log("[main] system module %s (%#x): %#x", m.name, m.id, rc);
    }
    sceUserServiceInitialize(nullptr);

    if (ps4_gpu_init())
    {
        ps4_softtest_run(JitMemory(), JIT_BYTES);
        quit(1);
    }
    ps4_input_init();
    DrivesMount();
    sceKernelMkdir(PS4_DATA_DIR "/roms", 0777);
    sceKernelMkdir(CONFIG_DIR, 0777);
    sceKernelMkdir(SAVES_DIR, 0777);

    IDirect3DDevice9 *dev = XbDevice();
    static Config cfg, base, game;
    ConfigDefaults(&cfg);
    sceKernelMkdir(PS4_DATA_DIR "/config", 0777);
    XbRaInit(PS4_DATA_DIR, 0, OsdText);
    strcpy(s_settingsPath, SETTINGS_PATH);
    ChooseLanguage(dev, &cfg, s_settingsPath, 1);
    SettingsSetLangFile(s_settingsPath);
    for (;;)
    {
        char path[256], folder[64];
        base = cfg;
        if (ConfigParseFile(&base, s_settingsPath)) H64_INFO("[main] settings %s", s_settingsPath);
        NetTestOff(base.noNet);
        {
            static int tested;
            if (base.netTest && !tested)
            {
                u8 *body = nullptr;
                u32 len = 0;
                double t0 = ps4_now_ms();
                int up = NetStart(0), st;
                tested = 1;
                st = up ? NetGet("retroachievements.org", "/dorequest.php?r=gameid&m=20b854b239203baf6c961b850a4a51a2", 0, &body, &len, 65536, nullptr, 0) : 0;
                ps4_log("[net] test: network %s, retroachievements.org answers HTTP %d, %u bytes in %.0f ms: %.80s", up ? "up" : "DOWN", st, len,
                        ps4_now_ms() - t0, body ? (const char *)body : "");
                free(body);
            }
        }
        XbRaSetEnabled(base.achievements);
        if (base.raTest)
        {
            static int raTested;
            if (!raTested) { raTested = 1; XbRaSelfTest(); }
        }
        if (!RomBrowser(dev, &base, s_settingsPath, path, sizeof(path))) break;
        H64_INFO("[main] chosen: %s", path);
        game = base;
        s_profilePath[0] = 0;
        if (RomFolderName(path, folder, sizeof(folder)))
        {
            snprintf(s_profilePath, sizeof(s_profilePath), "%s/%s.ini", CONFIG_DIR, folder);
            if (ConfigParseFile(&game, s_profilePath)) H64_INFO("[main] game profile %s", s_profilePath);
        }
        {
            int r = RunGame(dev, &game, path);
            SettingsSetMaxRes(3);
            SettingsSetBaseSize(0, 0);
            SettingsSetCurScale(0);
            if (r == RG_DASHBOARD) break;
        }
    }
    H64_INFO("[main] quit from the ROM list");
    quit(0);
}
