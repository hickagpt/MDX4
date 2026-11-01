#ifndef _MDX_ENVELOPE_H_
#define _MDX_ENVELOPE_H_

/*
 * mdx_envelope.h - per-operator envelope register mapping (SPEC 3.4 / 6.1).
 *
 * Writes the four envelope register groups of one operator:
 *   0x50 KS/AR, 0x60 AM/D1R, 0x70 D2R, 0x80 D1L/RR.
 *
 * This is the envelope portion of MdxOperator_Apply() exposed separately so
 * callers can retune an envelope without rewriting the whole operator. All
 * math is integer / fixed-point only - NO floating point.
 */

#include "mdx_patch.h"


/* Write the envelope register groups for operator opIndex of the channel.
 * Clamps a local copy first. opIndex is the canonical index 0..3 (S1..S4). */
void MdxEnvelope_Apply(u8 channel, u8 opIndex, const FMOperator* op);

#endif /* _MDX_ENVELOPE_H_ */
