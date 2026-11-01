/*
 * mdx_envelope_screen.c - envelope editor (SPEC 3.4 / 4.4).
 *
 * See mdx_envelope_screen.h. The envelope graph is an approximation derived from
 * the real parameters; the registers are the source of truth and are updated live.
 */

#include "mdx_envelope_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"


#define ROW0       1           /* first parameter row                       */
#define PARAM_COUNT 6

/* Graph box geometry (text cells). */
#define BOX_X    16
#define BOX_Y    9
#define BOX_W    18
#define BOX_H    8
#define IX0      (BOX_X + 1)   /* inside left   */
#define IY0      (BOX_Y + 1)   /* inside top    */
#define IW       (BOX_W - 2)
#define IH       (BOX_H - 2)
#define MAX_AMP  (IH - 1)


typedef struct
{
    const char* name;
    u8 min;
    u8 max;
} EnvParam;


static const EnvParam s_param[PARAM_COUNT] =
{
    { "AR ", 0, 31 },   /* attack rate      */
    { "D1R", 0, 31 },   /* decay 1 rate     */
    { "D1L", 0, 15 },   /* sustain level    */
    { "D2R", 0, 31 },   /* decay 2 rate     */
    { "RR ", 0, 31 },   /* release rate     */
    { "RS ", 0, 3  }    /* rate scaling (KS)*/
};


static FMOperator s_op;       /* working copy of the selected operator */
static u8         s_sel = 0; /* selected parameter 0..5               */


static u8 get_val(u8 idx)
{
    switch (idx)
    {
        case 0: return s_op.attackRate;
        case 1: return s_op.decay1Rate;
        case 2: return s_op.sustainLevel;
        case 3: return s_op.decay2Rate;
        case 4: return s_op.releaseRate;
        case 5: return s_op.keyScale;
        default: return 0;
    }
}


static void set_val(u8 idx, u8 v)
{
    switch (idx)
    {
        case 0: s_op.attackRate         = v; break;
        case 1: s_op.decay1Rate         = v; break;
        case 2: s_op.sustainLevel        = v; break;
        case 3: s_op.decay2Rate          = v; break;
        case 4: s_op.releaseRate         = v; break;
        case 5: s_op.keyScale            = v; break;
        default: break;
    }
}


/* ADSR-style approximation: amp 0..MAX_AMP, time across the inside width. */
static void draw_graph(void)
{
    u8  r;
    u8  attackW, decayW, sustainW, releaseW;
    u16 totalW;
    u8  sustainAmp;
    u16 x;

    /* clear + redraw the box (no overlap with the param list rows) */
    for (r = BOX_Y; r < (BOX_Y + BOX_H); r++) VDP_clearTextLine(r);
    MdxUi_DrawBox(BOX_X, BOX_Y, BOX_W, BOX_H);

    attackW   = (u8)(1 + ((31 - s_op.attackRate)  * 3) / 31);
    decayW    = (u8)(1 + ((31 - s_op.decay1Rate)  * 3) / 31);
    sustainW  = 3;
    releaseW  = (u8)(1 + ((31 - s_op.releaseRate) * 3) / 31);
    totalW    = (u16)attackW + decayW + sustainW + releaseW;

    /* D1L: 0 = no attenuation (sustain at peak), 15 = near silence */
    sustainAmp = (u8)((u16)MAX_AMP * (15 - s_op.sustainLevel) / 15);

    for (x = 0; x < totalW; x++)
    {
        u16 amp;
        u16 y;

        if (x < attackW)
        {
            amp = (u16)((u32)MAX_AMP * x / attackW);
        }
        else if (x < (u16)(attackW + decayW))
        {
            const u8 l = (u8)(x - attackW);
            amp = (u16)MAX_AMP - (u16)((u32)(MAX_AMP - sustainAmp) * l / decayW);
        }
        else if (x < (u16)(attackW + decayW + sustainW))
        {
            amp = sustainAmp;
        }
        else
        {
            const u8 l = (u8)(x - (attackW + decayW + sustainW));
            amp = (u16)((u32)sustainAmp * (releaseW - l) / releaseW);
        }

        y = (u16)IY0 + (MAX_AMP - (u8)amp);
        {
            char c[2] = { '#', '\0' };
            VDP_drawText(c, (u16)IX0 + x, (u16)y);
        }
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
    char buf[20];
    u8 op = MdxUi_GetOperator();

    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "ENVELOPE EDIT");

    buf[0] = 'O'; buf[1] = 'P'; buf[2] = ' ';
    buf[3] = (char)('1' + op);
    buf[4] = '\0';
    MdxUi_DrawText(16, 0, buf);
}


void MdxEnvelopeScreen_Enter(void)
{
    FMPatch p;
    MdxSynth_GetPatch(&p);
    s_op = p.operators[MdxUi_GetOperator()];
    s_sel = 0;

    draw_header();
    draw_all_params();
    draw_graph();

    MdxUi_ClearLine(18);
    MdxUi_DrawText(1, 18, "U/D:SEL A:OP B:BACK C:PLAY");

    MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel));
}


void MdxEnvelopeScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    u8 op;

    if (i == NULL) return;

    if (i->up)   { if (s_sel > 0)                    s_sel--; MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel)); }
    if (i->down) { if (s_sel < (PARAM_COUNT - 1))    s_sel++; MdxUi_MoveCursor(0, (u16)(ROW0 + s_sel)); }

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
        draw_graph();
        return;
    }

    if (i->b) { MdxUi_SetScreen(SCREEN_MAIN); return; }

    if (i->left || i->right)
    {
        const s8  dir = i->right ? 1 : -1;
        s16 val = (s16)get_val(s_sel) + dir;
        if (val < s_param[s_sel].min) val = s_param[s_sel].min;
        if (val > s_param[s_sel].max) val = s_param[s_sel].max;

        set_val(s_sel, (u8)val);
        MdxSynth_SetOperator(MdxUi_GetOperator(), &s_op);   /* 0x50/60/70/80 */
        draw_param(s_sel);
        draw_graph();
    }
}


void MdxEnvelopeScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_ENVELOPE, MdxEnvelopeScreen_Enter, MdxEnvelopeScreen_Handle);
}
