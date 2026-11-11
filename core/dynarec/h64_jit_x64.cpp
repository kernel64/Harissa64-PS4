// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 - recompiler code generation for x86-64 hosts (the PS4's Jaguar,
// PCs for the tests). Same contract as the PowerPC generator (h64_jit_gen.cpp):
// blocks made by the dispatcher of h64_jit.cpp, native code for the
// instructions the generator knows, the reference interpreter for the others,
// the interpreter's exact timing, h64test --lockstep to check it.
//
// Block layout:
//   entered through H64Jit.enter (shared prologue): callee-saved registers
//   pushed, rbx = &sys->cpu, r12 = sys, r13 = RDRAM, r14 = H64Jit.codeMap;
//   rbp and r15 hold a branch's outcome and target across its delay slot.
//   body: one sequence per MIPS instruction; every MIPS register lives in
//   H64Cpu (memory operands; no register cache yet).
//   tails: the slow paths (the interpreter runs the instruction), after the body.
//   exits: jumps to H64Jit.rtExit (shared epilogue).
//
// Accounting as in the PowerPC generator: native instructions are counted
// (`pending`) and written to H64Cpu's cycles, instructions and pc ("sync")
// before the interpreter runs and when leaving the block.
//
// Both RDRAM layouts are handled (h64_rdram.h): host words (H64_RDRAM_SWAP=1,
// the PS4: word accesses need no byte swap) and N64 byte order (bswap).
// Calling conventions: System V (PS4, Linux) and Windows x64.
#include "h64_jit.h"

#if defined(H64_JIT_ABI_X64)

#include "h64_jit_internal.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../common/h64_log.h"
#include "../system/h64_system.h"
#include "../memory/h64_rdram.h"

struct X64
{
    u8 *buf;
    u32 pos, cap;
    int overflow;
};

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { X_CPU = RBX, X_SYS = R12, X_RAM = R13, X_MAP = R14, X_TGT = R15, X_COND = RBP };
#if defined(_WIN32)
enum { ARG0 = RCX, ARG1 = RDX, ARG2 = R8, ARG3 = R9 };
#else
enum { ARG0 = RDI, ARG1 = RSI, ARG2 = RDX, ARG3 = RCX };
#endif
enum { CC_O = 0, CC_B = 2, CC_AE = 3, CC_E = 4, CC_NE = 5, CC_A = 7, CC_P = 10, CC_L = 12, CC_GE = 13, CC_LE = 14, CC_G = 15 };
enum { G_ADD = 0, G_OR = 1, G_AND = 4, G_SUB = 5, G_XOR = 6, G_CMP = 7 };
enum { SH_SHL = 4, SH_SHR = 5, SH_SAR = 7 };

static void p8(X64 *c, u32 v)
{
    if (c->pos < c->cap) c->buf[c->pos++] = (u8)v;
    else c->overflow = 1;
}
static void p32(X64 *c, u32 v) { p8(c, v); p8(c, v >> 8); p8(c, v >> 16); p8(c, v >> 24); }
static void p64(X64 *c, u64 v) { p32(c, (u32)v); p32(c, (u32)(v >> 32)); }

static void rex(X64 *c, int w, u32 reg, int index, u32 base)
{
    u32 r = 0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((index >= 0 && (index & 8)) ? 2 : 0) | ((base & 8) ? 1 : 0);
    if (r != 0x40) p8(c, r);
}
static void opcode(X64 *c, u32 op)
{
    if (op > 0xFF) p8(c, op >> 8);
    p8(c, op);
}
static void modrm_mem(X64 *c, u32 reg, u32 base, int index, u32 scale, s32 disp)
{
    u32 mod = (disp == 0 && (base & 7) != 5) ? 0 : (disp >= -128 && disp <= 127) ? 1 : 2;
    if (index >= 0 || (base & 7) == 4)
    {
        p8(c, (mod << 6) | ((reg & 7) << 3) | 4);
        p8(c, (scale << 6) | (((index >= 0 ? (u32)index : 4u) & 7) << 3) | (base & 7));
    }
    else
        p8(c, (mod << 6) | ((reg & 7) << 3) | (base & 7));
    if (mod == 1) p8(c, (u32)disp);
    else if (mod == 2) p32(c, (u32)disp);
}
static void x_rm(X64 *c, int w, u32 op, u32 reg, u32 base, s32 disp)
{
    rex(c, w, reg, -1, base);
    opcode(c, op);
    modrm_mem(c, reg, base, -1, 0, disp);
}
static void x_rx(X64 *c, int w, u32 op, u32 reg, u32 base, u32 index, u32 scale, s32 disp, int pre66)
{
    if (pre66) p8(c, 0x66);
    rex(c, w, reg, (int)index, base);
    opcode(c, op);
    modrm_mem(c, reg, base, (int)index, scale, disp);
}
static void x_rr(X64 *c, int w, u32 op, u32 reg, u32 rm)
{
    rex(c, w, reg, -1, rm);
    opcode(c, op);
    p8(c, 0xC0 | ((reg & 7) << 3) | (rm & 7));
}

static void mov_rm64(X64 *c, u32 reg, u32 base, s32 disp) { x_rm(c, 1, 0x8B, reg, base, disp); }
static void mov_rm32(X64 *c, u32 reg, u32 base, s32 disp) { x_rm(c, 0, 0x8B, reg, base, disp); }
static void mov_mr64(X64 *c, u32 base, s32 disp, u32 reg) { x_rm(c, 1, 0x89, reg, base, disp); }
static void mov_mr32(X64 *c, u32 base, s32 disp, u32 reg) { x_rm(c, 0, 0x89, reg, base, disp); }
static void mov_rr64(X64 *c, u32 dst, u32 src) { x_rr(c, 1, 0x8B, dst, src); }
static void mov_rr32(X64 *c, u32 dst, u32 src) { x_rr(c, 0, 0x8B, dst, src); }
static void movsxd_rr(X64 *c, u32 dst, u32 src) { x_rr(c, 1, 0x63, dst, src); }
static void xor_rr32(X64 *c, u32 r) { x_rr(c, 0, 0x33, r, r); }
static void alu_rm(X64 *c, int w, u32 g, u32 reg, u32 base, s32 disp) { x_rm(c, w, 8 * g + 3, reg, base, disp); }
static void alu_rr(X64 *c, int w, u32 g, u32 dst, u32 src) { x_rr(c, w, 8 * g + 3, dst, src); }
static void alu_ri(X64 *c, int w, u32 g, u32 reg, s32 imm)
{
    rex(c, w, 0, -1, reg);
    if (imm >= -128 && imm <= 127) { p8(c, 0x83); p8(c, 0xC0 | (g << 3) | (reg & 7)); p8(c, (u32)imm); }
    else { p8(c, 0x81); p8(c, 0xC0 | (g << 3) | (reg & 7)); p32(c, (u32)imm); }
}
static void alu_mi(X64 *c, int w, u32 g, u32 base, s32 disp, s32 imm)
{
    rex(c, w, 0, -1, base);
    if (imm >= -128 && imm <= 127) { p8(c, 0x83); modrm_mem(c, g, base, -1, 0, disp); p8(c, (u32)imm); }
    else { p8(c, 0x81); modrm_mem(c, g, base, -1, 0, disp); p32(c, (u32)imm); }
}
static void shift_ri(X64 *c, int w, u32 kind, u32 reg, u32 n)
{
    if (!n) return;
    rex(c, w, 0, -1, reg);
    p8(c, 0xC1);
    p8(c, 0xC0 | (kind << 3) | (reg & 7));
    p8(c, n);
}
static void shift_cl(X64 *c, int w, u32 kind, u32 reg)
{
    rex(c, w, 0, -1, reg);
    p8(c, 0xD3);
    p8(c, 0xC0 | (kind << 3) | (reg & 7));
}
static void not_r64(X64 *c, u32 reg) { rex(c, 1, 0, -1, reg); p8(c, 0xF7); p8(c, 0xD0 | (reg & 7)); }
static void test_rr(X64 *c, int w, u32 a, u32 b) { x_rr(c, w, 0x85, b, a); }
static void setcc_reg(X64 *c, u32 cc, u32 reg)
{
    p8(c, 0x0F); p8(c, 0x90 + cc); p8(c, 0xC0);
    x_rr(c, 0, 0x0FB6, reg, RAX);
}
static void cmov_rr64(X64 *c, u32 cc, u32 dst, u32 src) { x_rr(c, 1, 0x0F40 + cc, dst, src); }
static void mov_ri32(X64 *c, u32 reg, u32 imm) { rex(c, 0, 0, -1, reg); p8(c, 0xB8 + (reg & 7)); p32(c, imm); }
static void mov_ri64(X64 *c, u32 reg, u64 v)
{
    if (v <= 0xFFFFFFFFull) mov_ri32(c, reg, (u32)v);
    else if ((s64)v == (s64)(s32)(u32)v) { rex(c, 1, 0, -1, reg); p8(c, 0xC7); p8(c, 0xC0 | (reg & 7)); p32(c, (u32)v); }
    else { rex(c, 1, 0, -1, reg); p8(c, 0xB8 + (reg & 7)); p64(c, v); }
}
static void mov_m64_imm(X64 *c, u32 base, s32 disp, u64 v)
{
    if ((s64)v == (s64)(s32)(u32)v) { rex(c, 1, 0, -1, base); p8(c, 0xC7); modrm_mem(c, 0, base, -1, 0, disp); p32(c, (u32)v); }
    else { mov_ri64(c, RAX, v); mov_mr64(c, base, disp, RAX); }
}
static void mov_m32_imm(X64 *c, u32 base, s32 disp, u32 imm)
{
    rex(c, 0, 0, -1, base);
    p8(c, 0xC7);
    modrm_mem(c, 0, base, -1, 0, disp);
    p32(c, imm);
}
static void push_r(X64 *c, u32 reg) { if (reg & 8) p8(c, 0x41); p8(c, 0x50 + (reg & 7)); }
static void pop_r(X64 *c, u32 reg) { if (reg & 8) p8(c, 0x41); p8(c, 0x58 + (reg & 7)); }
static void bswap_r(X64 *c, int w, u32 reg) { rex(c, w, 0, -1, reg); p8(c, 0x0F); p8(c, 0xC8 + (reg & 7)); }

static u32 jcc_fwd(X64 *c, u32 cc) { p8(c, 0x0F); p8(c, 0x80 + cc); p32(c, 0); return c->pos - 4; }
static u32 jmp_fwd(X64 *c) { p8(c, 0xE9); p32(c, 0); return c->pos - 4; }
static void patch_here(X64 *c, u32 at)
{
    u32 rel = c->pos - (at + 4);
    if (at + 4 > c->cap) return;
    c->buf[at] = (u8)rel; c->buf[at + 1] = (u8)(rel >> 8); c->buf[at + 2] = (u8)(rel >> 16); c->buf[at + 3] = (u8)(rel >> 24);
}
static void jmp_to(X64 *c, const u8 *target)
{
    p8(c, 0xE9);
    p32(c, (u32)(s32)(target - (c->buf + c->pos + 4)));
}
static void call_to(X64 *c, const u8 *target)
{
    p8(c, 0xE8);
    p32(c, (u32)(s32)(target - (c->buf + c->pos + 4)));
}
static void jcc_to(X64 *c, u32 cc, const u8 *target)
{
    p8(c, 0x0F); p8(c, 0x80 + cc);
    p32(c, (u32)(s32)(target - (c->buf + c->pos + 4)));
}

#define OFF_GPR(i) ((s32)(offsetof(H64Cpu, gpr) + 8 * (i)))
#define OFF_HI ((s32)offsetof(H64Cpu, hi))
#define OFF_LO ((s32)offsetof(H64Cpu, lo))
#define OFF_PC ((s32)offsetof(H64Cpu, pc))
#define OFF_NEXTPC ((s32)offsetof(H64Cpu, nextPc))
#define OFF_BRANCH ((s32)offsetof(H64Cpu, branchPending))
#define OFF_CYCLES ((s32)offsetof(H64Cpu, cycles))
#define OFF_INSNS ((s32)offsetof(H64Cpu, instructions))
#define OFF_FGR(i) ((s32)(offsetof(H64Cpu, fgr) + 8 * (i)))
#define OFF_COP0(i) ((s32)(offsetof(H64Cpu, cop0) + 8 * (i)))

#define MAX_BLOCK_BYTES (H64_JIT_MAX_INSNS * 320 + 2048)

static u64 sext32(u32 v) { return (u64)(s64)(s32)v; }

static void jit_mi_mask_write(H64System *sys, u32 value)
{
    int i;
    for (i = 0; i < 6; i++)
    {
        if (value & (1u << (2 * i))) sys->mi.mask &= ~(1u << i);
        if (value & (2u << (2 * i))) sys->mi.mask |= (1u << i);
    }
    h64_cpu_set_ip(sys, 2, (sys->mi.intr & sys->mi.mask) != 0);
}

u32 h64_jit_mmio_hist[512][2];

static int helper_interp(H64System *sys, u32 expectedNext, u32 op, u32 haveOp)
{
    H64Cpu *cpu = &sys->cpu;
    sys->jit->stats.helperCalls++;
    if (sys->jit->opHist)
    {
        u32 opw = 0, opc, paddr0, pc0 = (u32)cpu->pc, h = (pc0 >> 2) & 4095, n;
        if (haveOp) opw = op;
        if (haveOp || h64_cpu_peek_op(sys, &opw) == 0)
        {
            opc = opw >> 26;
            if ((opc >= 0x20 && opc <= 0x27) || (opc >= 0x28 && opc <= 0x2B) || opc == 0x37 || opc == 0x3F)
            {
                u64 a = cpu->gpr[(opw >> 21) & 31] + (u64)(s64)(s16)(opw & 0xFFFF);
                u32 size = (opc & 3) == 0 ? 1 : (opc & 3) == 1 ? 2 : (opc == 0x37 || opc == 0x3F) ? 8 : 4;
                if ((u64)(s64)(s32)a != a || ((u32)a >> 30) != 2) sys->jit->opHist[192]++;
                else if (a & (size - 1)) sys->jit->opHist[193]++;
                else if ((paddr0 = (u32)a & 0x1FFFFFFF) >= H64_RDRAM_SIZE)
                {
                    sys->jit->opHist[194]++;
                    h64_jit_mmio_hist[(paddr0 >> 20) & 511][opc >= 0x28]++;
                }
                else if (opc >= 0x28 && sys->jit->codeMap[paddr0 >> H64_JIT_CHUNK_SHIFT]) sys->jit->opHist[195]++;
                else sys->jit->opHist[196]++;
            }
        }
        for (n = 0; n < 4096; n++, h = (h + 1) & 4095)
        {
            u32 *e = &sys->jit->opHist[232 + 2 * h];
            if (e[0] == pc0 || !e[1]) { e[0] = pc0; e[1]++; break; }
        }
    }
    {
        u64 t0 = h64_prof_now(sys);
        u64 nested = h64_prof_nested(sys);
        if (haveOp) h64_cpu_step_op(sys, op);
        else h64_cpu_step(sys);
        h64_prof_add_outer(sys, H64_PROF_HELPER, t0, nested);
    }
    if (sys->jit->opHist)
    {
        u32 i;
        op = cpu->lastOp;
        i = op >> 26;
        if (i == 0) i = 64 + (op & 63);
        else if (i == 0x11 && ((op >> 21) & 31) < 0x10) i = 200 + ((op >> 21) & 31);
        else if (i == 0x11 && ((op >> 21) & 31) >= 0x14) i = 216 + (op & 1);
        else if (i == 0x11) i = 128 + (op & 63);
        sys->jit->opHist[i]++;
    }
    if (cpu->exceptionRaised || cpu->pc != sext32(expectedNext)) return 1;
    return h64_jit_should_exit(sys);
}

enum { TAIL_SLOW, TAIL_SLOT, TAIL_EXIT, TAIL_TLB };
struct Tail
{
    int kind;
    void *block;
    u32 dtlbAt, dtlbBack, dtlbKind;
    u32 at[6], n;
    u32 pc, op, pend;
    u32 back;
    int dyn, always;
    u64 target;
};

struct Gen
{
    H64System *sys;
    X64 c;
    u32 pc0, paddr0;
    u32 pending;
    int native;
    u32 cpi;
    int condAlways;
    int cu1Known;
    int wantDtlb;
    u32 dtlbAt, dtlbBack, dtlbKind;
    struct { u32 at, target; } links[8];
    u32 nLinks;
    Tail tails[H64_JIT_MAX_INSNS + 2];
    u32 nTails;
};

static u32 op_at(Gen *g, u32 pc) { return h64_rdram_load32(g->sys->rdram, g->paddr0 + (pc - g->pc0)); }

static void load64(Gen *g, u32 reg, u32 mips)
{
    if (mips) mov_rm64(&g->c, reg, X_CPU, OFF_GPR(mips));
    else xor_rr32(&g->c, reg);
}
static void load32(Gen *g, u32 reg, u32 mips)
{
    if (mips) mov_rm32(&g->c, reg, X_CPU, OFF_GPR(mips));
    else xor_rr32(&g->c, reg);
}
static void store64(Gen *g, u32 reg, u32 mips)
{
    if (mips) mov_mr64(&g->c, X_CPU, OFF_GPR(mips), reg);
}

static void add_counters(Gen *g, u32 pending)
{
    if (!pending) return;
    alu_mi(&g->c, 1, G_ADD, X_CPU, OFF_CYCLES, (s32)(pending * g->cpi));
    alu_mi(&g->c, 1, G_ADD, X_CPU, OFF_INSNS, (s32)pending);
}

static void sync_to(Gen *g, u32 pending, u32 next)
{
    add_counters(g, pending);
    mov_m64_imm(&g->c, X_CPU, OFF_PC, sext32(next));
    mov_m64_imm(&g->c, X_CPU, OFF_NEXTPC, sext32(next) + 4);
    mov_m32_imm(&g->c, X_CPU, OFF_BRANCH, 0);
}

static void exit_block(Gen *g) { jmp_to(&g->c, (const u8 *)g->sys->jit->rtExit); }

static int can_link(Gen *g, u32 count)
{
    return g->native && !g->sys->jit->noLink && g->nLinks + count <= sizeof(g->links) / sizeof(g->links[0]);
}
static void link_jump(Gen *g, int cc, u32 target)
{
    X64 *c = &g->c;
    if (cc >= 0) { p8(c, 0x0F); p8(c, 0x80 + (u32)cc); }
    else p8(c, 0xE9);
    p32(c, 0);
    g->links[g->nLinks].at = c->pos - 4;
    g->links[g->nLinks].target = target;
    g->nLinks++;
}
static void exit_fixed(Gen *g, u32 target)
{
    if (!can_link(g, 1)) { exit_block(g); return; }
    call_to(&g->c, (const u8 *)g->sys->jit->rtLink);
    link_jump(g, -1, target);
}
static void exit_branch(Gen *g, int dynamicTarget, u64 target, u32 fallthrough)
{
    X64 *c = &g->c;
    if (dynamicTarget)
    {
        if (g->native && !g->sys->jit->noLink) jmp_to(c, (const u8 *)g->sys->jit->rtIndirect);
        else exit_block(g);
        return;
    }
    if (target != sext32((u32)target) || !can_link(g, 2)) { exit_block(g); return; }
    call_to(c, (const u8 *)g->sys->jit->rtLink);
    if (!g->condAlways)
    {
        test_rr(c, 0, X_COND, X_COND);
        link_jump(g, CC_NE, (u32)target);
        link_jump(g, -1, fallthrough);
    }
    else link_jump(g, -1, (u32)target);
}

static void call_helper(Gen *g, u32 expectedNext, u32 op, u32 haveOp)
{
    X64 *c = &g->c;
    mov_rr64(c, ARG0, X_SYS);
    mov_ri32(c, ARG1, expectedNext);
    mov_ri32(c, ARG2, op);
    mov_ri32(c, ARG3, haveOp);
    mov_ri64(c, RAX, (u64)(uintptr_t)&helper_interp);
    p8(c, 0xFF); p8(c, 0xD0);
}

static void call_interp_raw(Gen *g) { call_helper(g, 0, 0, 0); }

static void call_interp(Gen *g, u32 pc)
{
    sync_to(g, g->pending, pc);
    g->pending = 0;
    call_helper(g, pc + 4, op_at(g, pc), 1);
    test_rr(&g->c, 0, RAX, RAX);
    jcc_to(&g->c, CC_NE, (const u8 *)g->sys->jit->rtExit);
}

void h64_jit_emit_runtime(H64System *sys)
{
    H64Jit *j = sys->jit;
    X64 x, *c = &x;
    x.buf = j->mem;
    x.pos = 0;
    x.cap = 2048;
    x.overflow = 0;
    j->rtCpi = sys->cpu.cpi;
    j->enter = (void (*)(H64System *, u32 *))(void *)j->mem;
    push_r(c, RBX); push_r(c, RBP); push_r(c, R12); push_r(c, R13); push_r(c, R14); push_r(c, R15);
#if defined(_WIN32)
    push_r(c, RSI); push_r(c, RDI);
#endif
    alu_ri(c, 1, G_SUB, RSP, 40);
    mov_rr64(c, X_SYS, ARG0);
    x_rm(c, 1, 0x8D, X_CPU, X_SYS, (s32)offsetof(H64System, cpu));
    mov_rm64(c, X_RAM, X_SYS, (s32)offsetof(H64System, rdram));
    mov_rm64(c, RAX, X_SYS, (s32)offsetof(H64System, jit));
    mov_rm64(c, X_MAP, RAX, (s32)offsetof(H64Jit, codeMap));
    x_rr(c, 0, 0xFF, 4, ARG1);
    j->rtExit = (u32 *)(void *)(x.buf + x.pos);
    alu_ri(c, 1, G_ADD, RSP, 40);
#if defined(_WIN32)
    pop_r(c, RDI); pop_r(c, RSI);
#endif
    pop_r(c, R15); pop_r(c, R14); pop_r(c, R13); pop_r(c, R12); pop_r(c, RBP); pop_r(c, RBX);
    p8(c, 0xC3);
    {
        const s32 offNext = (s32)(offsetof(H64System, sched) + offsetof(H64Scheduler, next));
        const s32 span = (s32)(H64_JIT_MAX_INSNS * (j->rtCpi ? j->rtCpi : 1));
        u32 out[8], n = 0, k;
        j->rtLink = (u32 *)(void *)(x.buf + x.pos);
        mov_rm64(c, RAX, X_CPU, OFF_CYCLES);
        alu_ri(c, 1, G_ADD, RAX, span);
        alu_rm(c, 1, G_CMP, RAX, X_SYS, offNext);
        out[n++] = jcc_fwd(c, CC_A);
        mov_rm64(c, RCX, X_SYS, (s32)offsetof(H64System, jit));
        alu_rm(c, 1, G_CMP, RAX, RCX, (s32)offsetof(H64Jit, runEnd));
        out[n++] = jcc_fwd(c, CC_A);
        mov_mr64(c, RCX, (s32)offsetof(H64Jit, blockEndCycles), RAX);
        p8(c, 0xC3);
        for (k = 0; k < n; k++) patch_here(c, out[k]);
        alu_ri(c, 1, G_ADD, RSP, 8);
        jmp_to(c, (const u8 *)j->rtExit);

        j->rtIndirect = (u32 *)(void *)(x.buf + x.pos);
        mov_rm64(c, RAX, X_CPU, OFF_PC);
        mov_rm64(c, RDX, X_CPU, OFF_CYCLES);
        alu_ri(c, 1, G_ADD, RDX, span);
        alu_rm(c, 1, G_CMP, RDX, X_SYS, offNext);
        jcc_to(c, CC_A, (const u8 *)j->rtExit);
        mov_rm64(c, RCX, X_SYS, (s32)offsetof(H64System, jit));
        alu_rm(c, 1, G_CMP, RDX, RCX, (s32)offsetof(H64Jit, runEnd));
        jcc_to(c, CC_A, (const u8 *)j->rtExit);
        movsxd_rr(c, R8, RAX);
        alu_rr(c, 1, G_CMP, R8, RAX);
        jcc_to(c, CC_NE, (const u8 *)j->rtExit);
        mov_rr32(c, R8, RAX);
        shift_ri(c, 0, SH_SHR, R8, 2);
        alu_ri(c, 0, G_AND, R8, 1023);
        mov_rm64(c, R9, RCX, (s32)offsetof(H64Jit, indPc));
        x_rx(c, 0, 0x39, RAX, R9, R8, 2, 0, 0);
        jcc_to(c, CC_NE, (const u8 *)j->rtExit);
        mov_mr64(c, RCX, (s32)offsetof(H64Jit, blockEndCycles), RDX);
        mov_rm64(c, R9, RCX, (s32)offsetof(H64Jit, indBody));
        x_rx(c, 0, 0xFF, 4, R9, R8, 3, 0, 0);
    }
    {
        const u32 cpi = j->rtCpi ? j->rtCpi : 1;
        u32 leave;
        j->rtSlow = (u32 *)(void *)(x.buf + x.pos);
        alu_ri(c, 1, G_SUB, RSP, 40);
        mov_mr32(c, RSP, 32, RAX);
        x_rm(c, 1, 0x01, RAX, X_CPU, OFF_INSNS);
        if (cpi != 1) { p8(c, 0x48); p8(c, 0x6B); p8(c, 0xC0); p8(c, cpi); }
        x_rm(c, 1, 0x01, RAX, X_CPU, OFF_CYCLES);
        movsxd_rr(c, RAX, R10);
        mov_mr64(c, X_CPU, OFF_PC, RAX);
        alu_ri(c, 1, G_ADD, RAX, 4);
        mov_mr64(c, X_CPU, OFF_NEXTPC, RAX);
        mov_m32_imm(c, X_CPU, OFF_BRANCH, 0);
        mov_rr32(c, ARG1, RAX);
        mov_rr32(c, ARG2, R11);
        mov_rr64(c, ARG0, X_SYS);
        mov_ri32(c, ARG3, 1);
        mov_ri64(c, RAX, (u64)(uintptr_t)&helper_interp);
        p8(c, 0xFF); p8(c, 0xD0);
        test_rr(c, 0, RAX, RAX);
        leave = jcc_fwd(c, CC_NE);
        mov_rm32(c, RAX, RSP, 32);
        alu_ri(c, 0, G_ADD, RAX, 1);
        x_rm(c, 1, 0x29, RAX, X_CPU, OFF_INSNS);
        if (cpi != 1) { p8(c, 0x48); p8(c, 0x6B); p8(c, 0xC0); p8(c, cpi); }
        x_rm(c, 1, 0x29, RAX, X_CPU, OFF_CYCLES);
        alu_ri(c, 1, G_ADD, RSP, 40);
        p8(c, 0xC3);
        patch_here(c, leave);
        alu_ri(c, 1, G_ADD, RSP, 48);
        jmp_to(c, (const u8 *)j->rtExit);

        {
            u32 st, sz;
            for (st = 0; st < 2; st++)
                for (sz = 0; sz < 4; sz++)
                {
                    u32 out[2], n = 0;
                    j->rtDtlb[st][sz] = (u32 *)(void *)(x.buf + x.pos);
                    mov_rr32(c, RCX, RAX);
                    alu_ri(c, 0, G_AND, RCX, (s32)0xFFFFF000u);
                    alu_ri(c, 0, G_OR, RCX, 1);
                    mov_rr32(c, RDX, RAX);
                    shift_ri(c, 0, SH_SHR, RDX, 12);
                    alu_ri(c, 0, G_AND, RDX, 255);
                    x_rx(c, 0, 0x3B, RCX, X_CPU, RDX, 2, st ? (s32)offsetof(H64Cpu, dtlbW) : (s32)offsetof(H64Cpu, dtlbR), 0);
                    out[n++] = jcc_fwd(c, CC_NE);
                    if (sz)
                    {
                        p8(c, 0xA9); p32(c, (1u << sz) - 1);
                        out[n++] = jcc_fwd(c, CC_NE);
                    }
                    x_rx(c, 0, 0x8B, RCX, X_CPU, RDX, 2, (s32)offsetof(H64Cpu, dtlbPhys), 0);
                    alu_ri(c, 0, G_AND, RAX, 0xFFF);
                    alu_rr(c, 0, G_OR, RAX, RCX);
                    alu_rr(c, 0, G_CMP, RAX, RAX);
                    while (n) patch_here(c, out[--n]);
                    p8(c, 0xC3);
                }
        }

        j->rtUnlinked = (u32 *)(void *)(x.buf + x.pos);
        mov_rm64(c, RDX, X_SYS, (s32)offsetof(H64System, jit));
        alu_rm(c, 1, G_ADD, RCX, RDX, (s32)offsetof(H64Jit, mem));
        mov_mr64(c, RDX, (s32)offsetof(H64Jit, lastExit), RCX);
        mov_mr32(c, RDX, (s32)offsetof(H64Jit, lastExitTarget), RAX);
        jmp_to(c, (const u8 *)j->rtExit);
    }
    if (x.overflow) H64_ERROR("[jit] shared routines too large");
    j->memUsed = (x.pos + 15) & ~15u;
}

static int is_branch(u32 op)
{
    u32 opc = op >> 26, rt = (op >> 16) & 31, rs = (op >> 21) & 31, funct = op & 63;
    if (opc == 0) return funct == 0x08 || funct == 0x09;
    if (opc == 1) return (rt & 0x0C) == 0 && (rt & 0x10 ? rt <= 0x13 : 1);
    if (opc >= 2 && opc <= 7) return 1;
    if (opc >= 0x14 && opc <= 0x17) return 1;
    if (opc == 0x11 && rs == 0x08) return 1;
    return 0;
}

static int mtc0_plain(u32 op)
{
    u32 rd = (op >> 11) & 31;
    return (op >> 26) == 0x10 && ((op >> 21) & 31) == 4 && (rd == CP0_INDEX || rd == CP0_ENTRYLO0 || rd == CP0_ENTRYLO1 ||
                                                           rd == CP0_PAGEMASK);
}

static int ends_block(u32 op)
{
    return (op >> 26) == 0x10 && ((op >> 21) & 31) != 0 && ((op >> 21) & 31) != 1 && !mtc0_plain(op);
}

static int emit_alu(Gen *g, u32 op)
{
    X64 *c = &g->c;
    u32 opc = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sa = (op >> 6) & 31;
    s32 simm = (s32)(s16)(op & 0xFFFF);
    u32 uimm = op & 0xFFFF;
    if (opc == 0)
    {
        u32 f = op & 63;
        switch (f)
        {
        case 0x00: case 0x02:
            if (rd == 0) return 1;
            load32(g, RAX, rt);
            shift_ri(c, 0, f == 0 ? SH_SHL : SH_SHR, RAX, sa);
            movsxd_rr(c, RAX, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x03:
            if (rd == 0) return 1;
            load64(g, RAX, rt);
            shift_ri(c, 1, SH_SAR, RAX, sa);
            movsxd_rr(c, RAX, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x04: case 0x06:
            if (rd == 0) return 1;
            load32(g, RAX, rt);
            load32(g, RCX, rs);
            shift_cl(c, 0, f == 0x04 ? SH_SHL : SH_SHR, RAX);
            movsxd_rr(c, RAX, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x07:
            if (rd == 0) return 1;
            load64(g, RAX, rt);
            load32(g, RCX, rs);
            alu_ri(c, 0, G_AND, RCX, 31);
            shift_cl(c, 1, SH_SAR, RAX);
            movsxd_rr(c, RAX, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x14: case 0x16: case 0x17:
            if (rd == 0) return 1;
            load64(g, RAX, rt);
            load32(g, RCX, rs);
            shift_cl(c, 1, f == 0x14 ? SH_SHL : f == 0x16 ? SH_SHR : SH_SAR, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x38: case 0x3A: case 0x3B: case 0x3C: case 0x3E: case 0x3F:
        {
            u32 n = sa + (f >= 0x3C ? 32 : 0), k = f & 0x3B;
            if (rd == 0) return 1;
            load64(g, RAX, rt);
            shift_ri(c, 1, k == 0x38 ? SH_SHL : k == 0x3A ? SH_SHR : SH_SAR, RAX, n);
            store64(g, RAX, rd);
            return 1;
        }
        case 0x10: case 0x12:
            if (rd == 0) return 1;
            mov_rm64(c, RAX, X_CPU, f == 0x10 ? OFF_HI : OFF_LO);
            store64(g, RAX, rd);
            return 1;
        case 0x11: case 0x13:
            load64(g, RAX, rs);
            mov_mr64(c, X_CPU, f == 0x11 ? OFF_HI : OFF_LO, RAX);
            return 1;
        case 0x0F: return 1;
        case 0x18: case 0x19:
            if (f == 0x18)
            {
                if (rs) x_rm(c, 1, 0x63, RAX, X_CPU, OFF_GPR(rs)); else xor_rr32(c, RAX);
                if (rt) x_rm(c, 1, 0x63, RCX, X_CPU, OFF_GPR(rt)); else xor_rr32(c, RCX);
            }
            else
            {
                load32(g, RAX, rs);
                load32(g, RCX, rt);
            }
            x_rr(c, 1, 0x0FAF, RAX, RCX);
            movsxd_rr(c, RDX, RAX);
            mov_mr64(c, X_CPU, OFF_LO, RDX);
            shift_ri(c, 1, SH_SAR, RAX, 32);
            mov_mr64(c, X_CPU, OFF_HI, RAX);
            return 1;
        case 0x1C: case 0x1D:
            load64(g, RAX, rs);
            load64(g, RCX, rt);
            rex(c, 1, 0, -1, RCX);
            p8(c, 0xF7);
            p8(c, (f == 0x1C ? 0xE8 : 0xE0) | RCX);
            mov_mr64(c, X_CPU, OFF_LO, RAX);
            mov_mr64(c, X_CPU, OFF_HI, RDX);
            return 1;
        case 0x21: case 0x23:
            if (rd == 0) return 1;
            load32(g, RAX, rs);
            if (rt) alu_rm(c, 0, f == 0x21 ? G_ADD : G_SUB, RAX, X_CPU, OFF_GPR(rt));
            movsxd_rr(c, RAX, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x2D: case 0x2F:
            if (rd == 0) return 1;
            load64(g, RAX, rs);
            if (rt) alu_rm(c, 1, f == 0x2D ? G_ADD : G_SUB, RAX, X_CPU, OFF_GPR(rt));
            store64(g, RAX, rd);
            return 1;
        case 0x24: case 0x25: case 0x26: case 0x27:
            if (rd == 0) return 1;
            load64(g, RAX, rs);
            if (rt) alu_rm(c, 1, f == 0x24 ? G_AND : f == 0x26 ? G_XOR : G_OR, RAX, X_CPU, OFF_GPR(rt));
            else if (f == 0x24) xor_rr32(c, RAX);
            if (f == 0x27) not_r64(c, RAX);
            store64(g, RAX, rd);
            return 1;
        case 0x2A: case 0x2B:
            if (rd == 0) return 1;
            load64(g, RAX, rs);
            load64(g, RCX, rt);
            alu_rr(c, 1, G_CMP, RAX, RCX);
            setcc_reg(c, f == 0x2A ? CC_L : CC_B, RAX);
            store64(g, RAX, rd);
            return 1;
        }
        return 0;
    }
    switch (opc)
    {
    case 0x09:
        if (rt == 0) return 1;
        load32(g, RAX, rs);
        if (simm) alu_ri(c, 0, G_ADD, RAX, simm);
        movsxd_rr(c, RAX, RAX);
        store64(g, RAX, rt);
        return 1;
    case 0x19:
        if (rt == 0) return 1;
        load64(g, RAX, rs);
        if (simm) alu_ri(c, 1, G_ADD, RAX, simm);
        store64(g, RAX, rt);
        return 1;
    case 0x0A: case 0x0B:
        if (rt == 0) return 1;
        load64(g, RAX, rs);
        alu_ri(c, 1, G_CMP, RAX, simm);
        setcc_reg(c, opc == 0x0A ? CC_L : CC_B, RAX);
        store64(g, RAX, rt);
        return 1;
    case 0x0C: case 0x0D: case 0x0E:
        if (rt == 0) return 1;
        load64(g, RAX, rs);
        if (opc == 0x0C) alu_ri(c, 0, G_AND, RAX, (s32)uimm);
        else if (uimm)
        {
            mov_ri32(c, RCX, uimm);
            alu_rr(c, 1, opc == 0x0D ? G_OR : G_XOR, RAX, RCX);
        }
        store64(g, RAX, rt);
        return 1;
    case 0x0F:
        if (rt == 0) return 1;
        mov_m64_imm(c, X_CPU, OFF_GPR(rt), sext32(uimm << 16));
        return 1;
    }
    return 0;
}

#define OFF_FCR31 ((s32)offsetof(H64Cpu, fcr31))
static void sse_rm(X64 *c, u32 prefix, u32 op, u32 reg, u32 base, s32 disp)
{
    if (prefix) p8(c, prefix);
    x_rm(c, 0, 0x0F00 | op, reg, base, disp);
}
static void sse_rr(X64 *c, u32 prefix, u32 op, u32 reg, u32 rm)
{
    if (prefix) p8(c, prefix);
    x_rr(c, 0, 0x0F00 | op, reg, rm);
}
static void test_mi32(X64 *c, u32 base, s32 disp, u32 imm)
{
    x_rm(c, 0, 0xF7, 0, base, disp);
    p32(c, imm);
}
static void cu1_guard(Gen *g, u32 *slow, u32 *nSlow)
{
    if (g->cu1Known) return;
    test_mi32(&g->c, X_CPU, OFF_COP0(CP0_STATUS), SR_CU1);
    slow[(*nSlow)++] = jcc_fwd(&g->c, CC_E);
    g->cu1Known = 1;
}
static void fr_guard(Gen *g, u32 reg, u32 *slow, u32 *nSlow)
{
    if (!(reg & 1)) return;
    test_mi32(&g->c, X_CPU, OFF_COP0(CP0_STATUS), SR_FR);
    slow[(*nSlow)++] = jcc_fwd(&g->c, CC_E);
}
static void rm_guard(Gen *g, u32 *slow, u32 *nSlow)
{
    x_rm(&g->c, 0, 0xF6, 0, X_CPU, OFF_FCR31);
    p8(&g->c, 3);
    slow[(*nSlow)++] = jcc_fwd(&g->c, CC_NE);
}

static int emit_mem_fast(Gen *g, u32 op, u32 *slow, u32 *nSlowOut)
{
    X64 *c = &g->c;
    u32 opc = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31, size, store = 0, fpu = 0, nSlow = *nSlowOut;
    s32 simm = (s32)(s16)(op & 0xFFFF);
    switch (opc)
    {
    case 0x31: size = 4; fpu = 1; break;
    case 0x35: size = 8; fpu = 1; break;
    case 0x39: size = 4; store = 1; fpu = 1; break;
    case 0x3D: size = 8; store = 1; fpu = 1; break;
    case 0x20: case 0x24: size = 1; break;
    case 0x21: case 0x25: size = 2; break;
    case 0x23: case 0x27: size = 4; break;
    case 0x37: size = 8; break;
    case 0x28: size = 1; store = 1; break;
    case 0x29: size = 2; store = 1; break;
    case 0x2B: size = 4; store = 1; break;
    case 0x3F: size = 8; store = 1; break;
    default: return 0;
    }
    if (fpu)
    {
        cu1_guard(g, slow, &nSlow);
    }
    load64(g, RAX, rs);
    if (simm) alu_ri(c, 1, G_ADD, RAX, simm);
    movsxd_rr(c, RCX, RAX);
    alu_rr(c, 1, G_CMP, RCX, RAX);
    slow[nSlow++] = jcc_fwd(c, CC_NE);
    mov_rr32(c, RCX, RAX);
    alu_ri(c, 0, G_AND, RCX, (s32)(0xDF800000u | (size - 1)));
    alu_ri(c, 0, G_CMP, RCX, (s32)0x80000000u);
    if (g->wantDtlb)
    {
        g->dtlbAt = jcc_fwd(c, CC_NE);
        g->dtlbKind = 1 + store * 4 + (size == 8 ? 3 : size == 4 ? 2 : size == 2 ? 1 : 0);
    }
    else slow[nSlow++] = jcc_fwd(c, CC_NE);
    alu_ri(c, 0, G_AND, RAX, 0x007FFFFF);
    g->dtlbBack = c->pos;
    if (store)
    {
        mov_rr32(c, RCX, RAX);
        shift_ri(c, 0, SH_SHR, RCX, H64_JIT_CHUNK_SHIFT);
        x_rx(c, 0, 0x83, 7, X_MAP, RCX, 1, 0, 1);
        p8(c, 0);
        slow[nSlow++] = jcc_fwd(c, CC_NE);
        x_rm(c, 0, 0xF7, 0, X_SYS, (s32)(offsetof(H64System, mi) + offsetof(H64Mi, mode)));
        p32(c, 0x80);
        slow[nSlow++] = jcc_fwd(c, CC_NE);
    }
    if (store)
    {
        if (!fpu) load64(g, RCX, rt);
        else if (size == 8 || !(rt & 1))
        {
            if (size == 8 && (rt & 1))
            {
                u32 fr0, done;
                x_rm(c, 0, 0xF7, 0, X_CPU, OFF_COP0(CP0_STATUS));
                p32(c, 0x04000000u);
                fr0 = jcc_fwd(c, CC_E);
                mov_rm64(c, RCX, X_CPU, OFF_FGR(rt));
                done = jmp_fwd(c);
                patch_here(c, fr0);
                mov_rm64(c, RCX, X_CPU, OFF_FGR(rt & ~1u));
                patch_here(c, done);
            }
            else if (size == 8) mov_rm64(c, RCX, X_CPU, OFF_FGR(rt));
            else mov_rm32(c, RCX, X_CPU, OFF_FGR(rt));
        }
        else
        {
            u32 fr0, done;
            x_rm(c, 0, 0xF7, 0, X_CPU, OFF_COP0(CP0_STATUS));
            p32(c, 0x04000000u);
            fr0 = jcc_fwd(c, CC_E);
            mov_rm32(c, RCX, X_CPU, OFF_FGR(rt));
            done = jmp_fwd(c);
            patch_here(c, fr0);
            mov_rm32(c, RCX, X_CPU, OFF_FGR(rt - 1) + 4);
            patch_here(c, done);
        }
        switch (size)
        {
        case 1:
            if (H64_RDRAM_XOR) alu_ri(c, 0, G_XOR, RAX, 3);
            x_rx(c, 0, 0x88, RCX, X_RAM, RAX, 0, 0, 0);
            break;
        case 2:
            if (H64_RDRAM_XOR) alu_ri(c, 0, G_XOR, RAX, 2);
            else { p8(c, 0x86); p8(c, 0xE9); }
            x_rx(c, 0, 0x89, RCX, X_RAM, RAX, 0, 0, 1);
            break;
        case 4:
            if (!H64_RDRAM_XOR) bswap_r(c, 0, RCX);
            x_rx(c, 0, 0x89, RCX, X_RAM, RAX, 0, 0, 0);
            break;
        default:
            if (H64_RDRAM_XOR)
            {
                x_rx(c, 0, 0x89, RCX, X_RAM, RAX, 0, 4, 0);
                shift_ri(c, 1, SH_SHR, RCX, 32);
                x_rx(c, 0, 0x89, RCX, X_RAM, RAX, 0, 0, 0);
            }
            else
            {
                bswap_r(c, 1, RCX);
                x_rx(c, 1, 0x89, RCX, X_RAM, RAX, 0, 0, 0);
            }
            break;
        }
        *nSlowOut = nSlow;
        return 1;
    }
    switch (size)
    {
    case 1:
        if (H64_RDRAM_XOR) alu_ri(c, 0, G_XOR, RAX, 3);
        if (opc == 0x20) x_rx(c, 1, 0x0FBE, RCX, X_RAM, RAX, 0, 0, 0);
        else x_rx(c, 0, 0x0FB6, RCX, X_RAM, RAX, 0, 0, 0);
        break;
    case 2:
        if (H64_RDRAM_XOR)
        {
            alu_ri(c, 0, G_XOR, RAX, 2);
            if (opc == 0x21) x_rx(c, 1, 0x0FBF, RCX, X_RAM, RAX, 0, 0, 0);
            else x_rx(c, 0, 0x0FB7, RCX, X_RAM, RAX, 0, 0, 0);
        }
        else
        {
            x_rx(c, 0, 0x0FB7, RCX, X_RAM, RAX, 0, 0, 0);
            p8(c, 0x86); p8(c, 0xE9);
            if (opc == 0x21) x_rr(c, 1, 0x0FBF, RCX, RCX);
        }
        break;
    case 4:
        x_rx(c, 0, 0x8B, RCX, X_RAM, RAX, 0, 0, 0);
        if (!H64_RDRAM_XOR) bswap_r(c, 0, RCX);
        if (opc == 0x23) movsxd_rr(c, RCX, RCX);
        break;
    default:
        if (H64_RDRAM_XOR)
        {
            x_rx(c, 0, 0x8B, RCX, X_RAM, RAX, 0, 0, 0);
            x_rx(c, 0, 0x8B, RDX, X_RAM, RAX, 0, 4, 0);
            shift_ri(c, 1, SH_SHL, RCX, 32);
            alu_rr(c, 1, G_OR, RCX, RDX);
        }
        else
        {
            x_rx(c, 1, 0x8B, RCX, X_RAM, RAX, 0, 0, 0);
            bswap_r(c, 1, RCX);
        }
        break;
    }
    if (!fpu) store64(g, RCX, rt);
    else if (size == 8)
    {
        if (rt & 1)
        {
            u32 fr0, done;
            x_rm(c, 0, 0xF7, 0, X_CPU, OFF_COP0(CP0_STATUS));
            p32(c, 0x04000000u);
            fr0 = jcc_fwd(c, CC_E);
            mov_mr64(c, X_CPU, OFF_FGR(rt), RCX);
            done = jmp_fwd(c);
            patch_here(c, fr0);
            mov_mr64(c, X_CPU, OFF_FGR(rt & ~1u), RCX);
            patch_here(c, done);
        }
        else mov_mr64(c, X_CPU, OFF_FGR(rt), RCX);
    }
    else if (!(rt & 1)) mov_mr32(c, X_CPU, OFF_FGR(rt), RCX);
    else
    {
        u32 fr0, done;
        x_rm(c, 0, 0xF7, 0, X_CPU, OFF_COP0(CP0_STATUS));
        p32(c, 0x04000000u);
        fr0 = jcc_fwd(c, CC_E);
        mov_mr32(c, X_CPU, OFF_FGR(rt), RCX);
        done = jmp_fwd(c);
        patch_here(c, fr0);
        mov_mr32(c, X_CPU, OFF_FGR(rt - 1) + 4, RCX);
        patch_here(c, done);
    }
    *nSlowOut = nSlow;
    return 1;
}

static Tail *new_tail(Gen *g, int kind, const u32 *slow, u32 nSlow, u32 pc);

static void fp_result(Gen *g, u32 fd, int dbl)
{
    X64 *c = &g->c;
    if (dbl) sse_rm(c, 0xF2, 0x11, 0, X_CPU, OFF_FGR(fd));
    else
    {
        sse_rr(c, 0x66, 0x7E, 0, RAX);
        mov_mr64(c, X_CPU, OFF_FGR(fd), RAX);
    }
}

static int emit_fpu_fast(Gen *g, u32 op, u32 *slow, u32 *nSlow)
{
    X64 *c = &g->c;
    u32 rs = (op >> 21) & 31, ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31, funct = op & 63;
    u32 pre;
    int fast = g->sys->jit->fastFpu, dbl;
    if ((op >> 26) != 0x11 || g->sys->jit->noFpu) return 0;
    switch (rs)
    {
    case 0x00:
        cu1_guard(g, slow, nSlow);
        if (ft)
        {
            if (fs & 1)
            {
                u32 fr0, done;
                test_mi32(c, X_CPU, OFF_COP0(CP0_STATUS), SR_FR);
                fr0 = jcc_fwd(c, CC_E);
                x_rm(c, 1, 0x63, RAX, X_CPU, OFF_FGR(fs));
                done = jmp_fwd(c);
                patch_here(c, fr0);
                x_rm(c, 1, 0x63, RAX, X_CPU, OFF_FGR(fs - 1) + 4);
                patch_here(c, done);
            }
            else x_rm(c, 1, 0x63, RAX, X_CPU, OFF_FGR(fs));
            store64(g, RAX, ft);
        }
        return 1;
    case 0x01:
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        if (ft)
        {
            mov_rm64(c, RAX, X_CPU, OFF_FGR(fs));
            store64(g, RAX, ft);
        }
        return 1;
    case 0x02:
        cu1_guard(g, slow, nSlow);
        if (ft)
        {
            if (fs == 31) x_rm(c, 1, 0x63, RAX, X_CPU, OFF_FCR31);
            else if (fs == 0) mov_ri32(c, RAX, 0x00000A00u);
            else xor_rr32(c, RAX);
            store64(g, RAX, ft);
        }
        return 1;
    case 0x04:
        cu1_guard(g, slow, nSlow);
        load32(g, RAX, ft);
        if (fs & 1)
        {
            u32 fr0, done;
            test_mi32(c, X_CPU, OFF_COP0(CP0_STATUS), SR_FR);
            fr0 = jcc_fwd(c, CC_E);
            mov_mr32(c, X_CPU, OFF_FGR(fs), RAX);
            done = jmp_fwd(c);
            patch_here(c, fr0);
            mov_mr32(c, X_CPU, OFF_FGR(fs - 1) + 4, RAX);
            patch_here(c, done);
        }
        else mov_mr32(c, X_CPU, OFF_FGR(fs), RAX);
        return 1;
    case 0x05:
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        load64(g, RAX, ft);
        mov_mr64(c, X_CPU, OFF_FGR(fs), RAX);
        return 1;
    case 0x06:
        if (!g->sys->cpu.fpuNoTraps) return 0;
        cu1_guard(g, slow, nSlow);
        if (fs == 31)
        {
            load32(g, RAX, ft);
            alu_ri(c, 0, G_AND, RAX, (s32)FCR31_WRITABLE);
            mov_mr32(c, X_CPU, OFF_FCR31, RAX);
        }
        return 1;
    case 0x10: dbl = 0; break;
    case 0x11: dbl = 1; break;
    case 0x14:
        if (!fast || (funct != 0x20 && funct != 0x21)) return 0;
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        if (funct == 0x20) rm_guard(g, slow, nSlow);
        sse_rm(c, funct == 0x20 ? 0xF3 : 0xF2, 0x2A, 0, X_CPU, OFF_FGR(fs));
        fp_result(g, fd, funct == 0x21);
        return 1;
    default: return 0;
    }
    pre = dbl ? 0xF2 : 0xF3;
    if (funct == 0x06)
    {
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        mov_rm64(c, RAX, X_CPU, OFF_FGR(fs));
        mov_mr64(c, X_CPU, OFF_FGR(fd), RAX);
        return 1;
    }
    if (funct >= 0x30)
    {
        u32 cond = funct & 6;
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        alu_mi(c, 0, G_AND, X_CPU, OFF_FCR31, (s32)~(FCR31_C | FCR31_CAUSE));
        sse_rm(c, pre, 0x10, 0, X_CPU, OFF_FGR(fs));
        sse_rm(c, dbl ? 0x66 : 0, 0x2E, 0, X_CPU, OFF_FGR(ft));
        slow[(*nSlow)++] = jcc_fwd(c, CC_P);
        if (cond)
        {
            u32 skip = jcc_fwd(c, cond == 6 ? CC_A : cond == 4 ? CC_AE : CC_NE);
            alu_mi(c, 0, G_OR, X_CPU, OFF_FCR31, (s32)FCR31_C);
            patch_here(c, skip);
        }
        return 1;
    }
    if (!fast) return 0;
    switch (funct)
    {
    case 0x00: case 0x01: case 0x02: case 0x03:
    {
        static const u8 ops[4] = { 0x58, 0x5C, 0x59, 0x5E };
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        rm_guard(g, slow, nSlow);
        sse_rm(c, pre, 0x10, 0, X_CPU, OFF_FGR(fs));
        sse_rm(c, pre, ops[funct], 0, X_CPU, OFF_FGR(ft));
        fp_result(g, fd, dbl);
        return 1;
    }
    case 0x04:
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        rm_guard(g, slow, nSlow);
        sse_rm(c, pre, 0x51, 0, X_CPU, OFF_FGR(fs));
        fp_result(g, fd, dbl);
        return 1;
    case 0x05: case 0x07:
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        if (dbl)
        {
            mov_rm64(c, RAX, X_CPU, OFF_FGR(fs));
            rex(c, 1, 0, -1, RAX);
            p8(c, 0x0F); p8(c, 0xBA); p8(c, funct == 0x05 ? 0xF0 : 0xF8); p8(c, 63);
        }
        else
        {
            mov_rm32(c, RAX, X_CPU, OFF_FGR(fs));
            if (funct == 0x05) alu_ri(c, 0, G_AND, RAX, 0x7FFFFFFF);
            else alu_ri(c, 0, G_XOR, RAX, (s32)0x80000000u);
        }
        mov_mr64(c, X_CPU, OFF_FGR(fd), RAX);
        return 1;
    case 0x0C: case 0x0D: case 0x24:
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        if (funct == 0x24)
        {
            u32 nearest, done;
            mov_rm32(c, RCX, X_CPU, OFF_FCR31);
            alu_ri(c, 0, G_AND, RCX, 3);
            nearest = jcc_fwd(c, CC_E);
            alu_ri(c, 0, G_CMP, RCX, 1);
            slow[(*nSlow)++] = jcc_fwd(c, CC_NE);
            sse_rm(c, pre, 0x2C, RAX, X_CPU, OFF_FGR(fs));
            done = jmp_fwd(c);
            patch_here(c, nearest);
            sse_rm(c, pre, 0x2D, RAX, X_CPU, OFF_FGR(fs));
            patch_here(c, done);
        }
        else sse_rm(c, pre, funct == 0x0D ? 0x2C : 0x2D, RAX, X_CPU, OFF_FGR(fs));
        alu_ri(c, 0, G_CMP, RAX, (s32)0x80000000u);
        slow[(*nSlow)++] = jcc_fwd(c, CC_E);
        mov_mr64(c, X_CPU, OFF_FGR(fd), RAX);
        return 1;
    case 0x20:
        if (!dbl) return 0;
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        rm_guard(g, slow, nSlow);
        sse_rm(c, 0xF2, 0x5A, 0, X_CPU, OFF_FGR(fs));
        fp_result(g, fd, 0);
        return 1;
    case 0x21:
        if (dbl) return 0;
        cu1_guard(g, slow, nSlow);
        fr_guard(g, fs, slow, nSlow);
        sse_rm(c, 0xF3, 0x5A, 0, X_CPU, OFF_FGR(fs));
        fp_result(g, fd, 1);
        return 1;
    }
    return 0;
}

static int emit_int_fast(Gen *g, u32 op, u32 *slow, u32 *nSlow)
{
    X64 *c = &g->c;
    u32 opc = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, funct = op & 63;
    s32 simm = (s32)(s16)(op & 0xFFFF);
    if (opc == 0x08 || opc == 0x18)
    {
        int w = opc == 0x18;
        if (w) load64(g, RAX, rs);
        else load32(g, RAX, rs);
        alu_ri(c, w, G_ADD, RAX, simm);
        slow[(*nSlow)++] = jcc_fwd(c, CC_O);
        if (!w) movsxd_rr(c, RAX, RAX);
        store64(g, RAX, rt);
        return 1;
    }
    if (opc != 0) return 0;
    switch (funct)
    {
    case 0x20: case 0x22: case 0x2C: case 0x2E:
    {
        int w = funct >= 0x2C;
        if (w) load64(g, RAX, rs);
        else load32(g, RAX, rs);
        if (rt)
        {
            alu_rm(c, w, (funct & 2) ? G_SUB : G_ADD, RAX, X_CPU, OFF_GPR(rt));
            slow[(*nSlow)++] = jcc_fwd(c, CC_O);
        }
        if (!w) movsxd_rr(c, RAX, RAX);
        store64(g, RAX, rd);
        return 1;
    }
    case 0x1A: case 0x1B:
        load32(g, RAX, rs);
        load32(g, RCX, rt);
        test_rr(c, 0, RCX, RCX);
        slow[(*nSlow)++] = jcc_fwd(c, CC_E);
        if (funct == 0x1A)
        {
            alu_ri(c, 0, G_CMP, RCX, -1);
            slow[(*nSlow)++] = jcc_fwd(c, CC_E);
            p8(c, 0x99);
            p8(c, 0xF7); p8(c, 0xF9);
        }
        else
        {
            xor_rr32(c, RDX);
            p8(c, 0xF7); p8(c, 0xF1);
        }
        movsxd_rr(c, RAX, RAX);
        movsxd_rr(c, RDX, RDX);
        mov_mr64(c, X_CPU, OFF_LO, RAX);
        mov_mr64(c, X_CPU, OFF_HI, RDX);
        return 1;
    }
    return 0;
}

static int emit_slowable(Gen *g, u32 op, u32 *slow, u32 *nSlow)
{
    return emit_mem_fast(g, op, slow, nSlow) || emit_fpu_fast(g, op, slow, nSlow) || emit_int_fast(g, op, slow, nSlow);
}

static Tail *new_tail(Gen *g, int kind, const u32 *slow, u32 nSlow, u32 pc)
{
    Tail *t;
    u32 i;
    if (g->nTails >= H64_JIT_MAX_INSNS + 2 || nSlow > 6) { g->c.overflow = 1; return 0; }
    t = &g->tails[g->nTails++];
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    for (i = 0; i < nSlow; i++) t->at[i] = slow[i];
    t->n = nSlow;
    t->pc = pc;
    t->op = op_at(g, pc);
    t->pend = g->pending;
    return t;
}

static int emit_mem(Gen *g, u32 op, u32 pc)
{
    u32 slow[6], nSlow = 0;
    Tail *t;
    g->wantDtlb = 1;
    g->dtlbKind = 0;
    if (!emit_slowable(g, op, slow, &nSlow)) { g->wantDtlb = 0; return 0; }
    g->wantDtlb = 0;
    if (!nSlow && !g->dtlbKind) { g->pending++; return 1; }
    t = new_tail(g, TAIL_SLOW, slow, nSlow, pc);
    if (t)
    {
        t->back = g->c.pos;
        t->dtlbAt = g->dtlbAt;
        t->dtlbBack = g->dtlbBack;
        t->dtlbKind = g->dtlbKind;
    }
    else if (g->dtlbKind) g->c.overflow = 1;
    g->dtlbKind = 0;
    g->pending++;
    return 1;
}

static int emit_mtc0(Gen *g, u32 op)
{
    X64 *c = &g->c;
    u32 rt = (op >> 16) & 31, rd = (op >> 11) & 31, mask;
    if (!mtc0_plain(op)) return 0;
    mask = rd == CP0_INDEX ? 0x8000003Fu : rd == CP0_PAGEMASK ? 0x01FFE000u : 0x3FFFFFFFu;
    load32(g, RAX, rt);
    movsxd_rr(c, RCX, RAX);
    mov_mr64(c, X_CPU, (s32)offsetof(H64Cpu, cop0Latch), RCX);
    alu_ri(c, 0, G_AND, RAX, (s32)mask);
    mov_mr64(c, X_CPU, OFF_COP0(rd), RAX);
    return 1;
}

static int emit_mfc0(Gen *g, u32 op)
{
    X64 *c = &g->c;
    u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31;
    if ((op >> 26) != 0x10 || (rs != 0 && rs != 1) || rd == CP0_RANDOM) return 0;
    if (rt == 0) return 1;
    if (rd == CP0_COUNT)
    {
        mov_rm64(c, RAX, X_CPU, OFF_CYCLES);
        if (g->pending) alu_ri(c, 1, G_ADD, RAX, (s32)(g->pending * g->cpi));
        shift_ri(c, 1, SH_SHR, RAX, 1);
        alu_rm(c, 0, G_ADD, RAX, X_CPU, (s32)offsetof(H64Cpu, countOffset));
        if (rs == 0) movsxd_rr(c, RAX, RAX);
    }
    else
    {
        int latch = rd == 7 || (rd >= 21 && rd <= 25) || rd == 31;
        mov_rm64(c, RAX, X_CPU, latch ? (s32)offsetof(H64Cpu, cop0Latch) : OFF_COP0(rd));
        if (rs == 0) movsxd_rr(c, RAX, RAX);
    }
    store64(g, RAX, rt);
    return 1;
}

static int emit_native(Gen *g, u32 op, u32 pc)
{
    if (emit_alu(g, op)) { g->pending++; return 1; }
    if (emit_mfc0(g, op)) { g->pending++; return 1; }
    if (emit_mtc0(g, op)) { g->pending++; return 1; }
    if ((op >> 26) == 0x2F) { g->pending++; return 1; }
    return emit_mem(g, op, pc);
}

static int emit_branch_head(Gen *g, u32 op, u32 pc, int *likely, int *dynamicTarget, u64 *target)
{
    X64 *c = &g->c;
    u32 opc = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31;
    *target = sext32(pc) + 4 + ((u64)(s64)(s16)(op & 0xFFFF) << 2);
    *likely = 0;
    *dynamicTarget = 0;
    g->condAlways = 0;
    switch (opc)
    {
    case 0x00:
        load64(g, X_TGT, rs);
        if ((op & 63) == 0x09 && rd != 0) mov_m64_imm(c, X_CPU, OFF_GPR(rd), sext32(pc) + 8);
        g->condAlways = 1;
        *dynamicTarget = 1;
        return 1;
    case 0x02: case 0x03:
        *target = ((sext32(pc) + 4) & 0xFFFFFFFFF0000000ull) | ((u64)(op & 0x03FFFFFF) << 2);
        if (opc == 0x03) mov_m64_imm(c, X_CPU, OFF_GPR(31), sext32(pc) + 8);
        g->condAlways = 1;
        return 1;
    case 0x01:
        *likely = (rt & 2) != 0;
        load64(g, RAX, rs);
        test_rr(c, 1, RAX, RAX);
        setcc_reg(c, (rt & 1) ? CC_GE : CC_L, X_COND);
        if (rt & 0x10) mov_m64_imm(c, X_CPU, OFF_GPR(31), sext32(pc) + 8);
        return 1;
    case 0x04: case 0x05: case 0x14: case 0x15:
        *likely = opc >= 0x14;
        load64(g, RAX, rs);
        if (rt) alu_rm(c, 1, G_CMP, RAX, X_CPU, OFF_GPR(rt));
        else test_rr(c, 1, RAX, RAX);
        setcc_reg(c, (opc & 1) ? CC_NE : CC_E, X_COND);
        return 1;
    case 0x06: case 0x07: case 0x16: case 0x17:
        *likely = opc >= 0x16;
        load64(g, RAX, rs);
        test_rr(c, 1, RAX, RAX);
        setcc_reg(c, (opc & 1) ? CC_G : CC_LE, X_COND);
        return 1;
    case 0x11:
        *likely = (rt & 2) != 0;
        if (!g->cu1Known)
        {
            u32 slow[1];
            test_mi32(c, X_CPU, OFF_COP0(CP0_STATUS), SR_CU1);
            slow[0] = jcc_fwd(c, CC_E);
            new_tail(g, TAIL_EXIT, slow, 1, pc);
            g->cu1Known = 1;
        }
        test_mi32(c, X_CPU, OFF_FCR31, FCR31_C);
        setcc_reg(c, (rt & 1) ? CC_NE : CC_E, X_COND);
        return 1;
    }
    return 0;
}

static void store_branch_pc(Gen *g, int dynamicTarget, u64 target, u32 fallthrough)
{
    X64 *c = &g->c;
    if (dynamicTarget) mov_rr64(c, RAX, X_TGT);
    else if (g->condAlways) mov_ri64(c, RAX, target);
    else
    {
        mov_ri64(c, RAX, sext32(fallthrough));
        mov_ri64(c, RCX, target);
        test_rr(c, 0, X_COND, X_COND);
        cmov_rr64(c, CC_NE, RAX, RCX);
    }
    mov_mr64(c, X_CPU, OFF_PC, RAX);
    alu_ri(c, 1, G_ADD, RAX, 4);
    mov_mr64(c, X_CPU, OFF_NEXTPC, RAX);
}

static void emit_slot_interp(Gen *g, u32 pending, int dynamicTarget, u64 target, u32 dsPc)
{
    X64 *c = &g->c;
    add_counters(g, pending);
    store_branch_pc(g, dynamicTarget, target, dsPc + 4);
    mov_rm64(c, RAX, X_CPU, OFF_PC);
    mov_mr64(c, X_CPU, OFF_NEXTPC, RAX);
    mov_m64_imm(c, X_CPU, OFF_PC, sext32(dsPc));
    mov_m32_imm(c, X_CPU, OFF_BRANCH, 1);
    call_interp_raw(g);
    exit_block(g);
}

static void emit_branch(Gen *g, u32 op, u32 pc, u32 ds)
{
    X64 *c = &g->c;
    int likely, dynamicTarget;
    u64 target;
    u32 dsPc = pc + 4;
    if (!emit_branch_head(g, op, pc, &likely, &dynamicTarget, &target))
    {
        call_interp(g, pc);
        call_interp_raw(g);
        exit_block(g);
        return;
    }
    g->pending++;
    if (likely)
    {
        u32 taken;
        test_rr(c, 0, X_COND, X_COND);
        taken = jcc_fwd(c, CC_NE);
        sync_to(g, g->pending, pc + 8);
        exit_fixed(g, pc + 8);
        patch_here(c, taken);
    }
    if (!is_branch(ds) && !ends_block(ds))
    {
        u32 slow[6], nSlow = 0, pend = g->pending;
        int done = emit_alu(g, ds) || (ds >> 26) == 0x2F, mem = 0;
        if (!done)
        {
            g->wantDtlb = 1;
            g->dtlbKind = 0;
            mem = emit_slowable(g, ds, slow, &nSlow);
            g->wantDtlb = 0;
        }
        if (mem)
        {
            Tail *t = nSlow || g->dtlbKind ? new_tail(g, TAIL_SLOT, slow, nSlow, dsPc) : 0;
            if (!t && g->dtlbKind) g->c.overflow = 1;
            if (t)
            {
                t->dtlbAt = g->dtlbAt;
                t->dtlbBack = g->dtlbBack;
                t->dtlbKind = g->dtlbKind;
                t->pend = pend;
                t->dyn = dynamicTarget;
                t->always = g->condAlways;
                t->target = target;
            }
            g->dtlbKind = 0;
            done = 1;
        }
        if (done)
        {
            add_counters(g, pend + 1);
            store_branch_pc(g, dynamicTarget, target, dsPc + 4);
            mov_m32_imm(c, X_CPU, OFF_BRANCH, 0);
            g->pending = 0;
            exit_branch(g, dynamicTarget, target, dsPc + 4);
            return;
        }
    }
    emit_slot_interp(g, g->pending, dynamicTarget, target, dsPc);
    g->pending = 0;
}

static void emit_tails(Gen *g)
{
    X64 *c = &g->c;
    const u8 *rtExit = (const u8 *)g->sys->jit->rtExit;
    u32 i, k;
    for (i = 0; i < g->nLinks; i++)
    {
        patch_here(c, g->links[i].at);
        mov_ri32(c, RCX, (u32)((c->buf + g->links[i].at) - g->sys->jit->mem));
        mov_ri32(c, RAX, g->links[i].target);
        jmp_to(c, (const u8 *)g->sys->jit->rtUnlinked);
    }
    for (i = 0; i < g->nTails; i++)
    {
        Tail *t = &g->tails[i];
        if ((t->kind == TAIL_SLOW || t->kind == TAIL_SLOT) && t->dtlbKind)
        {
            patch_here(c, t->dtlbAt);
            if (t->kind == TAIL_SLOW && (t->op >> 26) == 0x23)
            {
                u32 rt = (t->op >> 16) & 31, k, next;
                for (k = 0; k < 2; k++)
                {
                    alu_ri(c, 0, G_CMP, RAX, (s32)(0xA4300008u + 4 * k));
                    next = jcc_fwd(c, CC_NE);
                    mov_rm32(c, RCX, X_SYS, (s32)(offsetof(H64System, mi) + (k ? offsetof(H64Mi, mask) : offsetof(H64Mi, intr))));
                    if (rt) { movsxd_rr(c, RCX, RCX); mov_mr64(c, X_CPU, OFF_GPR(rt), RCX); }
                    jmp_to(c, c->buf + t->back);
                    patch_here(c, next);
                }
            }
            if (t->kind == TAIL_SLOW && (t->op >> 26) == 0x2B)
            {
                u32 next, ok[2];
                alu_ri(c, 0, G_CMP, RAX, (s32)0xA430000Cu);
                next = jcc_fwd(c, CC_NE);
                load32(g, ARG1, (t->op >> 16) & 31);
                mov_rr64(c, ARG0, X_SYS);
                mov_ri64(c, RAX, (u64)(uintptr_t)&jit_mi_mask_write);
                p8(c, 0xFF); p8(c, 0xD0);
                mov_rm32(c, RCX, X_CPU, OFF_COP0(CP0_STATUS));
                mov_rr32(c, RAX, RCX);
                alu_ri(c, 0, G_AND, RAX, 7);
                alu_ri(c, 0, G_CMP, RAX, 1);
                ok[0] = jcc_fwd(c, CC_NE);
                mov_rm32(c, RDX, X_CPU, OFF_COP0(CP0_CAUSE));
                alu_rr(c, 0, G_AND, RDX, RCX);
                alu_ri(c, 0, G_AND, RDX, 0xFF00);
                ok[1] = jcc_fwd(c, CC_E);
                sync_to(g, t->pend + 1, t->pc + 4);
                jmp_to(c, rtExit);
                patch_here(c, ok[0]);
                patch_here(c, ok[1]);
                jmp_to(c, c->buf + t->back);
                patch_here(c, next);
            }
            call_to(c, (const u8 *)g->sys->jit->rtDtlb[(t->dtlbKind - 1) >> 2][(t->dtlbKind - 1) & 3]);
            jcc_to(c, CC_E, c->buf + t->dtlbBack);
        }
        for (k = 0; k < t->n; k++) patch_here(c, t->at[k]);
        if (t->kind == TAIL_SLOW)
        {
            mov_ri32(c, RAX, t->pend);
            mov_ri32(c, R10, t->pc);
            mov_ri32(c, R11, t->op);
            call_to(c, (const u8 *)g->sys->jit->rtSlow);
            jmp_to(c, c->buf + t->back);
        }
        else if (t->kind == TAIL_TLB)
        {
            sync_to(g, 0, t->pc);
            mov_rm64(c, RCX, X_SYS, (s32)offsetof(H64System, jit));
            mov_ri64(c, RAX, (u64)(uintptr_t)t->block);
            mov_mr64(c, RCX, (s32)offsetof(H64Jit, staleBlock), RAX);
            jmp_to(c, rtExit);
        }
        else if (t->kind == TAIL_EXIT)
        {
            sync_to(g, t->pend, t->pc);
            call_helper(g, t->pc + 4, t->op, 1);
            test_rr(c, 0, RAX, RAX);
            jcc_to(c, CC_NE, rtExit);
            call_interp_raw(g);
            jmp_to(c, rtExit);
        }
        else
        {
            g->condAlways = t->always;
            emit_slot_interp(g, t->pend, t->dyn, t->target, t->pc);
        }
    }
}

static int plain_branch_to(u32 op, u32 pc, u32 target)
{
    u32 opc = op >> 26, rt = (op >> 16) & 31;
    u64 off = sext32(pc) + 4 + ((u64)(s64)(s16)(op & 0xFFFF) << 2);
    if (opc == 0x02) return (((pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFF) << 2)) == target;
    if ((opc >= 0x04 && opc <= 0x07) || (opc == 0x01 && rt <= 0x01)) return off == sext32(target);
    return 0;
}

static void classify_idle(H64JitBlock *b, const u32 *ops, u32 n)
{
    u32 pc = b->vpc;
    b->idle = 0;
    if (n == 2 && ops[1] == 0 && plain_branch_to(ops[0], pc, pc))
        b->idle = 1;
    else if (n == 3 && ops[2] == 0 && plain_branch_to(ops[1], pc + 4, pc) && ((ops[1] >> 26) == 0x04 || (ops[1] >> 26) == 0x05))
    {
        u32 lop = ops[0] >> 26, rX = (ops[0] >> 16) & 31, base = (ops[0] >> 21) & 31;
        u32 bs = (ops[1] >> 21) & 31, bt = (ops[1] >> 16) & 31;
        u32 size = lop == 0x20 || lop == 0x24 ? 1 : lop == 0x21 || lop == 0x25 ? 2 : lop == 0x23 || lop == 0x27 ? 4 : 0;
        if (size && rX != 0 && rX != base && (bs == rX || bt == rX))
        {
            b->idle = 2;
            b->pollBase = base;
            b->pollOff = (s32)(s16)(ops[0] & 0xFFFF);
            b->pollSize = size;
        }
    }
    else if (n == 4 && ops[3] == 0 && (ops[0] >> 26) == 0x0F && plain_branch_to(ops[2], pc + 8, pc) &&
             ((ops[2] >> 26) == 0x04 || (ops[2] >> 26) == 0x05))
    {
        u32 lop = ops[1] >> 26, rX = (ops[1] >> 16) & 31, base = (ops[1] >> 21) & 31, lt = (ops[0] >> 16) & 31;
        u32 bs = (ops[2] >> 21) & 31, bt = (ops[2] >> 16) & 31, other = bs == rX ? bt : bs;
        u32 size = lop == 0x20 || lop == 0x24 ? 1 : lop == 0x21 || lop == 0x25 ? 2 : lop == 0x23 || lop == 0x27 ? 4 : 0;
        if (size && rX != 0 && lt != 0 && base == lt && (bs == rX || bt == rX) && other != lt && (other != rX || bs == bt))
        {
            b->idle = 2;
            b->pollBase = 0;
            b->pollOff = (s32)((ops[0] & 0xFFFF) << 16) + (s32)(s16)(ops[1] & 0xFFFF);
            b->pollSize = size;
        }
    }
}

H64JitBlock *h64_jit_compile(H64System *sys, u32 pc, u32 paddr)
{
    H64Jit *j = sys->jit;
    H64JitBlock *b;
    Gen g;
    u32 ops[H64_JIT_MAX_INSNS], n = 0, i;
    u32 pageEnd = (paddr | 0xFFF) + 1;
    int endsWithBranch = 0, statusFast = 0;
    u8 *start;

    while (n < H64_JIT_MAX_INSNS && paddr + n * 4 < pageEnd)
    {
        u32 op = h64_rdram_load32(sys->rdram, paddr + n * 4);
        if (is_branch(op))
        {
            if (paddr + n * 4 + 4 >= pageEnd || n + 2 > H64_JIT_MAX_INSNS) break;
            ops[n++] = op;
            ops[n] = h64_rdram_load32(sys->rdram, paddr + n * 4);
            n++;
            endsWithBranch = 1;
            break;
        }
        ops[n++] = op;
        if (ends_block(op)) break;
    }
    if (n == 0) return 0;

    if (j->blockCount >= j->blockCap || j->memUsed + MAX_BLOCK_BYTES + 64 > j->memSize)
    {
        H64_INFO("[jit] code cache full (%u blocks, %u KB): flushed", j->blockCount, j->memUsed >> 10);
        h64_jit_reset(sys);
    }

    j->memUsed = (j->memUsed + 15) & ~15u;
    start = j->mem + j->memUsed;
    memset(&g, 0, sizeof(g));
    g.sys = sys;
    g.c.buf = start;
    g.c.cap = MAX_BLOCK_BYTES;
    g.pc0 = pc;
    g.paddr0 = paddr;
    g.cpi = sys->cpu.cpi ? sys->cpu.cpi : 1;
    g.native = h64_jit_kernel_mode(&sys->cpu) && !j->noNative;

    if (!(pc >= 0x80000000u && pc < 0xC0000000u))
    {
        u32 at;
        Tail *t;
        alu_mi(&g.c, 0, G_CMP, X_CPU, (s32)(offsetof(H64Cpu, jitPage) + 4 * ((pc >> 12) & 255)), (s32)H64_JIT_PAGE_KEY(pc, paddr));
        at = jcc_fwd(&g.c, CC_NE);
        t = new_tail(&g, TAIL_TLB, &at, 1, pc);
        if (t) t->block = &j->blocks[j->blockCount];
    }
    for (i = 0; i < n; i++)
    {
        u32 ipc = pc + i * 4, op = ops[i];
        if (endsWithBranch && i == n - 2)
        {
            if (g.native)
            {
                emit_branch(&g, op, ipc, ops[i + 1]);
                j->stats.nativeInsns++;
            }
            else
            {
                call_interp(&g, ipc);
                call_interp_raw(&g);
                j->stats.helperInsns += 2;
                exit_block(&g);
            }
            break;
        }
        if (g.native && i == n - 1 && (op >> 26) == 0x10 && ((op >> 21) & 31) == 4 && ((op >> 11) & 31) == CP0_STATUS &&
            !j->noStatusFast)
        {
            X64 *c = &g.c;
            u32 fast, noInt[2];
            load32(&g, RCX, (op >> 16) & 31);
            alu_ri(c, 0, G_AND, RCX, (s32)0xFF57FFFFu);
            mov_rm32(c, RDX, X_CPU, OFF_COP0(CP0_STATUS));
            alu_rr(c, 0, G_XOR, RDX, RCX);
            alu_ri(c, 0, G_AND, RDX, (s32)~0xFF01u);
            fast = jcc_fwd(c, CC_E);
            sync_to(&g, g.pending, ipc);
            call_helper(&g, ipc + 4, op, 1);
            exit_block(&g);
            patch_here(c, fast);
            mov_mr64(c, X_CPU, OFF_COP0(CP0_STATUS), RCX);
            g.pending++;
            mov_rr32(c, RAX, RCX);
            alu_ri(c, 0, G_AND, RAX, 7);
            alu_ri(c, 0, G_CMP, RAX, 1);
            noInt[0] = jcc_fwd(c, CC_NE);
            mov_rm32(c, RDX, X_CPU, OFF_COP0(CP0_CAUSE));
            alu_rr(c, 0, G_AND, RDX, RCX);
            alu_ri(c, 0, G_AND, RDX, 0xFF00);
            noInt[1] = jcc_fwd(c, CC_E);
            sync_to(&g, g.pending, ipc + 4);
            exit_block(&g);
            patch_here(c, noInt[0]);
            patch_here(c, noInt[1]);
            statusFast = 1;
            j->stats.nativeInsns++;
            continue;
        }
        if (g.native && !ends_block(op) && emit_native(&g, op, ipc))
        {
            j->stats.nativeInsns++;
            continue;
        }
        call_interp(&g, ipc);
        j->stats.helperInsns++;
    }
    if (!endsWithBranch)
    {
        sync_to(&g, g.pending, pc + n * 4);
        if (ends_block(ops[n - 1]) && !statusFast) exit_block(&g);
        else exit_fixed(&g, pc + n * 4);
    }
    j->stats.hotBytes += g.c.pos;
    emit_tails(&g);
    if (g.c.overflow)
    {
        H64_ERROR("[jit] block at %08X too large", pc);
        return 0;
    }

    b = &j->blocks[j->blockCount++];
    memset(b, 0, sizeof(*b));
    b->vpc = pc;
    b->paddr = paddr;
    b->insns = n;
    b->kernel = g.native;
    b->body = (u32 *)(void *)start;
    b->linkHead = -1;
    b->valid = 1;
    b->hotWords = g.c.pos;
    if (g.native) classify_idle(b, ops, n);
    if (g.native && sys->options.idleYieldPc && pc == sys->options.idleYieldPc) b->idle = 3;
    if (g.native && sys->options.countWaitPc && pc == sys->options.countWaitPc) b->idle = 4;
    b->fn = 0;
    j->memUsed = (u32)((start - j->mem) + g.c.pos);

    b->hashNext = j->hash[(pc >> 2) & 8191];
    j->hash[(pc >> 2) & 8191] = b;
    b->pageNext = j->pageHead[paddr >> 12];
    j->pageHead[paddr >> 12] = b;
    h64_jit_code_map(j, b, 1);
    j->stats.blocksCompiled++;
    j->stats.codeBytes += g.c.pos;
    H64_DEBUG("[jit] block %08X (phys %06X): %u instructions, %u bytes, first %08X", pc, paddr, n, g.c.pos, ops[0]);
    return b;
}

#endif
