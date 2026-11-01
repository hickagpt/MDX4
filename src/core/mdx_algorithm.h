#ifndef _MDX_ALGORITHM_H_
#define _MDX_ALGORITHM_H_

/*
 * mdx_algorithm.h - 8 canonical DX7-style FM algorithm topologies (SPEC 3.3).
 *
 * Operators are indexed 0..3 == S1..S4 (canonical order). The topology
 * describes how the operators connect in each of the 8 YM2612 algorithms.
 *
 *   modulators[i] : bitmask of operators that feed the INPUT of operator i
 *                    (bit j set => operator j modulates operator i).
 *   carrierMask   : bitmask of operators that are CARRIERS (audible outputs);
 *                    bit i set => operator i is a carrier.
 *
 * 0xB0 feedback bits apply ONLY to operator S1 (self-feedback), NOT to the
 * whole algorithm. This is documented in MdxAlgorithm_Apply().
 */

#include "mdx_ym2612.h"


typedef struct
{
    u8 modulators[4];   /* per-operator input modulation sources (bitmask) */
    u8 carrierMask;     /* carriers (bit i => operator i is a carrier)     */
} MdxAlgTopology;


/* Return the topology for algorithm 0..7 (clamped). Never NULL. */
const MdxAlgTopology* MdxAlgorithm_GetTopology(u8 algo);

/* Write register 0xB0 = (feedback<<3) | (algorithm & 0x07) for the channel.
 * Clamps algorithm to 0..7. feedback is the S1 self-feedback amount (0..7). */
void MdxAlgorithm_Apply(u8 channel, u8 algorithm, u8 feedback);

/* True if operator opIndex (0..3) is a carrier in the given algorithm. */
bool MdxAlgorithm_IsCarrier(u8 algo, u8 opIndex);

#endif /* _MDX_ALGORITHM_H_ */
