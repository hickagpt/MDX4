/*
 * mdx_operator_screen.c - per-operator editor (SPEC 3.2 / 4.4).
 *
 * See mdx_operator_screen.h. Parameters are edited in place and applied live.
 */

#include "mdx_operator_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"
#include "ui/mdx_gfx.h"    /* MdxGfx toolkit: DrawHBar, PAL2 chrome, base tiles */


#define ROW0      1          /* (legacy) sprite-cursor base row for Handle */
#define PARAM_COUNT 11

/* Widget kind for each param tile in the grid. Cyclic / stepped params use a
 * rotary Knob; level / rate params use a horizontal Fader. Selection highlight
 * (MdxGfx_DrawSelectedBox) is drawn by the widget's own `selected` flag. */
#define PKNOB   0
#define PFADER  1

#define HEADER_ROW  2        /* inside the device-shell green panel (PAGE_Y) */
#define GRID_TOP    4        /* first param row in the panel                 */
#define FOOTER_ROW  23       /* help line near panel bottom                  */
#define LABEL_X     2        /* param-name text column                       */
#define WIDGET_X    9        /* widget tile column                           */
#define FADER_LEN   19       /* horizontal-fader span (cols 9..27)          */
#define NUM_X       30       /* numeric readout column (3 digits)           */

typedef struct
{
    const char* name;
    u8 min;
    u8 max;
    u8 kind;      /* PKNOB or PFADER */
} ParamDef;


/* Min / max and order are the original YM2612 ranges (do not re-range).
 * kind: KNOB for cyclic/stepped params (MUL DET KS AM SSG),
 *       FADER for level/rate params (TL AR D1R D1L D2R RR). */
static const ParamDef s_param[PARAM_COUNT] =
{
    { "MUL", 0, 15,  PKNOB  },   /* multiplier        */
    { "DET", 0, 7,   PKNOB  },   /* detune            */
    { "TL",  0, 127, PFADER },   /* total level       */
    { "KS",  0, 3,   PKNOB  },   /* key scale         */
    { "AR",  0, 31,  PFADER },   /* attack rate       */
    { "D1R", 0, 31,  PFADER },   /* decay 1 rate      */
    { "D1L", 0, 15,  PFADER },   /* sustain level     */
    { "D2R", 0, 31,  PFADER },   /* decay 2 rate      */
    { "RR",  0, 31,  PFADER },   /* release rate      */
    { "AM",  0, 1,   PKNOB  },   /* amplitude mod     */
    { "SSG", 0, 15,  PKNOB  }    /* SSG-EG            */
};


static FMOperator s_op;     /* working copy of the selected operator */
static u8         s_sel = 0; /* selected parameter 0..10              */


static u8 get_val(u8 idx)
{
    switch (idx)
    {
        case  0: return s_op.multiplier;
        case  1: return s_op.detune;
        case  2: return s_op.totalLevel;
        case  3: return s_op.keyScale;
        case  4: return s_op.attackRate;
        case  5: return s_op.decay1Rate;
        case  6: return s_op.sustainLevel;
        case  7: return s_op.decay2Rate;
        case  8: return s_op.releaseRate;
        case  9: return s_op.amplitudeModulation;
        case 10: return s_op.ssgEg;
        default: return 0;
    }
}


static void set_val(u8 idx, u8 v)
{
    switch (idx)
    {
        case  0: s_op.multiplier         = v; break;
        case  1: s_op.detune             = v; break;
        case  2: s_op.totalLevel          = v; break;
        case  3: s_op.keyScale            = v; break;
        case  4: s_op.attackRate          = v; break;
        case  5: s_op.decay1Rate          = v; break;
        case  6: s_op.sustainLevel        = v; break;
        case  7: s_op.decay2Rate          = v; break;
        case  8: s_op.releaseRate         = v; break;
        case  9: s_op.amplitudeModulation = v; break;
        case 10: s_op.ssgEg               = v; break;
        default: break;
    }
}


static void draw_param(u8 idx)
{
    const u16 y   = (u16)(GRID_TOP + idx);
    const u8  v   = get_val(idx);
    const bool sel = (idx == s_sel);   /* selected param -> DrawSelectedBox highlight */

    MdxUi_DrawText(LABEL_X, y, s_param[idx].name);

    if (s_param[idx].kind == PKNOB)
        MdxGfx_DrawKnob((u16)WIDGET_X, y, v, s_param[idx].max, sel);
    else
        MdxGfx_DrawFaderH((u16)WIDGET_X, y, FADER_LEN, v, s_param[idx].max, sel);

    MdxUi_DrawNumber((u16)NUM_X, y, v, 3);   /* numeric readout beside the widget */
}


static void draw_all_params(void)
{
    u8 i;
    for (i = 0; i < PARAM_COUNT; i++) draw_param(i);
}


static void draw_header(void)
{
    char buf[20];
    u8 op = MdxUi_GetOperator();

    MdxUi_DrawText(2, HEADER_ROW, "OPERATOR EDIT");

    buf[0] = 'O'; buf[1] = 'P'; buf[2] = ' ';
    buf[3] = (char)('1' + op);
    buf[4] = '\0';
    MdxUi_DrawText(20, HEADER_ROW, buf);
}


void MdxOperatorScreen_Enter(void)
{
    FMPatch p;
    MdxGfx_Init();          /* generate chrome palette + base tiles (idempotent) */
    MdxSynth_GetPatch(&p);
    s_op = p.operators[MdxUi_GetOperator()];
    s_sel = 0;

    MdxUi_ClearScreen();
    MdxGfx_DrawDeviceShell();   /* DX-7 shell on BG_A, content inside PAGE_* */

    draw_header();
    draw_all_params();

    MdxUi_DrawText(2, FOOTER_ROW, "U/D:SEL A:OP B:BACK C:PLAY");

    MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel));
}


void MdxOperatorScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    u8 op;

    if (i == NULL) return;

    /* parameter selection (dirty-rect repaint of the selection highlight only;
     * the widget `selected` flag drives DrawSelectedBox, so redraw the old and
     * new param rows instead of a full-screen clear). */
    if (i->up)
    {
        if (s_sel > 0)
        {
            draw_param(s_sel);
            s_sel--;
            draw_param(s_sel);
        }
        MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel));
    }

    if (i->down)
    {
        if (s_sel < (PARAM_COUNT - 1))
        {
            draw_param(s_sel);
            s_sel++;
            draw_param(s_sel);
        }
        MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel));
    }

    /* cycle the operator being edited */
    if (i->a)
    {
        op = (MdxUi_GetOperator() + 1) & 3;
        MdxUi_SetOperator(op);
        {
            FMPatch p;
            MdxSynth_GetPatch(&p);
            s_op = p.operators[op];
        }
        draw_header();
        draw_all_params();
        return;
    }

    if (i->b) { MdxUi_SetScreen(SCREEN_MAIN); return; }

    /* change value (live, via engine) */
    if (i->left || i->right)
    {
        const s8  dir = i->right ? 1 : -1;
        s16 val = (s16)get_val(s_sel) + dir;
        if (val < s_param[s_sel].min) val = s_param[s_sel].min;
        if (val > s_param[s_sel].max) val = s_param[s_sel].max;

        set_val(s_sel, (u8)val);
        MdxSynth_SetOperator(MdxUi_GetOperator(), &s_op);
        draw_param(s_sel);
    }
}


void MdxOperatorScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_OPERATOR, MdxOperatorScreen_Enter, MdxOperatorScreen_Handle);
}
