// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 PS4 - the front end's own achievements (the Xbox 360 version's
// xb_ach.h: its title's achievements, given to the signed-in profile).
//
// PS4: nothing yet. A homebrew cannot give PlayStation trophies (they need a
// trophy pack signed for the title), so XbAchUnlock does nothing here; the
// calls stay in the shared front-end code.
#ifndef XB_ACH_H
#define XB_ACH_H

#include "xb_ach_ids.h"

static inline void XbAchInit(int) {}
static inline void XbAchUnlock(int) {}

#endif
