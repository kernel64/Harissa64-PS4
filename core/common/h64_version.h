// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - version string, from the VERSION file at the repository
// root (passed by every build as H64_VERSION).
#ifndef H64_VERSION_H
#define H64_VERSION_H

#define H64_STRINGIFY2(x) #x
#define H64_STRINGIFY(x) H64_STRINGIFY2(x)

#ifdef H64_VERSION
#define H64_VERSION_STRING H64_STRINGIFY(H64_VERSION)
#else
#define H64_VERSION_STRING "unknown"
#endif

#endif
