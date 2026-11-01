#ifndef _MDX_LFO_H_
#define _MDX_LFO_H_

/*
 * mdx_lfo.h - LFO register mapping (SPEC 3.4 / 6.1).
 *
 * Writes:
 *   - the GLOBAL LFO register 0x22 (enable bit + frequency select), and
 *   - the CHANNEL-level AMS/PMS in 0xB4.
 *
 * There is exactly ONE 0xB4 per channel (not per operator). Bit layout of 0xB4:
 *   bits 7-6 = L/R output (we keep 0b11 from init),
 *   bits 5-4 = AMS (2-bit index 0..3),
 *   bit  3   = unused,
 *   bits 2-0 = PMS (3-bit index 0..7).
 *
 * The AMS/PMS index tables below are the chip's non-linear response
 * (AMS in dB, PMS in cents). The chip takes the INDEX, not the dB/cent value,
 * so the tables are for reference/UI only.
 */

#include "mdx_patch.h"


/* Apply LFO settings for the channel from a patch (0x22 global + 0xB4 AMS/PMS).
 * Returns the 0xB4 value written, so the owning module can cache it (the synth
 * is the single owner of the channel's 0xB4 value). */
u8 MdxLfo_Apply(u8 channel, const FMPatch* patch);

/* AMS depth in tenths of a dB for index 0..3 (0, 1.4, 5.9, 11.8). */
extern const u16 MDX_LFO_AMS_DB_X10[4];

/* PMS depth in cents (1/100 semitone) for index 0..7 (~0.034..0.80 semitone). */
extern const u16 MDX_LFO_PMS_CENT[8];

#endif /* _MDX_LFO_H_ */
