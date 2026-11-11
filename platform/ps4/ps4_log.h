// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - log: /data/Harissa64-PS4/harissa64.log and the kernel log
// (GoldHEN's klog server on port 3232 shows it on the PC).
#ifndef PS4_LOG_H
#define PS4_LOG_H

#define PS4_DATA_DIR "/data/Harissa64-PS4"

void ps4_log_open(void);
void ps4_log_close(void);
void ps4_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
double ps4_now_ms(void);

#endif
