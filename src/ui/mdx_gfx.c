/*
 * mdx_gfx.c - MdxGfx tile-based 2D UI toolkit (BUILD WAVE 1, SPEC 4.1 / 9.1).
 *
 * Runtime-generated tiles (no assets) fed to VRAM through VDP_loadTileData, the
 * same mechanism the sprite cursor uses in mdx_ui.c. The toolkit knows nothing
 * about the engine, audio, patch model, storage, or the screen state machine; it
 * only places tiles into the BG_A tilemap and owns two palette slots.
 *
 * Tile pixel encoding (SGDK standard): an 8x8 tile is u32[8]; row py is tile[py],
 * and pixel px (0 = left) occupies the nibble at bit ((7 - px) * 4). MdxGfx_PlotTile
 * follows that convention so callers can compose curve/key tiles in C buffers.
 */

#include "mdx_gfx.h"

#include <genesis.h>
#include <pal.h>


/* --- Internal edge bitmask for box/border tile construction. --- */
#define EDGE_T  1   /* top    */
#define EDGE_R  2   /* right  */
#define EDGE_B  4   /* bottom */
#define EDGE_L  8   /* left   */

/* Tile attribute shorthand for this module's two palettes. */
#define CHR(idx)  TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (MDXGFX_TILE_BASE + (idx)))
#define KEY(idx)  TILE_ATTR_FULL(MDXGFX_PAL_KEYS,   0, FALSE, FALSE, (MDXGFX_TILE_BASE + (idx)))


/* ----------------------------------------------------------------------- */
/* Pixel / tile helpers.                                                    */
/* ----------------------------------------------------------------------- */

void MdxGfx_PlotTile(u32* tile, u8 px, u8 py, u8 colorIndex)
{
    u8 shift;

    if (px > 7) px = 7;
    if (py > 7) py = 7;

    shift = (u8)((7 - px) * 4);
    tile[py] &= ~((u32)0xF << shift);
    tile[py] |=  ((u32)(colorIndex & 0xF)) << shift;
}


/* Build an 8x8 box: pixels on any requested edge become border colour (1);
 * all other pixels become 'interior'. Used for fill, border and bar tiles. */
static void make_box(u32* t, u8 edges, u8 interior)
{
    u8 px, py;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            u8 border = 0;

            if ((edges & EDGE_T) && (py == 0)) border = 1;
            if ((edges & EDGE_B) && (py == 7)) border = 1;
            if ((edges & EDGE_L) && (px == 0)) border = 1;
            if ((edges & EDGE_R) && (px == 7)) border = 1;

            MdxGfx_PlotTile(t, px, py, border ? 1 : interior);
        }
    }
}


/* Build a black-key tile: a narrow raised block (middle columns) with a 1px
 * outline, side columns left as background so adjacent keys read as separate. */
static void make_key_black(u32* t)
{
    u8 px, py;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            u8 c;
            if (px == 0 || px == 7)               c = 1;   /* side outline   */
            else if (px >= 2 && px <= 5)          c = 3;   /* black-key body */
            else                                   c = 0;   /* gap            */
            MdxGfx_PlotTile(t, px, py, c);
        }
    }
}


/* ----------------------------------------------------------------------- */
/* Init: palette + base tiles.                                             */
/* ----------------------------------------------------------------------- */

void MdxGfx_Init(void)
{
    u32 t[8];
    const u16 base = MDXGFX_TILE_BASE;

    /* Palette: PAL2 = UI chrome, PAL3 = keyboard keys. These are distinct from
     * PAL0 (SGDK text font) and PAL1 (sprite cursor); the engine owns no slot. */
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + 0), 0x0000); /* 0: background / transparent */
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + 1), 0x0EEE); /* 1: border (bright)          */
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + 2), 0x0444); /* 2: panel fill (dim)         */
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + 3), 0x00E0); /* 3: bar fill                 */

    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + 0), 0x0000);   /* 0: gap / background         */
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + 1), 0x0444);   /* 1: key outline              */
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + 2), 0x0EEE);   /* 2: white key fill           */
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + 3), 0x0222);   /* 3: black key fill           */

    /* FILL: a solid block of panel-fill colour. */
    make_box(t, 0, 2);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_FILL), 1, DMA);

    /* Border corner / edge tiles (interior stays panel-fill so it merges). */
    make_box(t, EDGE_T | EDGE_L, 2); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_TL), 1, DMA);
    make_box(t, EDGE_T,         2);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_T),  1, DMA);
    make_box(t, EDGE_T | EDGE_R, 2); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_TR), 1, DMA);
    make_box(t, EDGE_L,         2);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_L),  1, DMA);
    make_box(t, EDGE_R,         2);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_R),  1, DMA);
    make_box(t, EDGE_B | EDGE_L, 2); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_BL), 1, DMA);
    make_box(t, EDGE_B,         2);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_B),  1, DMA);
    make_box(t, EDGE_B | EDGE_R, 2); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_BR), 1, DMA);

    /* Bar segments: outlined box, hollow (0) vs filled (3). */
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, 0);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BAR_EMPTY), 1, DMA);
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, 3);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BAR_FULL), 1, DMA);

    /* Keyboard keys (Wave 4). Drawn with PAL3. */
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, 2);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_KEY_WHITE), 1, DMA);
    make_key_black(t);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_KEY_BLACK), 1, DMA);
}


/* ----------------------------------------------------------------------- */
/* Panel.                                                                  */
/* ----------------------------------------------------------------------- */

void MdxGfx_DrawPanel(u16 x, u16 y, u16 w, u16 h)
{
    if ((w < 2) || (h < 2)) return;

    /* Interior fill. */
    VDP_fillTileMapRect(BG_A, CHR(MDXGFX_TILE_FILL), x + 1, y + 1, w - 2, h - 2);

    /* Corners. */
    VDP_setTileMapXY(BG_A, CHR(MDXGFX_TILE_BORDER_TL), x,         y);
    VDP_setTileMapXY(BG_A, CHR(MDXGFX_TILE_BORDER_TR), x + w - 1, y);
    VDP_setTileMapXY(BG_A, CHR(MDXGFX_TILE_BORDER_BL), x,         y + h - 1);
    VDP_setTileMapXY(BG_A, CHR(MDXGFX_TILE_BORDER_BR), x + w - 1, y + h - 1);

    /* Top / bottom edge runs. */
    if (w > 2)
    {
        VDP_fillTileMapRect(BG_A, CHR(MDXGFX_TILE_BORDER_T), x + 1,     y,         w - 2, 1);
        VDP_fillTileMapRect(BG_A, CHR(MDXGFX_TILE_BORDER_B), x + 1,     y + h - 1, w - 2, 1);
    }

    /* Left / right edge runs. */
    if (h > 2)
    {
        VDP_fillTileMapRect(BG_A, CHR(MDXGFX_TILE_BORDER_L), x,     y + 1,     1, h - 2);
        VDP_fillTileMapRect(BG_A, CHR(MDXGFX_TILE_BORDER_R), x + w - 1, y + 1, 1, h - 2);
    }
}


/* ----------------------------------------------------------------------- */
/* Bars.                                                                   */
/* ----------------------------------------------------------------------- */

/* Map value/max onto a filled-segment count (clamped to len). */
static u16 bar_filled(u16 len, u16 value, u16 max)
{
    u16 filled;
    if (max == 0) return 0;
    filled = (u16)(((u32)value * (u32)len) / (u32)max);
    if (filled > len) filled = len;
    return filled;
}

void MdxGfx_DrawHBar(u16 x, u16 y, u16 len, u16 value, u16 max)
{
    u16 i;
    const u16 filled = bar_filled(len, value, max);

    if (len == 0) return;

    for (i = 0; i < len; i++)
    {
        const u16 idx = (i < filled) ? MDXGFX_TILE_BAR_FULL : MDXGFX_TILE_BAR_EMPTY;
        VDP_setTileMapXY(BG_A, CHR(idx), x + i, y);
    }
}

void MdxGfx_DrawVBar(u16 x, u16 y, u16 len, u16 value, u16 max)
{
    u16 i;
    const u16 filled = bar_filled(len, value, max);

    if (len == 0) return;

    /* i == 0 is the bottom-most segment; the bar fills upward. */
    for (i = 0; i < len; i++)
    {
        const u16 row = (u16)(y + len - 1 - i);
        const u16 idx = (i < filled) ? MDXGFX_TILE_BAR_FULL : MDXGFX_TILE_BAR_EMPTY;
        VDP_setTileMapXY(BG_A, CHR(idx), x, row);
    }
}


/* ----------------------------------------------------------------------- */
/* Keyboard key tile index.                                                */
/* ----------------------------------------------------------------------- */

u16 MdxGfx_KeyTile(MdxGfxKeyType type)
{
    const MdxGfxTile idx = (type == MDXGFX_KEY_BLACK)
        ? MDXGFX_TILE_KEY_BLACK
        : MDXGFX_TILE_KEY_WHITE;
    return (u16)(MDXGFX_TILE_BASE + idx);
}
