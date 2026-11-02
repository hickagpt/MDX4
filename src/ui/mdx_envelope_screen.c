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
 * (W3). The recessed LCD window drawn by MdxGfx_DrawReadout(BOX_*) in Enter is
 * the frame; this routine fills the interior (IX0..IX0+IW, IY0..IY0+IH) with
 * graph tiles. The curve is derived each redraw from s_op (the live operator copy). */
static void draw_graph(void)
{
    u8  attackW, decayW, sustainW, releaseW;
    u16 totalUnits;
    u16 attPx, decPx, susPx, relPx;   /* each stage's width in interior px  */
    u16 sustainPx;                    /* D1L level in interior px (up from bottom) */
    const u16 innerW = (u16)IW * 8;   /* 128 px  */
    const u16 innerH = (u16)IH * 8;   /* 48 px   */
    u16 x;
    u16 i;

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


/* Plate geometry: a single column of six recessed green-LCD readout plates
 * (MdxGfx_DrawReadout) to the LEFT of the graph window, inside PAGE_*. The
 * selected plate is framed by MdxGfx_DrawSelectedBox (IS-4). T14 renders the
 * ADSR curve into the LCD window established at BOX_X/BOX_Y/BOX_W/BOX_H. */
#define PLATE_X    2
#define PLATE_W    14
#define PLATE_H    3
#define PLATE_Y0   4           /* first plate top row (in PAGE_*)          */
#define PLATE_STEP 3           /* rows between plate tops                  */
#define plate_row(i)  ((u16)(PLATE_Y0 + (u16)(i) * PLATE_STEP))

/* One skin plate: a recessed LCD readout showing the param name + value. */
static void draw_plate(u8 idx)
{
    const u16 x = PLATE_X;
    const u16 y = plate_row(idx);

    MdxGfx_DrawReadout(x, y, PLATE_W, PLATE_H);
    MdxGfx_DrawReadoutStr((u16)(x + 1), (u16)(y + 1), s_param[idx].name);
    MdxGfx_DrawReadoutNum((u16)(x + 9), (u16)(y + 1), get_val(idx), 2);
}

static void draw_all_plates(void)
{
    u8 i;
    for (i = 0; i < PARAM_COUNT; i++) draw_plate(i);
}

/* IS-4 selection highlight: frame the active param plate (PAL2 SELECTED). */
static void draw_selection(void)
{
    MdxGfx_DrawSelectedBox(PLATE_X, plate_row(s_sel), PLATE_W, PLATE_H);
}


static void draw_header(void)
{
    char buf[20];
    u8 op = MdxUi_GetOperator();

    MdxUi_DrawText(PLATE_X, 2, "ENVELOPE EDIT");

    buf[0] = 'O'; buf[1] = 'P'; buf[2] = ' ';
    buf[3] = (char)('1' + op);
    buf[4] = '\0';
    MdxUi_DrawText(28, 2, buf);
}


void MdxEnvelopeScreen_Enter(void)
{
    FMPatch p;

    MdxGfx_Init();                  /* chrome palette + base tiles (idempotent) */
    MdxUi_ClearScreen();
    MdxGfx_DrawDeviceShell();       /* DX-7 x MD frame; content drawn in PAGE_* */

    MdxSynth_GetPatch(&p);
    s_op = p.operators[MdxUi_GetOperator()];
    s_sel = 0;

    draw_header();

    /* LCD window where T14 renders the ADSR curve (rect = BOX_*). */
    MdxGfx_DrawReadout(BOX_X, BOX_Y, BOX_W, BOX_H);

    draw_graph();                   /* legacy curve (T14 reworks into the window) */
    draw_all_plates();
    draw_selection();

    MdxUi_DrawText(2, 23, "U/D:SEL A:OP B:BACK C:PLAY");

    MdxUi_MoveCursor(PLATE_X, (u16)(plate_row(s_sel) + 1));
}


void MdxEnvelopeScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;
    u8 op;

    if (i == NULL) return;

    if (i->up || i->down)
    {
        const u8 old = s_sel;
        if (i->up)   { if (s_sel > 0)                  s_sel--; }
        if (i->down) { if (s_sel < (PARAM_COUNT - 1))  s_sel++; }
        if (s_sel != old)
        {
            draw_plate(old);          /* redraw old plate, dropping its highlight */
            draw_plate(s_sel);        /* redraw new plate */
            draw_selection();
        }
        MdxUi_MoveCursor(PLATE_X, (u16)(plate_row(s_sel) + 1));
    }

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
        draw_graph();
        draw_all_plates();
        draw_selection();
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
        draw_graph();
        draw_all_plates();
        draw_selection();
    }
}


void MdxEnvelopeScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_ENVELOPE, MdxEnvelopeScreen_Enter, MdxEnvelopeScreen_Handle);
}
