#ifndef _MDX_INPUT_H_
#define _MDX_INPUT_H_

/*
 * mdx_input.h - controller navigation for a three-button Mega Drive pad
 * (SPEC 4.3, Metis 1.9 "cancel" rule).
 *
 * Edge-triggered: MdxInput_Poll() reports a button as "pressed" only on the
 * new-press frame, NOT while it is held. The four directions additionally get a
 * defined auto-repeat cadence so holding Up/Down/Left/Right scrolls smoothly.
 *
 * Holding Up+Down together (or Left+Right) resolves to NO movement ("cancel",
 * Metis 1.9). MdxInput_Held() exposes the raw level for the C-button preview
 * (note must sound while held, stop on release).
 *
 * All reads go through SGDK JOY_readJoypad() - never raw $A10003/9.
 */

#include <genesis.h>


typedef struct
{
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;       /* enter / edit          */
    bool b;       /* back / cancel         */
    bool c;       /* preview note          */
    bool start;   /* main menu             */
} MdxInput;


/* Boot the input subsystem (no hardware access, just resets edge state). */
void MdxInput_Init(void);

/* Poll once per frame (call from the VBlank-tied loop). Fills *out with this
 * frame's edge-triggered / auto-repeated presses. */
void MdxInput_Poll(MdxInput* out);

/* Current held level for any BUTTON_* mask (used for the C preview). */
bool MdxInput_Held(u16 buttonMask);


#endif /* _MDX_INPUT_H_ */
