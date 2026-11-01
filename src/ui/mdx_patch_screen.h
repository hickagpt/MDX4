#ifndef _MDX_PATCH_SCREEN_H_
#define _MDX_PATCH_SCREEN_H_

/*
 * mdx_patch_screen.h - Patch Manager screen (SPEC 8.1 / 8.3 / Phase 4).
 *
 * Lists instrument slots, lets the user select one, edit its name, save the
 * sounding engine patch into a slot, and load a slot back into the engine.
 * All persistence goes through the versioned + checksummed blob format
 * (MdxPatchFormat_*) over the no-SRAM-guarded storage layer (MdxStorage_*).
 *
 * Controls:
 *   Up / Down      - move the slot cursor (navigate mode)
 *   A              - enter name-edit mode (then A again commits + saves)
 *   Left / Right   - move the character cursor within the name (edit mode)
 *   Up / Down      - cycle the character at the cursor (edit mode)
 *   B              - cancel name-edit, or back to the main screen
 *   C              - load the selected slot into the engine + preview note
 *   START          - main menu (handled globally by MdxUi_Run)
 *
 * If SRAM is unavailable the screen shows "SAVE/LOAD UNAVAILABLE" and never
 * attempts a write or read of user data (the storage layer is already guarded,
 * and the format functions return a no-SRAM error that we surface as a status).
 */

#include <genesis.h>


void MdxPatchScreen_Init(void);
void MdxPatchScreen_Enter(void);
void MdxPatchScreen_Handle(const void* input);


#endif /* _MDX_PATCH_SCREEN_H_ */
