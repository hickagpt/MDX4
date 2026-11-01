#ifndef _MDX_MODULATION_SCREEN_H_
#define _MDX_MODULATION_SCREEN_H_

/*
 * mdx_modulation_screen.h - feedback + basic LFO editor (SPEC 3.1 / 4.4).
 *
 * Edits the channel feedback (0..7) and the basic LFO settings: enable, frequency,
 * amplitude depth (AMS) and pitch depth (PMS). All changes apply through the engine
 * (MdxSynth_SetFeedback / MdxSynth_SetLfo*) so the tone changes audibly. Advanced
 * LFO controls beyond this "Basic" MVP scope are intentionally not exposed.
 */

#include <genesis.h>

void MdxModulationScreen_Init(void);
void MdxModulationScreen_Enter(void);
void MdxModulationScreen_Handle(const void* input);

#endif /* _MDX_MODULATION_SCREEN_H_ */
