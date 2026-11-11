// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - host floating-point environment (rounding mode, exception
// flags, denormals) behind one interface for MSVC, the XDK and gcc.
//
// The reference FPU computes with the host's IEEE arithmetic in the guest's
// rounding mode and reads the host's exception flags afterwards. Denormals
// must not be flushed (the XDK's default control word flushes them).
#ifndef H64_FENV_H
#define H64_FENV_H

#include "h64_types.h"

enum { H64_RM_NEAREST = 0, H64_RM_ZERO = 1, H64_RM_UP = 2, H64_RM_DOWN = 3 };

#define H64_FE_INEXACT   0x01u
#define H64_FE_UNDERFLOW 0x02u
#define H64_FE_OVERFLOW  0x04u
#define H64_FE_DIVZERO   0x08u
#define H64_FE_INVALID   0x10u

void h64_fenv_init(void);
void h64_fenv_set_round(int mipsRm);
void h64_fenv_clear(void);
void h64_fenv_begin(int mipsRm);
u32 h64_fenv_flags(void);
int h64_fenv_reliable(void);
void h64_fenv_disable_host_flags(void);

#endif
