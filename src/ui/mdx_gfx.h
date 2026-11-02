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
/* Full VRAM owned-range map  (AUTHORITATIVE after Build Wave 0).            */
/*                                                                          */
/* Every 8x8 tile-index range this project owns in VRAM, low -> high. A     */
/* static guard (tools/vram_guard.js) reconstructs the load-time ranges     */
/* from the VDP_loadTileData sites in the src/ui sources and enforces three rules: */
/*   (a) no two reserved ranges overlap;                                    */
/*   (b) no NEW skin tile (the T2..T6 widgets below) sits below 0x019C;     */
/*   (c) the >=0x019C skin tail stays under the TRUE 612-tile hardware      */
/*       ceiling (0x019C..0x03FF = 612 tiles). On success it prints the     */
/*       per-widget ranges, the aggregate >=0x019C total and the high-water */
/*       mark; on any violation it exits non-zero.                          */
/*                                                                          */
/*   Range              Count  Owner / widget                 Loaded in     */
/*   -----------------  -----  -----------------------------  ------------  */
/*   0x0000..~0x0060     ~97   SGDK text font (PAL0)          VDP_init      */
/*   0x0100               1    Sprite cursor (PAL1)           mdx_ui.c      */
/*   0x0110               1    Blank wipe tile (PAL0)         mdx_ui.c      */
/*   0x0120..0x012C      13    MdxGfx base tiles (PAL2)       mdx_gfx.c     */
/*   0x0130..0x0135       6    W2 algorithm routing diagram   mdx_algorithm */
/*   0x0136..0x0195      96    W3 envelope graph (PAL2)       mdx_envelope  */
/*   0x0196..0x019B       6    W5 mini routing diagram (PAL2) mdx_main_scrn */
/*   0x019C..0x01CD      50    Readout LCD plate + font (PAL2) mdx_gfx.c    */
/*   0x01CE..0x01E6      25    Controls knob/fader/led/btn    mdx_gfx.c     */
/*   0x01E7..0x01EF       9    DeviceShell DX-7 frame (PAL2)  mdx_gfx.c     */
/*   -----------------  -----                                              */
/*   HIGH-WATER MARK after Wave 0 = 0x01F0 (exclusive).                    */
/*                                                                          */
/* The three blocks 0x0130..0x019B (W2 / W3 / W5) PRE-EXIST this skin work  */
/* and are EXEMPT from the "< 0x019C" new-tile guard. The NEW skin tail     */
/* introduced by this plan (T2..T6) is 0x019C..0x01EF = 84 tiles:           */
/*   Readout  (W0-T2)     : 0x019C..0x01CD  (50)  high-water 0x01CE         */
/*   Controls (W0-T3/4/5) : 0x01CE..0x01E6  (25)  high-water 0x01E7         */
/*   Shell    (W0-T6)     : 0x01E7..0x01EF   (9)  high-water 0x01F0         */
/*   aggregate >=0x019C   = 50 + 25 + 9 = 84 tiles.                         */
/* The design sub-budget TARGET was 100 tiles; the TRUE hardware ceiling is */
/* 612 (0x019C..0x03FF). 84 sits well inside both. The older plan's soft    */
/* numbers (<=28 Readout / <=84 total / <=100) were TARGETS, not actual     */
/* allocations - the figures above are the verified ACTUAL high-water marks.*/
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* Palette slots owned by this module.                                      */
/* ----------------------------------------------------------------------- */

/* Distinct from PAL0 (SGDK text font) and PAL1 (sprite cursor). The engine
 * does not own any palette slot, so PAL2/PAL3 are free to claim. */
#define MDXGFX_PAL_CHROME  2   /* DX-7 faceplate: panel / body / LCD    */
#define MDXGFX_PAL_KEYS    3   /* controls, indicators, keyboard keys   */


/* ----------------------------------------------------------------------- */
/* DX-7 x Mega Drive skin palette.                                          */
/*                                                                          */
/* Two kinds of constant, kept separate on purpose:                         */
/*   MDXGFX_COL_*  = a 9-bit SGDK colour value  0x0RGB (one 4-bit nibble     */
/*                   per channel, 0..F). Passed to PAL_setColor in Init.     */
/*   MDXGFX_IDX_*  = the 0..15 palette-index a tile plots a pixel into.       */
/*                   Passed to make_box / MdxGfx_PlotTile.                   */
/*                                                                          */
/* Partition rule: a BG tile references exactly ONE of PAL2/PAL3 and index 0 */
/* is transparent, so a cutout cannot show the other palette's colour behind */
/* it. Every spatially-adjacent region therefore lives in ONE palette:       */
/*   PAL2 (chrome) = the faceplate: DX-Green panel + bevel, dark-charcoal    */
/*                   body/bezel, green LCD (bg + ink), panel meter/selected. */
/*   PAL3 (keys)   = controls & indicators: keyboard keys, Sega-blue accent, */
/*                   red/amber LEDs, knob / fader / button states.           */
/*                                                                          */
/* Indices 0..3 of each palette back the existing base tiles (fill, border,  */
/* bar, keys); indices 4+ are reserved+coloured here for the later widgets   */
/* (Readout/Knob/Fader/LED/Button/DeviceShell) so screens reference names.   */
/* ----------------------------------------------------------------------- */

/* --- PAL2 (MDXGFX_PAL_CHROME) : faceplate / body / display --- */
#define MDXGFX_IDX_CHROME_BG     0
#define MDXGFX_IDX_BORDER        1   /* panel outline / shadow            */
#define MDXGFX_IDX_PANEL         2   /* DX-Green panel fill               */
#define MDXGFX_IDX_METER         3   /* bar / level-meter fill            */
#define MDXGFX_IDX_PANEL_BEVEL   4   /* DX-Green raised-bevel highlight   */
#define MDXGFX_IDX_BODY          5   /* dark-charcoal body / bezel        */
#define MDXGFX_IDX_BODY_BEVEL    6   /* charcoal raised-bevel highlight   */
#define MDXGFX_IDX_LCD_BG        7   /* dark-green LCD window background  */
#define MDXGFX_IDX_LCD_INK       8   /* bright-green LCD text / segments  */
#define MDXGFX_IDX_SELECTED      9   /* selected / active highlight       */

#define MDXGFX_COL_CHROME_BG     0x000  /* transparent                   */
#define MDXGFX_COL_BORDER        0x111  /* near-black panel edge         */
#define MDXGFX_COL_PANEL         0x2A7  /* DX-Green faceplate panel      */
#define MDXGFX_COL_METER         0x5F5  /* bright-green level meter      */
#define MDXGFX_COL_PANEL_BEVEL   0x3B8  /* lighter DX-Green bevel        */
#define MDXGFX_COL_BODY          0x222  /* dark-charcoal body / bezel    */
#define MDXGFX_COL_BODY_BEVEL    0x333  /* charcoal bevel highlight      */
#define MDXGFX_COL_LCD_BG        0x131  /* dark-green LCD background     */
#define MDXGFX_COL_LCD_INK       0x4E5  /* bright-green LCD ink          */
#define MDXGFX_COL_SELECTED      0x6F6  /* bright-lime selected state    */

/* --- PAL3 (MDXGFX_PAL_KEYS) : controls, indicators, keyboard keys --- */
#define MDXGFX_IDX_KEYS_BG       0
#define MDXGFX_IDX_KEY_EDGE      1   /* key / control outline           */
#define MDXGFX_IDX_KEY_WHITE     2   /* white key fill                  */
#define MDXGFX_IDX_KEY_BLACK     3   /* black key fill                  */
#define MDXGFX_IDX_ACCENT        4   /* Sega-blue accent / active       */
#define MDXGFX_IDX_LED_RED       5   /* red LED on                      */
#define MDXGFX_IDX_LED_RED_DIM   6   /* red LED off / dim               */
#define MDXGFX_IDX_LED_AMBER     7   /* amber LED                       */
#define MDXGFX_IDX_KNOB_BODY     8   /* rotary knob body                */
#define MDXGFX_IDX_KNOB_MARKER   9   /* knob indicator marker           */
#define MDXGFX_IDX_FADER_TRACK   10  /* fader track / slot              */
#define MDXGFX_IDX_FADER_CAP     11  /* fader cap                       */
#define MDXGFX_IDX_BUTTON_PLATE  12  /* button plate                    */
#define MDXGFX_IDX_BUTTON_PRESSED 13 /* button pressed / depressed      */

#define MDXGFX_COL_KEYS_BG       0x000  /* transparent / key gap        */
#define MDXGFX_COL_KEY_EDGE      0x444  /* mid-grey key/control edge    */
#define MDXGFX_COL_KEY_WHITE     0xEEE  /* bright off-white key         */
#define MDXGFX_COL_KEY_BLACK     0x111  /* matte-black key              */
#define MDXGFX_COL_ACCENT        0x15B  /* Sega-blue accent             */
#define MDXGFX_COL_LED_RED       0xE44  /* bright red LED               */
#define MDXGFX_COL_LED_RED_DIM   0x400  /* dim red LED (off)            */
#define MDXGFX_COL_LED_AMBER     0xA60  /* amber LED                    */
#define MDXGFX_COL_KNOB_BODY     0x555  /* charcoal knob body           */
#define MDXGFX_COL_KNOB_MARKER   0xEEE  /* bright knob indicator        */
#define MDXGFX_COL_FADER_TRACK   0x111  /* dark fader slot              */
#define MDXGFX_COL_FADER_CAP     0x666  /* charcoal fader cap           */
#define MDXGFX_COL_BUTTON_PLATE  0x333  /* button plate                 */
#define MDXGFX_COL_BUTTON_PRESSED 0x222 /* depressed button             */


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
/* Readout / LCD-plate tiles (BUILD WAVE 0, TODO 2).  Palette PAL2 (chrome).*/
/*                                                                          */
/* These live in the SHARED >=0x019C tail and are ABSOLUTE VRAM addresses   */
/* (like the W5-mini diagram's 0x0196..0x019B in mdx_main_screen.c), NOT    */
/* offsets from MDXGFX_TILE_BASE. Generated procedurally in MdxGfx_Init.    */
/*                                                                          */
/* VRAM LEDGER (>=0x019C shared tail)                                       */
/*   Readout (W0-T2): 0x019C..0x01CD  (50 tiles)  high-water 0x01CE (excl.) */
/*     frame  0x019C..0x01A4  (9: FILL,TL,T,TR,L,R,BL,B,BR)                 */
/*     font   0x01A5..0x01CD  (41: A-Z, 0-9, - _ . : /)                     */
/*   NEXT widget (Knob/Fader/LED/Button/DeviceShell, Envelope LCD window)   */
/*   continues at MDXGFX_RD_READOUT_END (0x01CE).                           */
/* ----------------------------------------------------------------------- */

#define MDXGFX_RD_FILL        0x019C  /* LCD field: solid LCD_BG            */
#define MDXGFX_RD_TL          0x019D  /* recessed bezel corners/edges       */
#define MDXGFX_RD_T           0x019E
#define MDXGFX_RD_TR          0x019F
#define MDXGFX_RD_L           0x01A0
#define MDXGFX_RD_R           0x01A1
#define MDXGFX_RD_BL          0x01A2
#define MDXGFX_RD_B           0x01A3
#define MDXGFX_RD_BR          0x01A4
#define MDXGFX_RD_A           0x01A5  /* font start: A..Z = 0x01A5..0x01BE   */
#define MDXGFX_RD_Z           0x01BE  /* font end (letters)                 */
#define MDXGFX_RD_0           0x01BF  /* digits: 0..9 = 0x01BF..0x01C8       */
#define MDXGFX_RD_9           0x01C8  /* font end (digits)                  */
#define MDXGFX_RD_HYPHEN      0x01C9  /* '-'                                */
#define MDXGFX_RD_UNDERSCORE  0x01CA  /* '_'                                */
#define MDXGFX_RD_PERIOD      0x01CB  /* '.'                                */
#define MDXGFX_RD_COLON       0x01CC  /* ':'                                */
#define MDXGFX_RD_SLASH      0x01CD  /* '/'                                */
#define MDXGFX_RD_READOUT_END 0x01CE  /* exclusive high-water mark          */


/* ----------------------------------------------------------------------- */
/* Control widgets (BUILD WAVE 0, T3/T4/T5).  Palette: PAL3 (controls) for   */
/* knob / fader / led / button; PAL2 (chrome, SELECTED) for the shared      */
/* SelectedBox frame. All at ABSOLUTE VRAM addresses >= 0x019C, continuing   */
/* from MDXGFX_RD_READOUT_END. Generated procedurally in MdxGfx_Init.        */
/*                                                                          */
/* VRAM LEDGER (>=0x019C shared tail)                                       */
/*   Readout  (W0-T2): 0x019C..0x01CD  (50)  high-water 0x01CE              */
/*   Controls (W0-T3/T4/T5): 0x01CE..0x01E6  (25)  high-water 0x01E7        */
/*     knob   0x01CE..0x01D5  (8 rotation frames; value/max -> frame)       */
/*     fader  0x01D6 track, 0x01D7 cap  (shared vertical/horizontal)        */
/*     led    0x01D8 red-on, 0x01D9 red-off, 0x01DA amber-on, 0x01DB off    */
/*            (amber-off reuses LED_RED_DIM; no amber-dim palette slot)      */
/*     button 0x01DC plate, 0x01DD pressed                                */
/*     selbox 0x01DE..0x01E6 (TL,T,TR,L,R,BL,B,BR,ALL)  PAL2 SELECTED      */
/*   NEXT (DeviceShell, W0-T6) continues at MDXGFX_CTRL_END (0x01E7).       */
/* ----------------------------------------------------------------------- */

#define MDXGFX_KNOB0          0x01CE  /* knob frame 0 (value = 0)            */
#define MDXGFX_KNOB_FRAMES    8       /* rotation frames 0x01CE..0x01D5      */
#define MDXGFX_FADER_TRACK    0x01D6  /* fader groove (V/H shared)           */
#define MDXGFX_FADER_CAP      0x01D7  /* fader cap (V/H shared)             */
#define MDXGFX_LED_RED_ON     0x01D8  /* red LED lit                        */
#define MDXGFX_LED_RED_OFF    0x01D9  /* red LED unlit (LED_RED_DIM)        */
#define MDXGFX_LED_AMBER_ON   0x01DA  /* amber LED lit (LED_AMBER)          */
#define MDXGFX_LED_AMBER_OFF  0x01DB  /* amber LED unlit (reuses LED_RED_DIM)*/
#define MDXGFX_BTN_PLATE      0x01DC  /* membrane/plate button (released)   */
#define MDXGFX_BTN_PRESSED    0x01DD  /* membrane/plate button (pressed)    */
#define MDXGFX_SEL_TL         0x01DE  /* selected-frame corners/edges (PAL2)*/
#define MDXGFX_SEL_T          0x01DF
#define MDXGFX_SEL_TR         0x01E0
#define MDXGFX_SEL_L          0x01E1
#define MDXGFX_SEL_R          0x01E2
#define MDXGFX_SEL_BL         0x01E3
#define MDXGFX_SEL_B          0x01E4
#define MDXGFX_SEL_BR         0x01E5
#define MDXGFX_SEL_ALL        0x01E6  /* full 1px frame (1x1 / degenerate)  */
#define MDXGFX_CTRL_END       0x01E7  /* exclusive high-water mark          */


/* ----------------------------------------------------------------------- */
/* Device shell (BUILD WAVE 0, T6).  DX-7 x Mega Drive frame.  Palette PAL2  */
/* (chrome).  Tiles live at ABSOLUTE VRAM addresses >= 0x019C, continuing    */
/* from MDXGFX_CTRL_END.  Generated procedurally in MdxGfx_Init.             */
/*                                                                           */
/* VRAM LEDGER (>=0x019C shared tail)                                        */
/*   Readout  (W0-T2): 0x019C..0x01CD  (50)                                  */
/*   Controls (W0-T3/T4/T5): 0x01CE..0x01E6  (25)                            */
/*   Shell    (W0-T6): 0x01E7..0x01EF  (9)  high-water 0x01F0 (excl.)        */
/*   NEXT widget continues at MDXGFX_SHELL_END (0x01F0).                     */
/* ----------------------------------------------------------------------- */

#define MDXGFX_SHELL_BODY      0x01E7  /* solid dark-charcoal body fill     */
#define MDXGFX_SHELL_PANEL     0x01E8  /* solid DX-Green panel fill         */
#define MDXGFX_SHELL_BEVEL_T   0x01E9  /* bezel bevel: top highlight        */
#define MDXGFX_SHELL_BEVEL_B   0x01EA  /* bezel bevel: bottom shadow        */
#define MDXGFX_SHELL_BEVEL_L   0x01EB  /* bezel bevel: left highlight       */
#define MDXGFX_SHELL_BEVEL_R   0x01EC  /* bezel bevel: right shadow         */
#define MDXGFX_SHELL_ACCENT    0x01ED  /* Sega-blue accent / corner block   */
#define MDXGFX_SHELL_SCREW     0x01EE  /* faux screw head on body           */
#define MDXGFX_SHELL_VENT      0x01EF  /* faux vent slit on body            */
#define MDXGFX_SHELL_END       0x01F0  /* exclusive high-water mark         */


/* ----------------------------------------------------------------------- */
/* Page geometry (inner content rectangle inside the 2-tile bezel).          */
/*                                                                           */
/* The MD screen is 40x28 tiles (320x224). The device shell draws a 2-tile   */
/* charcoal bezel around the edge; PAGE_* is the green-panel content area    */
/* every migrated screen lays out inside. The shell never overlaps PAGE_*.    */
/* ----------------------------------------------------------------------- */

#define MDXGFX_BEZEL_T  2   /* bezel thickness (tiles) top/bottom/left/right */
#define PAGE_X  2           /* content area left edge                        */
#define PAGE_Y  2           /* content area top edge                         */
#define PAGE_W  36          /* content area width (cols 2..37)               */
#define PAGE_H  24          /* content area height (rows 2..25)              */


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

/* --- Readout: recessed green-LCD plate + green-LCD font (W0-T2). --- */

/* Draw a recessed green-LCD plate at tile (x,y) spanning (w,h) tiles: a dark-
 * green field (MDXGFX_IDX_LCD_BG) inside a beveled charcoal recess bezel (top/
 * left shadow, bottom/right highlight). Leaves a 1-tile bezel; the interior
 * (w-2 x h-2) is the text area. Render content with the two helpers below. */
void MdxGfx_DrawReadout(u16 x, u16 y, u16 w, u16 h);

/* Draw an uppercase LCD string (A-Z, 0-9, space, - _ . : /) starting at tile
 * (x,y), one glyph per tile left-to-right. Space / unknown chars render as a
 * blank LCD field. Glyphs are the readout font tiles (ink on green field). */
void MdxGfx_DrawReadoutStr(u16 x, u16 y, const char* s);

/* Draw a zero-padded unsigned number as LCD digits at tile (x,y): 'digits'
 * wide (e.g. DrawReadoutNum(x,y,7,2) renders "07"). Each digit is one glyph. */
void MdxGfx_DrawReadoutNum(u16 x, u16 y, u16 value, u8 digits);


/* --- Hardware control widgets (W0-T3/T4/T5). --- */

/* LED colour selector for MdxGfx_DrawLED. Red is the primary indicator; amber
 * is the secondary (e.g. voice/zone status). */
typedef enum
{
    MDXGFX_LEDCOLOR_RED = 0,
    MDXGFX_LEDCOLOR_AMBER = 1
} MdxGfxLedColor;

/* Rotary knob: indicator angle maps 1:1 from value/max across 8 pre-rendered
 * rotation frames (value 0 and value max produce visibly different markers).
 * 'selected' draws a SELECTED-coloured frame around the single tile. (T3) */
void MdxGfx_DrawKnob(u16 x, u16 y, u16 value, u16 max, bool selected);

/* Physical fader: a 'len'-tile groove with a movable cap whose position is
 * monotonic with value (cap index = bar_filled(len,value,max), clamped). V
 * fills bottom-up, H fills left-to-right; 'selected' frames the control. (T4) */
void MdxGfx_DrawFaderV(u16 x, u16 y, u16 len, u16 value, u16 max, bool selected);
void MdxGfx_DrawFaderH(u16 x, u16 y, u16 len, u16 value, u16 max, bool selected);

/* Status LED: red (primary) or amber (secondary), lit ('on') or unlit. (T5) */
void MdxGfx_DrawLED(u16 x, u16 y, MdxGfxLedColor color, bool on);

/* Membrane / plate button: 'pressed' renders the depressed plate; an optional
 * 'label' (uppercase LCD font) is centred when the button is wide enough. (T5) */
void MdxGfx_DrawButton(u16 x, u16 y, u16 w, u16 h, bool pressed, const char* label);

/* Unified IS-4 selection highlight: a SELECTED-coloured (PAL2 idx 9) 1px frame
 * around the (x,y,w,h) tile region. Reused by every screen's selected control. */
void MdxGfx_DrawSelectedBox(u16 x, u16 y, u16 w, u16 h);


/* --- Device shell (W0-T6): DX-7 x Mega Drive frame. --- */

/* Draw the persistent device shell on BG_A: a charcoal console bezel (bevel +
 * Sega-blue corner accents, faux screws/vents) carrying a DX-Green panel and an
 * "MDX-4" green-LCD branding plate. The inner content rectangle is PAGE_*.
 * Drawn once per screen Enter (NOT in Handle) after MdxUi_ClearScreen(). */
void MdxGfx_DrawDeviceShell(void);


#endif /* _MDX_GFX_H_ */
