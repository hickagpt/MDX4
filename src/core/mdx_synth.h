#ifndef _MDX_SYNTH_H_
#define _MDX_SYNTH_H_

/*
 * mdx_synth.h - synthesizer control API (SPEC 5.3).
 *
 * Owns the single monophonic channel-1 voice and the channel's 0xB0/0xB4
 * register state. Exposes a dirty-flag parameter update system: callers edit
 * parameters live; the engine writes ONLY the registers that changed.
 *
 * Key/note state machine:
 *   - LIVE-EDITABLE params (TL, MUL, DT, AR/D1R/D2R/RR, etc., LFO depth) may
 *     change under key-on without re-keying.
 *   - TOPOLOGY-changing params (algorithm, feedback) and NOTE changes require
 *     key-off -> key-on (retrigger) because the envelope restarts only on a
 *     0->1 key-bit transition.
 *   - "never touch key-on" means don't key-off a note you intend to keep
 *     sounding - not "never write 0x28 again".
 */

#include "mdx_patch.h"


/* Boot the synth: init the YM2612 driver, load DEFAULT_PATCH, apply it,
 * set the channel 1 pan/0xB4 cache (0xC0 = L/R enabled, AMS/PMS 0), and
 * configure the default note pitch. Does NOT key on. */
void MdxSynth_Init(void);

/* Load a full patch: copy + clamp, apply all operators/algorithm/LFO,
 * set the current note pitch, reset dirty flags. */
void MdxSynth_LoadPatch(const FMPatch* patch);

/* Live-edit one operator (0..3). Applies immediately; marks it dirty. */
void MdxSynth_SetOperator(u8 opIndex, const FMOperator* op);

/* Topology-changing: change algorithm / feedback. Applies via 0xB0 and marks
 * topology dirty (next NoteOn retriggers to restart the envelope). */
void MdxSynth_SetAlgorithm(u8 algo);
void MdxSynth_SetFeedback(u8 fb);

/* Note on (MIDI note number) / off. */
void MdxSynth_NoteOn(u8 note);
void MdxSynth_NoteOff(void);

/* Snapshot the current patch (clamped copy) for UI display. Read-only. */
void MdxSynth_GetPatch(FMPatch* out);

/* True if a note is currently sounding (for UI performance status). */
bool MdxSynth_IsKeyOn(void);

/* Set pitch from a frequency in Hz (converted to the nearest table note). */
void MdxSynth_SetPitch(u16 frequency);

#endif /* _MDX_SYNTH_H_ */
