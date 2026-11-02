/*
 * mdx_modulation_screen.c - feedback + basic LFO editor (SPEC 3.1 / 4.4).
 *
 * See mdx_modulation_screen.h. Feedback is a topology change (retrigger while a
 * note is held so it is audible); LFO changes are live (0x22 / 0xB4).
 *
 * Rendered through the MdxGfx toolkit (knobs / faders / LED / selected box)
 * inside the device shell, matching the Operator screen's migrated look.
 */

#include "mdx_modulation_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"
#include "ui/mdx_gfx.h"    /* MdxGfx toolkit: Init, DrawKnob/Fader/LED, DrawSelectedBox, DrawDeviceShell, PAGE_* */


#define ROW0       1
#define PARAM_COUNT 5

/* Content layout (tile coords, all inside PAGE_X/Y/W/H = 2/2/36/24). */
#define LBL_ROW   5    /* parameter label plates        */
#define CTL_ROW   8    /* control widgets               */
#define LED_ROW   11   /* LFO on/off indicator          */
#define VAL_ROW   15   /* numeric value plates          */
#define HELP_ROW  23   /* footer help line             */


typedef struct
{
    const char* name;
    u8 min;
    u8 max;
} ModParam;


static const ModParam s_param[PARAM_COUNT] =
{
    { "FB ", 0, 7 },   /* feedback                       */
    { "LFO", 0, 1 },   /* LFO enable                     */
    { "FRQ", 0, 7 },   /* LFO frequency select           */
    { "PMS", 0, 7 },   /* LFO pitch depth (PMS index)    */
    { "AMS", 0, 3 }    /* LFO amplitude depth (AMS index)*/
};


static FMPatch s_patch;     /* snapshot for display          */
static u8      s_sel = 0;  /* selected parameter 0..4        */


static u8 get_val(u8 idx)
{
    switch (idx)
    {
        case 0: return s_patch.feedback;
        case 1: return s_patch.lfoEnabled;
        case 2: return s_patch.lfoFrequency;
        case 3: return s_patch.lfoPitchDepth;
        case 4: return s_patch.lfoAmplitudeDepth;
        default: return 0;
    }
}


/* Column centre for parameter idx (0..4): 5, 12, 19, 26, 33. */
static u16 col_x(u8 idx)
{
    return (u16)(5 + 7 * idx);
}


/* Draw one parameter's label plate, control widget and numeric value. The
 * selected control is framed by the unified MdxGfx_DrawSelectedBox (the widget
 * draw helpers call it internally when 'selected' is set). */
static void draw_param(u8 idx)
{
    const u16 cx = col_x(idx);
    const u8  v  = get_val(idx);
    const bool sel = (idx == s_sel);

    /* label plate + name */
    MdxGfx_DrawReadout((u16)(cx - 1), LBL_ROW, 4, 2);
    MdxGfx_DrawReadoutStr(cx, LBL_ROW, s_param[idx].name);

    /* control widget (knob or fader) */
    switch (idx)
    {
        case 0:  /* FB  : rotary knob 0..7      */ MdxGfx_DrawKnob(cx, CTL_ROW, v, 7, sel); break;
        case 1:  /* LFO : vertical fader 0..1   */ MdxGfx_DrawFaderV(cx, CTL_ROW, 2, v, 1, sel); break;
        case 2:  /* FRQ : rotary knob 0..7      */ MdxGfx_DrawKnob(cx, CTL_ROW, v, 7, sel); break;
        case 3:  /* PMS : vertical fader 0..7   */ MdxGfx_DrawFaderV(cx, CTL_ROW, 6, v, 7, sel); break;
        case 4:  /* AMS : vertical fader 0..3   */ MdxGfx_DrawFaderV(cx, CTL_ROW, 4, v, 3, sel); break;
        default: break;
    }

    /* numeric value plate (zero-padded) */
    MdxGfx_DrawReadout((u16)(cx - 1), VAL_ROW, 4, 2);
    MdxGfx_DrawReadoutNum(cx, VAL_ROW, v, 2);
}


/* LFO on/off indicator (red LED in the LFO column). */
static void draw_led(void)
{
    MdxGfx_DrawLED(col_x(1), LED_ROW, MDXGFX_LEDCOLOR_RED,
                   s_patch.lfoEnabled ? TRUE : FALSE);
}


static void draw_header(void)
{
    /* LFO on/off is shown by the LED; refresh its state. */
    draw_led();
}


void MdxModulationScreen_Enter(void)
{
    u8 i;

    MdxGfx_Init();          /* generate PAL2/PAL3 + all widget tiles (this screen omitted it before) */
    MdxSynth_GetPatch(&s_patch);
    s_sel = 0;

    MdxUi_ClearScreen();            /* wipe BG_A plane                 */
    MdxGfx_DrawDeviceShell();       /* DX-7 shell; inner rect = PAGE_* */

    /* page title */
    MdxGfx_DrawReadout(2, 2, 12, 2);
    MdxGfx_DrawReadoutStr(4, 2, "MODULATION");

    /* controls + LFO indicator */
    for (i = 0; i < PARAM_COUNT; i++) draw_param(i);
    draw_led();

    MdxUi_DrawText(2, HELP_ROW, "U/D:SEL L/R:CHG B:BACK C:PLAY");

    MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel));
}


void MdxModulationScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;

    if (i == NULL) return;

    if (i->up)   { if (s_sel > 0)                 s_sel--; MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel)); }
    if (i->down) { if (s_sel < (PARAM_COUNT - 1)) s_sel++; MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel)); }

    if (i->b) { MdxUi_SetScreen(SCREEN_MAIN); return; }

    if (i->left || i->right)
    {
        const s8  dir = i->right ? 1 : -1;
        s16 val = (s16)get_val(s_sel) + dir;
        if (val < s_param[s_sel].min) val = s_param[s_sel].min;
        if (val > s_param[s_sel].max) val = s_param[s_sel].max;

        switch (s_sel)
        {
            case 0:  /* feedback: topology change -> retrigger while sounding */
                MdxSynth_SetFeedback((u8)val);
                if (MdxSynth_IsKeyOn()) MdxSynth_NoteOn(MdxUi_GetPreviewNote());
                break;
            case 1:  MdxSynth_SetLfoEnabled((u8)val);    break;
            case 2:  MdxSynth_SetLfoFrequency((u8)val);  break;
            case 3:  MdxSynth_SetLfoPitchDepth((u8)val); break;
            case 4:  MdxSynth_SetLfoAmplitudeDepth((u8)val); break;
            default: break;
        }

        MdxSynth_GetPatch(&s_patch);   /* refresh snapshot for display */

        draw_param(s_sel);
        if (s_sel == 1) draw_header();   /* LFO on/off indicator */
    }
}


void MdxModulationScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_MODULATION, MdxModulationScreen_Enter, MdxModulationScreen_Handle);
}
