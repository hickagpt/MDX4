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
#include "mdx_gfx.h"          /* MdxGfx toolkit: PlotTile, PAL2 chrome, border tiles */


#define ROW0       1           /* first parameter row                       */
#define PARAM_COUNT 6

/* Graph box geometry (text cells / tiles). */
#define BOX_X    16
#define BOX_Y    9
#define BOX_W    18
#define BOX_H    8
#define IX0      (BOX_X + 1)   /* inside left   */
#define IY0      (BOX_Y + 1)   /* inside top    */
#define IW       (BOX_W - 2)
#define IH       (BOX_H - 2)
#define MAX_AMP  (IH - 1)

/* Graph-interior tile VRAM range: IW*IH = 16*6 = 96 tiles, starting at 0x0136.
 * W2 owns 0x0130..0x0135; font 0..~0x60; cursor 0x0100; base tiles 0x0120..0x012C.
 * Range used: 0x0136 .. 0x0195 (inclusive). */
#define GRAPH_TILE_BASE   0x0136
#define GRAPH_TILE_COUNT  (IW * IH)   /* 96 */

/* RAM backing buffer: one 8x8 tile image (u32[8]) per interior cell. Filled
 * with PAL2 colour 2 (dim, 0x0444) as the interior background; the curve is
 * plotted in PAL2 colour 1 (0x0EEE, bright) on top. */
static u32 s_gtiles[GRAPH_TILE_COUNT][8];


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


/* ADSR-style approximation: amp 0..(innerH-1), time across the 128px-wide
 * interior. Rendered as a pixel-plotted curve in procedurally generated tiles
 * (W3): a 1px MdxGfx border frame + an interior filled with graph tiles.
 * The curve is derived each redraw from s_op (the live operator copy). */
static void draw_graph(void)
{
    u8  r;
    u8  attackW, decayW, sustainW, releaseW;
    u16 totalUnits;
    u16 attPx, decPx, susPx, relPx;   /* each stage's width in interior px  */
    u16 sustainPx;                    /* D1L level in interior px (up from bottom) */
    const u16 innerW = (u16)IW * 8;   /* 128 px  */
    const u16 innerH = (u16)IH * 8;   /* 48 px   */
    u16 x;
    u16 i;

    /* clear + redraw the box (no overlap with the param list rows) */
    for (r = BOX_Y; r < (BOX_Y + BOX_H); r++) VDP_clearTextLine(r);

    /* 1px border frame using the MdxGfx border tiles. */
    {
        const u16 b  = MDXGFX_TILE_BASE;
        const u16 tl = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_TL));
        const u16 tr = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_TR));
        const u16 bl = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_BL));
        const u16 br = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_BR));
        const u16 t  = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_T));
        const u16 bt = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_B));
        const u16 l  = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_L));
        const u16 rt = TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (u16)(b + MDXGFX_TILE_BORDER_R));

        VDP_setTileMapXY(BG_A, tl, BOX_X, BOX_Y);
        VDP_setTileMapXY(BG_A, tr, (u16)(BOX_X + BOX_W - 1), BOX_Y);
        VDP_setTileMapXY(BG_A, bl, BOX_X, (u16)(BOX_Y + BOX_H - 1));
        VDP_setTileMapXY(BG_A, br, (u16)(BOX_X + BOX_W - 1), (u16)(BOX_Y + BOX_H - 1));
        if (BOX_W > 2)
        {
            VDP_fillTileMapRect(BG_A, t,  (u16)(BOX_X + 1), BOX_Y,         (u16)(BOX_W - 2), 1);
            VDP_fillTileMapRect(BG_A, bt, (u16)(BOX_X + 1), (u16)(BOX_Y + BOX_H - 1), (u16)(BOX_W - 2), 1);
        }
        if (BOX_H > 2)
        {
            VDP_fillTileMapRect(BG_A, l,  BOX_X, (u16)(BOX_Y + 1),     1, (u16)(BOX_H - 2));
            VDP_fillTileMapRect(BG_A, rt, (u16)(BOX_X + BOX_W - 1), (u16)(BOX_Y + 1), 1, (u16)(BOX_H - 2));
        }
    }

    /* Stage widths in 'units' (the existing approximation math). decay2Rate now
     * drives sustainW, mirroring how decay1Rate drives decayW (was fixed 3). */
    attackW   = (u8)(1 + ((31 - s_op.attackRate)  * 3) / 31);
    decayW    = (u8)(1 + ((31 - s_op.decay1Rate)  * 3) / 31);
    sustainW  = (u8)(1 + ((31 - s_op.decay2Rate)  * 3) / 31);
    releaseW  = (u8)(1 + ((31 - s_op.releaseRate) * 3) / 31);
    totalUnits = (u16)attackW + decayW + sustainW + releaseW;

    /* Map stage units proportionally onto the 128px-wide interior. */
    attPx = (u16)((u32)attackW   * innerW / totalUnits);
    decPx = (u16)((u32)decayW   * innerW / totalUnits);
    susPx = (u16)((u32)sustainW * innerW / totalUnits);
    relPx = (u16)((u32)releaseW * innerW / totalUnits);

    /* D1L: 0 = no attenuation (sustain at peak), 15 = near silence. sustainPx
     * is the sustain amplitude measured up from the bottom of the interior. */
    sustainPx = (u16)((u32)(15 - s_op.sustainLevel) * (innerH - 1) / 15);

    /* Clear the interior backing buffer to the dim (colour 2) background. */
    for (i = 0; i < GRAPH_TILE_COUNT; i++)
    {
        u8 k;
        for (k = 0; k < 8; k++) s_gtiles[i][k] = 0x22222222;
    }

    /* Plot the curve: for each interior pixel column, compute the amplitude with
     * the same attack/decay/sustain/release ramp as before, then plot a bright
     * (colour 1) pixel into the right tile. Amp 0 sits at the bottom row. */
    for (x = 0; x < innerW; x++)
    {
        u16 ampPx;
        u16 yp;
        u8  c, row, px, py;
        u32* tile;

        if (x < attPx)
        {
            ampPx = (u16)((u32)x * (innerH - 1) / attPx);
        }
        else if (x < (u16)(attPx + decPx))
        {
            const u16 l = (u16)(x - attPx);
            ampPx = (u16)((innerH - 1) - (u32)((innerH - 1 - sustainPx) * (u32)l) / decPx);
        }
        else if (x < (u16)(attPx + decPx + susPx))
        {
            ampPx = sustainPx;
        }
        else
        {
            const u16 l = (u16)(x - (attPx + decPx + susPx));
            ampPx = (u16)((u32)sustainPx * (relPx - l) / relPx);
        }

        yp   = (u16)((innerH - 1) - ampPx);   /* amp 0 at bottom -> y = innerH-1 */

        c    = (u8)(x / 8);
        row  = (u8)(yp / 8);
        px   = (u8)(x % 8);
        py   = (u8)(yp % 8);
        tile = s_gtiles[(u16)row * IW + c];

        /* bright curve; thicken slightly by also plotting the pixel above */
        MdxGfx_PlotTile(tile, px, py, 1);
        if (py > 0) MdxGfx_PlotTile(tile, px, (u8)(py - 1), 1);
    }

    /* Upload the interior tiles to VRAM (0x0136..) and map the tilemap. */
    for (i = 0; i < GRAPH_TILE_COUNT; i++)
    {
        const u8  row = (u8)(i / IW);
        const u8  col = (u8)(i % IW);
        const u16 idx = (u16)(GRAPH_TILE_BASE + i);

        VDP_loadTileData(s_gtiles[i], idx, 1, DMA);
        VDP_setTileMapXY(BG_A,
                         TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, idx),
                         (u16)(IX0 + col), (u16)(IY0 + row));
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

    MdxGfx_Init();          /* generate chrome palette + base tiles (idempotent) */

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
