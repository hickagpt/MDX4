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
 * Controls:
 *   Navigate mode : Up/Down select slot, A edit name, C load, B back.
 *   Edit mode     : Left/Right move char cursor, Up/Down cycle char,
 *                   A commit+save, B cancel.
 */

#include "mdx_patch_screen.h"

#include "mdx_ui.h"
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

/* Layout. */
#define LIST_COL   1
#define NAME_COL   4   /* name starts after "NN " (3 chars) */
#define LIST_ROW0  3   /* first slot on row 3 */
#define STATUS_ROW 1
#define HINT_ROW   26


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


/* Draw one slot line: "NN NAME" (or "NN ----" when the slot is empty). */
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

    MdxUi_DrawText(LIST_COL, LIST_ROW0 + idx, line);
}


/* Draw the slot-number + status string at STATUS_ROW. */
static void draw_status(void)
{
    char num[3];

    num[0] = (char)('0' + (s_sel / 10));
    num[1] = (char)('0' + (s_sel % 10));
    num[2] = '\0';

    MdxUi_ClearLine(STATUS_ROW);

    if (!MdxStorage_IsAvailable())
    {
        MdxUi_DrawText(LIST_COL, STATUS_ROW, "SAVE/LOAD UNAVAILABLE");
        return;
    }

    switch (s_status)
    {
        case 1:  /* saved */    MdxUi_DrawText(LIST_COL, STATUS_ROW, "SAVED SLOT "); MdxUi_DrawText(LIST_COL + 11, STATUS_ROW, num); break;
        case 2:  /* loaded */   MdxUi_DrawText(LIST_COL, STATUS_ROW, "LOADED SLOT "); MdxUi_DrawText(LIST_COL + 12, STATUS_ROW, num); break;
        case 4:  /* empty */    MdxUi_DrawText(LIST_COL, STATUS_ROW, "SLOT "); MdxUi_DrawText(LIST_COL + 5, STATUS_ROW, num); MdxUi_DrawText(LIST_COL + 7, STATUS_ROW, " EMPTY"); break;
        case 5:  /* defaults */ MdxUi_DrawText(LIST_COL, STATUS_ROW, "DEFAULTS LOADED"); break;
        default: MdxUi_DrawText(LIST_COL, STATUS_ROW, "A:EDIT  C:LOAD  B:BACK"); break;
    }
}


static void draw_hint(void)
{
    MdxUi_ClearLine(HINT_ROW);
    if (s_edit)
        MdxUi_DrawText(LIST_COL, HINT_ROW, "U/D:CHG L/R:MOVE A:SAVE B:CANCEL");
    else
        MdxUi_DrawText(LIST_COL, HINT_ROW, "U/D:SEL A:EDIT C:LOAD B:BACK");
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

    MdxUi_ClearLine(0);
    MdxUi_DrawText(1, 0, "PATCH MANAGER");

    draw_status();
    draw_all_slots();
    draw_hint();

    MdxUi_MoveCursor(0, LIST_ROW0 + s_sel);
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
            MdxUi_MoveCursor(0, LIST_ROW0 + s_sel);
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
            draw_status();
            draw_hint();
            MdxUi_MoveCursor(0, LIST_ROW0 + s_sel);
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

        /* keep the cursor on the editing cell */
        MdxUi_MoveCursor((u16)(NAME_COL + s_charPos), LIST_ROW0 + s_sel);
        return;
    }

    /* ---- navigate mode ---- */
    if (i->b) { MdxUi_SetScreen(SCREEN_MAIN); return; }

    if (i->up)
    {
        if (s_sel > 0) s_sel--;
        draw_slot(s_sel + 1);
        draw_slot(s_sel);
        MdxUi_MoveCursor(0, LIST_ROW0 + s_sel);
        return;
    }
    if (i->down)
    {
        if (s_sel < MDX4_PATCH_SLOTS - 1) s_sel++;
        draw_slot(s_sel - 1);
        draw_slot(s_sel);
        MdxUi_MoveCursor(0, LIST_ROW0 + s_sel);
        return;
    }

    if (i->a)   /* enter name-edit mode */
    {
        s_edit = 1;
        s_charPos = 0;
        draw_hint();
        MdxUi_MoveCursor((u16)(NAME_COL + s_charPos), LIST_ROW0 + s_sel);
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
