// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - log.
#include "ps4_log.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <orbis/libkernel.h>

#include "../../core/common/h64_log.h"

static int s_fd = -1;

static void core_sink(int level, const char *line)
{
    static const char *tags[] = {"E", "W", "I", "D"};
    ps4_log("[%s] %s", level >= 0 && level < 4 ? tags[level] : "?", line);
}

void ps4_log_open(void)
{
    sceKernelMkdir(PS4_DATA_DIR, 0777);
    rename(PS4_DATA_DIR "/harissa64.log", PS4_DATA_DIR "/harissa64.prev.log");
    s_fd = open(PS4_DATA_DIR "/harissa64.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int err = open(PS4_DATA_DIR "/harissa64-stderr.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (err >= 0)
    {
        dup2(err, 2);
        if (err != 2) close(err);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    h64_log_set_sink(core_sink);
}

void ps4_log_close(void)
{
    fflush(stderr);
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
}

void ps4_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    if (line[n - 1] != '\n') line[n++] = '\n';
    line[n] = 0;
    if (s_fd >= 0) write(s_fd, line, (size_t)n);
    sceKernelDebugOutText(0, line);
}

double ps4_now_ms(void)
{
    return (double)sceKernelGetProcessTime() / 1000.0;
}
