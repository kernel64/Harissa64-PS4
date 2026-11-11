// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - the console's drives, see xb_drives.h.
// PS4: written for the PS4 after the Xbox 360 front end's file
// (platform/xbox360/xb_drives.cpp, v2 at ac20c3d), whose kernel symbolic
// links have no PS4 counterpart. The "drives" of the file browser are folders:
// the emulator's own (/data/Harissa64-PS4/), the user data folder (/data/)
// and the USB drives the system mounts (/mnt/usb0/ .. /mnt/usb7/).
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "xb_drives.h"
#include "../../../core/common/h64_log.h"

static const char *const s_roots[] = {
    XB_GAME_DIR "/", "/data/", "/mnt/usb0/", "/mnt/usb1/", "/mnt/usb2/", "/mnt/usb3/",
    "/mnt/usb4/", "/mnt/usb5/", "/mnt/usb6/", "/mnt/usb7/",
};
#define ROOT_COUNT ((int)(sizeof(s_roots) / sizeof(s_roots[0])))

void DrivesMount(void)
{
}

static int usable(const char *root, int needEntry)
{
    DIR *d = opendir(root);
    struct dirent *e;
    int any = 0;
    if (!d) return 0;
    if (!needEntry) { closedir(d); return 1; }
    while (!any && (e = readdir(d)) != NULL)
        any = strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    closedir(d);
    return any;
}

int DrivesList(char roots[][DRIVE_ROOT_MAX], int max)
{
    int n = 0, i;
    for (i = 0; i < ROOT_COUNT && n < max; i++)
    {
        if (!usable(s_roots[i], i >= 2)) continue;
        strncpy(roots[n], s_roots[i], DRIVE_ROOT_MAX - 1);
        roots[n][DRIVE_ROOT_MAX - 1] = 0;
        n++;
    }
    H64_INFO("[drives] %d folder(s) to browse", n);
    return n;
}

int DrivesIsRoot(const char *dir)
{
    int i;
    for (i = 0; i < ROOT_COUNT; i++)
        if (!strcmp(dir, s_roots[i])) return 1;
    return 0;
}
