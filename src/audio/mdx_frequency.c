/*
 * mdx_frequency.c - precomputed MIDI note -> YM2612 frequency table (SPEC 6.3).
 *
 * Generated (offline) from the Sega YM2612 pitch formula using the NTSC
 * FM clock f_FMCLK = 8.9488625 MHz (master 53.693175 MHz / 6):
 *
 *   FNUM = 144 * f_note * 2^(21 - BLOCK) / f_FMCLK
 *   f_note = 440 * 2^((midi - 69) / 12)
 *
 * For each note we choose the smallest BLOCK (0..7) that keeps FNUM in the
 * legal 10-bit range [1, 1023]; the table is then monotonic in frequency.
 * There is NO runtime division and NO floating point - only table lookups.
 */

#include "mdx_frequency.h"


/* Per-MIDI-note (0..127) BLOCK value (3-bit, 0..7). */
const u16 MDX_NOTE_BLOCK[128] =
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7
};

/* Per-MIDI-note (0..127) FNUM value (10-bit, 1..1023). */
const u16 MDX_NOTE_FNUM[128] =
{
     276,  292,  310,  328,  348,  368,  390,  413,  438,  464,  492,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983,  521,
     552,  585,  619,  656,  695,  737,  780,  827,  876,  928,  983, 1023,
    1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023,
    1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023
};


/* Set the channel pitch from a MIDI note (clamped to 0..127).
 * Writes 0xA4 (high: block + FNUM MSB) THEN 0xA0 (low: FNUM LSB). */
void MdxFrequency_SetNote(u8 channel, u8 midiNote)
{
    u8 note = midiNote;

    if (note > 127) note = 127;

    MdxYm2612_SetFrequency(channel, (u8)MDX_NOTE_BLOCK[note], MDX_NOTE_FNUM[note]);
}
