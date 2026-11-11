// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - scripted controller input, shared by h64test (--input) and
// the Xbox front end (input= in harissa64v2.ini), so that the same scene
// can be played on the host and in Xenia.
//
// Syntax: comma-separated events BUTTON@first[-last] (VI frames), buttons
// A B Z START L R DU DD DL DR CU CD CL CR; stick X=n@.. and Y=n@..
#ifndef H64_INPUT_SCRIPT_H
#define H64_INPUT_SCRIPT_H

#include "../common/h64_types.h"

struct H64System;

struct H64InputEvent
{
    u32 first, last;
    u16 buttons;
    int axis;
    int value;
};

struct H64InputScript
{
    H64InputEvent events[256];
    int count;
};

int h64_input_script_parse(H64InputScript *s, const char *text);
void h64_input_script_apply(const H64InputScript *s, H64System *sys);
void h64_input_script_apply_at(const H64InputScript *s, H64System *sys, u32 frame);

#endif
