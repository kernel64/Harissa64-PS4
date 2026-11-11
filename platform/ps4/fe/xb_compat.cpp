// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the Win32/XInput/D3D pieces the Xbox front end uses (see xb_compat.h).
#include "xb_compat.h"
#include "../ps4_gpu.h"
#include "../ps4_input.h"

#include <sys/time.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <orbis/libkernel.h>

DWORD XInputGetState(DWORD user, XINPUT_STATE *state)
{
    H64PadInput in;
    if (!state || !ps4_input_read((int)user, &in)) return 1;
    state->dwPacketNumber++;
    state->Gamepad.wButtons = in.buttons;
    state->Gamepad.bLeftTrigger = in.lt;
    state->Gamepad.bRightTrigger = in.rt;
    state->Gamepad.sThumbLX = in.lx;
    state->Gamepad.sThumbLY = in.ly;
    state->Gamepad.sThumbRX = in.rx;
    state->Gamepad.sThumbRY = in.ry;
    return ERROR_SUCCESS;
}

DWORD GetTickCount(void)
{
    return (DWORD)(sceKernelGetProcessTime() / 1000);
}

static int s_justPresented;

void Sleep(DWORD ms)
{
    if (s_justPresented && ms <= 16) { s_justPresented = 0; return; }
    s_justPresented = 0;
    sceKernelUsleep(ms * 1000);
}

void IDirect3DDevice9::Clear(DWORD, const void *, DWORD, D3DCOLOR, float, DWORD)
{
    if (!ps4_gpu_in_frame()) ps4_gpu_begin_frame();
}

void IDirect3DDevice9::Present(const void *, const void *, void *, const void *)
{
    if (!ps4_gpu_in_frame()) ps4_gpu_begin_frame();
    ps4_gpu_end_frame();
    s_justPresented = 1;
}

IDirect3DDevice9 *XbDevice(void)
{
    static IDirect3DDevice9 dev;
    return &dev;
}

static DWORD s_lastError;

HANDLE CreateFileA(const char *path, DWORD access, DWORD, void *, DWORD creation, DWORD, HANDLE)
{
    FILE *f;
    if (!path) return INVALID_HANDLE_VALUE;
    if (access & GENERIC_WRITE) f = fopen(path, creation == CREATE_ALWAYS ? "wb" : "r+b");
    else f = fopen(path, "rb");
    if (!f) { s_lastError = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
    return (HANDLE)f;
}

BOOL ReadFile(HANDLE h, void *buf, DWORD len, DWORD *got, void *)
{
    size_t n;
    if (got) *got = 0;
    if (h == INVALID_HANDLE_VALUE || !h) return FALSE;
    n = fread(buf, 1, len, (FILE *)h);
    if (got) *got = (DWORD)n;
    return n == len || !ferror((FILE *)h);
}

BOOL WriteFile(HANDLE h, const void *buf, DWORD len, DWORD *put, void *)
{
    size_t n;
    if (put) *put = 0;
    if (h == INVALID_HANDLE_VALUE || !h) return FALSE;
    n = fwrite(buf, 1, len, (FILE *)h);
    if (put) *put = (DWORD)n;
    return n == len;
}

DWORD SetFilePointer(HANDLE h, LONG offset, LONG *, DWORD method)
{
    if (h == INVALID_HANDLE_VALUE || !h) return INVALID_SET_FILE_POINTER;
    if (fseek((FILE *)h, (long)(DWORD)offset, method == FILE_BEGIN ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END)) return INVALID_SET_FILE_POINTER;
    return (DWORD)ftell((FILE *)h);
}

DWORD GetFileSize(HANDLE h, DWORD *high)
{
    long pos, end;
    if (high) *high = 0;
    if (h == INVALID_HANDLE_VALUE || !h) return INVALID_FILE_SIZE;
    pos = ftell((FILE *)h);
    if (fseek((FILE *)h, 0, SEEK_END)) return INVALID_FILE_SIZE;
    end = ftell((FILE *)h);
    fseek((FILE *)h, pos < 0 ? 0 : pos, SEEK_SET);
    return end < 0 || end > 0xFFFFFFFEl ? INVALID_FILE_SIZE : (DWORD)end;
}

BOOL CloseHandle(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE || !h) return FALSE;
    return fclose((FILE *)h) == 0;
}

BOOL DeleteFileA(const char *path) { return path && sceKernelUnlink(path) == 0; }

DWORD GetFileAttributesA(const char *path)
{
    DIR *d;
    FILE *f;
    if (!path || !path[0]) return 0xFFFFFFFFu;
    if ((d = opendir(path)) != NULL) { closedir(d); return FILE_ATTRIBUTE_DIRECTORY; }
    if ((f = fopen(path, "rb")) != NULL) { fclose(f); return FILE_ATTRIBUTE_NORMAL; }
    return 0xFFFFFFFFu;
}

BOOL CreateDirectoryA(const char *path, void *)
{
    return path && sceKernelMkdir(path, 0777) == 0;
}

BOOL GetFileAttributesExA(const char *, int, WIN32_FILE_ATTRIBUTE_DATA *out)
{
    if (out) memset(out, 0, sizeof(*out));
    return FALSE;
}

void QueryPerformanceCounter(LARGE_INTEGER *t) { t->QuadPart = (long long)sceKernelGetProcessTime(); }
void QueryPerformanceFrequency(LARGE_INTEGER *t) { t->QuadPart = 1000000; }

void GetSystemTimeAsFileTime(FILETIME *t)
{
    struct timeval tv;
    uint64_t ft;
    if (!t) return;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    gettimeofday(&tv, NULL);
    ft = ((uint64_t)tv.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)tv.tv_usec * 10ull;
    t->dwLowDateTime = (DWORD)ft;
    t->dwHighDateTime = (DWORD)(ft >> 32);
}

struct FindState
{
    DIR *dir;
    char folder[MAX_PATH];
};

static BOOL find_fill(FindState *s, WIN32_FIND_DATAA *fd)
{
    struct dirent *e;
    if (!s->dir || !(e = readdir(s->dir))) { s_lastError = ERROR_NO_MORE_FILES; return FALSE; }
    strncpy(fd->cFileName, e->d_name, sizeof(fd->cFileName) - 1);
    fd->cFileName[sizeof(fd->cFileName) - 1] = 0;
    fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    if (e->d_type == DT_DIR) fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    else if (e->d_type == DT_UNKNOWN)
    {
        char full[2 * MAX_PATH];
        DIR *d;
        snprintf(full, sizeof(full), "%s%s", s->folder, e->d_name);
        if ((d = opendir(full)) != NULL) { closedir(d); fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; }
    }
    return TRUE;
}

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd)
{
    FindState *s;
    size_t n;
    if (!pattern || !fd) return INVALID_HANDLE_VALUE;
    s = (FindState *)calloc(1, sizeof(FindState));
    if (!s) return INVALID_HANDLE_VALUE;
    strncpy(s->folder, pattern, sizeof(s->folder) - 1);
    n = strlen(s->folder);
    if (n && s->folder[n - 1] == '*') s->folder[--n] = 0;
    if (!n) strcpy(s->folder, "./");
    s->dir = opendir(s->folder);
    if (!s->dir) { free(s); s_lastError = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
    if (s->folder[strlen(s->folder) - 1] != '/') strncat(s->folder, "/", sizeof(s->folder) - strlen(s->folder) - 1);
    if (!find_fill(s, fd)) { closedir(s->dir); free(s); return INVALID_HANDLE_VALUE; }
    return (HANDLE)s;
}

BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd)
{
    if (h == INVALID_HANDLE_VALUE || !h || !fd) return FALSE;
    return find_fill((FindState *)h, fd);
}

BOOL FindClose(HANDLE h)
{
    FindState *s = (FindState *)h;
    if (h == INVALID_HANDLE_VALUE || !s) return FALSE;
    if (s->dir) closedir(s->dir);
    free(s);
    return TRUE;
}

DWORD GetLastError(void) { return s_lastError; }

struct ThreadState
{
    pthread_t thread;
    DWORD (*fn)(LPVOID);
    LPVOID arg;
    size_t stack;
    int started;
};

static void *thread_main(void *p)
{
    ThreadState *t = (ThreadState *)p;
    t->fn(t->arg);
    return NULL;
}

static int thread_start(ThreadState *t)
{
    pthread_attr_t attr;
    int rc;
    if (t->started) return 0;
    pthread_attr_init(&attr);
    if (t->stack) pthread_attr_setstacksize(&attr, t->stack < 65536 ? 65536 : t->stack);
    rc = pthread_create(&t->thread, &attr, thread_main, t);
    pthread_attr_destroy(&attr);
    if (rc) return -1;
    t->started = 1;
    return 0;
}

HANDLE CreateThread(void *, size_t stack, DWORD (*fn)(LPVOID), LPVOID arg, DWORD flags, DWORD *id)
{
    ThreadState *t = (ThreadState *)calloc(1, sizeof(ThreadState));
    if (id) *id = 0;
    if (!t || !fn) { free(t); return NULL; }
    t->fn = fn;
    t->arg = arg;
    t->stack = stack;
    if (!(flags & CREATE_SUSPENDED) && thread_start(t)) { free(t); return NULL; }
    return (HANDLE)t;
}

DWORD ResumeThread(HANDLE thread)
{
    ThreadState *t = (ThreadState *)thread;
    if (!t) return 0xFFFFFFFFu;
    return thread_start(t) ? 0xFFFFFFFFu : 1;
}

BOOL SetThreadPriority(HANDLE, int) { return TRUE; }
DWORD XSetThreadProcessor(HANDLE, DWORD) { return 0; }

void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t a;
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    cs->impl = m;
    if (!m) return;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
}

void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs && cs->impl) pthread_mutex_lock((pthread_mutex_t *)cs->impl);
}

void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs && cs->impl) pthread_mutex_unlock((pthread_mutex_t *)cs->impl);
}

struct EventState
{
    volatile int set;
};

HANDLE CreateEvent(void *, BOOL, BOOL initial, const char *)
{
    EventState *e = (EventState *)calloc(1, sizeof(EventState));
    if (e) e->set = initial ? 1 : 0;
    return (HANDLE)e;
}

BOOL SetEvent(HANDLE ev)
{
    EventState *e = (EventState *)ev;
    if (!e) return FALSE;
    __atomic_store_n(&e->set, 1, __ATOMIC_SEQ_CST);
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE ev, DWORD ms)
{
    EventState *e = (EventState *)ev;
    DWORD start = GetTickCount();
    if (!e) return WAIT_TIMEOUT;
    for (;;)
    {
        if (__atomic_exchange_n(&e->set, 0, __ATOMIC_SEQ_CST)) return WAIT_OBJECT_0;
        if (ms != INFINITE && GetTickCount() - start >= ms) return WAIT_TIMEOUT;
        sceKernelUsleep(2000);
    }
}
