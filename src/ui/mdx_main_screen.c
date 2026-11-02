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
#include "ui/mdx_gfx.h"   /* MdxGfx toolkit: Init, PlotTile, PAL2 chrome, base tiles */

/* snapshot of current patch (refreshed on Enter), selection + key status.
 * Declared before the mini-diagram helpers so draw_mini_diagram() can read s_patch. */
static FMPatch s_patch;
static u8      s_sel   = 0;
static u8      s_keyOn = 0;


/* ---------------------------------------------------------------------------
 * Mini routing diagram (W5): compact operator stack S1(top)..S4(bottom),
 * derived purely from MdxAlgorithm_GetTopology(s_patch.algorithm). Carrier
 * interior = PAL2 colour 3 (bright); modulator interior = PAL2 colour 2 (dim);
 * the current operator (MdxUi_GetOperator()) is highlighted (colour 1).
 *
 * Runtime-generated tiles (no assets). All indices >= 0x0196 so they never
 * collide with the font (0..~0x60), cursor (0x0100), base tiles (0x0120..0x012C),
 * W2 (0x0130..0x0135) or W3 (0x0136..0x0195). Used range: 0x0196 .. 0x019B.
 *   0x0196 MINI_MODFILL  modulator interior (PAL2 colour 2, dim)
 *   0x0197 MINI_CARFILL  carrier interior   (PAL2 colour 3, bright)
 *   0x0198 MINI_SELFILL  current-op highlight (PAL2 colour 1, bright)
 *   0x0199 MINI_VLINE    vertical link segment (PAL2 colour 1)
 *   0x019A MINI_HSTUB    horizontal link stub  (PAL2 colour 1)
 *   0x019B MINI_BLANK    transparent clear/erase (PAL2 colour 0)
 * ------------------------------------------------------------------------- */
#define MINI_MODFILL   0x0196
#define MINI_CARFILL   0x0197
#define MINI_SELFILL   0x0198
#define MINI_VLINE     0x0199
#define MINI_HSTUB     0x019A
#define MINI_BLANK     0x019B

#define MINI_NODE_W      6
#define MINI_NODE_H      3
#define MINI_NODE_LEFT   9
#define MINI_TOP0        12
#define MINI_STEP        4
#define MINI_LANE_BASE   4
#define MINI_X0          1
#define MINI_Y0          11
#define MINI_W           18
#define MINI_H           16

/* LCD window that frames the routing diagram: a recessed green plate drawn with
 * MdxGfx_DrawReadout (PAL2 RD_* tiles). 1 tile taller than the diagram so the
 * 4th operator cell (S4, rows 24..26) stays inside the plate's interior. The
 * W5 tiles 0x0196..0x019B are the routing's own range and are NOT relocated. */
#define MINI_WIN_X       MINI_X0
#define MINI_WIN_Y       MINI_Y0
#define MINI_WIN_W       MINI_W
#define MINI_WIN_H       (MINI_H + 1)

static bool s_miniTilesReady = FALSE;

#define MINI_CHR(idx)  TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (idx))
#define MINI_BORD(idx) TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (MDXGFX_TILE_BASE + (idx)))


/* --- Mini-diagram tile generation (runtime, once). --- */

static void mini_gen_tiles(void)
{
    u32 t[8];
    u8 a, b;

    if (s_miniTilesReady) return;
    s_miniTilesReady = TRUE;

    /* MODFILL: solid dim colour 2. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) for (b = 0; b < 8; b++) MdxGfx_PlotTile(t, b, a, 2);
    VDP_loadTileData(t, MINI_MODFILL, 1, DMA);

    /* CARFILL: solid bright colour 3. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) for (b = 0; b < 8; b++) MdxGfx_PlotTile(t, b, a, 3);
    VDP_loadTileData(t, MINI_CARFILL, 1, DMA);

    /* SELFILL: solid bright colour 1 (current-op highlight). */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) for (b = 0; b < 8; b++) MdxGfx_PlotTile(t, b, a, 1);
    VDP_loadTileData(t, MINI_SELFILL, 1, DMA);

    /* VLINE: 2px vertical line (cols 3-4), colour 1. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) { MdxGfx_PlotTile(t, 3, a, 1); MdxGfx_PlotTile(t, 4, a, 1); }
    VDP_loadTileData(t, MINI_VLINE, 1, DMA);

    /* HSTUB: 2px horizontal line (rows 3-4), colour 1. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) { MdxGfx_PlotTile(t, a, 3, 1); MdxGfx_PlotTile(t, a, 4, 1); }
    VDP_loadTileData(t, MINI_HSTUB, 1, DMA);

    /* BLANK: all transparent colour 0 (already zeroed). */
    for (a = 0; a < 8; a++) t[a] = 0;
    VDP_loadTileData(t, MINI_BLANK, 1, DMA);
}

/* Bordered box: bright border (reusing MdxGfx base border tiles) + solid interior. */
static void mini_draw_box(u16 x, u16 y, u16 w, u16 h, u16 fill)
{
    if ((w < 2) || (h < 2)) return;

    VDP_fillTileMapRect(BG_A, MINI_CHR(fill), x + 1, y + 1, w - 2, h - 2);

    VDP_setTileMapXY(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_TL), x,         y);
    VDP_setTileMapXY(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_TR), x + w - 1, y);
    VDP_setTileMapXY(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_BL), x,         y + h - 1);
    VDP_setTileMapXY(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_BR), x + w - 1, y + h - 1);

    if (w > 2)
    {
        VDP_fillTileMapRect(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_T), x + 1,     y,         w - 2, 1);
        VDP_fillTileMapRect(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_B), x + 1,     y + h - 1, w - 2, 1);
    }
    if (h > 2)
    {
        VDP_fillTileMapRect(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_L), x,     y + 1,     1, h - 2);
        VDP_fillTileMapRect(BG_A, MINI_BORD(MDXGFX_TILE_BORDER_R), x + w - 1, y + 1, 1, h - 2);
    }
}

/* Mini routing diagram: 4 operator cells S1(top)..S4(bottom) with carrier/
 * modulator colouring and the current operator highlighted, plus modulation
 * links derived from the topology. */
static void draw_mini_diagram(void)
{
    const MdxAlgTopology* topo = MdxAlgorithm_GetTopology(s_patch.algorithm);
    const u8 cur = MdxUi_GetOperator();
    const u16 top[4] = { MINI_TOP0, MINI_TOP0 + MINI_STEP, MINI_TOP0 + 2 * MINI_STEP, MINI_TOP0 + 3 * MINI_STEP };
    u8 op, i, j;
    char lbl[3];

    /* LCD window (recessed green plate) framing the routing diagram. This also
     * replaces the old transparent clear: the plate fills the area so the routing
     * sits on a continuous LCD field. The W5 tiles 0x0196..0x019B (MODFILL /
     * CARFILL / SELFILL / VLINE / HSTUB) still draw on top below, unchanged, and
     * MINI_BLANK stays loaded in mini_gen_tiles (the range is preserved, not
     * relocated). Topology derivation (GetTopology / carrierMask / modulators /
     * current-op) is untouched. */
    MdxGfx_DrawReadout(MINI_WIN_X, MINI_WIN_Y, MINI_WIN_W, MINI_WIN_H);

    MdxUi_DrawText(2, (u16)(MINI_WIN_Y + 1), "ROUTING");

    /* links first (under the nodes) */
    for (i = 1; i < 4; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (topo->modulators[i] & (1u << j))
            {
                const u16 lane   = (u16)(MINI_LANE_BASE + j);
                const u16 srcBot = (u16)(top[j] + MINI_NODE_H - 1);
                const u16 dstTop = top[i];
                u16 c, r;

                for (c = lane; c < MINI_NODE_LEFT; c++)
                    VDP_setTileMapXY(BG_A, MINI_CHR(MINI_HSTUB), c, srcBot);
                for (r = (u16)(srcBot + 1); r < dstTop; r++)
                    VDP_setTileMapXY(BG_A, MINI_CHR(MINI_VLINE), lane, r);
                for (c = lane; c < MINI_NODE_LEFT; c++)
                    VDP_setTileMapXY(BG_A, MINI_CHR(MINI_HSTUB), c, dstTop);
            }
        }
    }

    /* nodes: carrier/modulator colouring, current operator highlighted */
    for (op = 0; op < 4; op++)
    {
        const u8  carrier = (topo->carrierMask & (1u << op)) ? 1 : 0;
        const u16 fill    = (op == cur) ? MINI_SELFILL : (carrier ? MINI_CARFILL : MINI_MODFILL);

        mini_draw_box(MINI_NODE_LEFT, top[op], MINI_NODE_W, MINI_NODE_H, fill);
        lbl[0] = 'S'; lbl[1] = (char)('1' + op); lbl[2] = '\0';
        MdxUi_DrawText(MINI_NODE_LEFT + 2, (u16)(top[op] + 1), lbl);
    }
}


/* Selectable rows on this screen. */
#define ROW_ALGORITHM  2
#define ROW_OPERATOR   3
#define ROW_ENVELOPE   5
#define ROW_MODULATION 6
#define ROW_PATCH      8
#define ROW_PERFORMANCE 9

/* sel index -> cursor row. */
static const u8 s_row[6] = { ROW_ALGORITHM, ROW_OPERATOR, ROW_ENVELOPE, ROW_MODULATION, ROW_PATCH, ROW_PERFORMANCE };


/* --- per-row redraw helpers (paint the skin plate, then draw on top). ---
 *
 * Screen-local layout (inside PAGE_*): the device shell paints a charcoal bezel
 * + DX-Green panel once on Enter; every row below renders INSIDE that panel
 * (cols 2..37, rows 2..25). The three primary fields (NAME / ALGORITHM /
 * OPERATOR) are shown on green-LCD readout plates (MdxGfx_DrawReadout, PAL2).
 * Row labels and the <A> control hints are restyled onto green skin plates
 * (MdxGfx_DrawPanel, PAL2) with the SGDK text font (PAL0) drawn on top.
 * Plates are 2 tiles tall and stacked on consecutive rows, so their shared edge
 * reads as a divider. Each helper repaints only its own region (no full redraw). */

static void draw_name(void)
{
    /* Patch NAME: recessed green-LCD readout plate (right of the top row). */
    MdxGfx_DrawReadout(24, ROW_ALGORITHM, 14, 2);
    MdxGfx_DrawReadoutStr(26, ROW_ALGORITHM, s_patch.name);
}

static void draw_algorithm(void)
{
    MdxGfx_DrawPanel(2, ROW_ALGORITHM, 11, 2);
    MdxUi_DrawText(3, ROW_ALGORITHM, "ALGORITHM");
    MdxGfx_DrawReadout(13, ROW_ALGORITHM, 5, 2);
    MdxGfx_DrawReadoutNum(14, ROW_ALGORITHM, s_patch.algorithm, 2);
    MdxGfx_DrawPanel(18, ROW_ALGORITHM, 6, 2);
    MdxUi_DrawText(19, ROW_ALGORITHM, "<A>");
}

static void draw_operator(void)
{
    MdxGfx_DrawPanel(2, ROW_OPERATOR, 11, 2);
    MdxUi_DrawText(3, ROW_OPERATOR, "OPERATOR");
    MdxGfx_DrawReadout(13, ROW_OPERATOR, 5, 2);
    MdxGfx_DrawReadoutNum(14, ROW_OPERATOR, (u16)(MdxUi_GetOperator() + 1), 1);
    MdxGfx_DrawPanel(18, ROW_OPERATOR, 6, 2);
    MdxUi_DrawText(19, ROW_OPERATOR, "<A>");
}

static void draw_op_params(void)
{
    const FMOperator* op = &s_patch.operators[MdxUi_GetOperator()];
    MdxGfx_DrawPanel(2, 4, 30, 2);
    /* MUL : rotary knob (0..15) */
    MdxUi_DrawText(3, 4, "MUL");
    MdxGfx_DrawKnob(7, 4, op->multiplier, 15, FALSE);
    MdxUi_DrawNumber(9, 4, op->multiplier, 2);
    /* DT : rotary knob (0..7) */
    MdxUi_DrawText(13, 4, "DT");
    MdxGfx_DrawKnob(16, 4, op->detune, 7, FALSE);
    MdxUi_DrawNumber(18, 4, op->detune, 1);
    /* TL : horizontal fader (0..127) */
    MdxUi_DrawText(20, 4, "TL");
    MdxGfx_DrawFaderH(23, 4, 5, op->totalLevel, 127, FALSE);
    MdxUi_DrawNumber(29, 4, op->totalLevel, 3);
}

static void draw_envelope(void)
{
    const FMOperator* op = &s_patch.operators[MdxUi_GetOperator()];
    MdxGfx_DrawPanel(2, ROW_ENVELOPE, 30, 2);
    MdxUi_DrawText(3, ROW_ENVELOPE, "AR");
    MdxUi_DrawNumber(6, ROW_ENVELOPE, op->attackRate, 2);
    MdxUi_DrawText(9, ROW_ENVELOPE, "D1");
    MdxUi_DrawNumber(12, ROW_ENVELOPE, op->decay1Rate, 2);
    MdxUi_DrawText(15, ROW_ENVELOPE, "SL");
    MdxUi_DrawNumber(18, ROW_ENVELOPE, op->sustainLevel, 2);
    MdxUi_DrawText(21, ROW_ENVELOPE, "RR");
    MdxUi_DrawNumber(24, ROW_ENVELOPE, op->releaseRate, 2);
    MdxGfx_DrawPanel(33, ROW_ENVELOPE, 5, 2);
    MdxUi_DrawText(34, ROW_ENVELOPE, "<A>");
}

static void draw_modulation(void)
{
    MdxGfx_DrawPanel(2, ROW_MODULATION, 30, 2);
    MdxUi_DrawText(3, ROW_MODULATION, "FB");
    MdxUi_DrawNumber(6, ROW_MODULATION, s_patch.feedback, 1);
    MdxUi_DrawText(9, ROW_MODULATION, "LFO");
    MdxUi_DrawNumber(13, ROW_MODULATION, s_patch.lfoEnabled, 1);
    MdxGfx_DrawPanel(33, ROW_MODULATION, 5, 2);
    MdxUi_DrawText(34, ROW_MODULATION, "<A>");
}

static void draw_patch(void)
{
    MdxGfx_DrawPanel(2, ROW_PATCH, 30, 2);
    MdxUi_DrawText(3, ROW_PATCH, "PATCH MANAGER");
    MdxGfx_DrawPanel(33, ROW_PATCH, 5, 2);
    MdxUi_DrawText(34, ROW_PATCH, "<A>");
}

static void draw_performance(void)
{
    MdxGfx_DrawPanel(2, ROW_PERFORMANCE, 30, 2);
    MdxUi_DrawText(3, ROW_PERFORMANCE, "PERFORMANCE");
    MdxGfx_DrawPanel(33, ROW_PERFORMANCE, 5, 2);
    MdxUi_DrawText(34, ROW_PERFORMANCE, "<A>");
}


static void draw_key(void)
{
    MdxGfx_DrawPanel(2, 7, 30, 2);
    MdxUi_DrawText(3, 7, "KEY:");
    MdxUi_DrawText(8, 7, s_keyOn ? "ON " : "-- ");
    MdxUi_DrawText(13, 7, "C=PLAY");
}


static void draw_control_hint(void)
{
    MdxGfx_DrawPanel(2, 10, 36, 2);
    MdxUi_DrawText(3, 10, "A:EDIT  B:BACK  C:PLAY  START:MENU");
}


/* --- entry / handling. --- */

void MdxMainScreen_Enter(void)
{
    /* full render once */
    MdxGfx_Init();          /* generate chrome palette + base/skin tiles
                               (idempotent + guarded). Main screen boots FIRST. */
    mini_gen_tiles();       /* generate the mini-diagram's own tiles (once) */
    MdxSynth_GetPatch(&s_patch);
    s_sel = 0;
    s_keyOn = MdxSynth_IsKeyOn() ? 1 : 0;

    /* Device shell: charcoal bezel + DX-Green panel. Drawn ONCE here, after the
     * screen clear and BEFORE any content, so every migrated screen lands inside
     * the same PAGE_* rectangle. MdxGfx_DrawDeviceShell() is a one-shot paint. */
    MdxUi_ClearScreen();
    MdxGfx_DrawDeviceShell();

    draw_name();            /* patch NAME readout (top-right LCD plate) */
    draw_algorithm();
    draw_operator();
    draw_op_params();
    draw_envelope();
    draw_modulation();
    draw_patch();
    draw_performance();
    draw_key();
    draw_control_hint();    /* bottom control-hint strip */
    draw_mini_diagram();    /* W5: mini routing diagram in the free region */

    MdxUi_MoveCursor(0, s_row[s_sel]);
}


void MdxMainScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    u8 op;

    if (i == NULL) return;

    /* selection */
    if (i->down) { if (s_sel < 5) s_sel++; MdxUi_MoveCursor(0, s_row[s_sel]); }
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
            case 4: MdxUi_SetScreen(SCREEN_PATCH);      return;
            case 5: MdxUi_SetScreen(SCREEN_PERFORMANCE); return;
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
                draw_mini_diagram();
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
                draw_mini_diagram();
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
