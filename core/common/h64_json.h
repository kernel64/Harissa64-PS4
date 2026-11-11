// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - a small JSON reader: reports the string members of objects
// at depth 1 ({"k1": "v"}) and 2 ({"k1": {"k2": "v"}}); numbers, booleans,
// null and array items are skipped. Strings are decoded to UTF-8 (\n, \r
// and \t become spaces). Used for game information and the texture pack catalogue.
#ifndef H64_JSON_H
#define H64_JSON_H

#include "h64_types.h"
#include <string>

typedef void (*H64JsonFn)(void *user, int depth, const std::string &k1, const std::string &k2, const std::string &value);
int h64_json_walk(const char *text, u32 len, H64JsonFn fn, void *user);

#endif
