/*
 * mdx_operator.c - per-operator -> YM2612 register mapping (spec SPEC 3.2).
 *
 * For one operator we write the seven per-operator register groups:
 *   0x30 DT/MUL : (detune<<4) | multiplier
 *   0x40 TL     : totalLevel
 *   0x50 KS/AR  : (keyScale<<6) | attackRate
 *   0x60 AM/D1R : (amplitudeModulation<<7) | decay1Rate
 *   0x70 D2R    : decay2Rate
 *   0x80 D1L/RR : (releaseRate<<4) | sustainLevel
 *   0x90 SSG-EG : ssgEg
 *
 * opIndex is clamped to 0..3 and the operator is clamped to its ranges
 * before any write. The slot offset is ALWAYS MDX_OP_OFFSET[opIndex].
 */

#include "mdx_operator.h"
#include "mdx_patch.h"


static u8 mdx_op_clamp_u8(u8 v, u8 lo, u8 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


/* Clamp a single operator in place to supported YM2612 ranges. */
static void mdx_op_clamp(FMOperator* op)
{
    op->detune             = mdx_op_clamp_u8(op->detune, 0, 7);
    op->multiplier         = mdx_op_clamp_u8(op->multiplier, 0, 15);
    op->totalLevel         = mdx_op_clamp_u8(op->totalLevel, 0, 127);
    op->keyScale           = mdx_op_clamp_u8(op->keyScale, 0, 3);
    op->attackRate         = mdx_op_clamp_u8(op->attackRate, 0, 31);
    op->decay1Rate         = mdx_op_clamp_u8(op->decay1Rate, 0, 31);
    op->sustainLevel       = mdx_op_clamp_u8(op->sustainLevel, 0, 15);
    op->decay2Rate         = mdx_op_clamp_u8(op->decay2Rate, 0, 31);
    op->releaseRate        = mdx_op_clamp_u8(op->releaseRate, 0, 31);
    op->amplitudeModulation = mdx_op_clamp_u8(op->amplitudeModulation, 0, 1);
    op->ssgEg              = mdx_op_clamp_u8(op->ssgEg, 0, 15);
}


void MdxOperator_Apply(u8 channel, u8 opIndex, const FMOperator* op)
{
    FMOperator local;
    u8 off;

    if (op == NULL) return;
    if (opIndex > 3) opIndex = 3;   /* clamp canonical index */

    /* Clamp a private copy so the caller's struct is never mutated. */
    local = *op;
    mdx_op_clamp(&local);

    /* Slot offset: never use opIndex directly. */
    off = MDX_OP_OFFSET[opIndex];

    MdxYm2612_WriteReg(channel, MDX_REG_DT_MUL, off,
                       (u8)((local.detune << 4) | local.multiplier));
    MdxYm2612_WriteReg(channel, MDX_REG_TL, off, local.totalLevel);
    MdxYm2612_WriteReg(channel, MDX_REG_KS_AR, off,
                       (u8)((local.keyScale << 6) | local.attackRate));
    MdxYm2612_WriteReg(channel, MDX_REG_AM_D1R, off,
                       (u8)((local.amplitudeModulation << 7) | local.decay1Rate));
    MdxYm2612_WriteReg(channel, MDX_REG_D2R, off, local.decay2Rate);
    MdxYm2612_WriteReg(channel, MDX_REG_D1L_RR, off,
                       (u8)((local.releaseRate << 4) | local.sustainLevel));
    MdxYm2612_WriteReg(channel, MDX_REG_SSG_EG, off, local.ssgEg);
}
