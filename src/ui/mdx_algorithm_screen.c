/*
 * mdx_algorithm_screen.c - algorithm selection screen (SPEC 3.3 / 4.4).
 *
 * Up/Down/Left/Right step the selected algorithm (0..7, wrapping). The change is
 * applied live through MdxSynth_SetAlgorithm(); if a preview note is sounding the
 * engine retriggers so the new routing is audible. The routing diagram is rebuilt
 * from the topology data on every change.
 */

#include "mdx_algorithm_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_algorithm.h"


#define LIST_COL 1
#define DIAG_COL 14
#define LIST_ROW0 1          /* ALG 0 at row 1 */


/* Format one operator's routing line: "n T -> t1,t2" or "n C -> OUT". */
static void format_op_line(char* buf, u8 opIndex, const MdxAlgTopology* topo)
{
    u8 p = 0;
    u8 j;

    buf[p++] = (char)('1' + opIndex);
    buf[p++] = ' ';
    buf[p++] = (topo->carrierMask & (1u << opIndex)) ? 'C' : 'M';
    buf[p++] = ' ';
    buf[p++] = '-';
    buf[p++] = '>';
    buf[p++] = ' ';

    /* targets: operators that this op feeds */
    {
        u8 any = 0;
        for (j = 0; j < 4; j++)
        {
            if (topo->modulators[j] & (1u << opIndex))
            {
                if (any) buf[p++] = ',';
                buf[p++] = (char)('1' + j);
                any = 1;
            }
        }
        if (!any)
        {
            buf[p++] = 'O'; buf[p++] = 'U'; buf[p++] = 'T';
        }
    }
    buf[p] = '\0';
}


static void draw_list(u8 cur)
{
    u8 i;
    for (i = 0; i < 8; i++)
    {
        char line[8];
        line[0] = 'A'; line[1] = 'L'; line[2] = 'G'; line[3] = ' ';
        line[4] = (char)('0' + i);
        line[5] = '\0';
        MdxUi_DrawText(LIST_COL, LIST_ROW0 + i, line);
        (void)cur;
    }
}


static void draw_diagram(u8 algo)
{
    const MdxAlgTopology* topo = MdxAlgorithm_GetTopology(algo);
    char line[40];
    u8 i;

    /* header */
    MdxUi_ClearLine(1);
    {
        char hdr[22];
        u8 p = 0;
        const char* s = "ROUTING ALG";
        while (*s) hdr[p++] = *s++;
        hdr[p++] = ' ';
        hdr[p++] = (char)('0' + algo);
        hdr[p++] = '\0';
        MdxUi_DrawText(DIAG_COL, 1, hdr);
    }

    /* one line per operator */
    for (i = 0; i < 4; i++)
    {
        format_op_line(line, i, topo);
        MdxUi_DrawText(DIAG_COL, 2 + i, line);
    }

    /* feedback (S1 self-feedback) */
    {
        u8 fb = 0;
        FMPatch p;
        MdxSynth_GetPatch(&p);
        fb = p.feedback;
        MdxUi_ClearLine(6);
        MdxUi_DrawText(DIAG_COL, 6, "FB:");
        MdxUi_DrawNumber(DIAG_COL + 4, 6, fb, 1);
        if (fb > 0) MdxUi_DrawText(DIAG_COL + 6, 6, "S1");
    }
}


void MdxAlgorithmScreen_Enter(void)
{
    FMPatch p;
    MdxSynth_GetPatch(&p);

    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "ALGORITHM SELECT");

    draw_list(p.algorithm);
    draw_diagram(p.algorithm);

    MdxUi_ClearLine(8);
    MdxUi_DrawText(1, 8, "U/D/L/R:SEL  B:BACK  C:PLAY");

    MdxUi_MoveCursor(LIST_COL - 1, LIST_ROW0 + p.algorithm);
}


void MdxAlgorithmScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    FMPatch p;
    u8 algo;

    if (i == NULL) return;

    MdxSynth_GetPatch(&p);
    algo = p.algorithm;

    if (i->up || i->left)    algo = (algo == 0) ? 7 : (algo - 1);
    else if (i->down || i->right) algo = (algo + 1) & 7;
    else if (i->b) { MdxUi_SetScreen(SCREEN_MAIN); return; }
    else return;

    if (algo != p.algorithm)
    {
        MdxSynth_SetAlgorithm(algo);

        /* retrigger so the new routing is audible while a note is held */
        if (MdxSynth_IsKeyOn())
            MdxSynth_NoteOn(MdxUi_GetPreviewNote());

        draw_diagram(algo);
    }

    /* keep the cursor on the selected algorithm in the list */
    MdxUi_MoveCursor(LIST_COL - 1, LIST_ROW0 + algo);
}


void MdxAlgorithmScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_ALGORITHM, MdxAlgorithmScreen_Enter, MdxAlgorithmScreen_Handle);
}
