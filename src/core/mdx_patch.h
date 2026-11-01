#ifndef _MDX_PATCH_H_
#define _MDX_PATCH_H_

/*
 * mdx_patch.h - FM patch data model (spec SPEC 5.2 / 3.2).
 *
 * FMOperator models one YM2612 operator using the register-aligned
 * parameter set (detune/multiplier/totalLevel/.../ssgEg). FMPatch is a
 * full four-operator voice plus global algorithm/feedback/LFO settings.
 *
 * Every field is clamped to a legal YM2612 range by MdxPatch_Clamp()
 * before any register write. No hardware access happens here.
 */

#include "mdx_ym2612.h"   /* u8, MDX_OP_OFFSET, register group macros */


/* Per-operator parameter set (SPEC 3.2 + 5.2).
 * Field names follow the precise register meaning rather than the loose
 * SPEC 5.2 "decayRate"/"sustainRate" wording:
 *   decay1Rate = D1R, decay2Rate = D2R, sustainLevel = D1L,
 *   releaseRate = RR, keyScale = KS, amplitudeModulation = AM. */
typedef struct
{
    u8 detune;             /* DT  : 0..7   */
    u8 multiplier;         /* MUL : 0..15  */
    u8 totalLevel;         /* TL  : 0..127 */
    u8 keyScale;           /* KS  : 0..3   */
    u8 attackRate;         /* AR  : 0..31  */
    u8 decay1Rate;         /* D1R : 0..31  */
    u8 sustainLevel;       /* D1L : 0..15  */
    u8 decay2Rate;         /* D2R : 0..31  */
    u8 releaseRate;        /* RR  : 0..31  */
    u8 amplitudeModulation;/* AM  : 0..1   */
    u8 ssgEg;              /* SSG-EG: 0..15 */
} FMOperator;


typedef struct
{
    FMOperator operators[4];   /* S1, S3, S2, S4 (canonical order) */

    u8 algorithm;              /* 0..7  (YM2612 algorithm) */
    u8 feedback;               /* 0..7  (S1 self-feedback only) */

    u8 lfoEnabled;            /* 0..1  */
    u8 lfoFrequency;          /* 0..7  (LFO speed select) */
    u8 lfoPitchDepth;         /* 0..7  (PMS index) */
    u8 lfoAmplitudeDepth;     /* 0..3  (AMS index) */

    char name[16];            /* NUL-terminated patch name */
} FMPatch;


/* Clamp every field of *p to its supported YM2612 range (in place). */
void MdxPatch_Clamp(FMPatch* p);

/* Audibly-safe default voice (algorithm 0, S1..S4; pure carrier tone). */
extern const FMPatch DEFAULT_PATCH;

#endif /* _MDX_PATCH_H_ */
