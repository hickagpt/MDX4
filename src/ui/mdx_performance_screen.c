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

/* Edit-overlay rows. */
#define ROW_HEADER      0
#define ROW_NOTE        2
#define PANEL_ROW0      5        /* BEND / FB / LFRQ / LDEP                 */
#define PANEL_COUNT     4
#define ROW_EDITHELP    10
#define ROW_HELP        20
#define VAL_COL         9

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
    /* Worst case "NOTE C# 6  84" is 13 chars + NUL = 14; size 16 to be safe. */
    char      buf[16];
    const char* nm = s_noteName[pc];
    char*     p = buf;
    const char* s;
    u16       n = s_selNote;
    u8        d0, d1, d2;

    s = "NOTE ";
    while (*s) *p++ = *s++;
    *p++ = nm[0];
    if (nm[1] == '#') *p++ = '#';
    *p++ = (char)('0' + oct);
    *p++ = ' ';
    *p++ = ' ';
    d0 = (char)('0' + (n % 10));
    d1 = (char)('0' + ((n / 10) % 10));
    d2 = (char)('0' + (n / 100));
    *p++ = (n >= 100) ? d2 : ' ';
    *p++ = (n >= 10)  ? d1 : ' ';
    *p++ = d0;
    *p = '\0';

    MdxUi_ClearLine(ROW_NOTE);
    MdxUi_DrawText(1, ROW_NOTE, buf);
}

static void draw_keyboard(void)
{
    u8  i;
    u8  col = KB_COL;

    for (i = 0; i < KB_COUNT; i++)
    {
        char ch[2];
        ch[0] = s_chroma[(u8)((KB_BASE + i) % 12)];
        ch[1] = '\0';
        VDP_drawText(ch, (s16)col, (s16)KB_ROW);
        col++;
    }
}

static void draw_header(void)
{
    MdxUi_ClearLine(ROW_HEADER);
    MdxUi_DrawText(1, ROW_HEADER, "PERFORMANCE");
    MdxUi_DrawText(16, ROW_HEADER, (s_mode == MODE_EDIT) ? "EDIT" : "PLAY");
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

/* Draw one edit-panel row (name + value). Bend shows a signed value. */
static void draw_panel_row(u8 idx)
{
    const u16 row = (u16)(PANEL_ROW0 + idx);

    MdxUi_ClearLine(row);
    MdxUi_DrawText(1, row, s_modName[idx]);

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
        MdxUi_DrawText(VAL_COL, row, vbuf);
    }
    else
    {
        MdxUi_DrawNumber(VAL_COL, row, get_mod_val(idx), 3);
    }
}

static void draw_panel_all(void)
{
    u8 i;
    for (i = 0; i < PANEL_COUNT; i++) draw_panel_row(i);
}

static void clear_panel(void)
{
    MdxUi_ClearLine(PANEL_ROW0);
    MdxUi_ClearLine(PANEL_ROW0 + 1);
    MdxUi_ClearLine(PANEL_ROW0 + 2);
    MdxUi_ClearLine(PANEL_ROW0 + 3);
    MdxUi_ClearLine(ROW_EDITHELP);
}


/* Toggle into EDIT: start bend centred. */
static void enter_edit(void)
{
    s_mode   = MODE_EDIT;
    s_bend   = 0;
    s_modSel = MOD_BEND;
    MdxSynth_GetPatch(&s_patch);

    draw_header();
    draw_panel_all();

    MdxUi_ClearLine(ROW_EDITHELP);
    MdxUi_DrawText(1, ROW_EDITHELP, "U/D:SEL L/R:VAL A/B:PLAYMODE C:PLAY");
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
    s_selNote  = KB_BASE;
    s_playNote = 0xFF;
    s_playing  = FALSE;
    s_hold     = FALSE;
    s_bend     = 0;
    s_modSel   = MOD_BEND;
    s_mode     = MODE_PLAY;

    MdxSynth_GetPatch(&s_patch);

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
