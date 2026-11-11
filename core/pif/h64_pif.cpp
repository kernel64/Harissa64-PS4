// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// Harissa64 V2 - PIF: JoyBus command processing, the PIF RAM control byte,
// the CIC-NUS-6105 challenge, and the high-level boot (IPL1-IPL3 effects).
//
// Behaviour from n64brew ("PIF-NUS", "Joybus Protocol", "Boot process").
#include "../system/h64_system.h"

#include <string.h>

#include "../common/h64_endian.h"
#include "../common/h64_log.h"
#include "../system/h64_profile.h"
#include "../memory/h64_rdram.h"

// and Harissa64 V1. See THIRD_PARTY.md. Its licence:
// Copyright 2011 X-Scale. All rights reserved.
//    1. Redistributions of source code must retain the above copyright notice,
//    2. Redistributions in binary form must reproduce the above copyright
static void cic_6105_response(const u8 *chl, u8 *rsp, int len)
{
    static const u8 lut0[16] = { 0x4, 0x7, 0xA, 0x7, 0xE, 0x5, 0xE, 0x1, 0xC, 0xF, 0x8, 0xF, 0x6, 0x3, 0x6, 0x9 };
    static const u8 lut1[16] = { 0x4, 0x1, 0xA, 0x7, 0xE, 0x5, 0xE, 0x1, 0xC, 0x9, 0x8, 0x5, 0x6, 0x3, 0xC, 0x9 };
    u8 key = 0xB;
    const u8 *lut = lut0;
    int i;
    for (i = 0; i < len; i++)
    {
        int sgn, mag, mod;
        rsp[i] = (u8)((key + 5 * chl[i]) & 0xF);
        key = lut[rsp[i]];
        sgn = (rsp[i] >> 3) & 1;
        mag = ((sgn == 1) ? ~rsp[i] : rsp[i]) & 7;
        mod = (mag % 3 == 1) ? sgn : 1 - sgn;
        if (lut == lut1 && (rsp[i] == 0x1 || rsp[i] == 0x9)) mod = 1;
        if (lut == lut1 && (rsp[i] == 0xB || rsp[i] == 0xE)) mod = 0;
        lut = (mod == 1) ? lut1 : lut0;
    }
}

static void pif_challenge(H64System *sys)
{
    u8 chl[30], rsp[30];
    int i;
    for (i = 0; i < 15; i++)
    {
        chl[i * 2] = (sys->pifRam[0x30 + i] >> 4) & 0xF;
        chl[i * 2 + 1] = sys->pifRam[0x30 + i] & 0xF;
    }
    cic_6105_response(chl, rsp, 30);
    sys->pifRam[0x2E] = 0;
    sys->pifRam[0x2F] = 0;
    for (i = 0; i < 15; i++)
        sys->pifRam[0x30 + i] = (u8)((rsp[i * 2] << 4) | rsp[i * 2 + 1]);
    sys->pifRam[0x3F] = 0;
}

static int rumble_command(H64System *sys, int channel, const u8 *tx, int txLen, u8 *rx, int rxLen)
{
    u32 addr;
    if (txLen < 3) return 0;
    addr = ((u32)tx[1] << 8 | tx[2]) & 0xFFE0u;
    if (tx[0] == 0x02 && rxLen >= 33)
    {
        memset(rx, addr >= 0x8000 && addr < 0x9000 ? 0x80 : 0x00, 32);
        rx[32] = h64_save_pak_crc(rx, 32);
        return 1;
    }
    if (tx[0] == 0x03 && txLen >= 35 && rxLen >= 1)
    {
        if (addr == 0xC000) sys->rumbleMotor[channel] = tx[3] ? 1 : 0;
        rx[0] = h64_save_pak_crc(tx + 3, 32);
        return 1;
    }
    return 0;
}

static void joybus_command(H64System *sys, int channel, const u8 *tx, int txLen, u8 *rx, int rxLen, u8 *rxLenByte)
{
    u32 mask = sys->padMask ? sys->padMask : 1u;
#ifdef H64_PIFTRACE
    if (channel == 1 && txLen >= 1) H64_INFO("[piftrace] f%u port2 cmd %02X present %d", sys->vi.frames, tx[0], (mask >> 1) & 1);
#endif
    if (channel < 4 && (mask & (1u << channel)) && txLen >= 1)
    {
        const H64Pad *p = &sys->pad[channel];
        int pak = channel == 0 && sys->save->pak;
        int rumble = !pak && (sys->rumblePaks & (1u << channel)) != 0;
        int both = pak && (sys->rumblePaks & (1u << channel)) != 0;
        switch (tx[0])
        {
        case 0x00:
        case 0xFF:
            if (rxLen >= 3) { rx[0] = 0x05; rx[1] = 0x00; rx[2] = pak || rumble ? 0x01 : 0x00; }
            return;
        case 0x02:
        case 0x03:
            if (both && txLen >= 3 && (((u32)tx[1] << 8 | tx[2]) & 0x8000u) && rumble_command(sys, channel, tx, txLen, rx, rxLen)) return;
            if (pak && h64_save_pak_command(sys->save, tx, txLen, rx, rxLen) >= 0) return;
            if (rumble && rumble_command(sys, channel, tx, txLen, rx, rxLen)) return;
            break;
        case 0x01:
            if (rxLen >= 4)
            {
                if (sys->padHook && !(mask & ((1u << channel) - 1u))) sys->padHook(sys);
                rx[0] = (u8)(p->buttons >> 8);
                rx[1] = (u8)p->buttons;
                rx[2] = (u8)p->x;
                rx[3] = (u8)p->y;
                if (p->buttons)
                    H64_DEBUG("[pif] f%u controller %d read: buttons %04X", sys->vi.frames, channel + 1, p->buttons);
            }
            return;
        }
    }
    if (channel == 4 && h64_save_eeprom_command(sys->save, tx, txLen, rx, rxLen) >= 0) return;
    *rxLenByte |= 0x80;
}

static void run_block(H64System *sys, int controllersOnly)
{
    u8 *ram = sys->pifRam;
    int i = 0, channel = 0;

    while (i < 0x3F)
    {
        u8 tx = ram[i];
        if (tx == 0xFE) break;
        if (tx == 0xFF || tx == 0xFD) { i++; continue; }
        if (tx == 0x00) { channel++; i++; continue; }
        {
            int txLen = tx & 0x3F;
            int rxPos = i + 1;
            int rxLen, txData, rxData;
            if (rxPos >= 0x3F) break;
            if (ram[rxPos] == 0xFE) break;
            rxLen = ram[rxPos] & 0x3F;
            txData = rxPos + 1;
            rxData = txData + txLen;
            if (rxData + rxLen > 0x3F) break;
            if (!controllersOnly || channel < 4)
                joybus_command(sys, channel, ram + txData, txLen, ram + rxData, rxLen, ram + rxPos);
            i = rxData + rxLen;
            channel++;
        }
    }
}

void h64_pif_run_commands(H64System *sys)
{
    u8 control = sys->pifRam[0x3F];
    if (control & 0x02) { pif_challenge(sys); return; }
    if (!(control & 0x01)) return;
    run_block(sys, 0);
    sys->pifRam[0x3F] &= ~0x01u;
}

void h64_pif_read_hook(H64System *sys)
{
    if (sys->pifRam[0x3F] & 0x02) return;
    run_block(sys, 1);
}

void h64_pif_write_byte_hook(H64System *sys, u32 offset)
{
    u8 c;
    if (offset != 0x3C)
        return;
    c = sys->pifRam[0x3F];
    if (c & 0x40) { memset(sys->pifRam, 0, sizeof(sys->pifRam)); return; }
    if (c & 0x20) sys->pifRam[0x3F] |= 0x80;
    if (c & 0x08) sys->pifRam[0x3F] &= ~0x08u;
    if (c & 0x02) pif_challenge(sys);
}

void h64_pif_reset(H64System *sys)
{
    memset(sys->pifRam, 0, sizeof(sys->pifRam));
}

static void ipl3_boot(H64System *sys)
{
    static const u32 imem[8] = { 0x3C0DBFC0u, 0x8DA807FCu, 0x25AD07C0u, 0x31080080u,
                                 0x5500FFFCu, 0x3C0DBFC0u, 0x8DA80024u, 0x3C0BB000u };
    H64Cpu *cpu = &sys->cpu;
    u32 hdr = h64_load_be32(sys->rom.data), i;
    sys->pi.regs[5] = hdr & 0xFF;
    sys->pi.regs[6] = (hdr >> 8) & 0xFF;
    sys->pi.regs[7] = (hdr >> 16) & 0x0F;
    sys->pi.regs[8] = (hdr >> 20) & 0x03;
    memcpy(sys->spMem + 0x40, sys->rom.data + 0x40, 0xFC0);
    for (i = 0; i < 8; i++) h64_store_be32(sys->spMem + 0x1000 + i * 4, imem[i]);
    cpu->gpr[19] = 0;
    cpu->gpr[20] = (u64)sys->tvType;
    cpu->gpr[21] = 0;
    cpu->gpr[22] = sys->rom.cicSeed;
    cpu->gpr[23] = 0;
    cpu->gpr[11] = (u64)(s64)(s32)0xA4000040u;
    cpu->gpr[29] = (u64)(s64)(s32)0xA4001FF0u;
    cpu->gpr[31] = (u64)(s64)(s32)0xA4001550u;
    cpu->cop0[CP0_STATUS] = 0x34000000u;
    cpu->cop0[CP0_LLADDR] = 0xFFFFFFFFu;
    cpu->pc = (u64)(s64)(s32)0xA4000040u;
    cpu->nextPc = cpu->pc + 4;
    H64_INFO("[boot] IPL3 boot: CIC %s seed %02X, TV type %d", h64_cic_name(sys->rom.cic), sys->rom.cicSeed, sys->tvType);
}

void h64_hle_boot(H64System *sys)
{
    H64Cpu *cpu = &sys->cpu;
    u32 entry = sys->rom.entryPoint - (sys->rom.cic == H64_CIC_6103 ? 0x100000u :
                                       sys->rom.cic == H64_CIC_6106 ? 0x200000u : 0u);
    u32 phys = entry & 0x1FFFFFFFu;
    u32 i, len = 0x100000;
    u32 hdr = h64_load_be32(sys->rom.data);

    if (sys->rom.cic == H64_CIC_6105 || (h64_game_profile(sys->rom.gameCode, NULL) & H64_GP_IPL3_BOOT))
    {
        ipl3_boot(sys);
        return;
    }
    memcpy(sys->spMem, sys->rom.data, 0x1000);
    if (sys->rom.cic == H64_CIC_6101 || sys->rom.cic == H64_CIC_6102)
        memset(sys->spMem, 0, sizeof(sys->spMem));
    else if (sys->rom.cic == H64_CIC_6103)
    {
        memset(sys->spMem, 0xFF, sizeof(sys->spMem));
        h64_store_be32(sys->spMem + 0x1000, 0x17D7);
        if (sys->rom.size >= 0x764)
            h64_rdram_write(sys->rdram, 0, sys->rom.data + 0x4C0, 0x764 - 0x4C0);
    }
    for (i = 0; i < len; i++)
    {
        u32 src = 0x1000 + i;
        if (phys + i >= H64_RDRAM_SIZE) break;
        h64_rdram_store8(sys->rdram, phys + i, (u8)(src < sys->rom.size ? sys->rom.data[src] : 0));
    }

    h64_rdram_store32(sys->rdram, 0x300, (u32)sys->tvType);
    h64_rdram_store32(sys->rdram, 0x304, 0);
    h64_rdram_store32(sys->rdram, 0x308, 0xB0000000u);
    h64_rdram_store32(sys->rdram, 0x30C, 0);
    h64_rdram_store32(sys->rdram, 0x310, sys->rom.cic == H64_CIC_6103 ? 0x17D7u : sys->rom.cicSeed);
    h64_rdram_store32(sys->rdram, 0x314, 0);
    h64_rdram_store32(sys->rdram, 0x318, H64_RDRAM_SIZE);

    sys->pi.regs[5] = hdr & 0xFF;
    sys->pi.regs[6] = (hdr >> 8) & 0xFF;
    sys->pi.regs[7] = (hdr >> 16) & 0x0F;
    sys->pi.regs[8] = (hdr >> 20) & 0x03;
    sys->pi.regs[0] = 0x00101000;
    sys->pi.regs[1] = 0x1000000C;
    sys->pi.regs[2] = 0x7F;
    sys->pi.regs[3] = 0x7F;
    sys->ri.regs[0] = 0x0E;
    sys->ri.regs[1] = 0x40;
    sys->ri.regs[3] = 0x14;
    sys->ri.regs[4] = 0x00063634;

    cpu->gpr[19] = 0;
    cpu->gpr[20] = (u64)sys->tvType;
    cpu->gpr[21] = 0;
    cpu->gpr[22] = sys->rom.cicSeed;
    cpu->gpr[23] = 0;
    cpu->gpr[29] = (u64)(s64)(s32)0xA4001FF0u;
    cpu->cop0[CP0_STATUS] = 0x34000000u;
    cpu->cop0[CP0_CAUSE] = 0xB000007Cu;
    cpu->cop0[CP0_LLADDR] = 0xFFFFFFFFu;
    if (h64_game_profile(sys->rom.gameCode, NULL) & H64_GP_BOOT_TIME)
        cpu->countOffset = 46875000u - (u32)(cpu->cycles >> 1);
    cpu->pc = (u64)(s64)(s32)entry;
    cpu->nextPc = cpu->pc + 4;
    H64_INFO("[boot] HLE boot: CIC %s seed %02X, TV type %d, entry %08X", h64_cic_name(sys->rom.cic),
             sys->rom.cicSeed, sys->tvType, entry);
}
