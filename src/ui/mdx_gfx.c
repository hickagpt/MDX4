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

/* Readout tiles live at ABSOLUTE VRAM addresses in the >=0x019C tail (PAL2). */
#define RD(addr)  TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (addr))

/* Control widgets live at ABSOLUTE VRAM addresses in the >=0x019C tail (PAL3). */
#define CT(addr)  TILE_ATTR_FULL(MDXGFX_PAL_KEYS,   0, FALSE, FALSE, (addr))

/* Device shell lives at ABSOLUTE VRAM addresses in the >=0x019C tail (PAL2). */
#define SH(addr)  TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, (addr))


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

            /* Index 1 is each palette's edge/border slot (BORDER in PAL2,
             * KEY_EDGE in PAL3); interior is passed by the caller. */
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
            if (px == 0 || px == 7)               c = MDXGFX_IDX_KEY_EDGE;   /* side outline   */
            else if (px >= 2 && px <= 5)          c = MDXGFX_IDX_KEY_BLACK;  /* black-key body */
            else                                   c = MDXGFX_IDX_KEYS_BG;    /* gap            */
            MdxGfx_PlotTile(t, px, py, c);
        }
    }
}


/* ----------------------------------------------------------------------- */
/* Readout: recessed green-LCD plate + procedural green-LCD font (W0-T2).   */
/* All tiles live at absolute VRAM addresses 0x019C..0x01CD in PAL2 so the  */
/* plate and its text share one palette (spatially adjacent).               */
/* ----------------------------------------------------------------------- */

/* Recessed LCD bezel: top/left edges are the shadow (BORDER, near-black),
 * bottom/right edges catch the light (BODY_BEVEL); the tile interior is the
 * LCD field colour so edge tiles merge seamlessly with the filled window. */
static void make_lcd_frame(u32* t, u8 edges)
{
    u8 px, py;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            u8 c = MDXGFX_IDX_LCD_BG;                       /* field, merges */
            if ((edges & EDGE_T) && (py == 0)) c = MDXGFX_IDX_BORDER;        /* top shadow   */
            else if ((edges & EDGE_B) && (py == 7)) c = MDXGFX_IDX_BODY_BEVEL; /* bottom light */
            if ((edges & EDGE_L) && (px == 0)) c = MDXGFX_IDX_BORDER;        /* left shadow  */
            else if ((edges & EDGE_R) && (px == 7)) c = MDXGFX_IDX_BODY_BEVEL; /* right light  */
            MdxGfx_PlotTile(t, px, py, c);
        }
    }
}


/* 5x7 uppercase LCD font, rows top->bottom, columns left->right (low 5 bits;
 * bit4=col0 .. bit0=col4). Drives MdxGfx_DrawReadoutStr; one entry per glyph
 * in the order A-Z, 0-9, then '-','_','.','/', ':'. Unlit pixels of every
 * glyph tile are LCD_BG (dark green) with LCD_INK (bright green) for the lit
 * dots, so a glyph reads as green-on-green within the plate's field. */
static const u8 s_readout_font[41][7] =
{
    /* A-Z */
    {0x04,0x0E,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0F,0x10,0x10,0x13,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x11,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    /* 0-9 */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}, /* 9 */
    /* symbols: '-' '_' '.' ':' '/' */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* . */
    {0x00,0x04,0x04,0x00,0x00,0x04,0x04}, /* : */
    {0x01,0x02,0x04,0x04,0x08,0x10,0x10}  /* / */
};


/* Generate the readout tiles into VRAM once (idempotent: guarded). Mirrors
 * the W5-mini diagram's own-tile load. Safe to call from every screen's
 * MdxGfx_Init without re-uploading via DMA each time. */
static void mdxgfx_gen_readout(void)
{
    static bool ready = FALSE;
    u32 t[8];
    u8 i, r, c;

    if (ready) return;
    ready = TRUE;

    /* Frame: solid LCD field + 8 recessed bevel tiles. */
    for (i = 0; i < 8; i++) t[i] = 0;
    for (i = 0; i < 8; i++) for (c = 0; c < 8; c++) MdxGfx_PlotTile(t, c, i, MDXGFX_IDX_LCD_BG);
    VDP_loadTileData(t, MDXGFX_RD_FILL, 1, DMA);

    make_lcd_frame(t, EDGE_T | EDGE_L); VDP_loadTileData(t, MDXGFX_RD_TL, 1, DMA);
    make_lcd_frame(t, EDGE_T);          VDP_loadTileData(t, MDXGFX_RD_T,  1, DMA);
    make_lcd_frame(t, EDGE_T | EDGE_R); VDP_loadTileData(t, MDXGFX_RD_TR, 1, DMA);
    make_lcd_frame(t, EDGE_L);          VDP_loadTileData(t, MDXGFX_RD_L,  1, DMA);
    make_lcd_frame(t, EDGE_R);          VDP_loadTileData(t, MDXGFX_RD_R,  1, DMA);
    make_lcd_frame(t, EDGE_B | EDGE_L); VDP_loadTileData(t, MDXGFX_RD_BL, 1, DMA);
    make_lcd_frame(t, EDGE_B);          VDP_loadTileData(t, MDXGFX_RD_B,  1, DMA);
    make_lcd_frame(t, EDGE_B | EDGE_R); VDP_loadTileData(t, MDXGFX_RD_BR, 1, DMA);

    /* Font: 41 glyphs. Unlit pixels are LCD_BG so a glyph is green-on-green. */
    for (i = 0; i < 41; i++)
    {
        for (r = 0; r < 8; r++) t[r] = 0;
        for (r = 0; r < 8; r++) for (c = 0; c < 8; c++) MdxGfx_PlotTile(t, c, r, MDXGFX_IDX_LCD_BG);
        for (r = 0; r < 7; r++)
            for (c = 0; c < 5; c++)
                if (s_readout_font[i][r] & (u8)(1u << (4 - c)))
                    MdxGfx_PlotTile(t, (u8)(1 + c), (u8)r, MDXGFX_IDX_LCD_INK);
        VDP_loadTileData(t, (u16)(MDXGFX_RD_A + i), 1, DMA);
    }
}


/* Map a character to its readout glyph VRAM address; unknown/space -> blank
 * LCD field (RD_FILL) so the plate reads continuously. */
static u16 readout_glyph_vram(char c)
{
    if ((c >= 'A') && (c <= 'Z')) return (u16)(MDXGFX_RD_A + (c - 'A'));
    if ((c >= '0') && (c <= '9')) return (u16)(MDXGFX_RD_0 + (c - '0'));
    switch (c)
    {
        case '-': return MDXGFX_RD_HYPHEN;
        case '_': return MDXGFX_RD_UNDERSCORE;
        case '.': return MDXGFX_RD_PERIOD;
        case ':': return MDXGFX_RD_COLON;
        case '/': return MDXGFX_RD_SLASH;
        default:  return MDXGFX_RD_FILL;   /* space / unknown */
    }
}


/* ----------------------------------------------------------------------- */
/* Control widgets (W0-T3 / T4 / T5).  Tiles live at absolute VRAM          */
/* addresses 0x01CE..0x01E6 in PAL3 (controls) and PAL2 (selected frame),  */
/* generated once, idempotently, in MdxGfx_Init.                            */
/* ----------------------------------------------------------------------- */

/* Marker endpoint (relative to the tile centre ~ (3,3)) for each of the 8
 * knob rotation frames, swept clockwise from N (value 0) toward NW
 * (value max). Gives a visibly different indicator at 0 vs max. */
static const s8 s_knob_dx[8] = {  0,  2,  3,  2,  0, -2, -3, -2 };
static const s8 s_knob_dy[8] = { -3, -2,  0,  2,  3,  2,  0, -2 };

/* One knob frame: a KNOB_BODY disc with a KNOB_MARKER line from centre to the
 * step's endpoint. Outside the disc is transparent so the knob sits on panel. */
static void make_knob_frame(u32* t, u8 step)
{
    u8 px, py;
    s8 ex, ey;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            s8 cx = (s8)(px - 3);
            s8 cy = (s8)(py - 3);
            const u8 r2 = (u8)(cx * cx + cy * cy);
            MdxGfx_PlotTile(t, px, py, (r2 <= 9) ? MDXGFX_IDX_KNOB_BODY : 0);
        }
    }

    ex = (s8)(3 + s_knob_dx[step]);
    ey = (s8)(3 + s_knob_dy[step]);
    MdxGfx_PlotTile(t, 3, 3, MDXGFX_IDX_KNOB_MARKER);
    MdxGfx_PlotTile(t, (u8)((3 + ex) / 2), (u8)((3 + ey) / 2), MDXGFX_IDX_KNOB_MARKER);
    if ((ex >= 0) && (ex < 8) && (ey >= 0) && (ey < 8))
        MdxGfx_PlotTile(t, (u8)ex, (u8)ey, MDXGFX_IDX_KNOB_MARKER);
}

/* Filled lamp disc (radius ~3) of one PAL3 colour index. */
static void make_led_disc(u32* t, u8 idx)
{
    u8 px, py;
    for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            s8 cx = (s8)(px - 3);
            s8 cy = (s8)(py - 3);
            const u8 r2 = (u8)(cx * cx + cy * cy);
            MdxGfx_PlotTile(t, px, py, (r2 <= 9) ? idx : 0);
        }
}

/* Unlit lamp: an annulus of one PAL3 colour (centre transparent). */
static void make_led_off(u32* t, u8 idx)
{
    u8 px, py;
    for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            s8 cx = (s8)(px - 3);
            s8 cy = (s8)(py - 3);
            const u8 r2 = (u8)(cx * cx + cy * cy);
            MdxGfx_PlotTile(t, px, py, ((r2 >= 4) && (r2 <= 9)) ? idx : 0);
        }
}

/* Selected-frame corner/edge tile: SELECTED-coloured 1px border, transparent
 * interior so the underlying control shows through. 'edges' selects which sides
 * carry the border (0 = full frame for the degenerate 1x1 case). */
static void make_selbox(u32* t, u8 edges)
{
    u8 px, py;
    const u8 all = (edges == 0) ? (u8)(EDGE_T | EDGE_R | EDGE_B | EDGE_L) : edges;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            u8 border = 0;
            if ((all & EDGE_T) && (py == 0)) border = 1;
            if ((all & EDGE_B) && (py == 7)) border = 1;
            if ((all & EDGE_L) && (px == 0)) border = 1;
            if ((all & EDGE_R) && (px == 7)) border = 1;
            MdxGfx_PlotTile(t, px, py, border ? MDXGFX_IDX_SELECTED : 0);
        }
    }
}

/* Upload all control tiles into VRAM once. Mirrors mdxgfx_gen_readout. */
static void mdxgfx_gen_controls(void)
{
    static bool ready = FALSE;
    u32 t[8];
    u8 i;

    if (ready) return;
    ready = TRUE;

    /* Knob: 8 rotation frames, value 0 -> frame 0. */
    for (i = 0; i < MDXGFX_KNOB_FRAMES; i++)
    {
        make_knob_frame(t, i);
        VDP_loadTileData(t, (u16)(MDXGFX_KNOB0 + i), 1, DMA);
    }

    /* Fader: groove + cap (shared by V and H variants). */
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_FADER_TRACK);
    VDP_loadTileData(t, MDXGFX_FADER_TRACK, 1, DMA);
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_FADER_CAP);
    VDP_loadTileData(t, MDXGFX_FADER_CAP, 1, DMA);

    /* LED: red on/off, amber on/off (amber off reuses LED_RED_DIM as its dim). */
    make_led_disc(t, MDXGFX_IDX_LED_RED);
    VDP_loadTileData(t, MDXGFX_LED_RED_ON, 1, DMA);
    make_led_off(t, MDXGFX_IDX_LED_RED_DIM);
    VDP_loadTileData(t, MDXGFX_LED_RED_OFF, 1, DMA);
    make_led_disc(t, MDXGFX_IDX_LED_AMBER);
    VDP_loadTileData(t, MDXGFX_LED_AMBER_ON, 1, DMA);
    make_led_off(t, MDXGFX_IDX_LED_RED_DIM);
    VDP_loadTileData(t, MDXGFX_LED_AMBER_OFF, 1, DMA);

    /* Button: plate (released) + pressed states. */
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_BUTTON_PLATE);
    VDP_loadTileData(t, MDXGFX_BTN_PLATE, 1, DMA);
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_BUTTON_PRESSED);
    VDP_loadTileData(t, MDXGFX_BTN_PRESSED, 1, DMA);

    /* Selected-frame tiles (PAL2 SELECTED). */
    make_selbox(t, EDGE_T | EDGE_L);          VDP_loadTileData(t, MDXGFX_SEL_TL, 1, DMA);
    make_selbox(t, EDGE_T);                   VDP_loadTileData(t, MDXGFX_SEL_T,  1, DMA);
    make_selbox(t, EDGE_T | EDGE_R);          VDP_loadTileData(t, MDXGFX_SEL_TR, 1, DMA);
    make_selbox(t, EDGE_L);                   VDP_loadTileData(t, MDXGFX_SEL_L,  1, DMA);
    make_selbox(t, EDGE_R);                   VDP_loadTileData(t, MDXGFX_SEL_R,  1, DMA);
    make_selbox(t, EDGE_B | EDGE_L);          VDP_loadTileData(t, MDXGFX_SEL_BL, 1, DMA);
    make_selbox(t, EDGE_B);                   VDP_loadTileData(t, MDXGFX_SEL_B,  1, DMA);
    make_selbox(t, EDGE_B | EDGE_R);          VDP_loadTileData(t, MDXGFX_SEL_BR, 1, DMA);
    make_selbox(t, 0);                        VDP_loadTileData(t, MDXGFX_SEL_ALL, 1, DMA);
}


/* ----------------------------------------------------------------------- */
/* Device shell (W0-T6).  DX-7 x Mega Drive frame.  Tiles live at absolute   */
/* VRAM addresses 0x01E7..0x01EF in PAL2 (chrome), generated once,           */
/* idempotently, in MdxGfx_Init.                                             */
/* ----------------------------------------------------------------------- */

/* Bevel frame tile: charcoal BODY interior, BODY_BEVEL highlight on 'hi_edges'
 * and BORDER shadow on 'lo_edges'. Raised-frame look around the green panel. */
static void make_shell_bevel(u32* t, u8 hi_edges, u8 lo_edges)
{
    u8 px, py;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            u8 c = MDXGFX_IDX_BODY;

            if (hi_edges)
            {
                if ((hi_edges & EDGE_T) && (py == 0)) c = MDXGFX_IDX_BODY_BEVEL;
                else if ((hi_edges & EDGE_B) && (py == 7)) c = MDXGFX_IDX_BODY_BEVEL;
                else if ((hi_edges & EDGE_L) && (px == 0)) c = MDXGFX_IDX_BODY_BEVEL;
                else if ((hi_edges & EDGE_R) && (px == 7)) c = MDXGFX_IDX_BODY_BEVEL;
            }
            if (lo_edges)
            {
                if ((lo_edges & EDGE_T) && (py == 0)) c = MDXGFX_IDX_BORDER;
                else if ((lo_edges & EDGE_B) && (py == 7)) c = MDXGFX_IDX_BORDER;
                else if ((lo_edges & EDGE_L) && (px == 0)) c = MDXGFX_IDX_BORDER;
                else if ((lo_edges & EDGE_R) && (px == 7)) c = MDXGFX_IDX_BORDER;
            }
            MdxGfx_PlotTile(t, px, py, c);
        }
    }
}

/* Faux screw head: charcoal body, a BODY_BEVEL metal ring (radius ~2) and a
 * BORDER slot across the centre. */
static void make_shell_screw(u32* t)
{
    u8 px, py;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            s8 cx = (s8)(px - 3);
            s8 cy = (s8)(py - 3);
            const u8 r2 = (u8)(cx * cx + cy * cy);
            u8 c = MDXGFX_IDX_BODY;

            if ((r2 >= 4) && (r2 <= 9)) c = MDXGFX_IDX_BODY_BEVEL;
            if ((py == 3) && (px >= 2) && (px <= 4)) c = MDXGFX_IDX_BORDER;
            MdxGfx_PlotTile(t, px, py, c);
        }
    }
}

/* Faux vent slit: charcoal body with a 2px vertical BORDER gap. */
static void make_shell_vent(u32* t)
{
    u8 px, py;

    for (py = 0; py < 8; py++)
    {
        for (px = 0; px < 8; px++)
        {
            u8 c = MDXGFX_IDX_BODY;
            if ((px == 3) || (px == 4)) c = MDXGFX_IDX_BORDER;
            MdxGfx_PlotTile(t, px, py, c);
        }
    }
}

/* Upload all shell tiles into VRAM once. Mirrors mdxgfx_gen_controls. */
static void mdxgfx_gen_shell(void)
{
    static bool ready = FALSE;
    u32 t[8];

    if (ready) return;
    ready = TRUE;

    /* Solid fills. */
    make_box(t, 0, MDXGFX_IDX_BODY);   VDP_loadTileData(t, MDXGFX_SHELL_BODY,  1, DMA);
    make_box(t, 0, MDXGFX_IDX_PANEL);  VDP_loadTileData(t, MDXGFX_SHELL_PANEL, 1, DMA);

    /* Bevel ring (raised charcoal frame around the green panel). */
    make_shell_bevel(t, EDGE_T, 0);    VDP_loadTileData(t, MDXGFX_SHELL_BEVEL_T, 1, DMA);
    make_shell_bevel(t, 0, EDGE_B);    VDP_loadTileData(t, MDXGFX_SHELL_BEVEL_B, 1, DMA);
    make_shell_bevel(t, EDGE_L, 0);    VDP_loadTileData(t, MDXGFX_SHELL_BEVEL_L, 1, DMA);
    make_shell_bevel(t, 0, EDGE_R);    VDP_loadTileData(t, MDXGFX_SHELL_BEVEL_R, 1, DMA);

    /* Accent / screw / vent. */
    make_box(t, 0, MDXGFX_IDX_ACCENT); VDP_loadTileData(t, MDXGFX_SHELL_ACCENT, 1, DMA);
    make_shell_screw(t);               VDP_loadTileData(t, MDXGFX_SHELL_SCREW,  1, DMA);
    make_shell_vent(t);                VDP_loadTileData(t, MDXGFX_SHELL_VENT,   1, DMA);
}


/* ----------------------------------------------------------------------- */
/* Init: palette + base tiles.                                             */
/* ----------------------------------------------------------------------- */

void MdxGfx_Init(void)
{
    u32 t[8];
    const u16 base = MDXGFX_TILE_BASE;

    /* Palette (DX-7 x Mega Drive skin). PAL2 = faceplate (panel/body/LCD),
      * PAL3 = controls/indicators/keys. Distinct from PAL0 (SGDK font) and
      * PAL1 (sprite cursor); the engine owns no slot. Colours/index roles are
      * named in mdx_gfx.h; every slot of both palettes is written here. */
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_CHROME_BG),   MDXGFX_COL_CHROME_BG);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_BORDER),     MDXGFX_COL_BORDER);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_PANEL),      MDXGFX_COL_PANEL);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_METER),      MDXGFX_COL_METER);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_PANEL_BEVEL), MDXGFX_COL_PANEL_BEVEL);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_BODY),       MDXGFX_COL_BODY);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_BODY_BEVEL), MDXGFX_COL_BODY_BEVEL);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_LCD_BG),     MDXGFX_COL_LCD_BG);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_LCD_INK),    MDXGFX_COL_LCD_INK);
    PAL_setColor((u16)(MDXGFX_PAL_CHROME * 16 + MDXGFX_IDX_SELECTED),   MDXGFX_COL_SELECTED);

    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_KEYS_BG),      MDXGFX_COL_KEYS_BG);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_KEY_EDGE),     MDXGFX_COL_KEY_EDGE);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_KEY_WHITE),    MDXGFX_COL_KEY_WHITE);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_KEY_BLACK),    MDXGFX_COL_KEY_BLACK);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_ACCENT),       MDXGFX_COL_ACCENT);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_LED_RED),      MDXGFX_COL_LED_RED);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_LED_RED_DIM),  MDXGFX_COL_LED_RED_DIM);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_LED_AMBER),    MDXGFX_COL_LED_AMBER);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_KNOB_BODY),    MDXGFX_COL_KNOB_BODY);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_KNOB_MARKER),  MDXGFX_COL_KNOB_MARKER);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_FADER_TRACK),  MDXGFX_COL_FADER_TRACK);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_FADER_CAP),    MDXGFX_COL_FADER_CAP);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_BUTTON_PLATE), MDXGFX_COL_BUTTON_PLATE);
    PAL_setColor((u16)(MDXGFX_PAL_KEYS * 16 + MDXGFX_IDX_BUTTON_PRESSED), MDXGFX_COL_BUTTON_PRESSED);

    /* FILL: a solid block of panel-fill colour. */
    make_box(t, 0, MDXGFX_IDX_PANEL);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_FILL), 1, DMA);

    /* Border corner / edge tiles (interior stays panel-fill so it merges). */
    make_box(t, EDGE_T | EDGE_L, MDXGFX_IDX_PANEL); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_TL), 1, DMA);
    make_box(t, EDGE_T, MDXGFX_IDX_PANEL);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_T),  1, DMA);
    make_box(t, EDGE_T | EDGE_R, MDXGFX_IDX_PANEL); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_TR), 1, DMA);
    make_box(t, EDGE_L, MDXGFX_IDX_PANEL);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_L),  1, DMA);
    make_box(t, EDGE_R, MDXGFX_IDX_PANEL);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_R),  1, DMA);
    make_box(t, EDGE_B | EDGE_L, MDXGFX_IDX_PANEL); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_BL), 1, DMA);
    make_box(t, EDGE_B, MDXGFX_IDX_PANEL);  VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_B),  1, DMA);
    make_box(t, EDGE_B | EDGE_R, MDXGFX_IDX_PANEL); VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BORDER_BR), 1, DMA);

    /* Bar segments: outlined box, hollow (chrome-bg) vs filled (meter). */
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_CHROME_BG);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BAR_EMPTY), 1, DMA);
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_METER);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_BAR_FULL), 1, DMA);

    /* Keyboard keys (Wave 4). Drawn with PAL3. */
    make_box(t, EDGE_T | EDGE_R | EDGE_B | EDGE_L, MDXGFX_IDX_KEY_WHITE);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_KEY_WHITE), 1, DMA);
    make_key_black(t);
    VDP_loadTileData(t, (u16)(base + MDXGFX_TILE_KEY_BLACK), 1, DMA);

    /* Readout / LCD-plate tiles (0x019C..0x01CD, PAL2). Generated once. */
    mdxgfx_gen_readout();

    /* Control widgets (0x01CE..0x01E6, PAL3 + PAL2 selected frame). Generated once. */
    mdxgfx_gen_controls();

    /* Device shell (0x01E7..0x01EF, PAL2). Generated once. */
    mdxgfx_gen_shell();
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
/* Readout (LCD plate + green-LCD font).                                    */
/* ----------------------------------------------------------------------- */

/* Draw a recessed LCD plate: LCD-BG field inside a beveled charcoal recess
 * bezel (top/left shadow, bottom/right highlight). The interior (w-2 x h-2)
 * is the text area; render content with MdxGfx_DrawReadoutStr/Num. */
void MdxGfx_DrawReadout(u16 x, u16 y, u16 w, u16 h)
{
    if ((w < 2) || (h < 2)) return;

    if ((w > 2) && (h > 2))
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_RD_FILL), x + 1, y + 1, w - 2, h - 2);

    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_TL), x,         y);
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_TR), x + w - 1, y);
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_BL), x,         y + h - 1);
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_BR), x + w - 1, y + h - 1);

    if (w > 2)
    {
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_RD_T), x + 1,     y,         w - 2, 1);
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_RD_B), x + 1,     y + h - 1, w - 2, 1);
    }

    if (h > 2)
    {
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_RD_L), x,     y + 1,     1, h - 2);
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_RD_R), x + w - 1, y + 1, 1, h - 2);
    }
}

/* Draw an uppercase LCD string (A-Z, 0-9, space, - _ . : /) left-to-right,
 * one glyph per tile starting at (x,y). Space / unknown -> blank LCD field. */
void MdxGfx_DrawReadoutStr(u16 x, u16 y, const char* s)
{
    u16 i = 0;

    if (s == NULL) return;
    while (s[i] != '\0')
    {
        VDP_setTileMapXY(BG_A, RD(readout_glyph_vram(s[i])), (u16)(x + i), y);
        i++;
    }
}

/* Draw a zero-padded unsigned number as LCD digits, 'digits' wide. */
void MdxGfx_DrawReadoutNum(u16 x, u16 y, u16 value, u8 digits)
{
    u8 i;
    u16 div;

    if (digits == 0) return;

    div = 1;
    for (i = 1; i < digits; i++) div = (u16)(div * 10u);

    for (i = 0; i < digits; i++)
    {
        const u8 d = (u8)((value / div) % 10u);
        VDP_setTileMapXY(BG_A, RD((u16)(MDXGFX_RD_0 + d)), (u16)(x + i), y);
        div = (u16)(div / 10u);
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
/* Control widgets (W0-T3 / T4 / T5).                                       */
/* ----------------------------------------------------------------------- */

void MdxGfx_DrawKnob(u16 x, u16 y, u16 value, u16 max, bool selected)
{
    u8 step;

    if (max == 0) step = 0;
    else step = (u8)(((u32)value * (u32)(MDXGFX_KNOB_FRAMES - 1)) / (u32)max);
    if (step >= MDXGFX_KNOB_FRAMES) step = (u8)(MDXGFX_KNOB_FRAMES - 1);

    VDP_setTileMapXY(BG_A, CT((u16)(MDXGFX_KNOB0 + step)), x, y);
    if (selected) MdxGfx_DrawSelectedBox(x, y, 1, 1);
}

/* Place the fader cap at the position implied by value/max: index 0 = the
 * minimum end (bottom for V, left for H); monotonic with value. */
static u16 fader_cap_index(u16 len, u16 value, u16 max)
{
    const u16 filled = bar_filled(len, value, max);
    return (filled >= len) ? (u16)(len - 1) : filled;
}

void MdxGfx_DrawFaderV(u16 x, u16 y, u16 len, u16 value, u16 max, bool selected)
{
    u16 i, cap;

    if (len == 0) return;
    cap = fader_cap_index(len, value, max);

    for (i = 0; i < len; i++)
        VDP_setTileMapXY(BG_A, CT(MDXGFX_FADER_TRACK), x, (u16)(y + i));
    VDP_setTileMapXY(BG_A, CT(MDXGFX_FADER_CAP), x, (u16)(y + len - 1 - cap));
    if (selected) MdxGfx_DrawSelectedBox(x, y, 1, len);
}

void MdxGfx_DrawFaderH(u16 x, u16 y, u16 len, u16 value, u16 max, bool selected)
{
    u16 i, cap;

    if (len == 0) return;
    cap = fader_cap_index(len, value, max);

    for (i = 0; i < len; i++)
        VDP_setTileMapXY(BG_A, CT(MDXGFX_FADER_TRACK), (u16)(x + i), y);
    VDP_setTileMapXY(BG_A, CT(MDXGFX_FADER_CAP), (u16)(x + cap), y);
    if (selected) MdxGfx_DrawSelectedBox(x, y, len, 1);
}

void MdxGfx_DrawLED(u16 x, u16 y, MdxGfxLedColor color, bool on)
{
    const u16 tile = (color == MDXGFX_LEDCOLOR_AMBER)
        ? (on ? MDXGFX_LED_AMBER_ON : MDXGFX_LED_AMBER_OFF)
        : (on ? MDXGFX_LED_RED_ON   : MDXGFX_LED_RED_OFF);
    VDP_setTileMapXY(BG_A, CT(tile), x, y);
}

void MdxGfx_DrawButton(u16 x, u16 y, u16 w, u16 h, bool pressed, const char* label)
{
    u16 i, j;
    const u16 tile = pressed ? MDXGFX_BTN_PRESSED : MDXGFX_BTN_PLATE;

    if ((w == 0) || (h == 0)) return;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            VDP_setTileMapXY(BG_A, CT(tile), (u16)(x + i), (u16)(y + j));

    /* Optional centred LCD label (uppercase readout font). */
    if ((label != NULL) && (w >= 3))
    {
        u16 lx, li;
        u16 ll = 0;
        while (label[ll] != '\0') ll++;
        if (ll > 0)
        {
            lx = (u16)(x + (w - ll) / 2);
            for (i = 0, li = lx; i < ll; i++, li++)
                VDP_setTileMapXY(BG_A, RD(readout_glyph_vram(label[i])), li, y);
        }
    }
}

void MdxGfx_DrawSelectedBox(u16 x, u16 y, u16 w, u16 h)
{
    if ((w == 0) || (h == 0)) return;

    /* Degenerate 1x1 region: a single full-frame tile. */
    if ((w == 1) && (h == 1))
    {
        VDP_setTileMapXY(BG_A, RD(MDXGFX_SEL_ALL), x, y);
        return;
    }

    VDP_setTileMapXY(BG_A, RD(MDXGFX_SEL_TL), x, y);
    if (w > 1) VDP_setTileMapXY(BG_A, RD(MDXGFX_SEL_TR), (u16)(x + w - 1), y);
    if (h > 1) VDP_setTileMapXY(BG_A, RD(MDXGFX_SEL_BL), x, (u16)(y + h - 1));
    if ((w > 1) && (h > 1))
        VDP_setTileMapXY(BG_A, RD(MDXGFX_SEL_BR), (u16)(x + w - 1), (u16)(y + h - 1));

    if (w > 2)
    {
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_SEL_T), (u16)(x + 1), y, w - 2, 1);
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_SEL_B), (u16)(x + 1), (u16)(y + h - 1), w - 2, 1);
    }
    if (h > 2)
    {
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_SEL_L), x, (u16)(y + 1), 1, h - 2);
        VDP_fillTileMapRect(BG_A, RD(MDXGFX_SEL_R), (u16)(x + w - 1), (u16)(y + 1), 1, h - 2);
    }
}


/* ----------------------------------------------------------------------- */
/* Device shell (W0-T6): DX-7 x Mega Drive frame.                          */
/* ----------------------------------------------------------------------- */

/* Draw a recessed green-LCD "MDX-4" branding plate (8 wide x 2 tall) centred on
 * the top bezel, using the shared readout tiles (PAL2). */
static void draw_branding_plate(u16 x, u16 y)
{
    u16 c;

    VDP_fillTileMapRect(BG_A, RD(MDXGFX_RD_FILL), x, y, 8, 2);
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_TL), x,         y);
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_TR), (u16)(x + 7), y);
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_BL), x,         (u16)(y + 1));
    VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_BR), (u16)(x + 7), (u16)(y + 1));
    for (c = 1; c < 7; c++)
    {
        VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_T), (u16)(x + c), y);
        VDP_setTileMapXY(BG_A, RD(MDXGFX_RD_B), (u16)(x + c), (u16)(y + 1));
    }
    MdxGfx_DrawReadoutStr((u16)(x + 2), y, "MDX-4");
}

void MdxGfx_DrawDeviceShell(void)
{
    /* 1. Charcoal body fills the whole screen. */
    VDP_fillTileMapRect(BG_A, SH(MDXGFX_SHELL_BODY), 0, 0, 40, 28);

    /* 2. Green panel = the inner content area (PAGE_*). */
    VDP_fillTileMapRect(BG_A, SH(MDXGFX_SHELL_PANEL), PAGE_X, PAGE_Y, PAGE_W, PAGE_H);

    /* 3. Bevel ring around the panel (raised charcoal frame). */
    VDP_fillTileMapRect(BG_A, SH(MDXGFX_SHELL_BEVEL_T), PAGE_X, (u16)(PAGE_Y - 1), PAGE_W, 1);
    VDP_fillTileMapRect(BG_A, SH(MDXGFX_SHELL_BEVEL_B), PAGE_X, (u16)(PAGE_Y + PAGE_H), PAGE_W, 1);
    VDP_fillTileMapRect(BG_A, SH(MDXGFX_SHELL_BEVEL_L), (u16)(PAGE_X - 1), PAGE_Y, 1, PAGE_H);
    VDP_fillTileMapRect(BG_A, SH(MDXGFX_SHELL_BEVEL_R), (u16)(PAGE_X + PAGE_W), PAGE_Y, 1, PAGE_H);

    /* 4. Bezel corner screws (also serve as the bevel-ring corners). */
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_SCREW), (u16)(PAGE_X - 1), (u16)(PAGE_Y - 1));
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_SCREW), (u16)(PAGE_X + PAGE_W), (u16)(PAGE_Y - 1));
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_SCREW), (u16)(PAGE_X - 1), (u16)(PAGE_Y + PAGE_H));
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_SCREW), (u16)(PAGE_X + PAGE_W), (u16)(PAGE_Y + PAGE_H));

    /* 5. Sega-blue corner accents on the outer console corners. */
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_ACCENT), 0, 0);
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_ACCENT), 39, 0);
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_ACCENT), 0, 27);
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_ACCENT), 39, 27);

    /* 6. Faux vents flanking the nameplate on the top bezel. */
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_VENT), 4, 0);
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_VENT), 4, 1);
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_VENT), 35, 0);
    VDP_setTileMapXY(BG_A, SH(MDXGFX_SHELL_VENT), 35, 1);

    /* 7. Branding plate on the top bezel. */
    draw_branding_plate(16, 0);
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
