/*
 * mdx_modulation_screen.c - feedback + basic LFO editor (SPEC 3.1 / 4.4).
 *
 * See mdx_modulation_screen.h. Feedback is a topology change (retrigger while a
 * note is held so it is audible); LFO changes are live (0x22 / 0xB4).
 */

#include "mdx_modulation_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"


#define ROW0       1
#define PARAM_COUNT 5


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


static void draw_param(u8 idx)
{
    const u16 row = (u16)(ROW0 + idx);
    MdxUi_ClearLine(row);
    MdxUi_DrawText(1, row, s_param[idx].name);
    MdxUi_DrawNumber(8, row, get_val(idx), 3);
}


static void draw_all_params(void)
{
    u8 i;
    for (i = 0; i < PARAM_COUNT; i++) draw_param(i);
}


static void draw_header(void)
{
    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "MODULATION  LFO:");
    MdxUi_DrawText(15, 0, s_patch.lfoEnabled ? "ON " : "OFF");
}


void MdxModulationScreen_Enter(void)
{
    MdxSynth_GetPatch(&s_patch);
    s_sel = 0;

    draw_header();
    draw_all_params();

    MdxUi_ClearLine(7);
    MdxUi_DrawText(1, 7, "U/D:SEL L/R:CHG B:BACK C:PLAY");

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
