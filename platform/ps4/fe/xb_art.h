// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 - box art and game information for the ROM browser.
// PS4: copied from the Xbox 360 front end (platform/xbox360/xb_art.h, v2 at ac20c3d).
//
// Everything lives in <drive>:\art\games\ (game:\ on the console, cache:\ in
// Xenia), one pair of files per game, named by its cache key (the header's
// game code, "NSME"; h64_gameinfo_key):
//   NSME.png   the box art (libretro-thumbnails, as downloaded)
//   NSME.txt   what is known about the game (h64_gameinfo_format)
//   db\        the databases the matching uses (h64_gamedb_files)
// tools/fetch_game_art.py fills the same folder from a PC.
//
// A worker thread (hardware thread 3) reads, decodes and, when allowed,
// downloads; the UI thread never waits for it. The browser says which games
// it wants (the selection first, then its neighbours: those only from the
// files already there) and takes the results when they are ready.
#ifndef XB_ART_H
#define XB_ART_H

#include "../../../core/cart/h64_gameinfo.h"
#include "../../../core/common/h64_types.h"

#define ART_COVER_W 400
#define ART_COVER_H 232
#define ART_GLOW_W 16
#define ART_GLOW_H 10

struct ArtResult
{
    char path[256];
    char key[16];
    int hasInfo;
    H64GameInfo info;
    u8 *cover;
    int coverW, coverH;
    u8 *glow;
};

struct ArtStatus
{
    int bulk;
    int done, total;
    int found, covers;
    int offline;
    char busyPath[256];
};

void ArtStart(const char *drive);
void ArtSetNetwork(int allowed);
void ArtPause(int paused);
void ArtWant(const char *const *paths, int count);
int ArtTake(ArtResult *r);
void ArtResultFree(ArtResult *r);
void ArtForget(const char *path);
void ArtForgetAll(void);
void ArtBulkStart(const char *const *paths, int count);
void ArtGetStatus(ArtStatus *s);
int ArtTakeNote(char *text, int size);

#endif
