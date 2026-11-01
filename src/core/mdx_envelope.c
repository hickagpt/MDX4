/*
 * mdx_envelope.c - per-operator envelope register mapping (SPEC 3.4 / 6.1).
 *
 * Sub-field placement matches MdxOperator_Apply():
 *   0x50 KS/AR  : (keyScale<<6) | attackRate
 *   0x60 AM/D1R : (amplitudeModulation<<7) | decay1Rate
 *   0x70 D2R    : decay2Rate
 *   0x80 D1L/RR : (releaseRate<<4) | sustainLevel   (RR high, D1L low)
 *
 * All arithmetic is integer-only. The on-screen envelope graph is a visual
 * approximation; the registers carry the real YM2612 envelope parameters.
 */

#include "mdx_envelope.h"
#include "mdx_patch.h"


static u8 mdx_env_clamp_u8(u8 v, u8 lo, u8 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


void MdxEnvelope_Apply(u8 channel, u8 opIndex, const FMOperator* op)
{
    FMOperator local;
    u8 off;

    if (op == NULL) return;
    if (opIndex > 3) opIndex = 3;

    /* Clamp a private copy (envelope-relevant fields) before writing. */
    local = *op;
    local.keyScale           = mdx_env_clamp_u8(local.keyScale, 0, 3);
    local.attackRate        = mdx_env_clamp_u8(local.attackRate, 0, 31);
    local.decay1Rate        = mdx_env_clamp_u8(local.decay1Rate, 0, 31);
    local.sustainLevel      = mdx_env_clamp_u8(local.sustainLevel, 0, 15);
    local.decay2Rate        = mdx_env_clamp_u8(local.decay2Rate, 0, 31);
    local.releaseRate       = mdx_env_clamp_u8(local.releaseRate, 0, 31);
    local.amplitudeModulation = mdx_env_clamp_u8(local.amplitudeModulation, 0, 1);

    /* Slot offset: never the raw opIndex. */
    off = MDX_OP_OFFSET[opIndex];

    MdxYm2612_WriteReg(channel, MDX_REG_KS_AR, off,
                       (u8)((local.keyScale << 6) | local.attackRate));
    MdxYm2612_WriteReg(channel, MDX_REG_AM_D1R, off,
                       (u8)((local.amplitudeModulation << 7) | local.decay1Rate));
    MdxYm2612_WriteReg(channel, MDX_REG_D2R, off, local.decay2Rate);
    MdxYm2612_WriteReg(channel, MDX_REG_D1L_RR, off,
                       (u8)((local.releaseRate << 4) | local.sustainLevel));
}
