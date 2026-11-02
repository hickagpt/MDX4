/*
 * mdx_patch_screen.c - Patch Manager screen (SPEC 8.1 / 8.3 / Phase 4).
 *
 * Lists instrument slots, allows name editing, saving the sounding engine
 * patch into a slot, and loading a slot back into the engine. Persistence uses
 * the versioned + checksummed blob format (MdxPatchFormat_*) over the
 * no-SRAM-guarded storage layer, so a cart/region without backup RAM degrades
 * to a read-only "UNAVAILABLE" message and never crashes or writes ROM.
 *
 * On entry the screen loads the blob from SRAM; if that fails (no SRAM, corrupt
 * or version mismatch) it falls back to blank/default slots and stays fully
 * usable. Invalid data is rejected by the format layer before any field is
 * trusted (see mdx_patch_format.c).
 *
 * Build Wave 2 (T18): the screen now renders on the DX-7 x Mega Drive device
 * shell (MdxGfx_DrawDeviceShell) inside PAGE_* with the 16-slot list, a
 * readout-backed status line, and an inline name editor. All storage, the
 * navigate/edit state machine, the charset cycling, and C/A/B semantics are
 * byte-for-byte preserved.
 *
 * Controls:
 *   Navigate mode : Up/Down select slot, A edit name, C load, B back.
 *   Edit mode     : Left/Right move char cursor, Up/Down cycle char,
 *                   A commit+save, B cancel.
 */

#include "mdx_patch_screen.h"

#include "mdx_ui.h"
#include "mdx_gfx.h"           /* MdxGfx toolkit: shell, panel, readout, selected box */
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"
#include "storage/mdx_storage.h"
#include "storage/mdx_patch_format.h"


/* Number of instrument slots in the UI / blob. */
#define MDX4_PATCH_SLOTS   16

/* Where the blob lives in SRAM (logical byte offset; storage layer owns base). */
#define MDX4_PATCH_BASEOFF 0

/* Name field width. Index 15 is forced to a NUL terminator so VDP_drawText() on
 * a name is always safe (15 usable characters). */
#define NAME_LEN       16
#define NAME_EDIT_MAX  15

/* Layout (inside PAGE_* = cols 2..37, rows 2..25). The device shell owns the
 * 2-tile bezel; all content lives on the DX-Green panel face. */
#define HEADER_X    2
#define HEADER_Y    2
#define HEADER_W    36
#define HEADER_H    2           /* rows 2..3 */

#define SLOT_PANEL_X    2
#define SLOT_PANEL_Y    4
#define SLOT_PANEL_W    36
#define SLOT_PANEL_H    18      /* rows 4..21; interior rows 5..20 (16 slots) */
#define SLOT_ROW0       5       /* first slot line (interior top) */
#define SLOT_COL        3       /* slot-line text column (overwrites sel box) */
#define SLOT_LINE_W     19      /* "NN NAME" width; == sel-box width (clears it) */

#define STATUS_X    2
#define STATUS_Y    22
#define STATUS_W    36
#define STATUS_H    3           /* rows 22..24; interior row 23 (filled LCD) */
#define STATUS_IN_X 3
#define STATUS_IN_Y 23

#define NAME_COL    STATUS_IN_X /* name-editor cursor base column */
#define NAMEEDIT_Y  STATUS_IN_Y /* status-region row during edit */

#define HINT_X  3
#define HINT_Y  25              /* bottom row of PAGE_* */


/* Character set used by the inline name editor (index 0 = space). */
static const char s_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
#define CHARSET_LEN  39


/* Screen state. */
static FMPatch s_slots[MDX4_PATCH_SLOTS];
static u8      s_sel     = 0;
static u8      s_edit    = 0;   /* 0 = navigate, 1 = editing name */
static u8      s_charPos = 0;   /* 0..NAME_EDIT_MAX */
static u8      s_status  = 0;   /* 0 none, 1 saved, 2 loaded, 3 nosram/fail,
                                    4 empty, 5 defaults */


/* ----- helpers. ----- */

static u8 slot_is_empty(const FMPatch* p)
{
    return (p->name[0] == '\0');
}


static u8 char_to_idx(char c)
{
    u8 i;
    for (i = 0; i < CHARSET_LEN; i++)
        if (s_charset[i] == c) return i;
    return 0;   /* space fallback for anything not in the set */
}


static void ensure_name_terminated(FMPatch* p)
{
    p->name[NAME_EDIT_MAX] = '\0';   /* index 15 -> hard terminator */
}


/* Draw one slot line on the panel: "NN NAME" (or "NN ----" when empty). */
static void draw_slot(u8 idx)
{
    char line[NAME_LEN + 4];
    u8 p = 0;
    u8 i;

    line[p++] = (char)('0' + (idx / 10));
    line[p++] = (char)('0' + (idx % 10));
    line[p++] = ' ';

    if (slot_is_empty(&s_slots[idx]))
    {
        line[p++] = '-'; line[p++] = '-'; line[p++] = '-'; line[p++] = '-';
        for (i = 4; i < NAME_LEN; i++) line[p++] = ' ';
    }
    else
    {
        for (i = 0; i < NAME_LEN; i++)
        {
            char c = s_slots[idx].name[i];
            line[p++] = (c == '\0') ? ' ' : c;
        }
    }
    line[p] = '\0';

    MdxUi_DrawText(SLOT_COL, (u16)(SLOT_ROW0 + idx), line);
}


/* Draw the status readout (recessed green-LCD plate) with the slot number /
 * status string. The readout plate is redrawn first so the previous face is
 * cleared (no MdxUi_ClearLine inside the panel - it would punch a hole). */
static void draw_status(void)
{
    char num[3];

    num[0] = (char)('0' + (s_sel / 10));
    num[1] = (char)('0' + (s_sel % 10));
    num[2] = '\0';

    MdxGfx_DrawReadout(STATUS_X, STATUS_Y, STATUS_W, STATUS_H);

    if (!MdxStorage_IsAvailable())
    {
        MdxGfx_DrawReadoutStr(STATUS_IN_X, STATUS_IN_Y, "SAVE/LOAD UNAVAILABLE");
        return;
    }

    switch (s_status)
    {
        case 1:  /* saved */    MdxGfx_DrawReadoutStr(STATUS_IN_X, STATUS_IN_Y, "SAVED SLOT "); MdxGfx_DrawReadoutStr(STATUS_IN_X + 11, STATUS_IN_Y, num); break;
        case 2:  /* loaded */   MdxGfx_DrawReadoutStr(STATUS_IN_X, STATUS_IN_Y, "LOADED SLOT "); MdxGfx_DrawReadoutStr(STATUS_IN_X + 12, STATUS_IN_Y, num); break;
        case 4:  /* empty */    MdxGfx_DrawReadoutStr(STATUS_IN_X, STATUS_IN_Y, "SLOT "); MdxGfx_DrawReadoutStr(STATUS_IN_X + 5, STATUS_IN_Y, num); MdxGfx_DrawReadoutStr(STATUS_IN_X + 7, STATUS_IN_Y, " EMPTY"); break;
        case 5:  /* defaults */ MdxGfx_DrawReadoutStr(STATUS_IN_X, STATUS_IN_Y, "DEFAULTS LOADED"); break;
        default: MdxGfx_DrawReadoutStr(STATUS_IN_X, STATUS_IN_Y, "A:EDIT C:LOAD B:BACK"); break;
    }
}


/* Inline name editor: render the (live, in-place edited) slot name in the
 * status-region readout. The charset-cycling logic mutates s_slots[s_sel].name
 * directly, so this just reflects it. */
static void draw_name_editor(void)
{
    char buf[NAME_LEN + 1];
    u8 i;

    for (i = 0; i < NAME_LEN; i++)
    {
        char c = s_slots[s_sel].name[i];
        buf[i] = (c == '\0') ? ' ' : c;
    }
    buf[NAME_EDIT_MAX] = '\0';   /* index 15 is the forced NUL already */

    MdxGfx_DrawReadout(STATUS_X, STATUS_Y, STATUS_W, STATUS_H);
    MdxGfx_DrawReadoutStr(NAME_COL, NAMEEDIT_Y, buf);
}


/* Hint strip at the bottom of PAGE_*. Both strings are the same length so a
 * redraw fully overwrites the previous face (no ClearLine in the panel). */
static void draw_hint(void)
{
    if (s_edit)
        MdxUi_DrawText(HINT_X, HINT_Y, "U/D:CHG L/R:MOVE A:SAVE B:CANCEL");
    else
        MdxUi_DrawText(HINT_X, HINT_Y, "U/D:SEL A:EDIT C:LOAD B:BACK    ");
}


/* IS-4 selection highlight: frame the active slot entry (PAL2 SELECTED). */
static void draw_selection(void)
{
    MdxGfx_DrawSelectedBox(SLOT_COL, (u16)(SLOT_ROW0 + s_sel), SLOT_LINE_W, 1);
}


static void draw_all_slots(void)
{
    u8 i;
    for (i = 0; i < MDX4_PATCH_SLOTS; i++) draw_slot(i);
}


/* ----- entry / handling. ----- */

void MdxPatchScreen_Enter(void)
{
    int res;
    u8 i;

    /* Load persisted patches; on any failure fall back to blank/default slots
     * and stay usable (no crash, no garbage loaded). */
    res = MdxPatchFormat_Load(s_slots, MDX4_PATCH_SLOTS, MDX4_PATCH_BASEOFF);
    if (res < 0)
    {
        for (i = 0; i < MDX4_PATCH_SLOTS; i++)
        {
            s_slots[i] = DEFAULT_PATCH;
            s_slots[i].name[0] = '\0';   /* mark empty */
            ensure_name_terminated(&s_slots[i]);
        }
        s_status = 5;   /* defaults loaded */
    }
    else
    {
        for (i = 0; i < MDX4_PATCH_SLOTS; i++) ensure_name_terminated(&s_slots[i]);
        s_status = 0;
    }

    s_sel = 0;
    s_edit = 0;
    s_charPos = 0;

    /* Skin canvas + device shell, then content inside PAGE_*. */
    MdxUi_ClearScreen();
    MdxGfx_Init();                  /* chrome palette + base/widget tiles (guarded) */
    MdxGfx_DrawDeviceShell();       /* DX-7 x MD frame; content laid out in PAGE_* */

    /* Header plate (skin plate + PAL0 text on top). */
    MdxGfx_DrawPanel(HEADER_X, HEADER_Y, HEADER_W, HEADER_H);
    MdxUi_DrawText((u16)(HEADER_X + 1), HEADER_Y, "PATCH MANAGER");

    /* Slot list panel + 16 slot lines inside PAGE_*. */
    MdxGfx_DrawPanel(SLOT_PANEL_X, SLOT_PANEL_Y, SLOT_PANEL_W, SLOT_PANEL_H);
    draw_all_slots();
    draw_selection();

    draw_status();
    draw_hint();

    MdxUi_MoveCursor(0, (u16)(SLOT_ROW0 + s_sel));
}


void MdxPatchScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;

    if (i == NULL) return;

    if (s_edit)
    {
        /* ---- name-edit mode ---- */
        if (i->b)                       /* cancel */
        {
            s_edit = 0;
            draw_hint();
            draw_slot(s_sel);
            draw_selection();
            MdxUi_MoveCursor(0, (u16)(SLOT_ROW0 + s_sel));
            return;
        }

        if (i->a)                       /* commit + save */
        {
            FMPatch enginePatch;

            ensure_name_terminated(&s_slots[s_sel]);

            /* Take the currently sounding patch (with its live edits), then
             * keep the (possibly edited) name from the slot. */
            MdxSynth_GetPatch(&enginePatch);
            {
                u8 n;
                for (n = 0; n < NAME_LEN; n++)
                    enginePatch.name[n] = s_slots[s_sel].name[n];
                ensure_name_terminated(&enginePatch);
            }
            s_slots[s_sel] = enginePatch;

            if (!MdxStorage_IsAvailable())
            {
                s_status = 3;   /* no SRAM */
            }
            else if (MdxPatchFormat_Save(s_slots, MDX4_PATCH_SLOTS, MDX4_PATCH_BASEOFF) == MDX_PFMT_OK)
            {
                s_status = 1;   /* saved */
            }
            else
            {
                s_status = 3;   /* save failed */
            }

            s_edit = 0;
            draw_slot(s_sel);
            draw_selection();
            draw_status();
            draw_hint();
            MdxUi_MoveCursor(0, (u16)(SLOT_ROW0 + s_sel));
            return;
        }

        /* move character cursor */
        if (i->left)       { if (s_charPos > 0) s_charPos--; }
        else if (i->right) { if (s_charPos < NAME_EDIT_MAX - 1) s_charPos++; }

        /* cycle character at the cursor */
        if (i->up || i->down)
        {
            u8 idx = char_to_idx(s_slots[s_sel].name[s_charPos]);
            if (i->up)   idx = (u8)((idx + 1) % CHARSET_LEN);
            else         idx = (u8)((idx + CHARSET_LEN - 1) % CHARSET_LEN);
            s_slots[s_sel].name[s_charPos] = s_charset[idx];
            draw_slot(s_sel);
        }

        /* refresh the inline name-editor face and keep the cursor on the cell */
        draw_name_editor();
        MdxUi_MoveCursor((u16)(NAME_COL + s_charPos), NAMEEDIT_Y);
        return;
    }

    /* ---- navigate mode ---- */
    if (i->b) { MdxUi_SetScreen(SCREEN_MAIN); return; }

    if (i->up)
    {
        if (s_sel > 0) s_sel--;
        draw_slot((u8)(s_sel + 1));
        draw_slot(s_sel);
        draw_selection();
        MdxUi_MoveCursor(0, (u16)(SLOT_ROW0 + s_sel));
        return;
    }
    if (i->down)
    {
        if (s_sel < MDX4_PATCH_SLOTS - 1) s_sel++;
        draw_slot((u8)(s_sel - 1));
        draw_slot(s_sel);
        draw_selection();
        MdxUi_MoveCursor(0, (u16)(SLOT_ROW0 + s_sel));
        return;
    }

    if (i->a)   /* enter name-edit mode */
    {
        s_edit = 1;
        s_charPos = 0;
        draw_hint();
        draw_name_editor();
        MdxUi_MoveCursor((u16)(NAME_COL + s_charPos), NAMEEDIT_Y);
        return;
    }

    if (i->c)   /* load selected slot into the engine + preview */
    {
        if (slot_is_empty(&s_slots[s_sel]))
        {
            s_status = 4;   /* empty */
            draw_status();
            return;
        }
        MdxSynth_LoadPatch(&s_slots[s_sel]);
        MdxSynth_NoteOn(MdxUi_GetPreviewNote());
        s_status = 2;   /* loaded */
        draw_status();
        return;
    }
}


void MdxPatchScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_PATCH, MdxPatchScreen_Enter, MdxPatchScreen_Handle);
}
