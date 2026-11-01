/*
 * mdx_synth.c - synthesizer control engine (SPEC 5.3).
 *
 * Single owner of channel 1's voice and its 0xB0/0xB4 state. Drives the
 * lower-level modules (mdx_patch / mdx_operator / mdx_algorithm / mdx_lfo /
 * mdx_frequency). Every write goes through MdxYm2612_WriteReg(); channels
 * 2-6 and part 1 are never touched.
 *
 * Dirty-flag discipline: the engine applies ONLY the registers affected by an
 * edit. Live-editable params rewrite their register(s) under key-on without
 * re-keying; topology changes (algorithm/feedback) and note changes retrigger
 * the envelope via a key-off -> key-on transition.
 */

#include "mdx_synth.h"
#include "mdx_patch.h"
#include "mdx_operator.h"
#include "mdx_algorithm.h"
#include "mdx_envelope.h"
#include "mdx_lfo.h"
#include "mdx_frequency.h"
#include "mdx_ym2612.h"


/* ----- Module state (single owner of channel 1). ----- */
static FMPatch g_patch;          /* current patch (clamped copy)        */
static bool    g_keyOn       = FALSE;   /* a note is currently sounding  */
static u8      g_currentNote = 69;     /* default A4                     */
static u8      g_ch1B4       = 0xC0;   /* 0xB4 cache: L/R = 0b11        */

/* Dirty tracking. */
static u8      g_opsDirty      = 0;    /* bit i: operator i parameters dirty */
static bool    g_topologyDirty = FALSE;/* algorithm/feedback changed since key-on */


/* Apply the full current patch to the hardware (used at init / load). */
static void mdx_synth_apply_all(void)
{
    u8 i;

    for (i = 0; i < 4; i++)
        MdxOperator_Apply(1, i, &g_patch.operators[i]);

    MdxAlgorithm_Apply(1, g_patch.algorithm, g_patch.feedback);

    g_ch1B4 = MdxLfo_Apply(1, &g_patch);

    MdxFrequency_SetNote(1, g_currentNote);
}


void MdxSynth_Init(void)
{
    /* Boot the YM2612 driver (channel 1, PSG silenced, pan set). */
    MdxYm2612_Init();

    /* Start from the audibly-safe default patch. */
    g_patch = DEFAULT_PATCH;
    MdxPatch_Clamp(&g_patch);

    g_keyOn       = FALSE;
    g_currentNote = 69;            /* A4 */
    g_ch1B4       = 0xC0;
    g_opsDirty      = 0;
    g_topologyDirty = FALSE;

    mdx_synth_apply_all();
}


void MdxSynth_LoadPatch(const FMPatch* patch)
{
    if (patch == NULL) return;

    g_patch = *patch;
    MdxPatch_Clamp(&g_patch);

    /* Apply everything, then clear all dirty flags. */
    mdx_synth_apply_all();

    g_opsDirty      = 0;
    g_topologyDirty = FALSE;
}


void MdxSynth_SetOperator(u8 opIndex, const FMOperator* op)
{
    if (op == NULL) return;
    if (opIndex > 3) opIndex = 3;

    /* Copy + clamp into the current patch, then apply only this operator. */
    g_patch.operators[opIndex] = *op;
    MdxPatch_Clamp(&g_patch);            /* clamps whole patch; cheap + safe */
    MdxOperator_Apply(1, opIndex, &g_patch.operators[opIndex]);

    g_opsDirty |= (u8)(1u << opIndex);   /* no retrigger needed (live-edit) */
}


void MdxSynth_SetAlgorithm(u8 algo)
{
    g_patch.algorithm = (algo > 7) ? 7 : algo;
    MdxAlgorithm_Apply(1, g_patch.algorithm, g_patch.feedback);

    /* Topology change -> next NoteOn must retrigger the envelope. */
    g_topologyDirty = TRUE;
}


void MdxSynth_SetFeedback(u8 fb)
{
    g_patch.feedback = (fb > 7) ? 7 : fb;
    MdxAlgorithm_Apply(1, g_patch.algorithm, g_patch.feedback);

    g_topologyDirty = TRUE;
}


void MdxSynth_SetLfoEnabled(u8 enabled)
{
    g_patch.lfoEnabled = enabled ? 1 : 0;
    g_ch1B4 = MdxLfo_Apply(1, &g_patch);
}


void MdxSynth_SetLfoFrequency(u8 freq)
{
    g_patch.lfoFrequency = (freq > 7) ? 7 : freq;
    g_ch1B4 = MdxLfo_Apply(1, &g_patch);
}


void MdxSynth_SetLfoPitchDepth(u8 pms)
{
    g_patch.lfoPitchDepth = (pms > 7) ? 7 : pms;
    g_ch1B4 = MdxLfo_Apply(1, &g_patch);
}


void MdxSynth_SetLfoAmplitudeDepth(u8 ams)
{
    g_patch.lfoAmplitudeDepth = (ams > 3) ? 3 : ams;
    g_ch1B4 = MdxLfo_Apply(1, &g_patch);
}


void MdxSynth_NoteOn(u8 note)
{
    g_currentNote = (note > 127) ? 127 : note;

    /* Always set the pitch (live if already sounding). */
    MdxFrequency_SetNote(1, g_currentNote);

    if (g_keyOn)
    {
        if (g_topologyDirty)
        {
            /* Topology changed while sounding: retrigger so the envelope
             * restarts (key-off -> key-on 0->1 transition). */
            MdxYm2612_KeyOff(1);
            MdxYm2612_KeyOn(1, 0x0F);
        }
        /* else: pitch-only change, keep the note sounding. */
    }
    else
    {
        MdxYm2612_KeyOn(1, 0x0F);   /* all four operators */
        g_keyOn = TRUE;
    }

    g_topologyDirty = FALSE;
}


void MdxSynth_NoteOff(void)
{
    MdxYm2612_KeyOff(1);
    g_keyOn = FALSE;
}


void MdxSynth_GetPatch(FMPatch* out)
{
    if (out == NULL) return;
    *out = g_patch;
    MdxPatch_Clamp(out);
}


bool MdxSynth_IsKeyOn(void)
{
    return g_keyOn;
}


void MdxSynth_SetPitch(u16 frequency)
{
    /* Convert the frequency (Hz) to the nearest table note using a fixed-point
     * scan. proxy[note] = FNUM<<BLOCK is proportional to the note's frequency,
     * so we compare against frequency * SCALE. SCALE ~ 33.745 maps A4(440Hz)
     * to proxy[69] = 928<<4 = 14848. Integer-only, no float, no division. */
    const u32 scale = 33745u;          /* SCALE * 1000 */
    u32 target = (u32)frequency * scale / 1000u;
    u32 bestDiff = 0xFFFFFFFFu;
    u8  bestNote = 0;
    u8  n;

    for (n = 0; n < 128; n++)
    {
        const u32 proxy = (u32)MDX_NOTE_FNUM[n] << MDX_NOTE_BLOCK[n];
        const u32 diff  = (proxy > target) ? (proxy - target) : (target - proxy);

        if (diff < bestDiff)
        {
            bestDiff = diff;
            bestNote = n;
        }
    }

    g_currentNote = bestNote;

    if (g_keyOn)
        MdxFrequency_SetNote(1, g_currentNote);   /* live pitch change */
}
