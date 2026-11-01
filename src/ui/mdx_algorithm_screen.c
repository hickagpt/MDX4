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
#include "mdx_gfx.h"          /* MdxGfx toolkit: PlotTile, PAL2 chrome, base tiles */
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_algorithm.h"


#define LIST_COL 1
#define DIAG_COL 14
#define LIST_ROW0 1          /* ALG 0 at row 1 */

/* ---------------------------------------------------------------------------
 * Graphical routing diagram layout (W2, SPEC 3.3 / 4.4).
 *
 * The diagram sits to the right of the ALG 0-7 list (cols 14..38). Four
 * operator NODES (S1 top .. S4 bottom) are stacked vertically; modulation
 * LINKS run straight DOWN because of the topology invariant (every link has
 * source index < target index). The picture is derived purely from
 * MdxAlgorithm_GetTopology() - never hardcoded per algorithm.
 *
 * Carrier / modulator colour coding (PAL2 chrome palette, set by MdxGfx_Init):
 *   border    = colour 1 (0x0EEE, bright) - node outline + link lines + frame
 *   carrier   = colour 3 (0x00E0, bright) - interior of CARRIER nodes
 *   modulator = colour 2 (0x0444, dim)    - interior of MODULATOR nodes
 * Carriers are the audible outputs of the current algorithm, so they read
 * brighter than the dim modulator nodes.
 * ------------------------------------------------------------------------- */
#define DIAG_FRAME_X 14
#define DIAG_FRAME_Y 2
#define DIAG_FRAME_W 16      /* encloses link lanes + nodes: cols 14..29 */
#define DIAG_FRAME_H 25      /* rows 2..26 */

#define NODE_W 8
#define NODE_H 5
#define NODE_LEFT 21         /* node columns 21..28 */
#define NODE_TOP0 3
#define NODE_STEP 6          /* S1..S4 tops: 3, 9, 15, 21 (5-tall, 1-row gap) */
#define LANE_BASE 15         /* link from source j -> lane column 15+j (left of nodes) */
#define LEGEND_COL 30        /* per-operator routing text, right of the frame */

/* ---------------------------------------------------------------------------
 * Runtime-generated tiles (no assets). All indices >= 0x0130 so they never
 * collide with the font (0..~0x60), the sprite cursor (0x0100), or the MdxGfx
 * base tiles (0x0120..0x012C). Used range: 0x0130 .. 0x0135.
 *   0x0130 VLINE    vertical link segment      (PAL2 colour 1, bright)
 *   0x0131 HSTUB    horizontal link stub       (PAL2 colour 1)
 *   0x0132 MODFILL  modulator interior fill    (PAL2 colour 2, dim)
 *   0x0133 CARFILL  carrier interior fill      (PAL2 colour 3, bright)
 *   0x0134 SELFILL  selection highlight fill   (PAL2 colour 1, bright)
 *   0x0135 BLANK    transparent (PAL2 colour 0) - background / clear / erase
 * ------------------------------------------------------------------------- */
#define ALG_TILE_VLINE   0x0130
#define ALG_TILE_HSTUB   0x0131
#define ALG_TILE_MODFILL 0x0132
#define ALG_TILE_CARFILL 0x0133
#define ALG_TILE_SELFILL 0x0134
#define ALG_TILE_BLANK   0x0135


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


/* --- Tile generation (runtime, once). See VRAM range note above. --- */

static bool s_tilesReady = FALSE;

/* Attribute for our PAL2 chrome tiles. */
#define ALG_CHR(idx)  TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (idx))
/* Attribute for the MdxGfx base border tiles (already in VRAM). */
#define ALG_BORD(idx) TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (MDXGFX_TILE_BASE + (idx)))

static const u16 s_nodeTop[4] =
{
    NODE_TOP0,
    (u16)(NODE_TOP0 + NODE_STEP),
    (u16)(NODE_TOP0 + 2 * NODE_STEP),
    (u16)(NODE_TOP0 + 3 * NODE_STEP)
};

static void alg_gen_tiles(void)
{
    u32 t[8];
    u8 a, b;

    if (s_tilesReady) return;
    s_tilesReady = TRUE;

    /* VLINE: 2px vertical line (cols 3-4), bright colour 1. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) { MdxGfx_PlotTile(t, 3, a, 1); MdxGfx_PlotTile(t, 4, a, 1); }
    VDP_loadTileData(t, ALG_TILE_VLINE, 1, DMA);

    /* HSTUB: 2px horizontal line (rows 3-4), bright colour 1. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) { MdxGfx_PlotTile(t, a, 3, 1); MdxGfx_PlotTile(t, a, 4, 1); }
    VDP_loadTileData(t, ALG_TILE_HSTUB, 1, DMA);

    /* MODFILL: solid dim colour 2. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) for (b = 0; b < 8; b++) MdxGfx_PlotTile(t, b, a, 2);
    VDP_loadTileData(t, ALG_TILE_MODFILL, 1, DMA);

    /* CARFILL: solid bright colour 3. */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) for (b = 0; b < 8; b++) MdxGfx_PlotTile(t, b, a, 3);
    VDP_loadTileData(t, ALG_TILE_CARFILL, 1, DMA);

    /* SELFILL: solid bright colour 1 (selection highlight). */
    for (a = 0; a < 8; a++) t[a] = 0;
    for (a = 0; a < 8; a++) for (b = 0; b < 8; b++) MdxGfx_PlotTile(t, b, a, 1);
    VDP_loadTileData(t, ALG_TILE_SELFILL, 1, DMA);

    /* BLANK: all transparent colour 0 (already zeroed). */
    for (a = 0; a < 8; a++) t[a] = 0;
    VDP_loadTileData(t, ALG_TILE_BLANK, 1, DMA);
}

/* Bordered box: bright border (reusing MdxGfx border tiles) + solid interior. */
static void alg_draw_box(u16 x, u16 y, u16 w, u16 h, u16 fillIdx)
{
    if ((w < 2) || (h < 2)) return;

    VDP_fillTileMapRect(BG_A, ALG_CHR(fillIdx), x + 1, y + 1, w - 2, h - 2);

    VDP_setTileMapXY(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_TL), x,         y);
    VDP_setTileMapXY(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_TR), x + w - 1, y);
    VDP_setTileMapXY(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_BL), x,         y + h - 1);
    VDP_setTileMapXY(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_BR), x + w - 1, y + h - 1);

    if (w > 2)
    {
        VDP_fillTileMapRect(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_T), x + 1,     y,         w - 2, 1);
        VDP_fillTileMapRect(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_B), x + 1,     y + h - 1, w - 2, 1);
    }
    if (h > 2)
    {
        VDP_fillTileMapRect(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_L), x,     y + 1,     1, h - 2);
        VDP_fillTileMapRect(BG_A, ALG_BORD(MDXGFX_TILE_BORDER_R), x + w - 1, y + 1, 1, h - 2);
    }
}

/* Four operator nodes, S1 (top) .. S4 (bottom); carrier/modulator colour-coded. */
static void alg_draw_nodes(const MdxAlgTopology* topo)
{
    u8 op;
    for (op = 0; op < 4; op++)
    {
        const u8 carrier = (topo->carrierMask & (1u << op)) ? 1 : 0;
        const u16 fill   = carrier ? ALG_TILE_CARFILL : ALG_TILE_MODFILL;
        char lbl[3];

        alg_draw_box(NODE_LEFT, s_nodeTop[op], NODE_W, NODE_H, fill);
        lbl[0] = 'S'; lbl[1] = (char)('1' + op); lbl[2] = '\0';
        MdxUi_DrawText(NODE_LEFT + 3, (u16)(s_nodeTop[op] + 1), lbl);
    }
}

/* Modulation links: for each target i and source j (j < i) with bit j set,
 * draw a downward L-route in lane (15 + j) from node j to node i. The
 * invariant guarantees j is always above i, so every link flows downward. */
static void alg_draw_links(const MdxAlgTopology* topo)
{
    u8 i, j;
    for (i = 1; i < 4; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (topo->modulators[i] & (1u << j))
            {
                const u16 lane      = (u16)(LANE_BASE + j);
                const u16 srcBottom = (u16)(s_nodeTop[j] + NODE_H - 1);
                const u16 dstTop    = s_nodeTop[i];
                u16 r, c;

                for (c = lane; c < NODE_LEFT; c++)
                    VDP_setTileMapXY(BG_A, ALG_CHR(ALG_TILE_HSTUB), c, srcBottom);
                for (r = (u16)(srcBottom + 1); r < dstTop; r++)
                    VDP_setTileMapXY(BG_A, ALG_CHR(ALG_TILE_VLINE), lane, r);
                for (c = lane; c < NODE_LEFT; c++)
                    VDP_setTileMapXY(BG_A, ALG_CHR(ALG_TILE_HSTUB), c, dstTop);
            }
        }
    }
}

/* Selection highlight: bright bar on the current ALG entry of the list
 * (rows 1..7 are clear of other text; row 8 holds the instructions). */
static void alg_draw_selection(u8 algo)
{
    VDP_fillTileMapRect(BG_A, ALG_CHR(ALG_TILE_BLANK),   8, 1, 6, 7);
    if (algo < 7)
        VDP_fillTileMapRect(BG_A, ALG_CHR(ALG_TILE_SELFILL), 8, (u16)(1 + algo), 5, 1);
}

static void draw_diagram(u8 algo)
{
    const MdxAlgTopology* topo = MdxAlgorithm_GetTopology(algo);
    char line[40];
    u8 i;
    FMPatch p;

    /* clear only the changed region: frame + legend */
    VDP_fillTileMapRect(BG_A, ALG_CHR(ALG_TILE_BLANK), DIAG_FRAME_X,   DIAG_FRAME_Y, DIAG_FRAME_W, DIAG_FRAME_H);
    VDP_fillTileMapRect(BG_A, ALG_CHR(ALG_TILE_BLANK), LEGEND_COL,     DIAG_FRAME_Y, (u16)(40 - LEGEND_COL), DIAG_FRAME_H);

    /* outer frame marks the active-algorithm view (bright border, black interior) */
    alg_draw_box(DIAG_FRAME_X, DIAG_FRAME_Y, DIAG_FRAME_W, DIAG_FRAME_H, ALG_TILE_BLANK);

    /* nodes + links, derived purely from the topology */
    alg_draw_nodes(topo);
    alg_draw_links(topo);

    /* header */
    {
        char hdr[22];
        u8 q = 0;
        const char* s = "ROUTING ALG";
        while (*s) hdr[q++] = *s++;
        hdr[q++] = ' ';
        hdr[q++] = (char)('0' + algo);
        hdr[q++] = '\0';
        MdxUi_DrawText(DIAG_COL, 1, hdr);
    }

    /* per-operator routing legend (right of the frame) */
    for (i = 0; i < 4; i++)
    {
        format_op_line(line, i, topo);
        MdxUi_DrawText(LEGEND_COL, s_nodeTop[i], line);
    }

    /* feedback (S1 self-feedback) */
    MdxSynth_GetPatch(&p);
    MdxUi_ClearLine(27);
    MdxUi_DrawText(DIAG_COL, 27, "FB:");
    MdxUi_DrawNumber(DIAG_COL + 4, 27, p.feedback, 1);
    if (p.feedback > 0) MdxUi_DrawText(DIAG_COL + 6, 27, "S1");
}


void MdxAlgorithmScreen_Enter(void)
{
    FMPatch p;
    MdxSynth_GetPatch(&p);

    MdxGfx_Init();          /* generate chrome palette + base tiles (idempotent) */
    alg_gen_tiles();        /* generate the diagram's procedural tiles (once) */

    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "ALGORITHM SELECT");

    draw_list(p.algorithm);
    draw_diagram(p.algorithm);
    alg_draw_selection(p.algorithm);

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
        alg_draw_selection(algo);
    }

    /* keep the cursor on the selected algorithm in the list */
    MdxUi_MoveCursor(LIST_COL - 1, LIST_ROW0 + algo);
}


void MdxAlgorithmScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_ALGORITHM, MdxAlgorithmScreen_Enter, MdxAlgorithmScreen_Handle);
}
