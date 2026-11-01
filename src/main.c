/*
 * main.c - MDX-4 Phase 3 ROM entry point.
 *
 * Boots the YM2612 synth engine (channel 1, 4-operator FM voice), initialises the
 * controller input and the UI, registers all screens, then runs the UI main loop.
 *
 * The UI loop (MdxUi_Run) polls input each VBlank, dispatches to the active screen,
 * and drives the C-button note preview. The synth engine is the single owner of the
 * YM2612 and keeps sounding across all navigation and editing - the note started on
 * the main screen stays audible while you open any sub-screen and edit parameters.
 *
 * All YM2612 access goes through the engine/driver; no direct port writes, no XGM,
 * no floating point.
 */

#include <genesis.h>

#include "core/mdx_synth.h"
#include "input/mdx_input.h"
#include "ui/mdx_ui.h"
#include "ui/mdx_main_screen.h"
#include "ui/mdx_algorithm_screen.h"
#include "ui/mdx_operator_screen.h"
#include "ui/mdx_envelope_screen.h"
#include "ui/mdx_modulation_screen.h"


int main(bool hardReset)
{
    (void)hardReset;

    /* Boot the 320x224 tile VDP (MdxUi_Init owns VDP_init, palette, cursor). */
    SYS_disableInts();
    MdxUi_Init();
    SYS_enableInts();

    /* Boot the YM2612 driver + load the default patch (channel 1, 4-op FM). */
    MdxSynth_Init();

    /* Controller navigation. */
    MdxInput_Init();

    /* Register the screens with the UI state machine. */
    MdxMainScreen_Init();
    MdxAlgorithmScreen_Init();
    MdxOperatorScreen_Init();
    MdxEnvelopeScreen_Init();
    MdxModulationScreen_Init();

    /* Start on the main screen, then run the VBlank-tied UI loop. */
    MdxUi_SetScreen(SCREEN_MAIN);
    MdxUi_Run();

    return 0;
}
