#ifndef _MDX_UI_H_
#define _MDX_UI_H_

/*
 * mdx_ui.h - UI core (SPEC 4.1 / 9.1).
 *
 * Owns the 320x224 tile VDP, a limited palette, a SPRITE-based selection
 * cursor, and the SCREEN STATE MACHINE that the individual screens plug into.
 *
 * Rendering discipline (SPEC 9.1 "no unnecessary full-screen redraws"):
 *   - each screen renders ONCE on entry (its Enter() function);
 *   - the main loop only re-renders when state changed. The screens update
 *     only the lines / cursor that actually changed, never the whole frame.
 *
 * The UI never touches YM2612 registers directly - it calls the synth engine
 * (MdxSynth_*) for every audible change. The engine remains the single owner
 * of channel 1 and keeps sounding while the user navigates and edits.
 */

#include <genesis.h>


/* Screen identifiers. */
typedef enum
{
    SCREEN_MAIN = 0,
    SCREEN_ALGORITHM,
    SCREEN_OPERATOR,
    SCREEN_ENVELOPE,
    SCREEN_MODULATION,
    SCREEN_PATCH,        /* Patch Manager (save/load to SRAM) */
    SCREEN_PERFORMANCE,  /* Virtual keyboard / performance (Wave 5) */
    SCREEN_COUNT
} MdxScreen;


/* --- Screen registration (decouples mdx_ui.c from the screen modules). --- */
typedef void (*MdxScreenEnterFn)(void);
typedef void (*MdxScreenHandleFn)(const void* input);   /* input is MdxInput* */

void MdxUi_RegisterScreen(MdxScreen s, MdxScreenEnterFn enter,
                          MdxScreenHandleFn handle);


/* --- State machine. --- */
void MdxUi_Init(void);
void MdxUi_SetScreen(MdxScreen s);
MdxScreen MdxUi_GetScreen(void);

/* Shared selected operator (0..3) used by Operator + Envelope screens. */
u8  MdxUi_GetOperator(void);
void MdxUi_SetOperator(u8 op);

/* Preview note (MIDI note number) used by the C-button note preview. */
u8  MdxUi_GetPreviewNote(void);
void MdxUi_SetPreviewNote(u8 note);


/* --- Render helpers (tile coordinates: col 0..39, row 0..27). --- */
void MdxUi_DrawText(u16 col, u16 row, const char* text);
void MdxUi_DrawNumber(u16 col, u16 row, u16 value, u8 width); /* fixed-width, space-padded */
void MdxUi_DrawBox(u16 col, u16 row, u16 w, u16 h);
void MdxUi_ClearLine(u16 row);
void MdxUi_MoveCursor(u16 col, u16 row);   /* sprite cursor to text cell */


/* --- Top-level loop. main.c calls MdxSynth_Init() then MdxUi_Run(). --- */
void MdxUi_Run(void);


#endif /* _MDX_UI_H_ */
