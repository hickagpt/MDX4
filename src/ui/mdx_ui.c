/*
 * mdx_ui.c - UI core (SPEC 4.1 / 9.1).
 *
 * Implements the screen state machine, the sprite selection cursor, and the
 * render helpers. MdxUi_Run() is the VBlank-tied main loop: it polls input,
 * dispatches to the active screen's handler, drives the C-button note preview,
 * and lets the synth engine keep sounding across all navigation.
 *
 * Rendering: the active screen's Enter() draws the full screen once; Handle()
 * only repaints the lines / cursor that changed. The loop itself never forces a
 * full redraw.
 */

#include "mdx_ui.h"

#include <genesis.h>
#include <vdp_spr.h>
#include <pal.h>

#include "core/mdx_synth.h"
#include "input/mdx_input.h"


/* ----- Screen geometry. ----- */
#define MDX_UI_COLS  40
#define MDX_UI_ROWS  28

/* Cursor sprite: VRAM tile + palette slot (PAL1, colour 1 -> global 17). */
#define CURSOR_TILE_INDEX  256
#define CURSOR_PAL        1
#define CURSOR_COLOR_GIDX (CURSOR_PAL * 16 + 1)   /* 17 */

/* Solid 8x8 tile, every pixel = colour value 1 (rendered with CURSOR_PAL). */
static const u32 g_cursorTile[8] =
{
    0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111
};

/* ----- State. ----- */
static MdxScreen           g_screen = SCREEN_MAIN;
static MdxScreenEnterFn    g_enter[SCREEN_COUNT]  = { 0 };
static MdxScreenHandleFn   g_handle[SCREEN_COUNT] = { 0 };

static u8  g_operator     = 0;     /* shared selected operator 0..3 */
static u8  g_previewNote  = 69;    /* A4 */

/* Cursor sprite bookkeeping. */
static s16 g_cursorSpr = -1;
static u16 g_cursorCol = 0;
static u16 g_cursorRow = 0;


/* ----------------------------------------------------------------------- */
/* State machine.                                                           */
/* ----------------------------------------------------------------------- */

void MdxUi_RegisterScreen(MdxScreen s, MdxScreenEnterFn enter,
                          MdxScreenHandleFn handle)
{
    if (s >= SCREEN_COUNT) return;
    g_enter[s]  = enter;
    g_handle[s] = handle;
}


void MdxUi_SetScreen(MdxScreen s)
{
    if (s >= SCREEN_COUNT) return;
    if (s == g_screen) return;          /* no-op if already there */

    g_screen = s;

    if (g_enter[g_screen]) g_enter[g_screen]();   /* render once on entry */
}


MdxScreen MdxUi_GetScreen(void)
{
    return g_screen;
}


u8 MdxUi_GetOperator(void)
{
    return g_operator;
}


void MdxUi_SetOperator(u8 op)
{
    g_operator = (op > 3) ? 3 : op;
}


u8 MdxUi_GetPreviewNote(void)
{
    return g_previewNote;
}


void MdxUi_SetPreviewNote(u8 note)
{
    g_previewNote = (note > 127) ? 127 : note;
}


/* ----------------------------------------------------------------------- */
/* Render helpers.                                                         */
/* ----------------------------------------------------------------------- */

void MdxUi_DrawText(u16 col, u16 row, const char* text)
{
    VDP_drawText(text, col, row);
}


void MdxUi_ClearLine(u16 row)
{
    VDP_clearTextLine(row);
}


void MdxUi_DrawBox(u16 col, u16 row, u16 w, u16 h)
{
    u16 c, r;
    char buf[2];

    if ((w < 2) || (h < 2)) return;

    buf[1] = '\0';

    /* top + bottom edges */
    for (c = 1; c < w - 1; c++)
    {
        buf[0] = '-';
        VDP_drawText(buf, col + c, row);
        VDP_drawText(buf, col + c, row + h - 1);
    }
    /* left + right edges */
    for (r = 1; r < h - 1; r++)
    {
        buf[0] = '|';
        VDP_drawText(buf, col, row + r);
        VDP_drawText(buf, col + w - 1, row + r);
    }
    /* corners */
    buf[0] = '+';
    VDP_drawText(buf, col, row);
    VDP_drawText(buf, col + w - 1, row);
    VDP_drawText(buf, col, row + h - 1);
    VDP_drawText(buf, col + w - 1, row + h - 1);
}


void MdxUi_MoveCursor(u16 col, u16 row)
{
    if ((col == g_cursorCol) && (row == g_cursorRow)) return;   /* unchanged */

    g_cursorCol = col;
    g_cursorRow = row;

    if (g_cursorSpr < 0) return;   /* not initialised yet */

    VDP_setSpritePosition((u16)g_cursorSpr, (s16)(col * 8), (s16)(row * 8));
    VDP_updateSprites(1, DMA);
}


/* ----------------------------------------------------------------------- */
/* Init.                                                                   */
/* ----------------------------------------------------------------------- */

void MdxUi_Init(void)
{
    /* 320x224 tile display + default SGDK font (loaded by VDP_init()). */
    VDP_init();

    /* Limited palette: keep PAL0 (text) untouched; give the cursor a bright
     * colour in PAL1 slot 1 so it is clearly visible above the background. */
    PAL_setColor(CURSOR_COLOR_GIDX, 0x0EEE);

    /* Load a solid cursor tile into VRAM (index 256, well clear of the font). */
    VDP_loadTileData(g_cursorTile, CURSOR_TILE_INDEX, 1, DMA);

    /* Sprite cursor (low-level VDP sprite API, no SpriteDefinition needed). */
    VDP_resetSprites();
    g_cursorSpr = VDP_allocateSprites(1);
    if (g_cursorSpr >= 0)
    {
        VDP_setSprite((u16)g_cursorSpr,
                      (s16)(g_cursorCol * 8), (s16)(g_cursorRow * 8),
                      SPRITE_SIZE(1, 1),
                      TILE_ATTR_FULL(CURSOR_PAL, 1, 0, 0, CURSOR_TILE_INDEX));
        VDP_updateSprites(1, DMA);
    }

    g_screen   = SCREEN_MAIN;
    g_operator = 0;
}


/* ----------------------------------------------------------------------- */
/* Main loop.                                                              */
/* ----------------------------------------------------------------------- */

void MdxUi_Run(void)
{
    MdxInput inp;
    u8       prevCHeld = 0;

    /* First paint of the starting screen. */
    if (g_enter[g_screen]) g_enter[g_screen]();

    while (TRUE)
    {
        SYS_doVBlankProcess();

        MdxInput_Poll(&inp);

        /* Start always returns to the main screen (SPEC 4.3). */
        if (inp.start) MdxUi_SetScreen(SCREEN_MAIN);

        /* C-button note preview: edge-on -> NoteOn, release -> NoteOff.
         * A note started here keeps sounding while the user navigates and
         * edits other screens (engine is never stalled). */
        {
            const u8 cHeld = MdxInput_Held(BUTTON_C) ? 1 : 0;
            if (cHeld && !prevCHeld)
                MdxSynth_NoteOn(MdxUi_GetPreviewNote());
            else if (!cHeld && prevCHeld)
                MdxSynth_NoteOff();
            prevCHeld = cHeld;
        }

        /* Dispatch to the active screen (NULL-safe until a screen registers). */
        if (g_handle[g_screen]) g_handle[g_screen]((const void*)&inp);
    }
}
