#ifndef _MDX_ENVELOPE_SCREEN_H_
#define _MDX_ENVELOPE_SCREEN_H_

/*
 * mdx_envelope_screen.h - envelope editor (SPEC 3.4 / 4.4).
 *
 * Edits the ADSR-style envelope of the selected operator: AR, D1R, D1L, D2R, RR
 * and the rate scaling (RS = KS). Each change is pushed to the engine via
 * MdxSynth_SetOperator() (writes 0x50/0x60/0x70/0x80), so it is audible
 * immediately. An ADSR-style graph is drawn as a VISUAL APPROXIMATION of the
 * chip's attenuation behaviour - it is not a literal amplitude curve.
 */

#include <genesis.h>

void MdxEnvelopeScreen_Init(void);
void MdxEnvelopeScreen_Enter(void);
void MdxEnvelopeScreen_Handle(const void* input);

#endif /* _MDX_ENVELOPE_SCREEN_H_ */
