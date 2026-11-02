/*
 * mdx_performance_screen.c - on-screen virtual keyboard (SPEC 7.1 / 7.2 / 7.3).
 *
 * A monophonic performance screen for interactive sound testing:
 *   - an on-screen keyboard spanning approximately two octaves;
 *   - D-Pad Left/Right move the selected note by one semitone, Up/Down jump an
 *     octave, all clamped to the ~2-octave window;
 *   - C plays/holds the selected note (release to stop); A is an alternate
 *     trigger in play mode;
 *   - moving the selection to a NEW note always releases the previous note and
 *     plays the new one - a note is never left stuck.
 *
 * An A-toggled EDIT overlay adds the pitch bend (a controller-driven portamento
 * that bends the sounding note up/down a few semitones and back to base) and the
 * improved modulation controls (feedback + LFO frequency + LFO pitch depth),
 * all controller-driven - no MIDI.
 *
 * Rendering: the static keyboard strip + header are drawn once on Enter; the
 * Handle() path only repaints the lines that changed and moves the sprite
 * cursor - never a full-screen redraw.
 */

#include "mdx_performance_screen.h"

#include "mdx_gfx.h"
#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"
#include "core/mdx_patch.h"


/* ----- Keyboard geometry. ----- */
#define KB_COL          2        /* first column of the keyboard strip      */
#define KB_ROW          14       /* row of the static keyboard strip        */
#define KB_BASE         60       /* C4 - lowest note of the window          */
#define KB_COUNT        25       /* C4..C6 -> ~2 octaves (inclusive)        */
#define KB_LAST         (KB_BASE + KB_COUNT - 1)   /* C6 = 84               */
#define KB_OCTAVE_JUMP  12

/* Pitch bend range (semitones, signed) - a few semitones up/down and back. */
#define BEND_MAX        12

/* Note readout LCD window (inside PAGE_*): recessed green-LCD plate. */
#define NOTE_RD_X       2
#define NOTE_RD_Y       2
#define NOTE_RD_W       14
#define NOTE_RD_H       4

/* EDIT overlay panel (inside the device-shell PAGE_*). The plate is a bordered
 * MdxGfx_DrawPanel; rows 7..10 hold BEND/FB/LFRQ/LDEP, row 11 the help line.
 * Row 0 is reserved for the shell's "MDX-4" branding and is never touched. */
#define EDIT_PANEL_X    2
#define EDIT_PANEL_Y    6        /* top border row of the plate           */
#define EDIT_PANEL_W    36
#define EDIT_PANEL_H    7        /* rows 6..12, interior rows 7..11       */
#define PANEL_ROW0      7        /* first BEND row (panel interior)       */
#define PANEL_COUNT     4
#define ROW_EDITHELP    11
#define ROW_HELP        20
#define LABEL_COL       3
#define VAL_COL         22

enum { MOD_BEND = 0, MOD_FB, MOD_LFRQ, MOD_LDEP };

static const char s_chroma[12] = { 'C','#','D','#','E','F','#','G','#','A','#','B' };

static const char s_noteName[12][3] =
{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const char s_modName[PANEL_COUNT][6] =
{
    "BEND", "FB   ", "LFRQ ", "LDEP "
};


/* ----- State. ----- */
enum { MODE_PLAY = 0, MODE_EDIT };

static u8  s_mode     = MODE_PLAY;
static u8  s_selNote  = KB_BASE;   /* cursor's MIDI note                    */
static u8  s_playNote = 0xFF;      /* MIDI note currently held by the KB    */
static bool s_playing = FALSE;     /* a keyboard note is currently sounding */
static bool s_hold    = FALSE;     /* last sounding note was button-held    */
static s8  s_bend     = 0;         /* pitch bend, -BEND_MAX..+BEND_MAX      */
static u8  s_modSel   = MOD_BEND;  /* selected edit parameter               */
static FMPatch s_patch;            /* snapshot for display                  */


/* ----- Helpers. ----- */

static u8 clamp_note(s16 n)
{
    if (n < 0)    n = 0;
    if (n > 127)  n = 127;
    return (u8)n;
}

/* Release any held keyboard note and play *note* - the single no-stuck-note
 * path for the keyboard: the previous note is always released first. */
static void perf_play(u8 note)
{
    if ((note == s_playNote) && s_playing) return;   /* already sounding */

    MdxSynth_NoteOff();                /* release previous (never stuck) */
    MdxSynth_NoteOn(note);             /* play the new note              */
    s_playNote = note;
    s_playing  = TRUE;
}

static void perf_stop(void)
{
    if (s_playing)
    {
        MdxSynth_NoteOff();
        s_playNote = s_selNote;   /* next perf_play on the same note is fresh */
        s_playing  = FALSE;
    }
}

/* Apply the current bend to a sounding note (live pitch change, no retrigger). */
static void perf_bend_apply(void)
{
    if (!s_playing) return;
    MdxSynth_NoteOn(clamp_note((s16)s_selNote + s_bend));
}

/* --- drawing. --- */

static void draw_note_readout(void)
{
    const u8  pc   = s_selNote % 12;
    const u8  oct  = (u8)(s_selNote / 12 - 1);
    const char* nm = s_noteName[pc];
    char      tag[4];
    u8        t = 0;

    /* The LCD readout font (A-Z, 0-9, - _ . : /) has no '#' glyph, so the
     * sharp is dropped from the label; the MIDI number disambiguates pitch. */
    tag[t++] = nm[0];
    tag[t++] = (char)('0' + oct);
    tag[t]   = '\0';                 /* e.g. "C4", "E4", "C6"            */

    MdxGfx_DrawReadout(NOTE_RD_X, NOTE_RD_Y, NOTE_RD_W, NOTE_RD_H);
    MdxGfx_DrawReadoutStr(NOTE_RD_X + 1, NOTE_RD_Y + 1, tag);
    MdxGfx_DrawReadoutNum(NOTE_RD_X + 6, NOTE_RD_Y + 1, (u16)s_selNote, 3);
}

static void draw_keyboard(void)
{
    u8  i;
    u8  col = KB_COL;

    for (i = 0; i < KB_COUNT; i++)
    {
        const u8   pc      = (u8)((KB_BASE + i) % 12);
        const u16  tileIdx = MdxGfx_KeyTile(
            ((pc == 1) || (pc == 3) || (pc == 6) || (pc == 8) || (pc == 10))
                ? MDXGFX_KEY_BLACK : MDXGFX_KEY_WHITE);
        VDP_setTileMapXY(BG_A,
                         TILE_ATTR_FULL(MDXGFX_PAL_KEYS, 0, FALSE, FALSE, tileIdx),
                         (u16)col, KB_ROW);
        col++;
    }
}

static void draw_header(void)
{
    /* Row 0 carries the shell's "MDX-4" branding - never clear it. The title and
     * mode indicator sit on the green panel, to the right of the note readout. */
    MdxUi_DrawText(18, 2, "PERFORMANCE");
    MdxUi_DrawText(31, 2, (s_mode == MODE_EDIT) ? "EDIT" : "PLAY");
}

static u8 get_mod_val(u8 idx)
{
    switch (idx)
    {
        case MOD_FB:    return s_patch.feedback;
        case MOD_LFRQ:  return s_patch.lfoFrequency;
        case MOD_LDEP:  return s_patch.lfoPitchDepth;
        default:        return (u8)(s_bend + 12);   /* shifted 0..24 */
    }
}

/* Draw one edit-panel row (name + value). Bend shows a signed value. The label
 * is PAL0 text; the value is the green-LCD readout font, both painted on top of
 * the panel plate - no line clear (ClearLine would punch a hole in the panel). */
static void draw_panel_row(u8 idx)
{
    const u16 row = (u16)(PANEL_ROW0 + idx);

    MdxUi_DrawText(LABEL_COL, row, s_modName[idx]);

    if (idx == MOD_BEND)
    {
        char vbuf[6];
        s8   b = s_bend;
        char* p = vbuf;

        if (b < 0) { *p++ = '-'; b = (s8)(-b); }
        else       { *p++ = ' '; }
        vbuf[4] = '\0';
        vbuf[3] = (char)('0' + (b % 10));
        vbuf[2] = (char)('0' + (b / 10));
        vbuf[1] = ' ';
        *p = '\0';
        MdxGfx_DrawReadoutStr(VAL_COL, row, vbuf);
    }
    else
    {
        MdxGfx_DrawReadoutNum(VAL_COL, row, get_mod_val(idx), 3);
    }
}

static void draw_panel_all(void)
{
    u8 i;
    for (i = 0; i < PANEL_COUNT; i++) draw_panel_row(i);
}

/* Draw the bordered plate that surrounds the edit overlay's rows + help line. */
static void draw_edit_panel_plate(void)
{
    MdxGfx_DrawPanel(EDIT_PANEL_X, EDIT_PANEL_Y, EDIT_PANEL_W, EDIT_PANEL_H);
}

/* Returning to PLAY: restore the clean green panel over the edit-overlay region.
 * Do NOT use MdxUi_ClearLine here - it would punch a black hole through the
 * shell's green panel. Refilling with the panel-fill tile matches the shell. */
static void clear_panel(void)
{
    VDP_fillTileMapRect(BG_A,
                        TILE_ATTR_FULL(MDXGFX_PAL_CHROME, 0, FALSE, FALSE, MDXGFX_TILE_FILL),
                        EDIT_PANEL_X, EDIT_PANEL_Y, EDIT_PANEL_W, EDIT_PANEL_H);
}


/* Toggle into EDIT: start bend centred. */
static void enter_edit(void)
{
    s_mode   = MODE_EDIT;
    s_bend   = 0;
    s_modSel = MOD_BEND;
    MdxSynth_GetPatch(&s_patch);

    draw_header();
    draw_edit_panel_plate();
    draw_panel_all();

    MdxUi_DrawText(LABEL_COL, ROW_EDITHELP, "U/D:SEL L/R:VAL A/B:PLAYMODE C:PLAY");
    MdxUi_MoveCursor(1, (u16)(PANEL_ROW0 + s_modSel));
}

/* Toggle back to PLAY: release bend to base. */
static void enter_play(void)
{
    s_mode = MODE_PLAY;
    s_bend = 0;

    perf_bend_apply();          /* sounding note returns to base */

    clear_panel();
    draw_header();
    MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
}


void MdxPerformanceScreen_Enter(void)
{
    MdxGfx_Init();              /* ensure PAL3 + white/black key tiles exist */

    s_selNote  = KB_BASE;
    s_playNote = 0xFF;
    s_playing  = FALSE;
    s_hold     = FALSE;
    s_bend     = 0;
    s_modSel   = MOD_BEND;
    s_mode     = MODE_PLAY;

    MdxSynth_GetPatch(&s_patch);

    MdxUi_ClearScreen();
    MdxGfx_DrawDeviceShell();

    draw_header();
    draw_note_readout();
    draw_keyboard();

    MdxUi_ClearLine(ROW_HELP);
    MdxUi_DrawText(1, ROW_HELP, "L/R:NOTE  U/D:OCT  C:PLAY  A:EDIT  B:BACK");

    MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
}


/* --- edit-mode handling (pitch bend + feedback + LFO). --- */
static void handle_edit(const MdxInput* i)
{
    if (i->a || i->b) { enter_play(); return; }

    if (i->up)   { if (s_modSel > 0)                 s_modSel--; MdxUi_MoveCursor(1, (u16)(PANEL_ROW0 + s_modSel)); }
    if (i->down) { if (s_modSel < (PANEL_COUNT - 1)) s_modSel++; MdxUi_MoveCursor(1, (u16)(PANEL_ROW0 + s_modSel)); }

    if (i->left || i->right)
    {
        const s8  dir = i->right ? 1 : -1;
        s16       v;

        switch (s_modSel)
        {
            case MOD_BEND:
                v = (s16)s_bend + dir;
                if (v < -BEND_MAX) v = -BEND_MAX;
                if (v >  BEND_MAX) v =  BEND_MAX;
                s_bend = (s8)v;
                perf_bend_apply();          /* bend the sounding note live */
                draw_panel_row(MOD_BEND);
                break;

            case MOD_FB:
            {
                s16 fb = (s16)s_patch.feedback + dir;
                if (fb < 0) fb = 0;
                if (fb > 7) fb = 7;
                MdxSynth_SetFeedback((u8)fb);
                if (s_playing)              /* retrigger so the change is audible */
                    perf_bend_apply();
                MdxSynth_GetPatch(&s_patch);
                draw_panel_row(MOD_FB);
                break;
            }

            case MOD_LFRQ:
            {
                s16 f = (s16)s_patch.lfoFrequency + dir;
                if (f < 0) f = 0;
                if (f > 7) f = 7;
                MdxSynth_SetLfoFrequency((u8)f);
                MdxSynth_GetPatch(&s_patch);
                draw_panel_row(MOD_LFRQ);
                break;
            }

            default: /* MOD_LDEP */
            {
                s16 d = (s16)s_patch.lfoPitchDepth + dir;
                if (d < 0) d = 0;
                if (d > 7) d = 7;
                MdxSynth_SetLfoPitchDepth((u8)d);
                MdxSynth_GetPatch(&s_patch);
                draw_panel_row(MOD_LDEP);
                break;
            }
        }
    }

    /* C auditions the currently bent note (hold to sustain, release to stop). */
    if (i->c)
    {
        perf_play(clamp_note((s16)s_selNote + s_bend));
        s_hold = TRUE;
    }
    else if (s_hold && s_playing && !MdxInput_Held(BUTTON_C))
    {
        perf_stop();
        s_hold = FALSE;
    }
}


/* --- play-mode handling (keyboard navigation + trigger). --- */
static void move_sel(u8 newNote)
{
    u8 n = newNote;

    if (n < KB_BASE)  n = KB_BASE;
    if (n > KB_LAST)  n = KB_LAST;
    if (n == s_selNote) return;

    s_selNote = n;
    draw_note_readout();
    MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
    perf_play(s_selNote);     /* release previous + play new (never stuck) */
}

static void handle_play(const MdxInput* i)
{
    if (i->left)  move_sel((u8)(s_selNote - 1));
    if (i->right) move_sel((u8)(s_selNote + 1));
    if (i->up)
    {
        u8 n = s_selNote;
        move_sel((n <= (u8)(KB_LAST - KB_OCTAVE_JUMP)) ? (u8)(n + KB_OCTAVE_JUMP) : KB_LAST);
    }
    if (i->down)
    {
        u8 n = s_selNote;
        move_sel((n >= (u8)(KB_BASE + KB_OCTAVE_JUMP)) ? (u8)(n - KB_OCTAVE_JUMP) : KB_BASE);
    }

    /* Back: release any sounding note BEFORE leaving so no key-on dangles. */
    if (i->b)
    {
        perf_stop();
        MdxUi_SetScreen(SCREEN_MAIN);
        return;
    }

    /* A toggles into the pitch-bend + modulation EDIT overlay. */
    if (i->a)
    {
        enter_edit();
        return;
    }

    /* Trigger: C edge -> play the selected note; release -> stop. */
    if (i->c)
    {
        perf_play(s_selNote);
        s_hold = TRUE;
    }
    else if (s_hold && s_playing && !MdxInput_Held(BUTTON_C))
    {
        perf_stop();
        s_hold = FALSE;
    }
}


void MdxPerformanceScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;

    if (i == NULL) return;

    if (s_mode == MODE_EDIT) handle_edit(i);
    else                     handle_play(i);
}


void MdxPerformanceScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_PERFORMANCE,
                         MdxPerformanceScreen_Enter,
                         MdxPerformanceScreen_Handle);
}
