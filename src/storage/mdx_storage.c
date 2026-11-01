/*
 * mdx_storage.c - SRAM access layer (SPEC 8.2).
 *
 * See mdx_storage.h. All SRAM traffic goes through SGDK's byte accessors; the
 * availability guard short-circuits every read/write when no writable SRAM is
 * present, so the rest of the app can call us unconditionally and stay safe on
 * carts/regions without backup RAM.
 */

#include "mdx_storage.h"

#include <genesis.h>
#include <sram.h>     /* SRAM_enable(), SRAM_readByte(), SRAM_writeByte() */


/* Self-test scratch offsets (restored after the test so we never corrupt an
 * existing save that might live at the start of SRAM). */
#define MDX_STORAGE_TEST_OFF_0   0
#define MDX_STORAGE_TEST_OFF_1   1

#define MDX_STORAGE_SENTINEL_A   0x5A
#define MDX_STORAGE_SENTINEL_B   0xA5


static bool g_sramAvailable = FALSE;


void MdxStorage_Init(void)
{
    /* Open the SRAM window (writes the $A130xx mapper control register). */
    SRAM_enable();

    /* Presence self-test: write two DIFFERENT sentinels to two DIFFERENT
     * offsets, read them back. If both stick, we have writable SRAM. If either
     * read-back mismatches (write didn't take - ROM-mapped, write-protected, or
     * absent), SRAM is not usable and we keep g_sramAvailable = FALSE. */
    {
        const u8 orig0 = SRAM_readByte(MDX_STORAGE_TEST_OFF_0);
        const u8 orig1 = SRAM_readByte(MDX_STORAGE_TEST_OFF_1);

        SRAM_writeByte(MDX_STORAGE_TEST_OFF_0, MDX_STORAGE_SENTINEL_A);
        SRAM_writeByte(MDX_STORAGE_TEST_OFF_1, MDX_STORAGE_SENTINEL_B);

        {
            const u8 rb0 = SRAM_readByte(MDX_STORAGE_TEST_OFF_0);
            const u8 rb1 = SRAM_readByte(MDX_STORAGE_TEST_OFF_1);
            g_sramAvailable =
                (rb0 == MDX_STORAGE_SENTINEL_A) &&
                (rb1 == MDX_STORAGE_SENTINEL_B);
        }

        /* Restore the original bytes so any existing save at these offsets is
         * preserved (no-op when SRAM is not writable, harmless on ROM). */
        SRAM_writeByte(MDX_STORAGE_TEST_OFF_0, orig0);
        SRAM_writeByte(MDX_STORAGE_TEST_OFF_1, orig1);
    }
}


u8 MdxStorage_ReadByte(u32 offset)
{
    /* Guard: never touch SRAM (or anything else) when unavailable. */
    if (!g_sramAvailable) return 0;

    /* SRAM_readByte adds SRAM_BASE + offset*2 internally; we forward the
     * logical offset as-is and never use word/long access. */
    return SRAM_readByte(offset);
}


void MdxStorage_WriteByte(u32 offset, u8 value)
{
    /* Guard: no-op when unavailable (no write to ROM space, no crash). */
    if (!g_sramAvailable) return;

    SRAM_writeByte(offset, value);
}


bool MdxStorage_IsAvailable(void)
{
    return g_sramAvailable;
}
