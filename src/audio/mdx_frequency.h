#ifndef _MDX_FREQUENCY_H_
#define _MDX_FREQUENCY_H_

/*
 * mdx_frequency.h - precomputed MIDI note -> YM2612 frequency lookup (SPEC 6.3).
 *
 * MDX_NOTE_BLOCK / MDX_NOTE_FNUM cover the full playable MIDI range 0..127,
 * generated offline from the NTSC pitch formula (see mdx_frequency.c). No
 * runtime division, no floating point - just table lookups.
 */

#include "mdx_ym2612.h"


/* BLOCK (3-bit) per MIDI note 0..127. */
extern const u16 MDX_NOTE_BLOCK[128];

/* FNUM (10-bit) per MIDI note 0..127. */
extern const u16 MDX_NOTE_FNUM[128];


/* Set the channel pitch from a MIDI note (clamped 0..127). */
void MdxFrequency_SetNote(u8 channel, u8 midiNote);

#endif /* _MDX_FREQUENCY_H_ */
