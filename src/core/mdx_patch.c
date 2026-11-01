/*
 * mdx_patch.c - FM patch data model (spec SPEC 5.2 / 3.2).
 *
 * Field ranges are the authoritative YM2612 limits. MdxPatch_Clamp() pins
 * every member into range so that any downstream register write is always
 * legal. No YM2612 access happens in this module.
 */

#include "mdx_patch.h"


/* Clamp v into [lo, hi]. */
static u8 mdx_clamp_u8(u8 v, u8 lo, u8 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


void MdxPatch_Clamp(FMPatch* p)
{
    u8 i;

    if (p == NULL) return;

    /* Global voice params. */
    p->algorithm          = mdx_clamp_u8(p->algorithm, 0, 7);
    p->feedback          = mdx_clamp_u8(p->feedback, 0, 7);
    p->lfoEnabled        = mdx_clamp_u8(p->lfoEnabled, 0, 1);
    p->lfoFrequency      = mdx_clamp_u8(p->lfoFrequency, 0, 7);
    p->lfoPitchDepth     = mdx_clamp_u8(p->lfoPitchDepth, 0, 7);
    p->lfoAmplitudeDepth = mdx_clamp_u8(p->lfoAmplitudeDepth, 0, 3);

    /* Per-operator params. */
    for (i = 0; i < 4; i++)
    {
        FMOperator* op = &p->operators[i];

        op->detune             = mdx_clamp_u8(op->detune, 0, 7);
        op->multiplier         = mdx_clamp_u8(op->multiplier, 0, 15);
        op->totalLevel         = mdx_clamp_u8(op->totalLevel, 0, 127);
        op->keyScale           = mdx_clamp_u8(op->keyScale, 0, 3);
        op->attackRate         = mdx_clamp_u8(op->attackRate, 0, 31);
        op->decay1Rate         = mdx_clamp_u8(op->decay1Rate, 0, 31);
        op->sustainLevel       = mdx_clamp_u8(op->sustainLevel, 0, 15);
        op->decay2Rate         = mdx_clamp_u8(op->decay2Rate, 0, 31);
        op->releaseRate        = mdx_clamp_u8(op->releaseRate, 0, 31);
        op->amplitudeModulation = mdx_clamp_u8(op->amplitudeModulation, 0, 1);
        op->ssgEg              = mdx_clamp_u8(op->ssgEg, 0, 15);
    }
}


/* Audibly-safe default: algorithm 0 (only S4/S4 carrier), feedback 2.
 * Operator 4 (S4) is the carrier with a moderate total level so the patch
 * is clearly audible but not harsh; the three modulators are silenced
 * (TL 99) so it renders as a clean carrier tone (matches the verified
 * Phase-1 "Sound Test" configuration). LFO off. */
const FMPatch DEFAULT_PATCH =
{
    /* operators[4] : S1, S3, S2, S4 */
    {
        /* S1 - modulator (silent) */
        { 0, 2, 99, 0, 20, 15, 3, 10, 5, 0, 0 },
        /* S3 - modulator (silent) */
        { 0, 2, 99, 0, 20, 15, 3, 10, 5, 0, 0 },
        /* S2 - modulator (silent) */
        { 0, 2, 99, 0, 20, 15, 3, 10, 5, 0, 0 },
        /* S4 - carrier (audible) */
        { 0, 1, 40, 0, 20, 15, 3, 10, 5, 0, 0 }
    },
    /* algorithm */ 0,
    /* feedback */  2,
    /* lfoEnabled */      0,
    /* lfoFrequency */    0,
    /* lfoPitchDepth */   0,
    /* lfoAmplitudeDepth*/ 0,
    /* name */ "INIT PATCH"
};
