#ifndef _MDX_OPERATOR_SCREEN_H_
#define _MDX_OPERATOR_SCREEN_H_

/*
 * mdx_operator_screen.h - per-operator editor (SPEC 3.2 / 4.4).
 *
 * Edits the 11 parameters of one operator (selected 1..4). Up/Down selects a
 * parameter, Left/Right changes its value (clamped to the legal YM2612 range),
 * A cycles the operator being edited. Every change is pushed straight to the
 * engine with MdxSynth_SetOperator() so it is audible immediately. No operator
 * on/off switch: silence is achieved via total level only.
 */

#include <genesis.h>

void MdxOperatorScreen_Init(void);
void MdxOperatorScreen_Enter(void);
void MdxOperatorScreen_Handle(const void* input);

#endif /* _MDX_OPERATOR_SCREEN_H_ */
