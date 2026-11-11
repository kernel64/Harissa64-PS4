// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - menu languages (see xb_lang.h).
// PS4: copied from the Xbox 360 front end (platform/xbox360/xb_lang.cpp, v2 at ac20c3d).
#define XB_LANG_DEFINE_TABLES
#include "xb_lang.h"

#include <string.h>
#include "xb_compat.h"

#include <orbis/SystemService.h>

#include "../../../core/pif/h64_padmap.h"

static int s_lang = LANG_EN;

const char *TrIn(int lang, int id)
{
    if (lang < 0 || lang >= LANG_COUNT) lang = LANG_EN;
#if XB_PS4
    if (id >= T_COUNT && id < T_PS4_END) return s_ps4Text[lang][id - T_COUNT];
#endif
    if (id < 0 || id >= T_COUNT) return "";
    return s_langText[lang][id];
}

#if XB_PS4
static const char *ps4_button_names(int lang, int id, const char *text)
{
    static char s_fixed[LANG_COUNT][2][192];
    static int s_done[LANG_COUNT][2];
    int k = id == T_BACK_RESERVED ? 0 : id == T_KEEP_HOLDING ? 1 : -1;
    const char *at;
    if (k < 0 || lang < 0 || lang >= LANG_COUNT || !(at = strstr(text, "BACK"))) return text;
    if (!s_done[lang][k])
    {
        snprintf(s_fixed[lang][k], sizeof(s_fixed[lang][k]), "%.*s%s%s", (int)(at - text), text, "TOUCH PAD", at + 4);
        s_done[lang][k] = 1;
    }
    return s_fixed[lang][k];
}
#endif

const char *Tr(int id)
{
#if XB_PS4
    return ps4_button_names(s_lang, id, TrIn(s_lang, id));
#else
    return TrIn(s_lang, id);
#endif
}
int LangGet(void) { return s_lang; }
void LangSet(int lang) { s_lang = lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN; }
int LangRtl(void) { return s_lang == LANG_AR; }
const char *LangCode(int lang) { return s_langCode[lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN]; }

int LangFromCode(const char *code)
{
    int i;
    for (i = 0; i < LANG_COUNT; i++)
        if (!_stricmp(code, s_langCode[i])) return i;
    return -1;
}

int LangFromConsole(void)
{
#if XB_PS4
    int32_t l = 1;
    if (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG, &l) < 0) return LANG_EN;
    switch (l)
    {
    case 2: case 22: return LANG_FR;
    case 3: case 20: return LANG_ES;
    case 4: return LANG_DE;
    case 7: case 17: return LANG_PT;
    case 8: return LANG_RU;
    case 21: return LANG_AR;
    }
    return LANG_EN;
#else
    switch (XGetLanguage())
    {
    case XC_LANGUAGE_SPANISH: return LANG_ES;
    case XC_LANGUAGE_GERMAN: return LANG_DE;
    case XC_LANGUAGE_FRENCH: return LANG_FR;
    case XC_LANGUAGE_PORTUGUESE: return LANG_PT;
    case XC_LANGUAGE_RUSSIAN: return LANG_RU;
    }
    return LANG_EN;
#endif
}

const char *TrRegion(const char *english)
{
    static const char *const names[] = { "USA", "Japan", "Europe", "Germany", "France", "Italy", "Spain", "Australia", "Asia",
                                         "Brazil", "China", "Korea", "Netherlands", "Scandinavia", "Canada" };
    int i;
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        if (!strcmp(english, names[i])) return Tr(T_REGION_USA + i);
    return english;
}

const char *TrN64(int n64)
{
    switch (n64)
    {
    case H64_N64_DUP: return Tr(T_N64_DUP);
    case H64_N64_DDOWN: return Tr(T_N64_DDOWN);
    case H64_N64_DLEFT: return Tr(T_N64_DLEFT);
    case H64_N64_DRIGHT: return Tr(T_N64_DRIGHT);
    case H64_N64_CUP: return Tr(T_N64_CUP);
    case H64_N64_CDOWN: return Tr(T_N64_CDOWN);
    case H64_N64_CLEFT: return Tr(T_N64_CLEFT);
    case H64_N64_CRIGHT: return Tr(T_N64_CRIGHT);
    }
    return h64_padmap_n64_label(n64);
}

const char *TrSource(int src)
{
    switch (src)
    {
    case H64_SRC_DUP: return Tr(T_SRC_DUP);
    case H64_SRC_DDOWN: return Tr(T_SRC_DDOWN);
    case H64_SRC_DLEFT: return Tr(T_SRC_DLEFT);
    case H64_SRC_DRIGHT: return Tr(T_SRC_DRIGHT);
    case H64_SRC_LTHUMB: return Tr(T_SRC_LS);
    case H64_SRC_RTHUMB: return Tr(T_SRC_RS);
    case H64_SRC_LUP: return Tr(T_SRC_LUP);
    case H64_SRC_LDOWN: return Tr(T_SRC_LDOWN);
    case H64_SRC_LLEFT: return Tr(T_SRC_LLEFT);
    case H64_SRC_LRIGHT: return Tr(T_SRC_LRIGHT);
    case H64_SRC_RUP: return Tr(T_SRC_RUP);
    case H64_SRC_RDOWN: return Tr(T_SRC_RDOWN);
    case H64_SRC_RLEFT: return Tr(T_SRC_RLEFT);
    case H64_SRC_RRIGHT: return Tr(T_SRC_RRIGHT);
    }
#if XB_PS4
    {
        static const char *const ds4[] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "OPTIONS" };
        static const int srcs[] = { H64_SRC_A, H64_SRC_B, H64_SRC_X, H64_SRC_Y, H64_SRC_LB, H64_SRC_RB, H64_SRC_LT, H64_SRC_RT, H64_SRC_START };
        int i;
        for (i = 0; i < (int)(sizeof(srcs) / sizeof(srcs[0])); i++)
            if (src == srcs[i]) return ds4[i];
    }
#endif
    return h64_padmap_source_label(src);
}
