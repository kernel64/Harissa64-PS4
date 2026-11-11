// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - event scheduler.
//
// One timeline in CPU cycles (PClock, 93.75 MHz). Every timed device event
// (Count/Compare, VI lines, AI/PI/SI DMA completion, RSP/RDP tasks) is an
// entry with an absolute timestamp; the run loop executes CPU instructions
// until the earliest one is due. No hidden timing: everything that happens
// "later" goes through here.
#ifndef H64_SCHEDULER_H
#define H64_SCHEDULER_H

#include "../common/h64_types.h"

enum H64Event
{
    H64_EV_COMPARE = 0,
    H64_EV_VI,
    H64_EV_AI,
    H64_EV_PI,
    H64_EV_SI,
    H64_EV_SP,
    H64_EV_DP,
    H64_EV_RSP,
    H64_EV_COUNT
};

#define H64_NEVER 0xFFFFFFFFFFFFFFFFull

struct H64Scheduler
{
    u64 when[H64_EV_COUNT];
    u64 next;
};

void h64_sched_init(H64Scheduler *s);
void h64_sched_set(H64Scheduler *s, int ev, u64 when);
void h64_sched_cancel(H64Scheduler *s, int ev);
int h64_sched_pop_due(H64Scheduler *s, u64 now);

#endif
