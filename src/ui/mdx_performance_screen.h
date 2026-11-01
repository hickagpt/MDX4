#ifndef _MDX_PERFORMANCE_SCREEN_H_
#define _MDX_PERFORMANCE_SCREEN_H_

/*
 * mdx_performance_screen.h - on-screen virtual keyboard (SPEC 7.1 / 7.2).
 *
 * A monophonic performance screen for interactive sound testing:
 *   - an on-screen keyboard spanning approximately two octaves;
 *   - D-Pad Left/Right move the selected note by one semitone, Up/Down jump an
 *     octave, all clamped to the ~2-octave window;
 *   - C plays/holds the selected note (release to stop); A is an alternate
 *     trigger;
 *   - moving the selection to a NEW note always releases the previous note and
 *     plays the new one - a note is never left stuck.
 *
 * All audible changes go through the engine (MdxSynth_NoteOn/NoteOff); the
 * engine remains the single owner of channel 1 and keeps sounding across the
 * transition back to other screens. Pitch control + modulation are added in a
 * later wave (see mdx_performance_screen.c).
 */

#include <genesis.h>

void MdxPerformanceScreen_Init(void);
void MdxPerformanceScreen_Enter(void);
void MdxPerformanceScreen_Handle(const void* input);

#endif /* _MDX_PERFORMANCE_SCREEN_H_ */
