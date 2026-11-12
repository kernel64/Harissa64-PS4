// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - what the Xbox 360 front end (platform/xbox360, copied into
// this folder with its file names) expects from <xtl.h>: Win32 integer types,
// D3DCOLOR, XInput's controller state and constants, tick counts, the few
// Win32 file, thread and lock calls it makes, and a stand-in for the D3D
// device of its menu loops. The files keep their Xbox code; this header maps
// it onto the PS4 (XInputGetState reads the DualShock 4 through ps4_input,
// already in XInput's shape; files go through the C library; threads are
// pthreads).
//
// Paths: the Xbox code writes "game:\roms\" with backslashes; the PS4 copies
// use POSIX paths ("/data/Harissa64-PS4/roms/") and XB_SEP where the Xbox code
// cuts or joins at a backslash.
#ifndef XB_COMPAT_H
#define XB_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "../../../core/pif/h64_padmap.h"

#define XB_PS4 1
#define XB_HAS_RES_AUTO 1
#define XB_HAS_NET 1
#define XB_HAS_HD 0
#define XB_HAS_UPDATE 0
#define XB_GAME_DIR "/data/Harissa64-PS4"
#define XB_SEP '/'
#define XB_SEP_S "/"

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t BOOL;
typedef int32_t LONG;
typedef int16_t SHORT;
typedef char CHAR;
typedef void *LPVOID;
typedef void *HANDLE;
typedef uint32_t D3DCOLOR;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define WINAPI
#define MAX_PATH 260
#define D3DCOLOR_ARGB(a, r, g, b) ((D3DCOLOR)((((a) & 0xFF) << 24) | (((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF)))
#define D3DCOLOR_XRGB(r, g, b) D3DCOLOR_ARGB(0xFF, r, g, b)
#define D3DCLEAR_TARGET 1

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _snprintf snprintf

struct IDirect3DDevice9
{
    void Clear(DWORD count, const void *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    void Present(const void *src, const void *dst, void *wnd, const void *dirty);
};
IDirect3DDevice9 *XbDevice(void);
struct IDirect3DTexture9;

#define ERROR_SUCCESS 0
#define XUSER_MAX_COUNT 4
#define XINPUT_GAMEPAD_DPAD_UP        H64_XB_DPAD_UP
#define XINPUT_GAMEPAD_DPAD_DOWN      H64_XB_DPAD_DOWN
#define XINPUT_GAMEPAD_DPAD_LEFT      H64_XB_DPAD_LEFT
#define XINPUT_GAMEPAD_DPAD_RIGHT     H64_XB_DPAD_RIGHT
#define XINPUT_GAMEPAD_START          H64_XB_START
#define XINPUT_GAMEPAD_BACK           H64_XB_BACK
#define XINPUT_GAMEPAD_LEFT_THUMB     H64_XB_LEFT_THUMB
#define XINPUT_GAMEPAD_RIGHT_THUMB    H64_XB_RIGHT_THUMB
#define XINPUT_GAMEPAD_LEFT_SHOULDER  H64_XB_LEFT_SHOULDER
#define XINPUT_GAMEPAD_RIGHT_SHOULDER H64_XB_RIGHT_SHOULDER
#define XINPUT_GAMEPAD_A              H64_XB_A
#define XINPUT_GAMEPAD_B              H64_XB_B
#define XINPUT_GAMEPAD_X              H64_XB_X
#define XINPUT_GAMEPAD_Y              H64_XB_Y

struct XINPUT_GAMEPAD
{
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XINPUT_STATE
{
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};
DWORD XInputGetState(DWORD user, XINPUT_STATE *state);

DWORD GetTickCount(void);
void Sleep(DWORD ms);

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define FILE_BEGIN 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_NO_MORE_FILES 18

HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security, DWORD creation, DWORD attributes, HANDLE tmpl);
BOOL ReadFile(HANDLE h, void *buf, DWORD len, DWORD *got, void *overlapped);
BOOL WriteFile(HANDLE h, const void *buf, DWORD len, DWORD *put, void *overlapped);
DWORD SetFilePointer(HANDLE h, LONG offset, LONG *high, DWORD method);
DWORD GetFileSize(HANDLE h, DWORD *high);
BOOL CloseHandle(HANDLE h);
BOOL DeleteFileA(const char *path);
DWORD GetFileAttributesA(const char *path);
BOOL CreateDirectoryA(const char *path, void *security);
#define CreateDirectory CreateDirectoryA
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; };
enum { GetFileExInfoStandard = 0 };
BOOL GetFileAttributesExA(const char *path, int level, WIN32_FILE_ATTRIBUTE_DATA *out);
void GetSystemTimeAsFileTime(FILETIME *t);
union ULARGE_INTEGER { struct { DWORD LowPart, HighPart; }; uint64_t QuadPart; };
struct LARGE_INTEGER { long long QuadPart; };
void QueryPerformanceCounter(LARGE_INTEGER *t);
void QueryPerformanceFrequency(LARGE_INTEGER *t);

struct WIN32_FIND_DATAA
{
    DWORD dwFileAttributes;
    char cFileName[MAX_PATH];
};
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd);
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd);
BOOL FindClose(HANDLE h);
DWORD GetLastError(void);

#define CREATE_SUSPENDED 4
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
HANDLE CreateThread(void *security, size_t stack, DWORD (*fn)(LPVOID), LPVOID arg, DWORD flags, DWORD *id);
DWORD ResumeThread(HANDLE thread);
BOOL SetThreadPriority(HANDLE thread, int priority);
DWORD XSetThreadProcessor(HANDLE thread, DWORD cpu);

struct CRITICAL_SECTION { void *impl; };
void InitializeCriticalSection(CRITICAL_SECTION *cs);
void EnterCriticalSection(CRITICAL_SECTION *cs);
void LeaveCriticalSection(CRITICAL_SECTION *cs);

HANDLE CreateEvent(void *security, BOOL manualReset, BOOL initial, const char *name);
BOOL SetEvent(HANDLE ev);
DWORD WaitForSingleObject(HANDLE ev, DWORD ms);

static inline LONG InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }

#endif
