#ifndef _MDX_OPERATOR_H_
#define _MDX_OPERATOR_H_

/*
 * mdx_operator.h - per-operator -> YM2612 register mapping (spec SPEC 3.2).
 *
 * Maps one FMOperator onto the seven per-operator register groups of a
 * channel. All writes go through MdxYm2612_WriteReg() and ALWAYS use
 * MDX_OP_OFFSET[opIndex] for the slot offset (never 0/1/2/3 directly),
 * preserving the S1/S3/S2/S4 hardware ordering.
 */

#include "mdx_patch.h"


/* Write every register group of one operator for the given channel.
 * Clamps a local copy first so only legal values reach the hardware.
 * opIndex is the canonical operator index 0..3 (S1..S4). */
void MdxOperator_Apply(u8 channel, u8 opIndex, const FMOperator* op);

#endif /* _MDX_OPERATOR_H_ */
