// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - DualShock 4 input.
//
// The core's controller mapping (core/pif/h64_padmap.*) works on an XInput-
// shaped state, the Xbox front end's: each DualShock 4 is turned into that
// shape (Cross = A, Circle = B, Square = X, Triangle = Y, L1/R1 = LB/RB,
// L2/R2 = LT/RT, OPTIONS = START, touch pad button = BACK, the front end's
// menu button), so the Harissa64 mappings and their settings apply as they are.
#ifndef PS4_INPUT_H
#define PS4_INPUT_H

#include "../../core/pif/h64_padmap.h"

int ps4_input_init(void);
int ps4_input_read(int port, H64PadInput *out);
void ps4_input_rumble(int port, int on);
void ps4_input_rumble_pulse(int port, int ms);

#endif
