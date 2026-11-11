// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - game information for the ROM browser: matching a ROM with
// public game databases, and the small text files that cache the result.
//
// Sources (downloaded at run time, or by tools/fetch_game_art.py; never
// shipped with the emulator):
// - libretro-database (CC BY-SA 4.0), clrmamepro "dat" files: the No-Intro
//   list (name, region, serial = the header's game code, CRC-32 of the .z64
//   file) and the "metadat" lists keyed by that CRC (developer, publisher,
//   release year, genre, players). The No-Intro name is also the file name of
//   the box art in libretro-thumbnails.
// - GameDB-N64 (github.com/niemasd/GameDB-N64), N64.data.json keyed by serial
//   ("NUS-NSME-USA"): clean title, release date, rating.
// - Wikipedia's page summaries for a short description.
//
// Matching: the 4-character game code of the ROM header (e.g. "NSME") picks
// the No-Intro entries with that serial; the header's version byte picks the
// revision ("(Rev 1)"), and betas, demos, kiosk or Virtual Console dumps come
// last. ROMs without a usable code (homebrew, some prototypes) fall back to
// their file name, compared without punctuation or bracketed tags.
#ifndef H64_GAMEINFO_H
#define H64_GAMEINFO_H

#include "../common/h64_types.h"

struct H64GameInfo
{
    int found;
    int cover;
    int wiki;
    char name[128];
    char title[96];
    char year[8];
    char date[16];
    char developer[64];
    char publisher[64];
    char genre[48];
    char players[8];
    char region[32];
    char rating[24];
    char description[640];
};

void h64_gameinfo_clear(H64GameInfo *info);

void h64_gameinfo_key(const char *gameCode, u32 crc1, char *out);

const char *h64_gameinfo_region_name(char codeLetter);

int h64_gameinfo_format(const H64GameInfo *info, char *out, int outSize);
int h64_gameinfo_parse(H64GameInfo *info, const char *text, u32 len);

struct H64GameDbFile { const char *file, *host, *path; int json; };
#define H64_GAMEDB_FILE_COUNT 7
extern const H64GameDbFile h64_gamedb_files[H64_GAMEDB_FILE_COUNT];
#define H64_THUMB_HOST "raw.githubusercontent.com"
#define H64_THUMB_PATH "/libretro-thumbnails/Nintendo_-_Nintendo_64/master/Named_Boxarts/"
#define H64_WIKI_HOST "en.wikipedia.org"
#define H64_WIKI_PATH "/api/rest_v1/page/summary/"

struct H64GameDb;
H64GameDb *h64_gamedb_new(void);
void h64_gamedb_free(H64GameDb *db);
int h64_gamedb_add_dat(H64GameDb *db, const char *text, u32 len);
int h64_gamedb_add_gamedb_json(H64GameDb *db, const char *text, u32 len);
int h64_gamedb_count(const H64GameDb *db);

int h64_gamedb_lookup(const H64GameDb *db, const char *gameCode, int version, const char *fileStem, H64GameInfo *info,
                      char (*covers)[128], int maxCovers, int *coverCount);

void h64_gameinfo_thumb_name(const char *name, char *out, int outSize);
void h64_url_encode(const char *text, char *out, int outSize);
int h64_gameinfo_wiki_pages(const H64GameInfo *info, char (*out)[256], int max);
int h64_gameinfo_wiki_extract(const char *json, u32 len, char *out, int outSize);
void h64_ascii_fold(char *text);

#endif
