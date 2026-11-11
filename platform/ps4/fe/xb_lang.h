// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - menu languages (Xbox 360): English, Spanish, German, French,
// Arabic, Brazilian Portuguese and Russian.
// PS4: copied from the Xbox 360 front end (platform/xbox360/xb_lang.h, v2 at ac20c3d).
//
// The texts are in platform/xbox360/lang/<code>.txt; tools/gen_lang.py checks
// them and writes xb_lang_table.h (T_* ids, UTF-8 strings, Arabic already
// joined). Tr(T_X) gives a text in the current language. The language is
// chosen once on the first start (LanguageScreen in xb_menu.cpp), kept as
// lang= in config\settings.ini and changed in Settings. Logs, ini keys, game
// names and information, and the About panel stay in English.
#ifndef XB_LANG_H
#define XB_LANG_H

#include "xb_compat.h"
#include "xb_lang_table.h"
#if XB_PS4
#include "ps4_lang_table.h"
#endif

enum { LANG_EN = 0, LANG_ES, LANG_DE, LANG_FR, LANG_AR, LANG_PT, LANG_RU };

const char *Tr(int id);
const char *TrIn(int lang, int id);

int LangGet(void);
void LangSet(int lang);
int LangRtl(void);
const char *LangCode(int lang);
int LangFromCode(const char *code);
int LangFromConsole(void);

const char *TrRegion(const char *english);
const char *TrN64(int n64);
const char *TrSource(int src);

#endif
