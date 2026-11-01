/*
 * mdx_main_screen.c - main / overview screen (SPEC 4.2).
 *
 * Renders the current patch snapshot once on entry, then only repaints the rows
 * that changed. Value edits call the engine setters immediately so the change is
 * audible by construction. Navigation is edge-triggered (handled by mdx_input).
 */

#include "mdx_main_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"
#include "core/mdx_algorithm.h"


/* Selectable rows on this screen. */
#define ROW_ALGORITHM  2
#define ROW_OPERATOR   3
#define ROW_ENVELOPE   5
#define ROW_MODULATION 6

/* sel index -> cursor row. */
static const u8 s_row[4] = { ROW_ALGORITHM, ROW_OPERATOR, ROW_ENVELOPE, ROW_MODULATION };

static FMPatch s_patch;     /* snapshot of current patch (refreshed on Enter) */
static u8      s_sel   = 0; /* 0..3 */
static u8      s_keyOn = 0; /* last drawn performance status                */


/* --- per-row redraw helpers (clear the line, then draw). --- */

static void draw_algorithm(void)
{
    MdxUi_ClearLine(ROW_ALGORITHM);
    MdxUi_DrawText(1, ROW_ALGORITHM, "ALGORITHM");
    MdxUi_DrawNumber(11, ROW_ALGORITHM, s_patch.algorithm, 2);
    MdxUi_DrawText(14, ROW_ALGORITHM, "<A>");
}

static void draw_operator(void)
{
    MdxUi_ClearLine(ROW_OPERATOR);
    MdxUi_DrawText(1, ROW_OPERATOR, "OPERATOR");
    MdxUi_DrawNumber(11, ROW_OPERATOR, (u16)(MdxUi_GetOperator() + 1), 1);
    MdxUi_DrawText(14, ROW_OPERATOR, "<A>");
}

static void draw_op_params(void)
{
    const FMOperator* op = &s_patch.operators[MdxUi_GetOperator()];
    MdxUi_ClearLine(4);
    MdxUi_DrawText(1, 4, "MUL");
    MdxUi_DrawNumber(5, 4, op->multiplier, 2);
    MdxUi_DrawText(9, 4, "DT");
    MdxUi_DrawNumber(12, 4, op->detune, 1);
    MdxUi_DrawText(15, 4, "TL");
    MdxUi_DrawNumber(18, 4, op->totalLevel, 3);
}

static void draw_envelope(void)
{
    const FMOperator* op = &s_patch.operators[MdxUi_GetOperator()];
    MdxUi_ClearLine(ROW_ENVELOPE);
    MdxUi_DrawText(1, ROW_ENVELOPE, "ENV AR");
    MdxUi_DrawNumber(8, ROW_ENVELOPE, op->attackRate, 2);
    MdxUi_DrawText(12, ROW_ENVELOPE, "D1");
    MdxUi_DrawNumber(16, ROW_ENVELOPE, op->decay1Rate, 2);
    MdxUi_DrawText(19, ROW_ENVELOPE, "SL");
    MdxUi_DrawNumber(22, ROW_ENVELOPE, op->sustainLevel, 2);
    MdxUi_DrawText(25, ROW_ENVELOPE, "RR");
    MdxUi_DrawNumber(28, ROW_ENVELOPE, op->releaseRate, 2);
    MdxUi_DrawText(32, ROW_ENVELOPE, "<A>");
}

static void draw_modulation(void)
{
    MdxUi_ClearLine(ROW_MODULATION);
    MdxUi_DrawText(1, ROW_MODULATION, "FB");
    MdxUi_DrawNumber(4, ROW_MODULATION, s_patch.feedback, 1);
    MdxUi_DrawText(8, ROW_MODULATION, "LFO");
    MdxUi_DrawNumber(12, ROW_MODULATION, s_patch.lfoEnabled, 1);
    MdxUi_DrawText(15, ROW_MODULATION, "<A>");
}

static void draw_key(void)
{
    MdxUi_ClearLine(7);
    MdxUi_DrawText(1, 7, "KEY:");
    MdxUi_DrawText(6, 7, s_keyOn ? "ON " : "-- ");
    MdxUi_DrawText(11, 7, "C=PLAY");
}


/* --- entry / handling. --- */

void MdxMainScreen_Enter(void)
{
    /* full render once */
    MdxSynth_GetPatch(&s_patch);
    s_sel = 0;
    s_keyOn = MdxSynth_IsKeyOn() ? 1 : 0;

    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "MDX-4 FM SYNTH");

    MdxUi_ClearLine(1);
    MdxUi_DrawText(1, 1, "NAME:");
    MdxUi_DrawText(7, 1, s_patch.name);

    draw_algorithm();
    draw_operator();
    draw_op_params();
    draw_envelope();
    draw_modulation();
    draw_key();

    MdxUi_ClearLine(9);
    MdxUi_DrawText(1, 9, "A:EDIT  B:BACK  C:PLAY  START:MENU");

    MdxUi_MoveCursor(0, s_row[s_sel]);
}


void MdxMainScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    u8 op;

    if (i == NULL) return;

    /* selection */
    if (i->down) { if (s_sel < 3) s_sel++; MdxUi_MoveCursor(0, s_row[s_sel]); }
    if (i->up)   { if (s_sel > 0) s_sel--; MdxUi_MoveCursor(0, s_row[s_sel]); }

    /* open sub-screen */
    if (i->a)
    {
        switch (s_sel)
        {
            case 0: MdxUi_SetScreen(SCREEN_ALGORITHM); return;
            case 1: MdxUi_SetScreen(SCREEN_OPERATOR);   return;
            case 2: MdxUi_SetScreen(SCREEN_ENVELOPE);   return;
            case 3: MdxUi_SetScreen(SCREEN_MODULATION); return;
            default: break;
        }
    }

    /* in-place value changes (live, via engine) */
    if (i->left || i->right)
    {
        const s8 dir = i->right ? 1 : -1;

        switch (s_sel)
        {
            case 0:   /* algorithm: wrap 0..7 */
            {
                u8 a = s_patch.algorithm;
                if (dir > 0) a = (a + 1) & 7;
                else          a = (a == 0) ? 7 : (a - 1);
                MdxSynth_SetAlgorithm(a);
                s_patch.algorithm = a;
                draw_algorithm();
                break;
            }
            case 1:   /* selected operator: cycle 1..4 (display only) */
            {
                op = MdxUi_GetOperator();
                op = (u8)((op + (u8)dir) & 3);
                MdxUi_SetOperator(op);
                draw_operator();
                draw_op_params();
                draw_envelope();
                break;
            }
            case 3:   /* feedback: clamp 0..7 */
            {
                s16 fb = s_patch.feedback + dir;
                if (fb < 0) fb = 0;
                if (fb > 7) fb = 7;
                MdxSynth_SetFeedback((u8)fb);
                s_patch.feedback = (u8)fb;
                draw_modulation();
                break;
            }
            case 2:   /* envelope row is edited on the Envelope screen */
            default:
                break;
        }
    }

    /* keep the performance status in sync (single-line repaint only) */
    {
        const u8 on = MdxSynth_IsKeyOn() ? 1 : 0;
        if (on != s_keyOn) { s_keyOn = on; draw_key(); }
    }
}


void MdxMainScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_MAIN, MdxMainScreen_Enter, MdxMainScreen_Handle);
}
