/*
 * mdx_performance_screen.c - on-screen virtual keyboard (SPEC 7.1 / 7.2).
 *
 * See mdx_performance_screen.h. The keyboard spans approximately two octaves;
 * the D-Pad moves the selected note and C/A trigger it. Moving the selection
 * to a new note RELEASES the previous note and plays the new one, so no note is
 * ever left stuck. Pitch control + improved modulation controls are layered on
 * in a later wave via an A-toggled edit overlay.
 *
 * Rendering: the static keyboard strip + header are drawn once on Enter; the
 * Handle() path only repaints the note readout line and moves the sprite cursor
 * when the selection changes - never a full-screen redraw.
 */

#include "mdx_performance_screen.h"

#include "mdx_ui.h"
#include "input/mdx_input.h"
#include "core/mdx_synth.h"


/* ----- Keyboard geometry. ----- */
#define KB_COL          2        /* first column of the keyboard strip      */
#define KB_ROW          14       /* row of the static keyboard strip        */
#define KB_BASE         60       /* C4 - lowest note of the window          */
#define KB_COUNT        25       /* C4..C6 -> ~2 octaves (inclusive)        */
#define KB_LAST         (KB_BASE + KB_COUNT - 1)   /* C6 = 84               */
#define KB_OCTAVE_JUMP  12

/* Edit overlay rows (added in a later wave; reserved here). */
#define ROW_HEADER      0
#define ROW_NOTE        2
#define ROW_HELP        21

/* Chromatic glyph per pitch-class: naturals + '#' sharps, 1 char per semitone.
 * Reading the strip left->right is a chromatic run C # D # E F # G # A # B ... */
static const char s_chroma[12] = { 'C','#','D','#','E','F','#','G','#','A','#','B' };

static const char s_noteName[12][3] =
{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};


/* ----- State. ----- */
static u8  s_selNote  = KB_BASE;   /* cursor's MIDI note                    */
static u8  s_playNote = 0xFF;      /* MIDI note currently held by the KB    */
static bool s_playing = FALSE;     /* a keyboard note is currently sounding */
static u8  s_noteOn   = 0;         /* last-drawn note readout state         */


/* ----- Helpers. ----- */

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

static void draw_note_readout(void)
{
    const u8  pc   = s_selNote % 12;
    const u8  oct  = (u8)(s_selNote / 12 - 1);
    char      buf[10];
    const char* nm = s_noteName[pc];

    /* "NOTE  C4   60" - fixed layout, redrawn as one line on change. */
    buf[0] = '\0';
    {
        char* p = buf;
        const char* s = "NOTE ";
        while (*s) *p++ = *s++;
        *p++ = nm[0];
        if (nm[1] == '#') *p++ = '#';
        *p++ = (char)('0' + oct);
        *p++ = ' ';
        *p++ = ' ';
        /* MIDI number, right-aligned in 3. */
        {
            u16 n = s_selNote;
            char d0 = (char)('0' + (n % 10));
            char d1 = (char)('0' + ((n / 10) % 10));
            char d2 = (char)('0' + (n / 100));
            *p++ = (n >= 100) ? d2 : ' ';
            *p++ = (n >= 10)  ? d1 : ' ';
            *p++ = d0;
        }
        *p = '\0';
    }

    MdxUi_ClearLine(ROW_NOTE);
    MdxUi_DrawText(1, ROW_NOTE, buf);
}


/* Build + draw the static chromatic keyboard strip for the current window. */
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


void MdxPerformanceScreen_Enter(void)
{
    s_selNote  = KB_BASE;
    s_playNote = 0xFF;
    s_playing  = FALSE;
    s_noteOn   = 0;

    MdxUi_ClearLine(ROW_HEADER);
    MdxUi_DrawText(1, ROW_HEADER, "PERFORMANCE");

    MdxUi_ClearLine(20);
    MdxUi_DrawText(1, 20, "L/R:NOTE  U/D:OCT  C:PLAY  A:PLAY  B:BACK");

    draw_note_readout();
    draw_keyboard();

    MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
}


void MdxPerformanceScreen_Handle(const void* input)
{
    const MdxInput* i = (const MdxInput*)input;

    if (i == NULL) return;

    /* Move selection by semitone (L/R) or octave (U/D), clamped to window. */
    if (i->left)
    {
        if (s_selNote > KB_BASE)
        {
            s_selNote--;
            draw_note_readout();
            MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
            perf_play(s_selNote);
        }
    }
    if (i->right)
    {
        if (s_selNote < KB_LAST)
        {
            s_selNote++;
            draw_note_readout();
            MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
            perf_play(s_selNote);
        }
    }
    if (i->up)
    {
        u8 n = s_selNote;
        if (n <= (u8)(KB_LAST - KB_OCTAVE_JUMP)) n += KB_OCTAVE_JUMP;
        else                                     n = KB_LAST;
        if (n != s_selNote)
        {
            s_selNote = n;
            draw_note_readout();
            MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
            perf_play(s_selNote);
        }
    }
    if (i->down)
    {
        u8 n = s_selNote;
        if (n >= (u8)(KB_BASE + KB_OCTAVE_JUMP)) n -= KB_OCTAVE_JUMP;
        else                                     n = KB_BASE;
        if (n != s_selNote)
        {
            s_selNote = n;
            draw_note_readout();
            MdxUi_MoveCursor((u16)(KB_COL + (s_selNote - KB_BASE)), KB_ROW);
            perf_play(s_selNote);
        }
    }

    /* Trigger: C or A edge -> play the selected note; release -> stop.
     * Edge/release tracking is done here (not the global preview) so the
     * performance screen is the single owner of note-on while active. */
    {
        const bool cEdge = i->c;
        const bool aEdge = i->a;
        const bool held  = MdxInput_Held(BUTTON_C | BUTTON_A) ? TRUE : FALSE;

        if ((cEdge || aEdge) && !s_playing)
            perf_play(s_selNote);
        else if (!held && s_playing)
            perf_stop();
    }

    /* Back: release any sounding note BEFORE leaving so no key-on dangles. */
    if (i->b)
    {
        perf_stop();
        MdxUi_SetScreen(SCREEN_MAIN);
    }
}


void MdxPerformanceScreen_Init(void)
{
    MdxUi_RegisterScreen(SCREEN_PERFORMANCE,
                         MdxPerformanceScreen_Enter,
                         MdxPerformanceScreen_Handle);
}
