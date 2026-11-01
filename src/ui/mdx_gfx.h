#ifndef _MDX_GFX_H_
#define _MDX_GFX_H_

/*
 * mdx_gfx.h - MdxGfx tile-based 2D UI toolkit (BUILD WAVE 1, SPEC 4.1 / 9.1).
 *
 * A graphical layer that coexists with the existing text UI (VDP_drawText) and
 * the sprite selection cursor. Tiles are generated at runtime into VRAM from C
 * buffers (no assets), matching how the cursor tile is built in mdx_ui.c via
 * VDP_loadTileData(). This is the foundation later screens (algorithm routing
 * diagram, envelope curve, virtual keyboard) will render through.
 *
 * Rendering discipline: every draw is a one-shot placement into the BG_A tilemap.
 * Callers (screens) paint once on Enter and repaint only changed regions, same
 * as the text helpers (SPEC 553 / 9.1 "no unnecessary full-screen redraws").
 *
 * This module owns PAL2 (UI chrome) and PAL3 (keyboard keys). It never touches
 * the synth engine, audio driver, patch model, storage, or the screen state
 * machine.
 */

#include <genesis.h>


/* ----------------------------------------------------------------------- */
/* Tile index range (VRAM).                                                 */
/* ----------------------------------------------------------------------- */

/* The cursor tile lives at 0x0100 and the font occupies 0..~0x60. We start at
 * 0x0120 (288), clearly clear of both. 13 tiles are used (0x0120..0x012C). */
#define MDXGFX_TILE_BASE   0x0120


/* ----------------------------------------------------------------------- */
/* Palette slots owned by this module.                                      */
/* ----------------------------------------------------------------------- */

/* Distinct from PAL0 (SGDK text font) and PAL1 (sprite cursor). The engine
 * does not own any palette slot, so PAL2/PAL3 are free to claim. */
#define MDXGFX_PAL_CHROME  2   /* panel border / fill / bars          */
#define MDXGFX_PAL_KEYS    3   /* Wave-4 keyboard keys                */


/* ----------------------------------------------------------------------- */
/* Tile indices (relative to MDXGFX_TILE_BASE).                             */
/* ----------------------------------------------------------------------- */
typedef enum
{
    MDXGFX_TILE_FILL = 0,    /* solid interior fill                       */
    MDXGFX_TILE_BORDER_TL,   /* corner / edge tiles (1px border)         */
    MDXGFX_TILE_BORDER_T,
    MDXGFX_TILE_BORDER_TR,
    MDXGFX_TILE_BORDER_L,
    MDXGFX_TILE_BORDER_R,
    MDXGFX_TILE_BORDER_BL,
    MDXGFX_TILE_BORDER_B,
    MDXGFX_TILE_BORDER_BR,
    MDXGFX_TILE_BAR_EMPTY,   /* outlined, hollow segment                 */
    MDXGFX_TILE_BAR_FULL,    /* outlined, filled segment                 */
    MDXGFX_TILE_KEY_WHITE,   /* Wave-4 white key segment (PAL_KEYS)      */
    MDXGFX_TILE_KEY_BLACK,   /* Wave-4 black key segment (PAL_KEYS)      */
    MDXGFX_TILE_COUNT
} MdxGfxTile;


/* ----------------------------------------------------------------------- */
/* Keyboard key kinds (used by Wave 4).                                     */
/* ----------------------------------------------------------------------- */
typedef enum
{
    MDXGFX_KEY_WHITE = 0,
    MDXGFX_KEY_BLACK = 1
} MdxGfxKeyType;


/* ----------------------------------------------------------------------- */
/* API.                                                                     */
/* ----------------------------------------------------------------------- */

/* Initialise: allocate the two PAL slots and generate all base tiles into
 * VRAM. Call once after VDP_init() (the smoke test calls it from the main
 * screen Enter; production wiring should call it from MdxUi_Init()).
 * Idempotent: safe to call more than once. */
void MdxGfx_Init(void);

/* Draw a filled panel with a 1px border at tile (x,y) spanning (w,h) tiles.
 * An optional title may be drawn top-left via MdxUi_DrawText(x + 1, y). */
void MdxGfx_DrawPanel(u16 x, u16 y, u16 w, u16 h);

/* Segmented horizontal bar: 'len' segments, filled count = value / max. */
void MdxGfx_DrawHBar(u16 x, u16 y, u16 len, u16 value, u16 max);

/* Segmented vertical bar: 'len' segments, filled bottom-up from value / max. */
void MdxGfx_DrawVBar(u16 x, u16 y, u16 len, u16 value, u16 max);

/* Plot a single pixel (px,py in 0..7) into an 8x8 tile buffer (u32[8]).
 * colorIndex is a 0..15 palette index. Used to build curve/key tiles. */
void MdxGfx_PlotTile(u32* tile, u8 px, u8 py, u8 colorIndex);

/* Return the VRAM tile index for a keyboard key of the given type. Draw it
 * with MDXGFX_PAL_KEYS (PAL3). Wave 4 builds the on-screen keyboard from this. */
u16 MdxGfx_KeyTile(MdxGfxKeyType type);


#endif /* _MDX_GFX_H_ */
