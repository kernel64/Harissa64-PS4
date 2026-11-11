// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - the console's drives (Xbox 360).
// PS4: copied from the Xbox 360 front end (platform/xbox360/xb_drives.h, v2 at ac20c3d).
//
// A title sees only game:\ (its own folder) until it maps the other drives
// itself: DrivesMount links Hdd1:, Usb0:..Usb2: and Dvd: to their devices,
// as homebrew file managers do. The ROM list's file browser (xb_menu.cpp)
// lists the ones present.
#ifndef XB_DRIVES_H
#define XB_DRIVES_H

#include "xb_compat.h"

void DrivesMount(void);
#if XB_PS4
#define DRIVE_ROOT_MAX 32
#else
#define DRIVE_ROOT_MAX 16
#endif
int DrivesList(char roots[][DRIVE_ROOT_MAX], int max);
#if XB_PS4
int DrivesIsRoot(const char *dir);
#endif

#endif
