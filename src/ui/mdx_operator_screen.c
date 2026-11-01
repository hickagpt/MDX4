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


#define ROW0      1          /* first parameter row */
#define PARAM_COUNT 11

typedef struct
{
    const char* name;
    u8 min;
    u8 max;
} ParamDef;


static const ParamDef s_param[PARAM_COUNT] =
{
    { "MUL", 0, 15  },   /* multiplier        */
    { "DET", 0, 7   },   /* detune            */
    { "TL",  0, 127 },   /* total level       */
    { "KS",  0, 3   },   /* key scale         */
    { "AR",  0, 31  },   /* attack rate       */
    { "D1R", 0, 31  },   /* decay 1 rate      */
    { "D1L", 0, 15  },   /* sustain level     */
    { "D2R", 0, 31  },   /* decay 2 rate      */
    { "RR",  0, 31  },   /* release rate      */
    { "AM",  0, 1   },   /* amplitude mod     */
    { "SSG", 0, 15  }    /* SSG-EG            */
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
    const u16 row = (u16)(ROW0 + idx);
    const u8  v   = get_val(idx);
    MdxUi_ClearLine(row);
    MdxUi_DrawText(1, row, s_param[idx].name);
    MdxGfx_DrawHBar(12, row, 16, v, s_param[idx].max);   /* graphical slider */
    MdxUi_DrawNumber(30, row, v, 3);                     /* keep numeric readout */
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

    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "OPERATOR EDIT");

    buf[0] = 'O'; buf[1] = 'P'; buf[2] = ' ';
    buf[3] = (char)('1' + op);
    buf[4] = '\0';
    MdxUi_DrawText(16, 0, buf);
}


void MdxOperatorScreen_Enter(void)
{
    FMPatch p;
    MdxGfx_Init();          /* generate chrome palette + base tiles (idempotent) */
    MdxSynth_GetPatch(&p);
    s_op = p.operators[MdxUi_GetOperator()];
    s_sel = 0;

    draw_header();
    draw_all_params();

    MdxUi_ClearLine(13);
    MdxUi_DrawText(1, 13, "U/D:SEL A:OP B:BACK C:PLAY");

    MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel));
}


void MdxOperatorScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    u8 op;

    if (i == NULL) return;

    /* parameter selection */
    if (i->up)   { if (s_sel > 0)      s_sel--; MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel)); }
    if (i->down) { if (s_sel < (PARAM_COUNT - 1)) s_sel++; MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel)); }

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
