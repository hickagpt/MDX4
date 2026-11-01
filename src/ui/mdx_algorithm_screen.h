#ifndef _MDX_ALGORITHM_SCREEN_H_
#define _MDX_ALGORITHM_SCREEN_H_

/*
 * mdx_algorithm_screen.h - algorithm selection screen (SPEC 3.3 / 4.4).
 *
 * Lets the user pick one of the 8 FM algorithms. The operator routing diagram is
 * GENERATED from MdxAlgorithm_GetTopology() - it shows which operators are
 * modulators vs carriers and how they feed each other. Nothing is hardcoded per
 * algorithm, so the picture cannot drift from the topology data. Selecting an
 * algorithm updates the sounding patch's 0xB0 via MdxSynth_SetAlgorithm().
 */

#include <genesis.h>

void MdxAlgorithmScreen_Init(void);
void MdxAlgorithmScreen_Enter(void);
void MdxAlgorithmScreen_Handle(const void* input);

#endif /* _MDX_ALGORITHM_SCREEN_H_ */
