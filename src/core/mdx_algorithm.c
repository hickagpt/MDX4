/*
 * mdx_algorithm.c - 8 canonical DX7-style FM algorithm topologies (SPEC 3.3).
 *
 * Topology verified against the standard YM2612 / DX7 8-algorithm diagram
 * (and consistent with working emulator implementations):
 *   alg 0 -> only OP4 (S4) is a carrier            (1->2->3->4 chain)
 *   alg 7 -> all four operators are carriers         (4 independent carriers)
 *
 * modulators[i] is a bitmask: bit j set means operator j modulates operator i.
 * carrierMask bit i set means operator i is a carrier (audible output).
 *
 * Register 0xB0 packs (feedback<<3) | (algorithm & 0x07). The feedback field
 * is the operator-S1 self-feedback amount ONLY - it does NOT affect the other
 * operators or the algorithm routing.
 */

#include "mdx_algorithm.h"


/* modulators[i] = bitmask of operators feeding operator i's input.
 * carrierMask  = bitmask of carrier operators. */
static const MdxAlgTopology MDX_ALG[8] =
{
    /* alg 0 : 1->2->3->4 ; carrier = OP4 */
    { { 0x00, 0x01, 0x02, 0x04 }, 0x08 },
    /* alg 1 : (1+2)->3->4 ; carrier = OP4 */
    { { 0x00, 0x00, 0x03, 0x04 }, 0x08 },
    /* alg 2 : 1->2 , (1+3)->4 ; carrier = OP4 */
    { { 0x00, 0x00, 0x02, 0x05 }, 0x08 },
    /* alg 3 : 1->2 , (2+3)->4 ; carrier = OP4 */
    { { 0x00, 0x01, 0x00, 0x06 }, 0x08 },
    /* alg 4 : 1->2 , 3->4 ; carriers = OP2, OP4 */
    { { 0x00, 0x01, 0x00, 0x04 }, 0x0A },
    /* alg 5 : 1->2 , 1->3 , 1->4 ; carriers = OP2, OP3, OP4 */
    { { 0x00, 0x01, 0x01, 0x01 }, 0x0E },
    /* alg 6 : 1->2 , OP3, OP4 independent ; carriers = OP2, OP3, OP4 */
    { { 0x00, 0x01, 0x00, 0x00 }, 0x0E },
    /* alg 7 : all four independent ; carriers = OP1..OP4 */
    { { 0x00, 0x00, 0x00, 0x00 }, 0x0F }
};


const MdxAlgTopology* MdxAlgorithm_GetTopology(u8 algo)
{
    if (algo > 7) algo = 7;
    return &MDX_ALG[algo];
}


void MdxAlgorithm_Apply(u8 channel, u8 algorithm, u8 feedback)
{
    u8 value;

    if (algorithm > 7) algorithm = 7;
    if (feedback > 7)  feedback  = 7;

    /* 0xB0: bits 3-5 = feedback (S1 self-feedback only),
     *       bits 0-2 = algorithm number. */
    value = (u8)((feedback << 3) | (algorithm & 0x07));

    MdxYm2612_WriteReg(channel, MDX_REG_B0, 0, value);
}


bool MdxAlgorithm_IsCarrier(u8 algo, u8 opIndex)
{
    const MdxAlgTopology* topo;

    if (opIndex > 3) return FALSE;
    topo = MdxAlgorithm_GetTopology(algo);
    return (topo->carrierMask & (1u << opIndex)) != 0;
}
