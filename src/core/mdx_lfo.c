/*
 * mdx_lfo.c - LFO register mapping (SPEC 3.4 / 6.1).
 *
 * MdxLfo_Apply() configures:
 *   1. Global LFO register 0x22:  (enable?0x08:0x00) | (frequency & 0x07).
 *      This is a single chip-wide register, written once.
 *   2. Channel 0xB4 AMS/PMS: 0xC0 | (ams<<4) | pms.
 *      0xB4 is one register per channel (not per operator). We OR the AMS/PMS
 *      field into the L/R bits (0b11) that init/load established, keeping them
 *      enabled and never touching the unused bit 3.
 *
 * AMS/PMS are non-linear index tables; the chip consumes the index directly,
 * so we write the index and keep the dB/cent tables only for reference/UI.
 * All values are clamped; no floating point is used.
 */

#include "mdx_lfo.h"
#include "mdx_patch.h"


/* AMS depth (tenths of a dB) for index 0..3: 0, 1.4, 5.9, 11.8 dB. */
const u16 MDX_LFO_AMS_DB_X10[4] = { 0, 14, 59, 118 };

/* PMS depth (cents) for index 0..7: ~0.034..0.80 semitone (3.4..80 cents). */
const u16 MDX_LFO_PMS_CENT[8] = { 0, 3, 7, 11, 17, 34, 50, 80 };


/* Ensure v is within [lo, hi]. */
static u8 mdx_lfo_clamp(u8 v, u8 lo, u8 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


u8 MdxLfo_Apply(u8 channel, const FMPatch* patch)
{
    u8 ams;
    u8 pms;
    u8 lfo22;
    u8 b4;

    if (patch == NULL) return 0xC0;

    ams = mdx_lfo_clamp(patch->lfoAmplitudeDepth, 0, 3);
    pms = mdx_lfo_clamp(patch->lfoPitchDepth,      0, 7);

    /* Global LFO register 0x22: enable bit (0x08) + frequency index (0..7). */
    lfo22 = (u8)(((patch->lfoEnabled ? 1 : 0) << 3) | (patch->lfoFrequency & 0x07));
    MdxYm2612_WriteReg(channel, 0x22, 0, lfo22);

    /* Channel 0xB4: keep L/R = 0b11, OR in AMS (bits5-4) and PMS (bits2-0). */
    b4 = (u8)(0xC0 | (ams << 4) | pms);
    MdxYm2612_WriteReg(channel, MDX_REG_B4, 0, b4);

    return b4;
}
