// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
#include "h64_fenv.h"

#if defined(_MSC_VER)
#include <float.h>

static void init_env(void)
{
#if defined(_DN_SAVE) && defined(_MCW_DN)
    _controlfp(_DN_SAVE, _MCW_DN);
#endif
    _controlfp(_MCW_EM, _MCW_EM);
    _controlfp(_RC_NEAR, _MCW_RC);
}

void h64_fenv_set_round(int rm)
{
    static const unsigned int modes[4] = { _RC_NEAR, _RC_CHOP, _RC_UP, _RC_DOWN };
    _controlfp(modes[rm & 3], _MCW_RC);
}

#if defined(_XBOX)
#include <ppcintrinsics.h>

__declspec(noinline) static void fpscr_read(double *out)
{
    __emit(0xFC00048E);
    __emit(0xD8030000);
}

__declspec(noinline) static void fpscr_write(const double *in)
{
    __emit(0xC8030000);
    __emit(0xFDFE058E);
}

static void (*volatile s_fpscrRead)(double *) = fpscr_read;
static void (*volatile s_fpscrWrite)(const double *) = fpscr_write;

static u32 fpscr_get(void)
{
    union { double d; u64 u; } v;
    v.u = 0;
    s_fpscrRead(&v.d);
    return (u32)v.u;
}

static void clear_flags(void)
{
    union { double d; u64 u; } v;
    v.u = 0;
    s_fpscrRead(&v.d);
    v.u &= ~(u64)0xFFFFFF00u;
    s_fpscrWrite(&v.d);
}

static u32 read_flags(void)
{
    u32 s = fpscr_get(), f = 0;
    if (s & 0x02000000u) f |= H64_FE_INEXACT;
    if (s & 0x08000000u) f |= H64_FE_UNDERFLOW;
    if (s & 0x10000000u) f |= H64_FE_OVERFLOW;
    if (s & 0x04000000u) f |= H64_FE_DIVZERO;
    if (s & 0x20000000u) f |= H64_FE_INVALID;
    return f;
}
#else
static void clear_flags(void) { _clearfp(); }

static u32 read_flags(void)
{
    unsigned int s = _statusfp();
    u32 f = 0;
    if (s & _SW_INEXACT) f |= H64_FE_INEXACT;
    if (s & _SW_UNDERFLOW) f |= H64_FE_UNDERFLOW;
    if (s & _SW_OVERFLOW) f |= H64_FE_OVERFLOW;
    if (s & _SW_ZERODIVIDE) f |= H64_FE_DIVZERO;
    if (s & _SW_INVALID) f |= H64_FE_INVALID;
    return f;
}
#endif

#else
#include <fenv.h>

static void init_env(void)
{
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
}

void h64_fenv_set_round(int rm)
{
    static const int modes[4] = { FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD };
    fesetround(modes[rm & 3]);
}

static void clear_flags(void) { feclearexcept(FE_ALL_EXCEPT); }

static u32 read_flags(void)
{
    int s = fetestexcept(FE_ALL_EXCEPT);
    u32 f = 0;
    if (s & FE_INEXACT) f |= H64_FE_INEXACT;
    if (s & FE_UNDERFLOW) f |= H64_FE_UNDERFLOW;
    if (s & FE_OVERFLOW) f |= H64_FE_OVERFLOW;
    if (s & FE_DIVBYZERO) f |= H64_FE_DIVZERO;
    if (s & FE_INVALID) f |= H64_FE_INVALID;
    return f;
}
#endif

#include "h64_log.h"

static int s_checked, s_reliable = 1;

void h64_fenv_disable_host_flags(void)
{
    s_checked = 1;
    s_reliable = 0;
}

void h64_fenv_clear(void)
{
    if (s_reliable) clear_flags();
}

void h64_fenv_init(void)
{
    init_env();
    if (!s_checked)
    {
        volatile float one = 1.0f, three = 3.0f, r;
        u32 a, b;
        h64_fenv_clear();
        r = one + one;
        a = read_flags();
        h64_fenv_clear();
        r = one / three;
        b = read_flags();
        h64_fenv_clear();
        (void)r;
        s_reliable = a == 0 && b == H64_FE_INEXACT;
        s_checked = 1;
        if (!s_reliable)
            H64_WARN("[fpu] host exception flags are not reliable (read %02X and %02X): the FPU reports none", a, b);
    }
}

int h64_fenv_reliable(void) { return s_reliable; }

u32 h64_fenv_flags(void) { return s_reliable ? read_flags() : 0; }

#if defined(_XBOX)
static int s_ctrlKnown;
static u64 s_ctrl;

void h64_fenv_begin(int rm)
{
    union { double d; u64 u; } v;
    if (!s_reliable) { h64_fenv_set_round(rm); return; }
    if (!s_ctrlKnown)
    {
        v.u = 0;
        s_fpscrRead(&v.d);
        s_ctrl = v.u & 0xFCu;
        s_ctrlKnown = 1;
    }
    v.u = s_ctrl | (u32)(rm & 3);
    s_fpscrWrite(&v.d);
}
#else
void h64_fenv_begin(int rm)
{
    h64_fenv_set_round(rm);
    h64_fenv_clear();
}
#endif
