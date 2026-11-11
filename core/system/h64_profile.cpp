// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 - per-game settings built into the emulator (see h64_profile.h).
#include "h64_profile.h"
#include "h64_system.h"
#include "../common/h64_log.h"
#include <string.h>

struct GameProfile
{
    const char *code;
    const char *name;
    u32 flags;
};

static const GameProfile s_profiles[] = {
    { "NFU", "Conker's Bad Fur Day", H64_GP_LATE_PARSE },
    { "NKT", "Mario Kart 64", H64_GP_VIEWPORT_CLIP | H64_GP_FRAME_TO_RDRAM },
    { "NMQ", "Paper Mario", H64_GP_CPU_WRITTEN },
    { "NYS", "Yoshi's Story", H64_GP_IPL3_BOOT | H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT | H64_GP_SOFT_8BIT },
    { "NMF", "Mario Golf", H64_GP_OTHERMODE_INIT | H64_GP_COPY_BACK_FULL },
    { "NYW", "Harvest Moon 64", H64_GP_OTHERMODE_INIT },
    { "NM8", "Mario Tennis", H64_GP_NO_COPY_BACK | H64_GP_SOFT_8BIT },
    { "N22", "Ready 2 Rumble Boxing: Round 2", H64_GP_OTHERMODE_INIT },
    { "NSQ", "StarCraft 64", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT },
    { "NBO", "Bottom of the 9th", H64_GP_COPY_BACK_WHOLE },
    { "NRS", "Star Wars: Rogue Squadron", H64_GP_COPY_BACK_SAME_WIDTH | H64_GP_TALL_FRAME },
    { "NB7", "Banjo-Tooie", H64_GP_TALL_FRAME | H64_GP_SOFT_8BIT },
    { "NDZ", "Duke Nukem: Zero Hour", H64_GP_TALL_FRAME },
    { "NP3", "Pokemon Stadium 2", H64_GP_SMALL_TO_RDRAM },
    { "NFD", "Flying Dragon", H64_GP_COPY_BACK_SAME_WIDTH },
    { "NXO", "Cruis'n Exotica", H64_GP_OTHERMODE_INIT },
    { "NHO", "NHL Blades of Steel '99", H64_GP_BIG_TEX_POOL },
    { "NRE", "Resident Evil 2", H64_GP_RDRAM_COPY | H64_GP_SLOT_BY_WIDTH | H64_GP_TALL_FRAME },
    { "NN6", "Dr. Mario 64", H64_GP_COPY_BACK_WHOLE | H64_GP_CPU_SPRITES },
    { "ND3", "Castlevania", H64_GP_FRAME_TO_RDRAM | H64_GP_OTHERMODE_INIT },
    { "ND4", "Castlevania: Legacy of Darkness", H64_GP_FRAME_TO_RDRAM | H64_GP_OTHERMODE_INIT },
    { "CZL", "The Legend of Zelda: Ocarina of Time", H64_GP_SMALL_TO_RDRAM | H64_GP_SLOT_BY_WIDTH },
    { "NZL", "The Legend of Zelda: Ocarina of Time", H64_GP_SMALL_TO_RDRAM | H64_GP_SLOT_BY_WIDTH },
    { "NSM", "Super Mario 64", H64_GP_PI_ODD_CART | H64_GP_FPU_NO_TRAPS | H64_GP_LAX_ALIGN64 | H64_GP_NO_OV_TRAP },
    { "NV8", "Vigilante 8", H64_GP_COPY_BACK_SAME_WIDTH },
    { "NVG", "Vigilante 8: Second Offense", H64_GP_COPY_BACK_SAME_WIDTH },
    { "NDO", "Donkey Kong 64", H64_GP_WS_BORDER | H64_GP_NO_OLD_FRAME | H64_GP_DEPTH_FILL_RDRAM },
    { "NRR", "Roadsters Trophy", H64_GP_COPY_BACK_SAME_WIDTH },
    { "NM6", "Mega Man 64", H64_GP_COPY_BACK_SAME_WIDTH },
    { "NAL", "Super Smash Bros.", H64_GP_COPY_BACK_SAME_WIDTH },
    { "NBX", "BattleTanx", H64_GP_BOOT_TIME },
    { "NIJ", "Indiana Jones and the Infernal Machine", H64_GP_BOOT_TIME | H64_GP_TALL_FRAME },
    { "NNA", "Star Wars Episode I: Battle for Naboo", H64_GP_TALL_FRAME | H64_GP_COPY_BACK_SAME_WIDTH },
    { "N7I", "FIFA Soccer 64", H64_GP_BOOT_TIME },
    { "NCW", "Cruis'n World", H64_GP_IPL3_BOOT },
    { "NBK", "Banjo-Kazooie", H64_GP_IPL3_BOOT },
    { "NBV", "Bomberman 64: The Second Attack!", H64_GP_S2DEX1 },
    { "NOB", "Ogre Battle 64: Person of Lordly Caliber", H64_GP_S2DEX1 },
    { "NDE", "Destruction Derby 64", H64_GP_S2DEX1 },
    { "NSV", "Space Station Silicon Valley", H64_GP_OTHERMODE_INIT },
    { "NTT", "Tonic Trouble", H64_GP_OTHERMODE_INIT },
    { "NQK", "Quake 64", H64_GP_OTHERMODE_INIT },
    { "NTD", "O.D.T.", H64_GP_OTHERMODE_INIT },
    { "NX2", "Gex 64: Enter the Gecko", H64_GP_OTHERMODE_INIT },
    { "NBU", "Bust-A-Move 2", H64_GP_OTHERMODE_INIT },
    { "NC2", "Clay Fighter: Sculptor's Cut", H64_GP_OTHERMODE_INIT },
    { "NDM", "Doom 64", H64_GP_OTHERMODE_INIT },
    { "NEL", "Elmo's Letter Adventure", H64_GP_OTHERMODE_INIT },
    { "NSP", "Superman", H64_GP_OTHERMODE_INIT },
    { "NJM", "Earthworm Jim 3D", H64_GP_OTHERMODE_INIT },
    { "NVR", "Virtual Pool 64", H64_GP_OTHERMODE_INIT },
    { "NFF", "Fighting Force 64", H64_GP_KEEP_SHOWN },
    { "NML", "Mickey's Speedway USA", H64_GP_MASK_TO_TILE },
    { "NEP", "Star Wars Episode I: Racer", H64_GP_TALL_FRAME },
    { "NRI", "The New Tetris", H64_GP_RECT_POINT | H64_GP_NO_COPY_BACK },
    { "NT6", "Tetris 64", H64_GP_OTHERMODE_INIT },
    { "NHW", "Hot Wheels Turbo Racing", H64_GP_ALPHA_REF0_PASS },
    { "NL2", "Top Gear Rally 2", H64_GP_NO_OLD_FRAME },
    { "NGR", "Top Gear Rally", H64_GP_ALPHA_REF0_PASS },
    { "NWO", "World Driver Championship", H64_GP_COPY_BACK_SAME_WIDTH | H64_GP_TALL_FRAME },
    { "NCS", "S.C.A.R.S.", H64_GP_SHORT_GFX_TASK },
    { "NGX", "Gauntlet Legends", H64_GP_GFX_STREAM },
    { "NMT", "Magical Tetris Challenge", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT },
    { "NK4", "Kirby 64: The Crystal Shards", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT },
    { "NCC", "Command & Conquer", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT },
    { "NVL", "V-Rally Edition 99", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT | H64_GP_COPY_BACK_SAME_WIDTH },
    { "NPN", "Pokemon Puzzle League", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT },
    { "NS6", "Star Soldier: Vanishing Earth", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT | H64_GP_COPY_BACK_SAME_WIDTH },
    { "NGC", "GT 64: Championship Edition", H64_GP_S2DEX1 | H64_GP_OTHERMODE_INIT },
};

enum { H64_GX_SOFT_TINY = 1,
       H64_GX_COPYBACK_ALPHA = 2,
       H64_GX_MODIFY_ST_HALF = 4,
       H64_GX_CIMG_FROM_RDRAM = 8,
       H64_GX_XLU_NO_BIAS = 16,
       H64_GX_VI_LINE0 = 32,
       H64_GX_VI_LINE_START = 64,
       H64_GX_SKIP_ZIMAGE = 128,
       H64_GX_DECAL_STENCIL = 256,
       H64_GX_ALPHA_DITHER = 512,
       H64_GX_LOD_TILE = 1024 };
struct GameExtra { const char *code; u32 extra; };
static const GameExtra s_extras[] = {
    { "NMQ", H64_GX_SOFT_TINY },
    { "NLG", H64_GX_COPYBACK_ALPHA | H64_GX_CIMG_FROM_RDRAM },
    { "NCY", H64_GX_MODIFY_ST_HALF },
    { "NM8", H64_GX_SKIP_ZIMAGE },
    { "NJP", H64_GX_DECAL_STENCIL },
    { "NMV", H64_GX_XLU_NO_BIAS },
    { "N7I", H64_GX_VI_LINE0 },
    { "NWZ", H64_GX_VI_LINE_START },
    { "NGR", H64_GX_ALPHA_DITHER | H64_GX_LOD_TILE },
};

static void apply_idle_yield(H64System *sys);

u32 h64_game_profile(const char *gameCode, const char **name)
{
    u32 i;
    if (name) *name = NULL;
    if (!gameCode) return 0;
    for (i = 0; i < sizeof(s_profiles) / sizeof(s_profiles[0]); i++)
        if (!memcmp(gameCode, s_profiles[i].code, 3))
        {
            if (name) *name = s_profiles[i].name;
            return s_profiles[i].flags;
        }
    return 0;
}

void h64_apply_game_profile(H64System *sys)
{
    const char *name;
    u32 f = h64_game_profile(sys->rom.gameCode, &name), i, extra = 0;
    h64_apply_profile_flags(sys, f);
    apply_idle_yield(sys);
    for (i = 0; i < sizeof(s_extras) / sizeof(s_extras[0]); i++)
        if (!memcmp(sys->rom.gameCode, s_extras[i].code, 3)) extra = s_extras[i].extra;
    sys->options.gfxSoftTiny = (extra & H64_GX_SOFT_TINY) != 0;
    sys->options.gfxCopyBackAlpha = (extra & H64_GX_COPYBACK_ALPHA) != 0;
    sys->options.gfxModStHalf = (extra & H64_GX_MODIFY_ST_HALF) != 0;
    sys->options.gfxCimgFromRdram = (extra & H64_GX_CIMG_FROM_RDRAM) != 0;
    sys->options.gfxXluNoBias = (extra & H64_GX_XLU_NO_BIAS) != 0;
    sys->options.viIntrLine0 = (extra & H64_GX_VI_LINE0) != 0;
    sys->options.gfxViLineStart = (extra & H64_GX_VI_LINE_START) != 0;
    sys->options.gfxSkipZImage = (extra & H64_GX_SKIP_ZIMAGE) != 0;
    sys->options.gfxDecalStencil = (extra & H64_GX_DECAL_STENCIL) != 0;
    sys->options.gfxAlphaDither = (extra & H64_GX_ALPHA_DITHER) != 0;
    sys->options.gfxLodTile = (extra & H64_GX_LOD_TILE) != 0;
    if (name) H64_INFO("[profile] %s: flags %X%s", name, f, extra ? " (+ extra options)" : "");
}

struct GameCpi { const char *code; u32 cpi; };
static const GameCpi s_gameCpi[] = {
    { "NNA", 3 },
    { "NIJ", 3 },
};

struct IdleYield { const char *code; u32 pc, runQueue; };
static const IdleYield s_idleYield[] = {
    { "NIJ", 0x40036DA0u, 0x000479F8u },
};

struct CountWait { const char *code; u32 pc, step, queue; };
static const CountWait s_countWait[] = {
    { "NFF", 0x80022948u, 4096, 0 },
    { "NVR", 0x800062ACu, 0, 0x00271C78u },
    { "NRT", 0x80049848u, 32768, 0 },
};

static void apply_idle_yield(H64System *sys)
{
    u32 i;
    sys->options.idleYieldPc = sys->options.idleRunQueue = 0;
    sys->options.gameCpi = 0;
    for (i = 0; i < sizeof(s_gameCpi) / sizeof(s_gameCpi[0]); i++)
        if (!memcmp(sys->rom.gameCode, s_gameCpi[i].code, 3)) sys->options.gameCpi = s_gameCpi[i].cpi;
    sys->options.countWaitPc = sys->options.countWaitStep = sys->options.countWaitQueue = 0;
    for (i = 0; i < sizeof(s_countWait) / sizeof(s_countWait[0]); i++)
        if (!memcmp(sys->rom.gameCode, s_countWait[i].code, 3))
        {
            sys->options.countWaitPc = s_countWait[i].pc;
            sys->options.countWaitStep = s_countWait[i].step;
            sys->options.countWaitQueue = s_countWait[i].queue;
        }
    for (i = 0; i < sizeof(s_idleYield) / sizeof(s_idleYield[0]); i++)
        if (!memcmp(sys->rom.gameCode, s_idleYield[i].code, 3))
        {
            sys->options.idleYieldPc = s_idleYield[i].pc;
            sys->options.idleRunQueue = s_idleYield[i].runQueue;
        }
}

struct DeferDraw { const char *code; int cost; };
static const DeferDraw s_deferDraw[] = {
    { "NB7", 1 },
    { "NBK", 1 },
    { "NFU", 1 },
    { "NGE", 1 },
    { "NV8", 1 },
    { "NPD", 175 },
    { "NRR", 150 },
};

void h64_apply_profile_flags(H64System *sys, u32 f)
{
    {
        size_t k;
        sys->options.gfxDeferDraw = 0;
        for (k = 0; k < sizeof(s_deferDraw) / sizeof(s_deferDraw[0]); k++)
            if (!memcmp(sys->rom.gameCode, s_deferDraw[k].code, 3)) sys->options.gfxDeferDraw = s_deferDraw[k].cost;
    }
    sys->options.gfxLateParse = (f & H64_GP_LATE_PARSE) != 0;
    sys->options.gfxViewportClip = (f & H64_GP_VIEWPORT_CLIP) != 0;
    sys->options.gfxCpuWritten = (f & H64_GP_CPU_WRITTEN) != 0;
    sys->options.gfxOtherModeInit = (f & H64_GP_OTHERMODE_INIT) != 0;
    sys->options.gfxNoCopyBack = (f & H64_GP_NO_COPY_BACK) != 0;
    sys->options.gfxS2dex1 = (f & H64_GP_S2DEX1) != 0;
    sys->options.gfxCopyBackFull = (f & H64_GP_COPY_BACK_FULL) != 0;
    sys->options.gfxRdramCopy = (f & H64_GP_RDRAM_COPY) != 0;
    sys->options.gfxCopyBackWhole = (f & H64_GP_COPY_BACK_WHOLE) != 0;
    sys->options.gfxFrameToRdram = (f & H64_GP_FRAME_TO_RDRAM) != 0;
    sys->options.gfxSmallToRdram = (f & H64_GP_SMALL_TO_RDRAM) != 0;
    sys->options.gfxSlotByWidth = (f & H64_GP_SLOT_BY_WIDTH) != 0;
    sys->options.piOddCart = (f & H64_GP_PI_ODD_CART) != 0;
    sys->options.gfxWsBorder = (f & H64_GP_WS_BORDER) != 0;
    sys->options.gfxCopyBackSameWidth = (f & H64_GP_COPY_BACK_SAME_WIDTH) != 0;
    sys->options.gfxShortTask = (f & H64_GP_SHORT_GFX_TASK) != 0;
    sys->options.gfxBigTexPool = (f & H64_GP_BIG_TEX_POOL) != 0;
    sys->options.gfxStream = (f & H64_GP_GFX_STREAM) != 0;
    sys->options.gfxNoOldFrame = (f & H64_GP_NO_OLD_FRAME) != 0;
    sys->options.gfxSoft8bit = (f & H64_GP_SOFT_8BIT) != 0;
    sys->options.gfxDepthFillToRdram = (f & H64_GP_DEPTH_FILL_RDRAM) != 0;
    sys->options.gfxAlphaRef0Pass = (f & H64_GP_ALPHA_REF0_PASS) != 0;
    sys->options.gfxTallFrame = (f & H64_GP_TALL_FRAME) != 0;
    sys->options.gfxRectPoint = (f & H64_GP_RECT_POINT) != 0;
    sys->options.gfxCpuSprites = (f & H64_GP_CPU_SPRITES) != 0;
    sys->options.gfxKeepShown = (f & H64_GP_KEEP_SHOWN) != 0;
    sys->options.gfxMaskToTile = (f & H64_GP_MASK_TO_TILE) != 0;
    if (f & H64_GP_FPU_NO_TRAPS) sys->cpu.fpuNoTraps = 1;
    sys->cpu.laxAlign64 = (f & H64_GP_LAX_ALIGN64) != 0;
    sys->cpu.noOvTrap = (f & H64_GP_NO_OV_TRAP) != 0;
    if (!sys->options.strictCpu)
    {
        sys->options.piOddCart = 1;
        sys->cpu.fpuNoTraps = 1;
        sys->cpu.laxAlign64 = 1;
        sys->cpu.noOvTrap = 1;
    }
}
