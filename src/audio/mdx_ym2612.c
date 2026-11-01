/*
 * mdx_ym2612.c - direct YM2612 register driver for the MDX-4 synthesizer.
 *
 * All YM2612 register access goes through SGDK's YM2612_writeReg() here.
 * No code anywhere writes the YM2612 address ports ($A04000..$A04003) directly.
 *
 * The MVP drives channel 1 only (monophonic). Channels 2-6 and the second
 * register bank (part 1) are intentionally unreachable.
 */

#include "mdx_ym2612.h"


/* ----------------------------------------------------------------------- */
/* Boot-time initialisation.                                                */
/* ----------------------------------------------------------------------- */
void MdxYm2612_Init(void)
{
    /* 1. FM-enable / Z80 bus grant. YM2612_reset() internally requests the
     *    Z80 bus so the 68000 owns the chip. (This SDK has no SYS_setFMEnable()
     *    and no YM2612_init() - YM2612_reset() is the path.) */
    YM2612_reset();

    /* 2. Silence the SN76489 PSG so only the YM2612 is audible. */
    {
        volatile u8 *psg = (volatile u8 *)0xC00011;
        *psg = 0x9F;
        *psg = 0xBF;
        *psg = 0xDF;
        *psg = 0xFF;
    }

    /* 3. Channel 1 pan: L/R output enable (0b11) with AMS/PMS = 0. */
    MdxYm2612_WriteReg(1, MDX_REG_B4, 0x00, 0xC0);

    /* 4. SSG-EG off for every operator of channel 1. */
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[0], 0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[1], 0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[2], 0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[3], 0);

    /* 5. Leave 0x27 (CSM/special mode) and 0x2B (DAC enable) untouched. */
}


/* ----------------------------------------------------------------------- */
/* Generic per-operator register write (channel 1 only).                    */
/* ----------------------------------------------------------------------- */
void MdxYm2612_WriteReg(u8 channel, u8 group, u8 opOffset, u8 value)
{
    u16 part = 0;

    /* Monophonic MVP: only channel 1 is supported. Reject everything else so
     * we never touch channels 2-6 or the second register bank (part 1). */
    if (channel != 1) return;

    /* Channels 4-6 would map to part 1; we do not use them. */
    if (channel >= 4) part = 1;

    const u8 reg = group + opOffset;
    YM2612_writeReg(part, reg, value);
}


/* ----------------------------------------------------------------------- */
/* Frequency (FNUM + BLOCK).                                                */
/* ----------------------------------------------------------------------- */
void MdxYm2612_SetFrequency(u8 channel, u8 block, u16 fnum)
{
    u16 part = 0;

    if (channel != 1) return;
    if (channel >= 4) part = 1;

    const u8 fnumLow  = (u8)(fnum & 0xFF);
    const u8 fnumHigh = (u8)((fnum >> 8) & 0x07);   /* FNUM is 10-bit */
    const u8 blockBits = (u8)((block & 0x07) << 3);  /* BLOCK is 3-bit */

    /* Order matters: write the HIGH byte (0xA4) THEN the LOW byte (0xA0). */
    YM2612_writeReg(part, MDX_REG_A4, (u8)(blockBits | fnumHigh));
    YM2612_writeReg(part, MDX_REG_A0, fnumLow);
}


/* ----------------------------------------------------------------------- */
/* Key on / key off (register 0x28).                                        */
/* ----------------------------------------------------------------------- */
void MdxYm2612_KeyOn(u8 channel, u8 opBits)
{
    u16 part = 0;

    if (channel != 1) return;
    if (channel >= 4) part = 1;

    /* For channel 1, (channel - 1) == 0. Operator bits occupy 0x28 bits 4-7. */
    YM2612_writeReg(part, MDX_REG_KEYON, (u8)((opBits << 4) | (u8)(channel - 1)));
}

void MdxYm2612_KeyOff(u8 channel)
{
    u16 part = 0;

    if (channel != 1) return;
    if (channel >= 4) part = 1;

    /* Operator bits 0 => key off for this channel. */
    YM2612_writeReg(part, MDX_REG_KEYON, (u8)(channel - 1));
}
