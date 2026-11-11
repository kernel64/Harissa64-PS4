// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 - HTTPS downloads from the console (the update check, box art and
// game information).
//
// The Xbox 360 libraries speak no modern TLS: BearSSL (third_party/bearssl,
// MIT) does TLS 1.2. Server certificates are not checked against root
// certificates (the console has none to give BearSSL), so a download can only
// be trusted as much as the network: callers use what they fetch as data
// (version numbers, pictures, text), never as code, and check it as such.
// Sockets to the internet need XNET_STARTUP_BYPASS_SECURITY and the
// "insecure" socket option, which RGH/JTAG kernels honour.
//
// Calls block (DNS, connection, transfer; 10 s time-outs): use them from a
// worker thread, never from the UI thread. Several threads may download at
// the same time (each call has its own TLS state).
#ifndef XB_NET_H
#define XB_NET_H

#include "../../../core/common/h64_types.h"

int NetStart(int retry);

void NetTestOff(int mode);
int NetTestMode(void);

int NetGet(const char *host, const char *path, int follow, u8 **body, u32 *len, u32 maxLen, char *location, int locSize);

int NetPost(const char *host, const char *path, const char *data, const char *type, const char *agent, u8 **body, u32 *len, u32 maxLen);
int NetDownload(const char *url, const char *file, u32 expected, int (*progress)(void *user, u32 done, u32 total), void *user);
int NetSha256File(const char *file, char hex[65], int (*progress)(void *user, u32 done, u32 total), void *user);

#endif
