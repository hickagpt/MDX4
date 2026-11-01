/*
 * ym2612_host.c - host-side YM2612 driver replacement for the register-trace
 * harness.
 *
 * Every register write the ENGINE produces is captured, in order, into g_cap.
 * The register-derivation logic here (reg = group+opOffset; 0xA4-before-0xA0;
 * 0x28 = (opBits<<4)|channel) is a line-for-line copy of src/audio/mdx_ym2612.c
 * (already reviewed by F1/F2/F4): the only things omitted are the two
 * Genesis-only side effects that would fault on the host - YM2612_reset() and
 * the PSG silence write to $C00011. The monophonic guard (channel != 1 -> no
 * write) is preserved exactly, so the guard is exercised by the harness too.
 *
 * The ENGINE (mdx_synth.c) - the new orchestration logic under test - is the
 * REAL source, compiled unmodified.
 */
#include "mdx_ym2612.h"   /* types + register macros + driver API decls */


/* --- capture buffer (register, value) in write order. --- */
#define CAP_MAX 1024
typedef struct { u8 reg; u8 val; } RegWrite;

RegWrite g_cap[CAP_MAX];
int      g_capN = 0;

static void cap(u8 reg, u8 val)
{
    if (g_capN < CAP_MAX)
    {
        g_cap[g_capN].reg = reg;
        g_cap[g_capN].val = val;
        g_capN++;
    }
}

/* Low-level capture sink (stands in for SGDK's YM2612_writeReg). */
static void ym_write(u8 part, u8 reg, u8 val) { (void)part; cap(reg, val); }


/* Boot init: B4 pan + SSG-EG off captured; YM2612_reset() and the PSG $C00011
 * write are Genesis-only side effects omitted on the host. */
void MdxYm2612_Init(void)
{
    MdxYm2612_WriteReg(1, MDX_REG_B4, 0x00, 0xC0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[0], 0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[1], 0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[2], 0);
    MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[3], 0);
}


/* Faithful copy of mdx_ym2612.c:WriteReg (channel-1 monophonic guard kept). */
void MdxYm2612_WriteReg(u8 channel, u8 group, u8 opOffset, u8 value)
{
    if (channel != 1) return;          /* monophonic guard - reject others */
    cap(group + opOffset, value);
}


/* Faithful copy of mdx_ym2612.c:SetFrequency (0xA4 HIGH before 0xA0 LOW). */
void MdxYm2612_SetFrequency(u8 channel, u8 block, u16 fnum)
{
    u8 fnumLow   = (u8)(fnum & 0xFF);
    u8 fnumHigh  = (u8)((fnum >> 8) & 0x07);
    u8 blockBits = (u8)((block & 0x07) << 3);
    cap(MDX_REG_A4, (u8)(blockBits | fnumHigh));   /* HIGH first */
    cap(MDX_REG_A0, fnumLow);                      /* LOW second */
}


/* Faithful copy of mdx_ym2612.c:KeyOn / KeyOff (0x28 = (opBits<<4)|channel). */
void MdxYm2612_KeyOn(u8 channel, u8 opBits)
{
    if (channel != 1) return;
    cap(MDX_REG_KEYON, (u8)((opBits << 4) | (u8)(channel - 1)));
}

void MdxYm2612_KeyOff(u8 channel)
{
    if (channel != 1) return;
    cap(MDX_REG_KEYON, (u8)(channel - 1));
}
