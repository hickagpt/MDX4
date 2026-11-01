#ifndef _MDX_YM2612_H_
#define _MDX_YM2612_H_

#include <genesis.h>

/* Operator -> slot offset order for a channel (YM2612 hardware layout):
 *   S1 = 0x00, S3 = 0x04, S2 = 0x08, S4 = 0x0C
 * The ORDER is significant (the classic S2<->S3 bug).  Index 3 (S4) is the
 * final carrier for algorithm 0.
 *
 * Offsets are macros, not a static const array, because C does not treat array
 * indexing as a constant expression, which the _Static_assert below requires. */
#define MDX_OP_S1 0x00
#define MDX_OP_S3 0x04
#define MDX_OP_S2 0x08
#define MDX_OP_S4 0x0C

/* Runtime table in canonical order S1,S3,S2,S4. */
static const u8 MDX_OP_OFFSET[4] = { MDX_OP_S1, MDX_OP_S3, MDX_OP_S2, MDX_OP_S4 };

/* Strictly increasing order (0x00<0x04<0x08<0x0C) so a S2<->S3 (or any) value
 * swap fails the build. */
_Static_assert((MDX_OP_S1 == 0x00) && (MDX_OP_S3 == 0x04) &&
               (MDX_OP_S2 == 0x08) && (MDX_OP_S4 == 0x0C),
               "MDX operator offset values wrong (expect S1=0x00,S3=0x04,S2=0x08,S4=0x0C)");
_Static_assert((MDX_OP_S1 < MDX_OP_S3) && (MDX_OP_S3 < MDX_OP_S2) &&
               (MDX_OP_S2 < MDX_OP_S4),
               "MDX operator offset order wrong (expect S1<S3<S2<S4)");

/* Per-operator register group bases. */
#define MDX_REG_DT_MUL   0x30   /* detune (bits4-6) / multiplier (bits0-3) */
#define MDX_REG_TL       0x40   /* total level (7-bit) */
#define MDX_REG_KS_AR    0x50   /* key scaling (bits6-7) / attack rate (bits0-4) */
#define MDX_REG_AM_D1R   0x60   /* AM (bit7) / decay1 rate (bits0-4) */
#define MDX_REG_D2R      0x70   /* decay2 rate (bits0-4) */
#define MDX_REG_D1L_RR   0x80   /* sustain level (bits0-3) / release rate (bits4-7) */
#define MDX_REG_SSG_EG   0x90   /* SSG-EG (disabled when 0) */

/* Per-channel registers (one per channel, not per operator). */
#define MDX_REG_A0       0xA0   /* FNUM low byte */
#define MDX_REG_A4       0xA4   /* block (bits3-5) + FNUM high (bits0-2) */
#define MDX_REG_B0       0xB0   /* algorithm (bits0-2) + feedback (bits3-5) */
#define MDX_REG_B4       0xB4   /* L/R out (bits6-7=0b11), AMS (bits4-5), PMS (bits0-2) */
#define MDX_REG_KEYON    0x28   /* key on/off */

/* Mandatory boot initialisation of the YM2612 (channel 1 monophonic MVP):
 * resets the chip, silences the PSG, configures channel 1 pan, disables SSG-EG
 * and leaves 0x27 (CSM) and 0x2B (DAC) untouched. */
void MdxYm2612_Init(void);

/* Generic per-operator register write. Only channel 1 is supported; any other
 * channel is rejected (no bank-1 / part-1 access). Maps channel 1 -> part 0. */
void MdxYm2612_WriteReg(u8 channel, u8 group, u8 opOffset, u8 value);

/* Frequency write (FNUM + BLOCK). Writes 0xA4 (high) THEN 0xA0 (low). */
void MdxYm2612_SetFrequency(u8 channel, u8 block, u16 fnum);

/* Key-on: sets operator bits (opBits) for the channel. Key-off: clears them. */
void MdxYm2612_KeyOn(u8 channel, u8 opBits);
void MdxYm2612_KeyOff(u8 channel);

#endif /* _MDX_YM2612_H_ */
