/*
 * main.c - MDX-4 Phase 1 "Sound Test" ROM.
 *
 * Boots SGDK, brings up the YM2612 driver (channel 1, 4-operator FM voice),
 * and enters a VBlank-tied controller loop:
 *
 *   C  (held)   -> note on  (all 4 operators of channel 1), release -> note off
 *   Up / Down   -> shift the current note up/down one step in the table
 *   Left/Right  -> raise / lower the carrier (OP4/S4) total level (0..127)
 *
 * The note/frequency change is audible immediately (SetFrequency / TL write
 * happen on the same frame the button is pressed). All YM2612 access goes
 * through mdx_ym2612.c; no direct port writes, no XGM, no floating point.
 */

#include <genesis.h>
#include "mdx_ym2612.h"


/* ----------------------------------------------------------------------- */
/* Note table (NTSC, block 4).                                              */
/*                                                                          */
/* fnum values come from the standard YM2612 pitch formula:                 */
/*   Hz = fnum * 2^block * 53267 / 2097152                                  */
/* which yields an in-tune 12-tone scale (C4=644 ... B4=1214). The 11-bit   */
/* fnum fits the driver's A0/A4 write path. C5 is included as a wrap-safe    */
/* top entry (still < 2047).                                                */
/* ----------------------------------------------------------------------- */
typedef struct
{
    u8  block;
    u16 fnum;
} MdxNote;

static const MdxNote NOTE_TABLE[] =
{
    { 4,  644 },   /* C4  */
    { 4,  681 },   /* C#4 */
    { 4,  722 },   /* D4  */
    { 4,  765 },   /* D#4 */
    { 4,  810 },   /* E4  */
    { 4,  858 },   /* F4  */
    { 4,  910 },   /* F#4 */
    { 4,  964 },   /* G4  */
    { 4, 1021 },   /* G#4 */
    { 4, 1081 },   /* A4  */
    { 4, 1146 },   /* A#4 */
    { 4, 1214 },   /* B4  */
    { 4, 1288 },   /* C5  */
};

#define NOTE_COUNT  (sizeof(NOTE_TABLE) / sizeof(NOTE_TABLE[0]))


/* Minimal 3-digit unsigned decimal formatter (no libc printf, no float). */
static void itoa3(u16 value, char *buf)
{
    buf[0] = (char)('0' + (value / 100) % 10);
    buf[1] = (char)('0' + (value / 10)  % 10);
    buf[2] = (char)('0' + (value)       % 10);
    buf[3] = '\0';
}


int main(bool hardReset)
{
    (void)hardReset;

    /* --- Boot: disable interrupts around VDP access, re-enable after. --- */
    SYS_disableInts();
    VDP_init();
    VDP_clearTextLine(0);
    VDP_drawText("MDX-4 SOUND TEST", 1, 0);
    SYS_enableInts();

    /* --- YM2612 driver boot (channel 1, PSG silenced, pan set). --- */
    MdxYm2612_Init();

    /* --- Configure channel 1 as a SAFE, AUDIBLE 4-op FM voice. --- */
    /* Algorithm 0, feedback 2: only OP4 (S4) is the carrier. */
    MdxYm2612_WriteReg(1, MDX_REG_B0, 0, (u8)((2 << 3) | 0));

    {
        u8 i;
        for (i = 0; i < 4; i++)
        {
            const u8 isCarrier = (i == 3) ? 1 : 0;   /* S4 is the carrier */
            const u8 mul = isCarrier ? 1 : 2;          /* gentle ratios   */
            const u8 tl  = isCarrier ? 40 : 99;        /* silence mods    */
            const u8 ar  = 20;                         /* moderate attack */
            const u8 d1r = 15;                         /* decay 1        */
            const u8 sl  = 3;                          /* sustain level  */
            const u8 d2r = 10;                         /* decay 2        */
            const u8 rr  = 5;                          /* release        */

            /* Detune 0, so DT_MUL value is just the multiplier. */
            MdxYm2612_WriteReg(1, MDX_REG_DT_MUL, MDX_OP_OFFSET[i],
                               (u8)((0 << 4) | mul));
            /* Total level (7-bit). */
            MdxYm2612_WriteReg(1, MDX_REG_TL, MDX_OP_OFFSET[i], tl);
            /* Key scaling 0, attack rate. */
            MdxYm2612_WriteReg(1, MDX_REG_KS_AR, MDX_OP_OFFSET[i],
                               (u8)((0 << 6) | ar));
            /* AM off, decay1 rate. */
            MdxYm2612_WriteReg(1, MDX_REG_AM_D1R, MDX_OP_OFFSET[i],
                               (u8)((0 << 7) | d1r));
            /* Decay2 rate. */
            MdxYm2612_WriteReg(1, MDX_REG_D2R, MDX_OP_OFFSET[i], d2r);
            /* Release (high nibble) + sustain level (low nibble). */
            MdxYm2612_WriteReg(1, MDX_REG_D1L_RR, MDX_OP_OFFSET[i],
                               (u8)((rr << 4) | sl));
            /* SSG-EG off. */
            MdxYm2612_WriteReg(1, MDX_REG_SSG_EG, MDX_OP_OFFSET[i], 0);
        }
    }

    /* --- State. --- */
    bool noteOn    = FALSE;
    u8   curNote   = 9;     /* start on A4 (matches the example table entry) */
    u8   carrierTL = 40;    /* carrier total level */
    u16  prevPad   = 0;

    /* Set the starting pitch. */
    MdxYm2612_SetFrequency(1, NOTE_TABLE[curNote].block, NOTE_TABLE[curNote].fnum);

    /* Status-line buffer: "N=xx T=xxx\0" (indices 0..11). */
    char status[12];

    /* --- Main controller loop (VBlank-synced). --- */
    while (TRUE)
    {
        SYS_doVBlankProcess();

        const u16 pad = JOY_readJoypad(JOY_1);

        /* C held = note on; release = note off. Edge-triggered so we only
         * write the key-on/key-off on the press/release transition. */
        {
            const bool cNow  = (pad  & BUTTON_C) != 0;
            const bool cPrev = (prevPad & BUTTON_C) != 0;

            if (cNow && !cPrev)
            {
                MdxYm2612_KeyOn(1, 0x0F);   /* all 4 operators */
                noteOn = TRUE;
            }
            else if (!cNow && cPrev)
            {
                MdxYm2612_KeyOff(1);
                noteOn = FALSE;
            }
        }

        /* Up / Down: shift the note by one step (edge-triggered, clamped). */
        if ((pad & BUTTON_UP) && !(prevPad & BUTTON_UP))
        {
            if (curNote < (NOTE_COUNT - 1)) curNote++;
            MdxYm2612_SetFrequency(1, NOTE_TABLE[curNote].block,
                                      NOTE_TABLE[curNote].fnum);
        }
        if ((pad & BUTTON_DOWN) && !(prevPad & BUTTON_DOWN))
        {
            if (curNote > 0) curNote--;
            MdxYm2612_SetFrequency(1, NOTE_TABLE[curNote].block,
                                      NOTE_TABLE[curNote].fnum);
        }

        /* Left / Right: change carrier total level (clamped 0..127). */
        if ((pad & BUTTON_RIGHT) && !(prevPad & BUTTON_RIGHT))
        {
            if (carrierTL < 127) carrierTL++;
            MdxYm2612_WriteReg(1, MDX_REG_TL, MDX_OP_OFFSET[3], carrierTL);
        }
        if ((pad & BUTTON_LEFT) && !(prevPad & BUTTON_LEFT))
        {
            if (carrierTL > 0) carrierTL--;
            MdxYm2612_WriteReg(1, MDX_REG_TL, MDX_OP_OFFSET[3], carrierTL);
        }

        /* Safety: if nothing is held, make sure the tone is off. */
        if (!noteOn)
        {
            MdxYm2612_KeyOff(1);
        }

        /* Status line (line 2). */
        status[0] = 'N'; status[1] = '=';
        itoa3((u16)curNote, &status[2]);
        status[5] = ' '; status[6] = 'T'; status[7] = '=';
        itoa3((u16)carrierTL, &status[8]);
        VDP_drawText(status, 1, 2);

        prevPad = pad;
    }

    return 0;
}
